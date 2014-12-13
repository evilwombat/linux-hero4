/*
 * arch/arm/plat-ambarella/generic/sd.c
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
#include <linux/irq.h>
#include <linux/moduleparam.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/proc_fs.h>

#include <mach/hardware.h>
#include <plat/fio.h>
#include <plat/sd.h>
#include <plat/clk.h>

/* ==========================================================================*/
#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX	"ambarella_config."

/* ==========================================================================*/
#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_HAL)
#else
static struct clk gclk_sd = {
	.parent		= NULL,
	.name		= "gclk_sd",
	.rate		= 0,
	.frac_mode	= 0,
	.ctrl_reg	= PLL_REG_UNAVAILABLE,
	.pres_reg	= PLL_REG_UNAVAILABLE,
	.post_reg	= SCALER_SD48_REG,
	.frac_reg	= PLL_REG_UNAVAILABLE,
	.ctrl2_reg	= PLL_REG_UNAVAILABLE,
	.ctrl3_reg	= PLL_REG_UNAVAILABLE,
	.lock_reg	= PLL_REG_UNAVAILABLE,
	.lock_bit	= 0,
	.divider	= 0,
	.max_divider	= (1 << 16) - 1,
	.extra_scaler	= 0,
	.ops		= &ambarella_rct_scaler_ops,
};

static struct clk *ambarella_sd_register_clk(void)
{
	struct clk *pgclk_sd = NULL;
	struct clk *ppll_out_core = NULL;

	pgclk_sd = clk_get(NULL, "gclk_sd");
	if (IS_ERR(pgclk_sd)) {
		ppll_out_core = clk_get(NULL, "pll_out_core");
		if (IS_ERR(ppll_out_core)) {
			BUG();
		}
		gclk_sd.parent = ppll_out_core;
		ambarella_register_clk(&gclk_sd);
		pgclk_sd = &gclk_sd;
		pr_info("SYSCLK:SD[%lu]\n", clk_get_rate(pgclk_sd));
	}

	return pgclk_sd;
}

#if (SD_HAS_SDXC_CLOCK == 1)
static struct clk gclk_sdxc = {
	.parent		= NULL,
	.name		= "gclk_sdxc",
	.rate		= 0,
	.frac_mode	= 0,
	.ctrl_reg	= PLL_SDXC_CTRL_REG,
	.pres_reg	= PLL_REG_UNAVAILABLE,
	.post_reg	= SCALER_SDXC_POST_REG,
	.frac_reg	= PLL_SDXC_FRAC_REG,
	.ctrl2_reg	= PLL_SDXC_CTRL2_REG,
	.ctrl3_reg	= PLL_SDXC_CTRL3_REG,
	.lock_reg	= PLL_LOCK_REG,
	.lock_bit	= 10,
	.divider	= 0,
	.max_divider	= 0,
	.extra_scaler	= 0,
	.ops		= &ambarella_rct_pll_ops,
};

static struct clk *ambarella_sdxc_register_clk(void)
{
	struct clk *pgclk_sdxc = NULL;

	pgclk_sdxc = clk_get(NULL, "gclk_sdxc");
	if (IS_ERR(pgclk_sdxc)) {
		ambarella_register_clk(&gclk_sdxc);
		pgclk_sdxc = &gclk_sdxc;
		pr_info("SYSCLK:SDXC[%lu]\n", clk_get_rate(pgclk_sdxc));
	}

	return pgclk_sdxc;
}
#endif

#if (SD_HAS_SDIO_CLOCK == 1)
static struct clk gclk_sdio = {
	.parent		= NULL,
	.name		= "gclk_sdio",
	.rate		= 0,
	.frac_mode	= 0,
	.ctrl_reg	= PLL_REG_UNAVAILABLE,
	.pres_reg	= PLL_REG_UNAVAILABLE,
	.post_reg	= SCALER_SDIO_REG,
	.frac_reg	= PLL_REG_UNAVAILABLE,
	.ctrl2_reg	= PLL_REG_UNAVAILABLE,
	.ctrl3_reg	= PLL_REG_UNAVAILABLE,
	.lock_reg	= PLL_REG_UNAVAILABLE,
	.lock_bit	= 0,
	.divider	= 0,
	.max_divider	= (1 << 16) - 1,
	.extra_scaler	= 0,
	.ops		= &ambarella_rct_scaler_ops,
};

static struct clk *ambarella_sdio_register_clk(void)
{
	struct clk *pgclk_sdio = NULL;
	struct clk *ppll_out_core = NULL;

	pgclk_sdio = clk_get(NULL, "gclk_sdio");
	if (IS_ERR(pgclk_sdio)) {
		ppll_out_core = clk_get(NULL, "pll_out_core");
		if (IS_ERR(ppll_out_core)) {
			BUG();
		}
		gclk_sdio.parent = ppll_out_core;
		ambarella_register_clk(&gclk_sdio);
		pgclk_sdio = &gclk_sdio;
		pr_info("SYSCLK:SDIO[%lu]\n", clk_get_rate(pgclk_sdio));
	}

	return pgclk_sdio;
}
#endif

#endif

static void ambarella_sd_set_pll(u32 freq_hz)
{
#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_HAL)
	amb_set_sd_clock_frequency(HAL_BASE_VP, freq_hz);
#else
	clk_set_rate(ambarella_sd_register_clk(), freq_hz);
#endif
}

static u32 ambarella_sd_get_pll(void)
{
#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_HAL)
	return (u32)amb_get_sd_clock_frequency(HAL_BASE_VP);
#else
	return clk_get_rate(ambarella_sd_register_clk());
#endif
}

#if (SD_HAS_SDXC_CLOCK == 1)
static void ambarella_sdxc_set_pll(u32 freq_hz)
{
#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_HAL)
	amb_set_sdxc_clock_frequency(HAL_BASE_VP, freq_hz);
#else
	clk_set_rate(ambarella_sdxc_register_clk(), freq_hz);
#endif
}

static u32 ambarella_sdxc_get_pll(void)
{
#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_HAL)
	return (u32)amb_get_sdxc_clock_frequency(HAL_BASE_VP);
#else
	return clk_get_rate(ambarella_sdxc_register_clk());
#endif
}
#endif

#if (SD_HAS_SDIO_CLOCK == 1)
static void ambarella_sdio_set_pll(u32 freq_hz)
{
#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_HAL)
	amb_set_sdio_clock_frequency(HAL_BASE_VP, freq_hz);
#else
	clk_set_rate(ambarella_sdio_register_clk(), freq_hz);
#endif
}

static u32 ambarella_sdio_get_pll(void)
{
#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_HAL)
	return (u32)amb_get_sdio_clock_frequency(HAL_BASE_VP);
#else
	return clk_get_rate(ambarella_sdio_register_clk());
#endif
}
#endif

/* ==========================================================================*/
typedef void (*mmc_dc_fn)(struct mmc_host *host, unsigned long delay);

#if defined(CONFIG_AMBARELLA_SYS_SD_CALL)
int ambarella_sd_set_fixed_cd(const char *val, const struct kernel_param *kp)
{
	struct ambarella_sd_slot		*pslotinfo;
	mmc_dc_fn				mmc_dc = NULL;
	int					retval;

	retval = param_set_int(val, kp);
	pslotinfo = container_of(kp->arg, struct ambarella_sd_slot, fixed_cd);
#if defined(CONFIG_MMC)
	mmc_dc = mmc_detect_change;
#else
#if defined(CONFIG_KALLSYMS)
	mmc_dc = (mmc_dc_fn)module_kallsyms_lookup_name("mmc_detect_change");
#endif
#endif
	if (!retval && pslotinfo && mmc_dc && pslotinfo->pmmc_host) {
		mmc_dc(pslotinfo->pmmc_host, pslotinfo->cd_delay);
	}

	return retval;
}

static struct kernel_param_ops param_ops_ambarella_sd_cdpos = {
	.set = ambarella_sd_set_fixed_cd,
	.get = param_get_int,
};
#endif

/* ==========================================================================*/
struct resource ambarella_sd0_resources[] = {
	[0] = {
		.start	= SD_BASE,
		.end	= SD_BASE + 0x0FFF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= SD_IRQ,
		.end	= SD_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

void fio_amb_sd0_slot1_request(void)
{
	fio_select_lock(SELECT_FIO_SD);
}

void fio_amb_sd0_slot1_release(void)
{
	fio_unlock(SELECT_FIO_SD);
}

#if (SD_HAS_INTERNAL_MUXER == 1)
void fio_amb_sd0_slot2_request(void)
{
	fio_select_lock(SELECT_FIO_SDIO);
}

void fio_amb_sd0_slot2_release(void)
{
	fio_unlock(SELECT_FIO_SDIO);
}
#endif

int fio_amb_sd_check_owner(void)
{
#if (SD_HOST1_HOST2_HAS_MUX == 1)
	return 1;
#else
	return fio_amb_sd0_is_enable();
#endif
}

struct ambarella_sd_controller ambarella_platform_sd_controller0 = {
#if (SD_HAS_INTERNAL_MUXER == 1)
	.num_slots		= 2,
#else
	.num_slots		= 1,
#endif
	.slot[0] = {
		.pmmc_host	= NULL,

		.default_caps	= MMC_CAP_4_BIT_DATA |
				MMC_CAP_SDIO_IRQ |
				MMC_CAP_ERASE |
				MMC_CAP_BUS_WIDTH_TEST,
		.active_caps	= 0,
		.default_caps2	= 0,
		.active_caps2	= 0,
		.private_caps	= AMBA_SD_PRIVATE_CAPS_DTO_BY_SDCLK,

		.ext_power	= {
			.gpio_id	= -1,
			.active_level	= GPIO_LOW,
			.active_delay	= 1,
		},
		.ext_reset	= {
			.gpio_id	= -1,
			.active_level	= GPIO_LOW,
			.active_delay	= 1,
		},
		.fixed_cd	= -1,
#if (SD_HAS_INTERNAL_MUXER == 1)
		.gpio_cd	= {
			.irq_gpio	= SD1_CD,
			.irq_line	= SD1CD_IRQ,
			.irq_type	= IRQ_TYPE_EDGE_BOTH,
			.irq_gpio_val	= GPIO_LOW,
			.irq_gpio_mode	= GPIO_FUNC_HW,
		},
#else
		.gpio_cd	= {
			.irq_gpio	= -1,
			.irq_line	= -1,
			.irq_type	= -1,
			.irq_gpio_val	= GPIO_LOW,
			.irq_gpio_mode	= GPIO_FUNC_SW_INPUT,
		},
#endif
		.cd_delay	= 100,
		.fixed_wp	= -1,
		.gpio_wp	= {
			.gpio_id	= -1,
			.active_level	= GPIO_HIGH,
			.active_delay	= 1,
		},
		.check_owner	= fio_amb_sd_check_owner,
		.request	= fio_amb_sd0_slot1_request,
		.release	= fio_amb_sd0_slot1_release,
		.set_int	= fio_amb_sd0_set_int,
		.set_vdd	= NULL,
		.set_bus_timing	= NULL,
	},
#if (SD_HAS_INTERNAL_MUXER == 1)
	.slot[1] = {
		.pmmc_host	= NULL,

		.default_caps	= MMC_CAP_4_BIT_DATA |
				MMC_CAP_SDIO_IRQ |
				MMC_CAP_ERASE |
				MMC_CAP_BUS_WIDTH_TEST,
		.active_caps	= 0,
		.default_caps2	= 0,
		.active_caps2	= 0,
		.private_caps	= AMBA_SD_PRIVATE_CAPS_DTO_BY_SDCLK,

		.ext_power	= {
			.gpio_id	= -1,
			.active_level	= GPIO_LOW,
			.active_delay	= 1,
		},
		.ext_reset	= {
			.gpio_id	= -1,
			.active_level	= GPIO_LOW,
			.active_delay	= 1,
		},
		.fixed_cd	= -1,
		.gpio_cd	= {
			.irq_gpio	= -1,
			.irq_line	= -1,
			.irq_type	= -1,
			.irq_gpio_val	= GPIO_LOW,
			.irq_gpio_mode	= GPIO_FUNC_SW_INPUT,
		},
		.cd_delay	= 100,
		.fixed_wp	= -1,
		.gpio_wp	= {
			.gpio_id	= -1,
			.active_level	= GPIO_HIGH,
			.active_delay	= 1,
		},
		.check_owner	= fio_amb_sdio0_is_enable,
		.request	= fio_amb_sd0_slot2_request,
		.release	= fio_amb_sd0_slot2_release,
		.set_int	= fio_amb_sdio0_set_int,
		.set_vdd	= NULL,
		.set_bus_timing	= NULL,
	},
#endif
	.set_pll		= ambarella_sd_set_pll,
	.get_pll		= ambarella_sd_get_pll,
	.max_blk_mask		= SD_BLK_SZ_128KB,
	.max_clock		= 24000000,
	.active_clock		= 0,
	.wait_tmo		= ((5 * HZ) / 2),
	.pwr_delay		= 1,

	.dma_fix		= 0,
#if (SD_SUPPORT_PLL_SCALER == 1)
	.support_pll_scaler	= 1,
#else
	.support_pll_scaler	= 0,
#endif
};
#if defined(CONFIG_AMBARELLA_SYS_SD_CALL)
module_param_cb(sd0_max_block, &param_ops_uint,
	&(ambarella_platform_sd_controller0.max_blk_mask), 0644);
module_param_cb(sd0_max_clock, &param_ops_uint,
	&(ambarella_platform_sd_controller0.max_clock), 0644);
module_param_cb(sd0_active_clock, &param_ops_uint,
	&(ambarella_platform_sd_controller0.active_clock), 0444);
module_param_cb(sd0_wait_timeout, &param_ops_uint,
	&(ambarella_platform_sd_controller0.wait_tmo), 0644);
module_param_cb(sd0_pwr_delay, &param_ops_uint,
	&(ambarella_platform_sd_controller0.pwr_delay), 0644);
AMBA_SD_PARAM_CALL(0, 0, ambarella_platform_sd_controller0,
	&param_ops_ambarella_sd_cdpos, 0644);
#if (SD_HAS_INTERNAL_MUXER == 1)
AMBA_SD_PARAM_CALL(0, 1, ambarella_platform_sd_controller0,
	&param_ops_ambarella_sd_cdpos, 0644);
#endif
#endif

struct platform_device ambarella_sd0 = {
	.name		= "ambarella-sd",
	.id		= 0,
	.resource	= ambarella_sd0_resources,
	.num_resources	= ARRAY_SIZE(ambarella_sd0_resources),
	.dev		= {
		.platform_data		= &ambarella_platform_sd_controller0,
		.dma_mask		= &ambarella_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	}
};

#if (SD_INSTANCES >= 2)
struct resource ambarella_sd1_resources[] = {
	[0] = {
		.start	= SD2_BASE,
		.end	= SD2_BASE + 0x0FFF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= SD2_IRQ,
		.end	= SD2_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

int fio_amb_sd2_check_owner(void)
{
#if (FIO_SUPPORT_AHB_CLK_ENA == 1)
	return fio_amb_sd2_is_enable();
#endif
	return 1;
}

void fio_amb_sd2_request(void)
{
#if (FIO_SUPPORT_AHB_CLK_ENA == 1)
	fio_select_lock(SELECT_FIO_SD2);
#endif
#if (SD_HOST1_HOST2_HAS_MUX == 1)
	fio_select_lock(SELECT_FIO_SD2);
#endif
}

void fio_amb_sd2_release(void)
{
#if (FIO_SUPPORT_AHB_CLK_ENA == 1)
	fio_unlock(SELECT_FIO_SD2);
#endif
#if (SD_HOST1_HOST2_HAS_MUX == 1)
	fio_unlock(SELECT_FIO_SD2);
#endif
}

static DEFINE_SPINLOCK(fio_amb_sd2_int_lock);
void fio_amb_sd2_set_int(u32 mask, u32 on)
{
	unsigned long				flags;
	u32					int_flag;

	spin_lock_irqsave(&fio_amb_sd2_int_lock, flags);
	int_flag = amba_readl(SD2_NISEN_REG);
	if (on)
		int_flag |= mask;
	else
		int_flag &= ~mask;
	amba_writel(SD2_NISEN_REG, int_flag);
	amba_writel(SD2_NIXEN_REG, int_flag);
	spin_unlock_irqrestore(&fio_amb_sd2_int_lock, flags);
}

struct ambarella_sd_controller ambarella_platform_sd_controller1 = {
	.num_slots		= 1,
	.slot[0] = {
		.pmmc_host	= NULL,

		.default_caps	= MMC_CAP_4_BIT_DATA |
				MMC_CAP_SDIO_IRQ |
				MMC_CAP_ERASE |
				MMC_CAP_BUS_WIDTH_TEST,
		.active_caps	= 0,
		.default_caps2	= 0,
		.active_caps2	= 0,
		.private_caps	= AMBA_SD_PRIVATE_CAPS_DTO_BY_SDCLK,

		.ext_power	= {
			.gpio_id	= -1,
			.active_level	= GPIO_LOW,
			.active_delay	= 1,
		},
		.ext_reset	= {
			.gpio_id	= -1,
			.active_level	= GPIO_LOW,
			.active_delay	= 1,
		},
		.fixed_cd	= -1,
		.gpio_cd	= {
			.irq_gpio	= -1,
			.irq_line	= -1,
			.irq_type	= -1,
			.irq_gpio_val	= GPIO_LOW,
			.irq_gpio_mode	= GPIO_FUNC_SW_INPUT,
		},
		.cd_delay	= 100,
		.fixed_wp	= -1,
		.gpio_wp	= {
			.gpio_id	= -1,
			.active_level	= GPIO_HIGH,
			.active_delay	= 1,
		},
		.check_owner	= fio_amb_sd2_check_owner,
		.request	= fio_amb_sd2_request,
		.release	= fio_amb_sd2_release,
		.set_int	= fio_amb_sd2_set_int,
		.set_vdd	= NULL,
		.set_bus_timing	= NULL,
	},
#if (SD_HAS_SDXC_CLOCK == 1)
	.set_pll		= ambarella_sdxc_set_pll,
	.get_pll		= ambarella_sdxc_get_pll,
#elif (SD_HAS_SDIO_CLOCK == 1)
	.set_pll		= ambarella_sdio_set_pll,
	.get_pll		= ambarella_sdio_get_pll,
#else
	.set_pll		= ambarella_sd_set_pll,
	.get_pll		= ambarella_sd_get_pll,
#endif
	.max_blk_mask		= SD_BLK_SZ_128KB,
	.max_clock		= 24000000,
	.active_clock		= 0,
	.wait_tmo		= ((5 * HZ) / 2),
	.pwr_delay		= 1,

#if (CHIP_REV == S2)
	.dma_fix		= 0xC0000000,
#else
	.dma_fix		= 0,
#endif
#if (SD_SUPPORT_PLL_SCALER == 1)
	.support_pll_scaler	= 1,
#else
	.support_pll_scaler	= 0,
#endif
};
#if defined(CONFIG_AMBARELLA_SYS_SD_CALL)
module_param_cb(sd1_max_block, &param_ops_uint,
	&(ambarella_platform_sd_controller1.max_blk_mask), 0644);
module_param_cb(sd1_max_clock, &param_ops_uint,
	&(ambarella_platform_sd_controller1.max_clock), 0644);
module_param_cb(sd1_active_clock, &param_ops_uint,
	&(ambarella_platform_sd_controller1.active_clock), 0444);
module_param_cb(sd1_wait_timeout, &param_ops_uint,
	&(ambarella_platform_sd_controller1.wait_tmo), 0644);
module_param_cb(sd1_pwr_delay, &param_ops_uint,
	&(ambarella_platform_sd_controller1.pwr_delay), 0644);
AMBA_SD_PARAM_CALL(1, 0, ambarella_platform_sd_controller1,
	&param_ops_ambarella_sd_cdpos, 0644);
#endif

struct platform_device ambarella_sd1 = {
	.name		= "ambarella-sd",
	.id		= 1,
	.resource	= ambarella_sd1_resources,
	.num_resources	= ARRAY_SIZE(ambarella_sd1_resources),
	.dev		= {
		.platform_data		= &ambarella_platform_sd_controller1,
		.dma_mask		= &ambarella_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	}
};
#endif
static struct proc_dir_entry *proc_sdio;
static int ambarella_sdio_ds_value_2ma(u32 value)
{
	enum amba_sdio_ds_value {
		DS_3MA = 0,
		DS_12MA,
		DS_6MA,
		DS_18MA
	};

	switch (value) {
	case DS_3MA:
		return 3;
	case DS_12MA:
		return 12;
	case DS_6MA:
		return 6;
	case DS_18MA:
		return 18;
	default:
		printk(KERN_ERR "%s: unknown driving strength\n", __func__);
		return 0;
	}
}

static int ambarella_sdio_proc_read(char *page, char **start, off_t off,
				   int count, int *eof, void *data)
{
#ifndef IOCTRL_GPIO_DS0_2
#define IOCTRL_GPIO_DS0_2		RCT_REG(0x278)
#endif
#ifndef IOCTRL_GPIO_DS1_2
#define IOCTRL_GPIO_DS1_2		RCT_REG(0x28c)
#endif

	int len = 0;
	u32 reg, ds1, ds_value, b0, b1;

	reg = amba_readl(SD2_REG(SD_HOST_OFFSET));
	pr_debug("amba_readl 0x%x = 0x%x\n", SD2_REG(SD_HOST_OFFSET), reg);
	if (reg & SD_HOST_HIGH_SPEED)
		len += sprintf(page+len, "SDIO high speed mode:            yes\n");
	else
		len += sprintf(page+len, "SDIO high speed mode:            no\n");

	reg = amba_rct_readl(MS_DELAY_CTRL_REG);
	pr_debug("amba_rct_readl 0x%x = 0x%x\n", MS_DELAY_CTRL_REG, reg);
	//[31:29] SDIO output data delay control
	len += sprintf(page+len, "SDIO output data delay:          %u\n",
		(reg & (0x7 << 29)) >> 29);
	//[23:21] SDIO output clock delay control
	len += sprintf(page+len, "SDIO output clock delay:         %u\n",
		(reg & (0x7 << 21)) >> 21);
	//[15:13] SDIO input data delay control
	len += sprintf(page+len, "SDIO input data delay:           %u\n",
		(reg & (0x7 << 13)) >> 13);
	//[7:5] SDIO clock input delay control
	len += sprintf(page+len, "SDIO input clock delay:          %u\n",
		(reg & (0x7 << 5)) >> 5);

	reg = amba_rct_readl(IOCTRL_GPIO_DS0_2);
	pr_debug("amba_rct_readl 0x%x = 0x%x\n", IOCTRL_GPIO_DS0_2, reg);
	ds1 = amba_rct_readl(IOCTRL_GPIO_DS1_2);
	pr_debug("amba_rct_readl 0x%x = 0x%x\n", IOCTRL_GPIO_DS1_2, ds1);

	// [11:8] SDIO data, assume all bits have same value
	b0 = (reg & (0x1 << 8)) >> 8;
	b1 = (ds1 & (0x1 << 8)) >> 8;
	ds_value = b0 + (b1 << 0x1);
	len += sprintf(page+len, "SDIO data driving strength:      %u mA\n",
		ambarella_sdio_ds_value_2ma(ds_value));
	// [6] SDIO clock,
	b0 = (reg & (0x1 << 6)) >> 6;
	b1 = (ds1 & (0x1 << 6)) >> 6;
	ds_value = b0 + (b1 << 0x1);
	len += sprintf(page+len, "SDIO clock driving strength:     %u mA\n",
		ambarella_sdio_ds_value_2ma(ds_value));
	// [7] SDIO cmd,
	b0 = (reg & (0x1 << 7)) >> 7;
	b1 = (ds1 & (0x1 << 7)) >> 7;
	ds_value = b0 + (b1 << 0x1);
	len += sprintf(page+len, "SDIO command driving strength:   %u mA\n",
		ambarella_sdio_ds_value_2ma(ds_value));
	// [13:12] SDIO cdwp, assume both bits have same value
	b0 = (reg & (0x1 << 12)) >> 12;
	b1 = (ds1 & (0x1 << 12)) >> 12;
	ds_value = b0 + (b1 << 0x1);
	len += sprintf(page+len, "SDIO CD and WP driving strength: %u mA\n",
		ambarella_sdio_ds_value_2ma(ds_value));

	*eof = 1;
	return len;
}

int __init ambarella_init_sd(void)
{
	int retval = 0;

	proc_sdio = create_proc_entry("sdio_info", S_IRUGO | S_IWUSR, get_ambarella_proc_dir());
	proc_sdio->read_proc = ambarella_sdio_proc_read;

	return retval;
}

void ambarella_detect_sd_slot(int bus, int slot, int fixed_cd)
{
	struct ambarella_sd_slot		*pslotinfo = NULL;
	mmc_dc_fn				mmc_dc = NULL;

	pr_debug("%s:%d[%d:%d:%d]\n", __func__, __LINE__, bus, slot, fixed_cd);
	if ((bus == 0) && (slot == 0)) {
		pslotinfo = &ambarella_platform_sd_controller0.slot[0];
	}
#if (SD_HAS_INTERNAL_MUXER == 1)
	if ((bus == 0) && (slot == 1)) {
		pslotinfo = &ambarella_platform_sd_controller0.slot[1];
	}
#endif
#if (SD_INSTANCES >= 2)
	if ((bus == 1) && (slot == 0)) {
		pslotinfo = &ambarella_platform_sd_controller1.slot[0];
	}
#endif

#if defined(CONFIG_MMC)
	mmc_dc = mmc_detect_change;
#else
#if defined(CONFIG_KALLSYMS)
	mmc_dc = (mmc_dc_fn)module_kallsyms_lookup_name("mmc_detect_change");
#endif
#endif
	if (pslotinfo && mmc_dc && pslotinfo->pmmc_host) {
		pslotinfo->fixed_cd = fixed_cd;
		mmc_dc(pslotinfo->pmmc_host, pslotinfo->cd_delay);
		pr_debug("%s:%d[%p:%p:%p:%d]\n", __func__, __LINE__, pslotinfo,
			mmc_dc, pslotinfo->pmmc_host, pslotinfo->cd_delay);
	}
}
EXPORT_SYMBOL(ambarella_detect_sd_slot);

