/*
 * st7789fb.c - Copyright 2013, evilwombat
 * Based on the HGAFB implementation by Ferenc Bakonyi
 * (fero@drama.obuda.kando.hu) and on the ST7735 framebuffer driver
 * by Kamal Mostafa <kamal@whence.com> et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>

#define PANEL_ID_UNKNOWN	-1
#define PANEL_ID_ERROR		-2

static int st7789_panel_id = PANEL_ID_UNKNOWN;

#define VRAM_LEN	(240*320*4)
#define TXBUF_LEN	(240*320*3*2)	/* SPI transactions require 16 bits */

static struct fb_var_screeninfo st7789_var = {
	.height		= 320,
	.width		= 240,
	.activate	= FB_ACTIVATE_TEST,
	.vmode		= FB_VMODE_NONINTERLACED,
	.xres 		= 240,
	.yres 		= 320,
	.bits_per_pixel = 24,
	.red		= {0, 8},
	.green		= {8, 8},
	.blue		= {16, 8},
	.transp		= {0, 0},
};

static struct fb_fix_screeninfo st7789_fix = {
	.id		= "st7789",
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_TRUECOLOR,
	.accel		= FB_ACCEL_NONE,
	.line_length 	= 240*3,
};

static void st7789fb_schedule_refresh(struct fb_info *info, const struct fb_fillrect *rect);

static int st7789_open(struct fb_info *info, int init)
{
	return 0;
}

static int st7789_release(struct fb_info *info, int init)
{
	return 0;
}

/* setcolreg implementation from 'simplefb', upstream */
static int st7789_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			      u_int transp, struct fb_info *info)
{
	u32 *pal = info->pseudo_palette;
	u32 cr = red >> (16 - info->var.red.length);
	u32 cg = green >> (16 - info->var.green.length);
	u32 cb = blue >> (16 - info->var.blue.length);
	u32 value;

	if (regno >= 16)
		return -EINVAL;

	value = (cr << info->var.red.offset) |
		(cg << info->var.green.offset) |
		(cb << info->var.blue.offset);

	if (info->var.transp.length > 0) {
		u32 mask = (1 << info->var.transp.length) - 1;
		mask <<= info->var.transp.offset;
		value |= mask;
	}

	pal[regno] = value;
	return 0;
}

static int st7789_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	st7789fb_schedule_refresh(info, NULL);
	return 0;
}

static int st7789_blank(int blank_mode, struct fb_info *info)
{
	/* TODO: Write to LCD blanking control register */
	return 0;
}

static void st7789_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	cfb_fillrect(info, rect);
	st7789fb_schedule_refresh(info, rect);
}

static void st7789_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	cfb_copyarea(info, area);
	st7789fb_schedule_refresh(info, NULL);
}

static void st7789_imageblit(struct fb_info *info, const struct fb_image *image)
{
	cfb_imageblit(info, image);
	st7789fb_schedule_refresh(info, NULL);
}

static ssize_t st7789_write(struct fb_info *info, const char __user *buf, size_t count, loff_t *ppos)
{
	ssize_t ret = fb_sys_write(info, buf, count, ppos);
	st7789fb_schedule_refresh(info, NULL);
	return ret;
}

static int st7789_check_var(struct fb_var_screeninfo *var,
			    struct fb_info *info)
{
	var->bits_per_pixel = 24;
	var->red.offset = 0;
	var->red.length = 8;
	var->red.msb_right = 0;

	var->green.offset = 8;
	var->green.length = 8;
	var->green.msb_right = 0;

	var->blue.offset = 16;
	var->blue.length = 8;
	var->blue.msb_right = 0;

	var->transp.offset = 0;
	var->transp.length = 0;
	var->transp.msb_right = 0;

	var->xres_virtual = 240;
	var->yres_virtual = 320;
	var->vmode = FB_VMODE_NONINTERLACED;
	var->xres = 240;
	var->yres = 320;

	return 0;
}

static struct fb_ops st7789fb_ops = {
	.owner		= THIS_MODULE,
	.fb_open	= st7789_open,
	.fb_write	= st7789_write,
	.fb_release	= st7789_release,
	.fb_setcolreg	= st7789_setcolreg,
	.fb_pan_display	= st7789_pan_display,
	.fb_blank	= st7789_blank,
	.fb_fillrect	= st7789_fillrect,
	.fb_copyarea	= st7789_copyarea,
	.fb_imageblit	= st7789_imageblit,
	.fb_check_var	= st7789_check_var,
};

/*
 * rvmalloc implementation from VFB (drivers/video/vfb.c
 */
static void *rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr;

	size = PAGE_ALIGN(size);
	mem = vmalloc_32(size);
	if (!mem)
		return NULL;

	memset(mem, 0, size); /* Clear the ram out, no junk to the user */
	adr = (unsigned long) mem;
	while (size > 0) {
		SetPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	return mem;
}

struct st7789_data {
	spinlock_t		lock;
	struct spi_device	*spi;
	struct fb_info 		*info;
	unsigned char		*vram;
	unsigned short		txbuf[TXBUF_LEN];
};

static void st7789_send_cmd(struct st7789_data *drvdata, unsigned short v)
{
	v &= 0xff;
	spi_write(drvdata->spi, (u8 *) &v, 2);
}

static void st7789_send_data(struct st7789_data *drvdata, unsigned short v)
{
	v &= 0xff;
	v |= 0x100;
	spi_write(drvdata->spi, (u8 *) &v, 2);
}

static void st7789fb_deferred_io(struct fb_info *info,
				 struct list_head *pagelist)
{
	unsigned int i;
	unsigned char *vram = info->screen_base;
	struct st7789_data *dd = info->par;
	
	/* Convert framebuffer contents into a SPI transmit buffer */
	for (i = 0; i < 320*240*3; i++)
		dd->txbuf[i] = 0x100 | (vram[i] & 0xfc);
	
	st7789_send_cmd(dd, 0x2a);
	st7789_send_data(dd, 0);
	st7789_send_data(dd, 0);
	st7789_send_data(dd, 0);
	st7789_send_data(dd, 239);

	st7789_send_cmd(dd, 0x2b);
	st7789_send_data(dd, 0);
	st7789_send_data(dd, 0);
	st7789_send_data(dd, 319 >> 8);
	st7789_send_data(dd, 319 & 0xff);

	st7789_send_cmd(dd, 0x2c);

	spi_write(dd->spi, (u8 *) dd->txbuf, 320*240*3*2);
}

static struct fb_deferred_io st7789fb_defio = {
	.delay		= HZ / 5,
	.deferred_io	= st7789fb_deferred_io,
};

static unsigned short st7789_init_11_d5[] = {
	0xB7, 0x154, 0xBA, 0x100, 0xBB, 0x129, 0xC0, 0x12C, 0xC2, 0x100, 0xC3,
	0x100, 0xC4, 0x120, 0xC6, 0x10F, 0xD0, 0x1A4, 0x101, 0xD2, 0x14C, 0xDF,
	0x15A, 0x169, 0x102, 0x100, 0xE0, 0x1F0, 0x106, 0x10B, 0x10A, 0x10A,
	0x106, 0x133, 0x143, 0x149, 0x137, 0x112, 0x111, 0x12D, 0x132, 0xE1,
	0x1F0, 0x106, 0x10B, 0x10A, 0x10A, 0x106, 0x133, 0x143, 0x149, 0x137,
	0x112, 0x111, 0x12D, 0x132, 0xE4, 0x127, 0x100, 0x110, 0xE7, 0x101,
	0xE8, 0x193, 0xE9, 0x102, 0x102, 0x100, 0xEC, 0x100, 0xFA, 0x15A, 0x169,
	0x1EE, 0x100, 0xFC, 0x100, 0x100, 0xFE, 0x100, 0x100, 0xB8, 0x12A,
	0x12B, 0x101, 0x1FF, 0xB1, 0x1C0, 0x104, 0x10A, 0xB2, 0x10C, 0x10C,
	0x100, 0x133, 0x133, 0xB3, 0x100, 0x10F, 0x10F, 0xB0, 0x111, 0x1F4
};

static unsigned short st7789_init_50_5a[] = {
	0xB7, 0x135, 0xBB, 0x139, 0xC0, 0x12C, 0xC2, 0x101, 0xC3, 0x10F, 0xC4,
	0x120, 0xC6, 0x10F, 0xD0, 0x1A4, 0x1A1, 0xE0, 0x1F0, 0x10C, 0x113,
	0x10B, 0x10A, 0x126, 0x139, 0x144, 0x14D, 0x108, 0x114, 0x115, 0x12E,
	0x134, 0xE1, 0x1F0, 0x10C, 0x113, 0x10B, 0x10A, 0x126, 0x139, 0x144,
	0x14D, 0x108, 0x114, 0x115, 0x12E, 0x134, 0xE9, 0x108, 0x108, 0x104,
	0xB8, 0x12A, 0x12B, 0x120, 0xB1, 0x1C0, 0x102, 0x114, 0xB0, 0x111, 0x1E4
};

static unsigned short st7789_init_default[] = {
	0xB7, 0x120, 0xBB, 0x13D, 0xC0, 0x12C, 0xC2, 0x101, 0xC3, 0x115, 0xC4,
	0x120, 0xC6, 0x10F, 0xD0, 0x1A4, 0x1A1, 0xE0, 0x1F0, 0x10C, 0x113,
	0x10B, 0x10A, 0x126, 0x139, 0x144, 0x14D, 0x108, 0x114, 0x115, 0x12E,
	0x134, 0xE1, 0x1F0, 0x10C, 0x113, 0x10B, 0x10A, 0x126, 0x139, 0x144,
	0x14D, 0x108, 0x114, 0x115, 0x12E, 0x134, 0xE9, 0x108, 0x108, 0x104,
	0xB8, 0x12A, 0x12B, 0x120, 0xB1, 0x1C0, 0x102, 0x114, 0xB0, 0x111, 0x1E4
};

static void init_lcd(struct st7789_data *dd)
{
	int i;
	struct spi_device *spi = dd->spi;

	st7789_send_cmd(dd, 0x01);
	msleep(20);
	st7789_send_cmd(dd, 0x11);
	msleep(120);
	st7789_send_cmd(dd, 0x36);
	st7789_send_data(dd, 0x00);
	st7789_send_cmd(dd, 0x3A);
	st7789_send_data(dd, 0x66);

	dev_info(&spi->dev, "Initializing LCD panel with ID %02x\n",
		 st7789_panel_id);

	/* Panel-specific configuration */
	switch (st7789_panel_id) {
		case 0x11:
		case 0xd5:
			spi_write(spi, (u8 *) st7789_init_11_d5,
				  sizeof(st7789_init_11_d5));
			break;

		case 0x50:
		case 0x5a:
			spi_write(spi, (u8 *) st7789_init_50_5a,
				  sizeof(st7789_init_50_5a));
			break;

		default:
			dev_info(&spi->dev, "Unknown panel ID %02x; assuming default initialization sequence\n",
				 st7789_panel_id);
			spi_write(dd->spi, (u8 *) st7789_init_default,
				  sizeof(st7789_init_default));
			break;
	};

	st7789_send_cmd(dd, 0x2A);
	st7789_send_data(dd, 0x00);
	st7789_send_data(dd, 0x00);
	st7789_send_data(dd, 0x00);
	st7789_send_data(dd, 0xEF);

	st7789_send_cmd(dd, 0x2B);
	st7789_send_data(dd, 0x00);
	st7789_send_data(dd, 0x00);
	st7789_send_data(dd, 0x01);
	st7789_send_data(dd, 0x3F);

	st7789_send_cmd(dd, 0x21);
	st7789_send_cmd(dd, 0x29);

	/* Draw something */
	st7789_send_cmd(dd, 0x36);
	st7789_send_data(dd, 0);
	st7789_send_cmd(dd, 0xb1);
	st7789_send_data(dd, 0);
	st7789_send_cmd(dd, 0xb0);
	st7789_send_data(dd, 0);

	st7789_send_cmd(dd, 0x2a);
	st7789_send_data(dd, 0);
	st7789_send_data(dd, 0);
	st7789_send_data(dd, 0);
	st7789_send_data(dd, 10);

	st7789_send_cmd(dd, 0x2b);
	st7789_send_data(dd, 0);
	st7789_send_data(dd, 0);
	st7789_send_data(dd, 10 >> 8);
	st7789_send_data(dd, 10 & 0xff);

	st7789_send_cmd(dd, 0x2c);

	for (i = 0; i < 100; i++) {
		st7789_send_data(dd, 0xfc);
		st7789_send_data(dd, 0xcc);
		st7789_send_data(dd, 0x00);
	}
}

static int st7789_probe(struct spi_device *spi)
{
	struct st7789_data *drvdata;
	int ret;

	/* 
	 * The panel ID must be read over I2C, via a separate probe call.
	 * If the panel ID has not been read yet, defer probe.
	 */
	if (st7789_panel_id == PANEL_ID_UNKNOWN)
		return -EPROBE_DEFER;

	/* If we could not actually read the panel ID, give up now.	 */
	if (st7789_panel_id == PANEL_ID_ERROR)
		return -EINVAL;

	drvdata = devm_kzalloc(&spi->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata) {
		dev_err(&spi->dev, "Error allocating drvdata struct\n");
		return -ENOMEM;
	}

	drvdata->vram = rvmalloc(VRAM_LEN);
	if (!drvdata->vram) {
		dev_err(&spi->dev, "Error allocating video memory struct\n");
		return -ENOMEM;
	}

	drvdata->spi = spi;
	spin_lock_init(&drvdata->lock);
	spi_set_drvdata(spi, drvdata);

	spi->mode = 3;
	spi->bits_per_word = 9;
	spi->max_speed_hz = 1000000000;
	ret = spi_setup(spi);

	if (ret) {
		dev_err(&spi->dev, "Error configuring SPI controller\n");
		return ret;
	}

	init_lcd(drvdata);

	drvdata->info = framebuffer_alloc(sizeof(u32) * 16, &spi->dev);
	if (!drvdata->info) {
		return -ENOMEM;
	}

	st7789_fix.smem_start = (unsigned long)drvdata->vram;
	st7789_fix.smem_len = VRAM_LEN;

	drvdata->info->par = drvdata;
	drvdata->info->flags = FBINFO_DEFAULT;
	drvdata->info->var = st7789_var;
	drvdata->info->fix = st7789_fix;
	drvdata->info->fbops = &st7789fb_ops;
	drvdata->info->screen_base = drvdata->vram;
	drvdata->info->fbdefio = &st7789fb_defio;
	fb_deferred_io_init(drvdata->info);
	drvdata->info->pseudo_palette = (void *)(drvdata->info + 1);

	ret = register_framebuffer(drvdata->info);

	if (ret < 0) {
		framebuffer_release(drvdata->info);
		return -EINVAL;
	}
	
        printk(KERN_INFO "fb%d: %s frame buffer device\n",
	       drvdata->info->node, drvdata->info->fix.id);

	return 0;
}

static void st7789fb_schedule_refresh(struct fb_info *info, const struct fb_fillrect *rect)
{
	if (!info->fbdefio)
		return;

	schedule_delayed_work(&info->deferred_work, info->fbdefio->delay);
}

static int st7789_remove(struct spi_device *spi)
{
	struct st7789_data *drvdata = spi_get_drvdata(spi);

	spin_lock_irq(&drvdata->lock);
	drvdata->spi = NULL;
	spi_set_drvdata(spi, NULL);

	if (drvdata->info) {
		unregister_framebuffer(drvdata->info);
		fb_deferred_io_cleanup(drvdata->info);
		framebuffer_release(drvdata->info);
	}
	spin_unlock_irq(&drvdata->lock);
	return 0;
}

static struct spi_driver st7789_spi_driver = {
	.driver = {
		.name =		"st7789fb",
		.owner =	THIS_MODULE,
	},
	.probe =	st7789_probe,
	.remove =	st7789_remove,
};


/*
 * This is relatively heinous.
 *
 * To properly initialize the panel, we need to send some model-specific
 * commands to it. We cannot directly ask the panel for its model ID - 
 * instead, we must ask the associated *touchscreen controller*.
 * 
 * Therefore, we need to have a tiny i2c driver that knows how to retrieve
 * the panel ID from the touchscreen, and to hand it to the panel driver
 * itself.
 * 
 * I really wish it did not need to be this way....
 */
static int st7789fb_panel_id_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	unsigned int panel_id;

	/* Let's see whether this adapter can support what we need. */
	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "insufficient functionality!\n");
		return -ENODEV;
	}
	
	panel_id = i2c_smbus_read_byte_data(client, 0xA8);

	if (panel_id < 0) {
		dev_err(&client->dev, "Could not query panel ID: %d\n", panel_id);
		st7789_panel_id = PANEL_ID_ERROR;
		return panel_id;
	}
	
	st7789_panel_id = panel_id;
	
	dev_info(&client->dev, "Detected panel ID: %02x\n", panel_id);
	return 0;
}

static int st7789fb_panel_id_remove(struct i2c_client *client)
{
	/* We don't have anything to clean up */
	return 0;
}

static const struct i2c_device_id st7789fb_panel_id_id[] = { /* :) */
	{"st7789fb_panel_id", 0},
	{}
};

static struct i2c_driver st7789fb_panel_id_driver = {
	.driver   = {
		   .name = "st7789fb_panel_id",
	},
	.probe    = st7789fb_panel_id_probe,
	.remove   = st7789fb_panel_id_remove,
	.id_table = st7789fb_panel_id_id,
};

module_spi_driver(st7789_spi_driver);
module_i2c_driver(st7789fb_panel_id_driver);

MODULE_AUTHOR("evilwombat");
MODULE_DESCRIPTION("Sitronix ST7789 LCD Controller Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:st7789fb");
MODULE_DEVICE_TABLE(i2c, st7789fb_panel_id);
