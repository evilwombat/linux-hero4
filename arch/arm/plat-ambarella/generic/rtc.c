/*
 * arch/arm/plat-ambarella/generic/rtc.c
 *
 * Author: Anthony Ginger <hfjiang@ambarella.com>
 *
 * Copyright (C) 2004-2010, Ambarella, Inc.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>

#include <mach/hardware.h>
#include <mach/init.h>

/* ==========================================================================*/
#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX	"ambarella_config."

/* ==========================================================================*/
int rtc_check_capacity(u32 tm)
{
	int					errCode = 0;

#if (RTC_SUPPORT_30BITS_PASSED_SECONDS == 1)
	if (tm & 0x40000000) {
		errCode = 0;
	} else {
		pr_err("%s: Invalid date (at least 2005.1.1) (0x%08x)\n",
			__func__, tm);
		errCode = -EINVAL;
	}
#endif
	return errCode;
}

static inline u32 rtc_time_write_fix(u32 tm)
{
#if (RTC_SUPPORT_30BITS_PASSED_SECONDS == 1)
	return (tm & 0x3fffffff);
#else
	return tm;
#endif
}

static inline void rtc_update_pos(void)
{
	amba_writel(RTC_POS0_REG, ambarella_platform_rtc_controller0.pos0);
	amba_writel(RTC_POS1_REG, ambarella_platform_rtc_controller0.pos1);
	amba_writel(RTC_POS2_REG, ambarella_platform_rtc_controller0.pos2);
}

void rtc_set_curt_time(u32 tm)
{
#ifndef CONFIG_PLAT_AMBARELLA_AMBALINK
	u32					bak_alat;

	bak_alat = amba_readl(RTC_ALAT_REG);
	rtc_update_pos();

	amba_writel(RTC_PWC_ALAT_REG, 0x0);
	amba_writel(RTC_PWC_CURT_REG, 0x0);
	amba_writel(RTC_RESET_REG, 0x01);
	mdelay(ambarella_platform_rtc_controller0.reset_delay);
	amba_writel(RTC_RESET_REG, 0x00);
	mdelay(ambarella_platform_rtc_controller0.reset_delay);

	amba_writel(RTC_PWC_CURT_REG, rtc_time_write_fix(tm));
	amba_writel(RTC_PWC_ALAT_REG, rtc_time_write_fix(bak_alat));
	amba_writel(RTC_RESET_REG, 0x01);
	mdelay(ambarella_platform_rtc_controller0.reset_delay);
	amba_writel(RTC_RESET_REG, 0x00);
	mdelay(ambarella_platform_rtc_controller0.reset_delay);
#endif
}

u32 rtc_check_status(void)
{
	u32					rtc_status;
	u32					need_clear = 0;
#if (RTC_POWER_LOST_DETECT == 1)
	u32					rtc_pwc_status;
#endif

	rtc_status = amba_readl(RTC_STATUS_REG);

#if (RTC_POWER_LOST_DETECT == 1)
	rtc_pwc_status = amba_readl(RTC_PWC_REG_STA_REG);
	amba_setbitsl(RTC_PWC_SET_STATUS_REG, 0x20);
	if ((rtc_pwc_status & 0x20) == 0x20)
		rtc_status |= RTC_STATUS_PC_RST;
	else
		rtc_status &= ~RTC_STATUS_PC_RST;
#endif

	if ((rtc_status & RTC_STATUS_PC_RST) != RTC_STATUS_PC_RST) {
		need_clear = 1;
		pr_warning("=====RTC ever lost power=====\n");
	}

	if ((rtc_status & RTC_STATUS_WKUP) == RTC_STATUS_WKUP)
		pr_debug("=====RTC wake up=====\n");

	if ((rtc_status & RTC_STATUS_ALA_WK) == RTC_STATUS_ALA_WK)
		pr_info("=====RTC alarm wake up=====\n");

	if (need_clear)
		rtc_set_curt_time(0);

	return rtc_status;
}

u32 rtc_get_curt_time(void)
{
	rtc_check_status();

#if (RTC_SUPPORT_30BITS_PASSED_SECONDS == 1)
	return (amba_readl(RTC_CURT_REG) | 0x40000000);
#else
#ifdef CONFIG_PLAT_AMBARELLA_AMBALINK
	/* Synchronize to ThreadX system time (TAI time spec).*/
	return (amba_readl(RTC_CURT_REG) + 10);
#else
	return amba_readl(RTC_CURT_REG);
#endif
#endif
}

void rtc_set_alat_time(u32 tm)
{
	u32					bak_curt;

	bak_curt = amba_readl(RTC_CURT_REG);
	rtc_update_pos();

	amba_writel(RTC_PWC_ALAT_REG, 0x0);
	amba_writel(RTC_PWC_CURT_REG, 0x0);
	amba_writel(RTC_RESET_REG, 0x01);
	mdelay(ambarella_platform_rtc_controller0.reset_delay);
	amba_writel(RTC_RESET_REG, 0x00);
	mdelay(ambarella_platform_rtc_controller0.reset_delay);

	amba_writel(RTC_PWC_CURT_REG, rtc_time_write_fix(bak_curt));
	amba_writel(RTC_PWC_ALAT_REG, rtc_time_write_fix(tm));
	amba_writel(RTC_RESET_REG, 0x01);
	mdelay(ambarella_platform_rtc_controller0.reset_delay);
	amba_writel(RTC_RESET_REG, 0x00);
	mdelay(ambarella_platform_rtc_controller0.reset_delay);
}

u32 rtc_get_alat_time(void)
{
	rtc_check_status();

#if (RTC_SUPPORT_30BITS_PASSED_SECONDS == 1)
	return (amba_readl(RTC_ALAT_REG) | 0x40000000);
#else
	return amba_readl(RTC_ALAT_REG);
#endif
}

#if defined(CONFIG_AMBARELLA_SYS_RTC_CALL)
int rtc_set_pos(const char *val, const struct kernel_param *kp)
{
	u32					bak_curt;
	u32					bak_alat;

	bak_curt = amba_readl(RTC_CURT_REG);
	bak_alat = amba_readl(RTC_ALAT_REG);
	rtc_update_pos();

	amba_writel(RTC_PWC_ALAT_REG, 0x0);
	amba_writel(RTC_PWC_CURT_REG, 0x0);
	amba_writel(RTC_RESET_REG, 0x01);
	mdelay(ambarella_platform_rtc_controller0.reset_delay);
	amba_writel(RTC_RESET_REG, 0x00);
	mdelay(ambarella_platform_rtc_controller0.reset_delay);

	amba_writel(RTC_PWC_CURT_REG, rtc_time_write_fix(bak_curt));
	amba_writel(RTC_PWC_ALAT_REG, rtc_time_write_fix(bak_alat));
	amba_writel(RTC_RESET_REG, 0x01);
	mdelay(ambarella_platform_rtc_controller0.reset_delay);
	amba_writel(RTC_RESET_REG, 0x00);
	mdelay(ambarella_platform_rtc_controller0.reset_delay);

	return 0;
}

static struct kernel_param_ops param_ops_rtcpos = {
	.set = rtc_set_pos,
	.get = NULL,
};
#endif

struct ambarella_rtc_controller ambarella_platform_rtc_controller0 = {
	.pos0		= 0x80,
	.pos1		= 0x80,
	.pos2		= 0x80,
	.check_capacity	= rtc_check_capacity,
	.check_status	= rtc_check_status,
	.set_curt_time	= rtc_set_curt_time,
	.get_curt_time	= rtc_get_curt_time,
	.set_alat_time	= rtc_set_alat_time,
	.get_alat_time	= rtc_get_alat_time,
#if	(CHIP_REV == A5S)
	.reset_delay	= 3,
#else
	.reset_delay	= 1,
#endif
};
#if defined(CONFIG_AMBARELLA_SYS_RTC_CALL)
AMBA_RTC_PARAM_CALL(0, ambarella_platform_rtc_controller0,
	0644, param_ops_rtcpos);
#endif

struct platform_device ambarella_rtc0 = {
	.name		= "ambarella-rtc",
	.id		= -1,
	.resource	= NULL,
	.num_resources	= 0,
	.dev		= {
		.platform_data		= &ambarella_platform_rtc_controller0,
		.dma_mask		= &ambarella_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	}
};

