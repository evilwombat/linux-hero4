/*
 * st7585fb.c - Copyright 2013, evilwombat
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
#include <linux/fb.h>
#include <linux/delay.h>

#include <linux/spi/spi.h>

static struct fb_var_screeninfo st7585_var = {
	.xres		= 60,
	.yres 		= 75,
	.xres_virtual 	= 60,
	.yres_virtual	= 75,
	.bits_per_pixel = 1,
	.red 		= {0, 1, 0},
	.green 		= {0, 1, 0},
	.blue 		= {0, 1, 0},
	.transp 	= {0, 0, 0},
	.height 	= -1,
	.width 		= -1,
};

static struct fb_fix_screeninfo st7585_fix = {
	.id 		= "st7585",
	.type 		= FB_TYPE_PACKED_PIXELS,
	.visual 	= FB_VISUAL_MONO10,
	.line_length 	= 8,
	.accel 		= FB_ACCEL_NONE
};

static void st7585fb_schedule_refresh(struct fb_info *info, const struct fb_fillrect *rect);

static int st7585_open(struct fb_info *info, int init)
{
	return 0;
}

static int st7585_release(struct fb_info *info, int init)
{
	return 0;
}

static int st7585_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			   u_int transp, struct fb_info *info)
{
	if (regno > 1)
		return 1;
	return 0;
}

static int st7585_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	st7585fb_schedule_refresh(info, NULL);
	return 0;
}

static int st7585_blank(int blank_mode, struct fb_info *info)
{
	/* TODO: Write to LCD blanking control register */
	return 0;
}

static void st7585_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	sys_fillrect(info, rect);
	st7585fb_schedule_refresh(info, rect);
}

static void st7585_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	sys_copyarea(info, area);
	st7585fb_schedule_refresh(info, NULL);
}

static void st7585_imageblit(struct fb_info *info, const struct fb_image *image)
{
	sys_imageblit(info, image);
	st7585fb_schedule_refresh(info, NULL);
}

static ssize_t st7585_write(struct fb_info *info, const char __user *buf, size_t count, loff_t *ppos)
{
	ssize_t ret = fb_sys_write(info, buf, count, ppos);
	st7585fb_schedule_refresh(info, NULL);
	return ret;
}

static struct fb_ops st7585fb_ops = {
	.owner		= THIS_MODULE,
	.fb_open	= st7585_open,
	.fb_write	= st7585_write,
	.fb_release	= st7585_release,
	.fb_setcolreg	= st7585_setcolreg,
	.fb_pan_display	= st7585_pan_display,
	.fb_blank	= st7585_blank,
	.fb_fillrect	= st7585_fillrect,
	.fb_copyarea	= st7585_copyarea,
	.fb_imageblit	= st7585_imageblit,
};

#define VRAM_LEN	600

struct st7585_data {
	spinlock_t		lock;
	struct spi_device	*spi;
	struct fb_info 		*info;
	unsigned char		vram[VRAM_LEN];
};

static int st7585_spi_send_word(struct st7585_data *drvdata, unsigned int val)
{
	return spi_write(drvdata->spi, (u8 *) &val, 2);
}

static void st7585fb_deferred_io(struct fb_info *info,
				struct list_head *pagelist)
{
	unsigned int row, col, t, i;
	unsigned char *vram = info->screen_base;
	struct st7585_data *dd = info->par;

	for (col = 0; col < 8; col++) {
		st7585_spi_send_word(dd, 0x40 | col);
		st7585_spi_send_word(dd, 0x8d);

		for (row = 0; row < 75; row++) {
			t = 0;

			for (i = 0; i < 8; i++)
				t |= ((vram[col + row * 8]) & (1 << (7-i))) ? (1 << i) : 0;
			st7585_spi_send_word(dd, 0x100 | t);
		}
	}
}

static struct fb_deferred_io st7585fb_defio = {
	.delay		= HZ/10,
	.deferred_io	= st7585fb_deferred_io,
};


static int st7585_probe(struct spi_device *spi)
{
	struct st7585_data *drvdata;
	int ret;

	drvdata = devm_kzalloc(&spi->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->spi = spi;
	spin_lock_init(&drvdata->lock);
	spi_set_drvdata(spi, drvdata);

	spi->mode = 3;
	spi->bits_per_word = 9;
	spi->max_speed_hz = 100000000;
	ret = spi_setup(spi);

	if (ret)
		return ret;

	drvdata->info = framebuffer_alloc(0, &spi->dev);
	if (!drvdata->info) {
		return -ENOMEM;
	}

	st7585_fix.smem_start = (unsigned long)drvdata->vram;
	st7585_fix.smem_len = VRAM_LEN;

	drvdata->info->par = drvdata;
	drvdata->info->flags = FBINFO_DEFAULT;
	drvdata->info->var = st7585_var;
	drvdata->info->fix = st7585_fix;
	drvdata->info->monspecs.hfmin = 0;
	drvdata->info->monspecs.hfmax = 0;
	drvdata->info->monspecs.vfmin = 10000;
	drvdata->info->monspecs.vfmax = 10000;
	drvdata->info->monspecs.dpms = 0;
	drvdata->info->fbops = &st7585fb_ops;
	drvdata->info->screen_base = drvdata->vram;
	drvdata->info->fbdefio = &st7585fb_defio;
	fb_deferred_io_init(drvdata->info);

	ret = register_framebuffer(drvdata->info);

	if (ret < 0) {
		framebuffer_release(drvdata->info);
		return -EINVAL;
	}

	/* Initialize panel */
	st7585_spi_send_word(drvdata, 0x21);
	st7585_spi_send_word(drvdata, 0x9c);
	st7585_spi_send_word(drvdata, 0x20);
	st7585_spi_send_word(drvdata, 0x08);

	/* Write initial buffer contents to panel to clear dirty lines */
	st7585fb_deferred_io(drvdata->info, NULL);

	/* Turn on panel */
	st7585_spi_send_word(drvdata, 0x20);
	st7585_spi_send_word(drvdata, 0x0c);
	st7585_spi_send_word(drvdata, 0x8d);


        printk(KERN_INFO "fb%d: %s frame buffer device\n",
	       drvdata->info->node, drvdata->info->fix.id);

	return 0;
}

static void st7585fb_schedule_refresh(struct fb_info *info, const struct fb_fillrect *rect)
{
	if (!info->fbdefio)
		return;

	schedule_delayed_work(&info->deferred_work, info->fbdefio->delay);
}

static int st7585_remove(struct spi_device *spi)
{
	struct st7585_data *drvdata = spi_get_drvdata(spi);

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

static struct spi_driver st7585_spi_driver = {
	.driver = {
		.name =		"st7585fb",
		.owner =	THIS_MODULE,
	},
	.probe =	st7585_probe,
	.remove =	st7585_remove,
};

static int __init st7585_init(void)
{
	return spi_register_driver(&st7585_spi_driver);
}
module_init(st7585_init);

static void __exit st7585_exit(void)
{
	spi_unregister_driver(&st7585_spi_driver);
}
module_exit(st7585_exit);

MODULE_AUTHOR("evilwombat");
MODULE_DESCRIPTION("Sitronix ST7585 LCD Controller Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:spitest");
