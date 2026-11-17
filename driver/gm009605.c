// SPDX-License-Identifier: GPL-2.0-only
/*
 * gm009605.c - Linux I2C driver for the GM009605 0.96" 128x64 OLED module
 *              (Solomon SSD1306 controller).
 *
 * User-space interface
 * --------------------
 *   /dev/gm009605        1024-byte framebuffer in SSD1306 page format.
 *                        read()/write()/lseek(); every write is flushed
 *                        to the panel immediately.
 *
 *   /sys/bus/i2c/devices/<bus>-<addr>/
 *       contrast     (rw)  0..255
 *       invert       (rw)  0|1   white-on-black / black-on-white
 *       display_on   (rw)  0|1   panel sleep control
 *       flip         (rw)  0|1   rotate the image 180 degrees
 *       clear        (wo)  any value clears the screen
 *       text         (wo)  render text, '\n' starts a new line
 *                          (21 columns x 8 lines, 5x7 font)
 *       test_pattern (wo)  fill | checker | stripes | border
 *
 * The device is bound either from Device Tree (driver/gm009605-overlay.dts)
 * or at run time via:
 *   echo gm009605 0x3c | sudo tee /sys/bus/i2c/devices/i2c-1/new_device
 *
 * Copyright (C) 2026 Lan Tran
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>

#include "font5x7.h"
#include "ssd1306.h"

#define GM_NAME			"gm009605"
#define GM_TEXT_COLS		(SSD1306_WIDTH / FONT5X7_ADVANCE)	/* 21 */
#define GM_TEXT_ROWS		SSD1306_PAGES				/* 8 */
#define GM_XFER_CHUNK		SSD1306_WIDTH	/* data bytes per I2C write */
#define GM_BOOT_TEXT		"GM009605 ready\n\n/dev/gm009605"

static DEFINE_IDA(gm_ida);

struct gm009605 {
	struct i2c_client	*client;
	struct miscdevice	misc;
	char			misc_name[24];
	int			id;
	struct kref		ref;		/* driver + every open file */
	struct mutex		lock;		/* fb, state and bus access */
	bool			removed;	/* device unbound, fail I/O */

	u8			contrast;
	bool			inverted;
	bool			display_on;
	bool			flipped;

	u8			fb[SSD1306_FB_SIZE];
};

/* ------------------------------------------------------------------------ */
/* Low-level I2C helpers                                                    */
/* ------------------------------------------------------------------------ */

static int gm_send_cmds(struct gm009605 *gm, const u8 *cmds, size_t len)
{
	u8 buf[32];
	int ret;

	if (WARN_ON(len + 1 > sizeof(buf)))
		return -EINVAL;

	buf[0] = SSD1306_CTRL_CMD;
	memcpy(buf + 1, cmds, len);

	ret = i2c_master_send(gm->client, buf, len + 1);
	if (ret < 0)
		return ret;
	return ret == len + 1 ? 0 : -EIO;
}

#define gm_cmds(gm, ...) ({						\
	static const u8 __c[] = { __VA_ARGS__ };			\
	gm_send_cmds(gm, __c, sizeof(__c));				\
})

static int gm_cmd1(struct gm009605 *gm, u8 cmd)
{
	return gm_send_cmds(gm, &cmd, 1);
}

static int gm_cmd2(struct gm009605 *gm, u8 cmd, u8 arg)
{
	u8 c[2] = { cmd, arg };

	return gm_send_cmds(gm, c, sizeof(c));
}

/* Push the whole shadow framebuffer to display RAM. Caller holds gm->lock. */
static int gm_flush(struct gm009605 *gm)
{
	u8 buf[1 + GM_XFER_CHUNK];
	size_t off;
	int ret;

	ret = gm_cmds(gm, SSD1306_COL_RANGE, 0, SSD1306_WIDTH - 1,
			  SSD1306_PAGE_RANGE, 0, SSD1306_PAGES - 1);
	if (ret)
		return ret;

	buf[0] = SSD1306_CTRL_DATA;
	for (off = 0; off < SSD1306_FB_SIZE; off += GM_XFER_CHUNK) {
		memcpy(buf + 1, gm->fb + off, GM_XFER_CHUNK);
		ret = i2c_master_send(gm->client, buf, sizeof(buf));
		if (ret < 0)
			return ret;
		if (ret != sizeof(buf))
			return -EIO;
	}
	return 0;
}

static int gm_hw_init(struct gm009605 *gm)
{
	int ret;

	ret = gm_cmds(gm,
		      SSD1306_DISPLAY_OFF,
		      SSD1306_CLOCK_DIV, 0x80,
		      SSD1306_MUX_RATIO, SSD1306_HEIGHT - 1,
		      SSD1306_DISPLAY_OFFSET, 0x00,
		      SSD1306_START_LINE(0),
		      SSD1306_CHARGE_PUMP, SSD1306_CHARGE_PUMP_ON,
		      SSD1306_MEM_MODE, SSD1306_MEM_MODE_HORIZONTAL,
		      SSD1306_COM_PINS, SSD1306_COM_PINS_ALT,
		      SSD1306_PRECHARGE, 0xF1,
		      SSD1306_VCOMH, 0x40,
		      SSD1306_DISPLAY_RAM,
		      SSD1306_SCROLL_OFF);
	if (ret)
		return ret;

	ret = gm_cmd1(gm, gm->flipped ? SSD1306_SEG_REMAP_OFF
				      : SSD1306_SEG_REMAP_ON);
	ret = ret ?: gm_cmd1(gm, gm->flipped ? SSD1306_COM_SCAN_INC
					     : SSD1306_COM_SCAN_DEC);
	ret = ret ?: gm_cmd2(gm, SSD1306_SET_CONTRAST, gm->contrast);
	ret = ret ?: gm_cmd1(gm, gm->inverted ? SSD1306_INVERT
					      : SSD1306_NORMAL);
	ret = ret ?: gm_flush(gm);
	ret = ret ?: gm_cmd1(gm, gm->display_on ? SSD1306_DISPLAY_ON
						: SSD1306_DISPLAY_OFF);
	return ret;
}

/* ------------------------------------------------------------------------ */
/* Drawing helpers (operate on the shadow framebuffer only)                 */
/* ------------------------------------------------------------------------ */

static void gm_draw_char(struct gm009605 *gm, int col, int row, char c)
{
	unsigned char ch = c;

	if (ch < FONT5X7_FIRST || ch > FONT5X7_LAST)
		ch = '?';
	memcpy(&gm->fb[row * SSD1306_WIDTH + col * FONT5X7_ADVANCE],
	       font5x7[ch - FONT5X7_FIRST], FONT5X7_WIDTH);
}

static void gm_draw_text(struct gm009605 *gm, const char *s, size_t len)
{
	int col = 0, row = 0;
	size_t i;

	memset(gm->fb, 0, sizeof(gm->fb));

	for (i = 0; i < len && row < GM_TEXT_ROWS; i++) {
		if (s[i] == '\n') {
			col = 0;
			row++;
			continue;
		}
		if (col >= GM_TEXT_COLS) {	/* wrap long lines */
			col = 0;
			if (++row >= GM_TEXT_ROWS)
				break;
		}
		gm_draw_char(gm, col++, row, s[i]);
	}
}

static int gm_draw_pattern(struct gm009605 *gm, const char *name)
{
	int i;

	if (sysfs_streq(name, "fill")) {
		memset(gm->fb, 0xFF, sizeof(gm->fb));
	} else if (sysfs_streq(name, "checker")) {
		for (i = 0; i < SSD1306_FB_SIZE; i++)
			gm->fb[i] = (i / 4) % 2 ? 0x0F : 0xF0;
	} else if (sysfs_streq(name, "stripes")) {
		for (i = 0; i < SSD1306_FB_SIZE; i++)
			gm->fb[i] = 0x55;
	} else if (sysfs_streq(name, "border")) {
		memset(gm->fb, 0, sizeof(gm->fb));
		for (i = 0; i < SSD1306_WIDTH; i++) {
			gm->fb[i] |= 0x01;
			gm->fb[(SSD1306_PAGES - 1) * SSD1306_WIDTH + i] |= 0x80;
		}
		for (i = 0; i < SSD1306_PAGES; i++) {
			gm->fb[i * SSD1306_WIDTH] = 0xFF;
			gm->fb[i * SSD1306_WIDTH + SSD1306_WIDTH - 1] = 0xFF;
		}
	} else {
		return -EINVAL;
	}
	return 0;
}

/* ------------------------------------------------------------------------ */
/* Character device: /dev/gm009605                                          */
/* ------------------------------------------------------------------------ */

static void gm_release_ref(struct kref *ref)
{
	struct gm009605 *gm = container_of(ref, struct gm009605, ref);

	mutex_destroy(&gm->lock);
	kfree(gm);
}

static int gm_open(struct inode *inode, struct file *file)
{
	/* misc core has set private_data to our struct miscdevice */
	struct gm009605 *gm = container_of(file->private_data,
					   struct gm009605, misc);

	kref_get(&gm->ref);
	file->private_data = gm;
	return 0;
}

static int gm_release(struct inode *inode, struct file *file)
{
	struct gm009605 *gm = file->private_data;

	kref_put(&gm->ref, gm_release_ref);
	return 0;
}

static ssize_t gm_read(struct file *file, char __user *ubuf, size_t count,
		       loff_t *ppos)
{
	struct gm009605 *gm = file->private_data;
	ssize_t ret;

	mutex_lock(&gm->lock);
	if (gm->removed)
		ret = -ENODEV;
	else
		ret = simple_read_from_buffer(ubuf, count, ppos, gm->fb,
					      sizeof(gm->fb));
	mutex_unlock(&gm->lock);
	return ret;
}

static ssize_t gm_write(struct file *file, const char __user *ubuf,
			size_t count, loff_t *ppos)
{
	struct gm009605 *gm = file->private_data;
	loff_t pos = *ppos;
	ssize_t ret;

	if (pos < 0)
		return -EINVAL;
	if (pos >= SSD1306_FB_SIZE)
		return count ? -ENOSPC : 0;
	count = min_t(size_t, count, SSD1306_FB_SIZE - pos);

	mutex_lock(&gm->lock);
	if (gm->removed) {
		ret = -ENODEV;
		goto out;
	}
	if (copy_from_user(gm->fb + pos, ubuf, count)) {
		ret = -EFAULT;
		goto out;
	}
	ret = gm_flush(gm);
	if (ret)
		goto out;

	*ppos = pos + count;
	ret = count;
out:
	mutex_unlock(&gm->lock);
	return ret;
}

static loff_t gm_llseek(struct file *file, loff_t offset, int whence)
{
	return fixed_size_llseek(file, offset, whence, SSD1306_FB_SIZE);
}

static const struct file_operations gm_fops = {
	.owner		= THIS_MODULE,
	.open		= gm_open,
	.release	= gm_release,
	.read		= gm_read,
	.write		= gm_write,
	.llseek		= gm_llseek,
};

/* ------------------------------------------------------------------------ */
/* sysfs attributes                                                         */
/* ------------------------------------------------------------------------ */

static struct gm009605 *dev_to_gm(struct device *dev)
{
	return i2c_get_clientdata(to_i2c_client(dev));
}


static ssize_t contrast_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", dev_to_gm(dev)->contrast);
}

static ssize_t contrast_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct gm009605 *gm = dev_to_gm(dev);
	u8 val;
	int ret;

	ret = kstrtou8(buf, 0, &val);
	if (ret)
		return ret;

	mutex_lock(&gm->lock);
	ret = gm_cmd2(gm, SSD1306_SET_CONTRAST, val);
	if (!ret)
		gm->contrast = val;
	mutex_unlock(&gm->lock);
	return ret ?: count;
}
static DEVICE_ATTR_RW(contrast);

static ssize_t invert_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", dev_to_gm(dev)->inverted);
}

static ssize_t invert_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct gm009605 *gm = dev_to_gm(dev);
	bool val;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret)
		return ret;

	mutex_lock(&gm->lock);
	ret = gm_cmd1(gm, val ? SSD1306_INVERT : SSD1306_NORMAL);
	if (!ret)
		gm->inverted = val;
	mutex_unlock(&gm->lock);
	return ret ?: count;
}
static DEVICE_ATTR_RW(invert);

static ssize_t display_on_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", dev_to_gm(dev)->display_on);
}

static ssize_t display_on_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct gm009605 *gm = dev_to_gm(dev);
	bool val;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret)
		return ret;

	mutex_lock(&gm->lock);
	ret = gm_cmd1(gm, val ? SSD1306_DISPLAY_ON : SSD1306_DISPLAY_OFF);
	if (!ret)
		gm->display_on = val;
	mutex_unlock(&gm->lock);
	return ret ?: count;
}
static DEVICE_ATTR_RW(display_on);

static ssize_t flip_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", dev_to_gm(dev)->flipped);
}

static ssize_t flip_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct gm009605 *gm = dev_to_gm(dev);
	bool val;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret)
		return ret;

	/* Segment remap only affects data written afterwards, so re-flush. */
	mutex_lock(&gm->lock);
	ret = gm_cmd1(gm, val ? SSD1306_SEG_REMAP_OFF : SSD1306_SEG_REMAP_ON);
	if (!ret)
		ret = gm_cmd1(gm, val ? SSD1306_COM_SCAN_INC
				      : SSD1306_COM_SCAN_DEC);
	if (!ret)
		ret = gm_flush(gm);
	if (!ret)
		gm->flipped = val;
	mutex_unlock(&gm->lock);
	return ret ?: count;
}
static DEVICE_ATTR_RW(flip);

static ssize_t clear_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct gm009605 *gm = dev_to_gm(dev);
	int ret;

	mutex_lock(&gm->lock);
	memset(gm->fb, 0, sizeof(gm->fb));
	ret = gm_flush(gm);
	mutex_unlock(&gm->lock);
	return ret ?: count;
}
static DEVICE_ATTR_WO(clear);

static ssize_t text_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct gm009605 *gm = dev_to_gm(dev);
	size_t len = count;
	int ret;

	/* drop the trailing newline that `echo` appends */
	if (len && buf[len - 1] == '\n')
		len--;

	mutex_lock(&gm->lock);
	gm_draw_text(gm, buf, len);
	ret = gm_flush(gm);
	mutex_unlock(&gm->lock);
	return ret ?: count;
}
static DEVICE_ATTR_WO(text);

static ssize_t test_pattern_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct gm009605 *gm = dev_to_gm(dev);
	int ret;

	mutex_lock(&gm->lock);
	ret = gm_draw_pattern(gm, buf);
	if (!ret)
		ret = gm_flush(gm);
	mutex_unlock(&gm->lock);
	return ret ?: count;
}
static DEVICE_ATTR_WO(test_pattern);

static struct attribute *gm_attrs[] = {
	&dev_attr_contrast.attr,
	&dev_attr_invert.attr,
	&dev_attr_display_on.attr,
	&dev_attr_flip.attr,
	&dev_attr_clear.attr,
	&dev_attr_text.attr,
	&dev_attr_test_pattern.attr,
	NULL,
};
ATTRIBUTE_GROUPS(gm);

/* ------------------------------------------------------------------------ */
/* I2C driver glue                                                          */
/* ------------------------------------------------------------------------ */

static int gm_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct gm009605 *gm;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return dev_err_probe(dev, -EOPNOTSUPP,
				     "adapter lacks plain I2C transfers\n");

	/* Not devm: the struct must outlive unbind while files are open. */
	gm = kzalloc(sizeof(*gm), GFP_KERNEL);
	if (!gm)
		return -ENOMEM;

	gm->client	= client;
	gm->contrast	= SSD1306_DEFAULT_CONTRAST;
	gm->display_on	= true;
	kref_init(&gm->ref);
	mutex_init(&gm->lock);
	i2c_set_clientdata(client, gm);

	mutex_lock(&gm->lock);
	gm_draw_text(gm, GM_BOOT_TEXT, strlen(GM_BOOT_TEXT));
	ret = gm_hw_init(gm);
	mutex_unlock(&gm->lock);
	if (ret) {
		dev_err_probe(dev, ret,
			      "no response at 0x%02x - check wiring\n",
			      client->addr);
		goto err_put;
	}

	gm->id = ida_alloc(&gm_ida, GFP_KERNEL);
	if (gm->id < 0) {
		ret = gm->id;
		goto err_put;
	}
	if (gm->id == 0)
		strscpy(gm->misc_name, GM_NAME, sizeof(gm->misc_name));
	else
		snprintf(gm->misc_name, sizeof(gm->misc_name), GM_NAME "-%d",
			 gm->id);

	gm->misc.minor	= MISC_DYNAMIC_MINOR;
	gm->misc.name	= gm->misc_name;
	gm->misc.fops	= &gm_fops;
	gm->misc.parent	= dev;
	gm->misc.mode	= 0660;

	ret = misc_register(&gm->misc);
	if (ret) {
		dev_err_probe(dev, ret, "misc_register failed\n");
		goto err_ida;
	}

	dev_info(dev, "%ux%u OLED ready on /dev/%s\n",
		 SSD1306_WIDTH, SSD1306_HEIGHT, gm->misc_name);
	return 0;

err_ida:
	ida_free(&gm_ida, gm->id);
err_put:
	kref_put(&gm->ref, gm_release_ref);
	return ret;
}

static void gm_remove(struct i2c_client *client)
{
	struct gm009605 *gm = i2c_get_clientdata(client);

	/* After this returns no new open() can reach us. */
	misc_deregister(&gm->misc);

	mutex_lock(&gm->lock);
	gm_cmd1(gm, SSD1306_DISPLAY_OFF);	/* best effort, may be unplugged */
	gm->removed = true;
	mutex_unlock(&gm->lock);

	ida_free(&gm_ida, gm->id);
	kref_put(&gm->ref, gm_release_ref);
}

static void gm_shutdown(struct i2c_client *client)
{
	struct gm009605 *gm = i2c_get_clientdata(client);

	/* Avoid OLED burn-in: blank the panel on poweroff / reboot. */
	mutex_lock(&gm->lock);
	gm_cmd1(gm, SSD1306_DISPLAY_OFF);
	mutex_unlock(&gm->lock);
}

static const struct of_device_id gm_of_match[] = {
	{ .compatible = "lantran,gm009605" },
	{ }
};
MODULE_DEVICE_TABLE(of, gm_of_match);

static const struct i2c_device_id gm_id[] = {
	{ GM_NAME },
	{ }
};
MODULE_DEVICE_TABLE(i2c, gm_id);

static struct i2c_driver gm_driver = {
	.driver = {
		.name		= GM_NAME,
		.of_match_table	= gm_of_match,
		.dev_groups	= gm_groups,
	},
	.probe		= gm_probe,
	.remove		= gm_remove,
	.shutdown	= gm_shutdown,
	.id_table	= gm_id,
};
module_i2c_driver(gm_driver);

MODULE_AUTHOR("Lan Tran");
MODULE_DESCRIPTION("GM009605 / SSD1306 128x64 I2C OLED driver");
MODULE_LICENSE("GPL");
