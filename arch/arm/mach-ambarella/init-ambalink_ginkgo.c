/*
 * arch/arm/mach-ambarella/init-ambalink_ginkgo.c
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
#include <linux/clk.h>
// Next line is for GoPro Banzailink version string
#include <linux_fw_version.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>
#include <asm/gpio.h>
#include <asm/system_info.h>

#include <mach/hardware.h>
#include <mach/init.h>
#include <mach/board.h>
#include <mach/common.h>

#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <linux/mmc/host.h>

#include <plat/ambinput.h>
#include <plat/ambcache.h>

#include <plat/dma.h>
#include <plat/clk.h>

#ifdef CONFIG_RPROC_S2
extern struct platform_device ambarella_rproc_cortex_dev;
#endif

/* ==========================================================================*/
static struct platform_device *ambarella_devices[] __initdata = {
	&ambarella_sd1,
	&ambarella_uart1,
	&ambarella_uart2,
	&ambarella_nand,
#ifdef CONFIG_RPROC_S2
	&ambarella_rproc_cortex_dev,
#endif
};

static void ginkgo_ipcam_sd_set_bus_timing(u32 timing)
{
	u32 ms_delay_reg;

	pr_debug("%s = %d\n", __func__, timing);
	ms_delay_reg = amba_rct_readl(MS_DELAY_CTRL_REG);
	ms_delay_reg &= 0xE3E0E3E0;
	if (timing == MMC_TIMING_UHS_DDR50) {
		ms_delay_reg |= ((0x7 << 16) | (0x7 << 10));
	}
	amba_rct_writel(MS_DELAY_CTRL_REG, ms_delay_reg);
}

static void ginkgo_ipcam_sdio_set_bus_timing(u32 timing)
{
	u32 ms_delay_reg;

	pr_debug("%s = %d\n", __func__, timing);
	ms_delay_reg = amba_rct_readl(MS_DELAY_CTRL_REG);
	ms_delay_reg &= 0x1C1F1C1F;
	if (timing == MMC_TIMING_UHS_DDR50) {
		ms_delay_reg |= ((0x7 << 21) | (0x7 << 13));
	}
	amba_rct_writel(MS_DELAY_CTRL_REG, ms_delay_reg);
}

/* ==========================================================================*/
static void __init ambarella_init_ginkgo(void)
{
	int i;
	u32 core_freq;

	ambarella_init_machine("GoPro " LINUX_FW_VERSION " HAWAII banzailink 18.1");

#if defined(CONFIG_OUTER_CACHE) && !defined(CONFIG_AMBALINK_MULTIPLE_CORE)
	ambcache_l2_enable();
#endif

	/* Config SD */
	fio_default_owner = SELECT_FIO_SD;
	ambarella_platform_sd_controller1.max_blk_mask = SD_BLK_SZ_512KB;

#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_HAL)
	core_freq = get_core_bus_freq_hz();
#else
	core_freq = clk_get_rate(clk_get(NULL, "gclk_core"));
#endif

	if (AMBARELLA_BOARD_TYPE(system_rev) == AMBARELLA_BOARD_TYPE_IPCAM) {
		switch (AMBARELLA_BOARD_REV(system_rev)) {
		case 'A':
#ifdef CONFIG_AMBARELLA_SDIO_MAX_CLK
			ambarella_platform_sd_controller1.max_clock = CONFIG_AMBARELLA_SDIO_MAX_CLK;
#else
			ambarella_platform_sd_controller1.max_clock = 48000000;
#endif
			ambarella_platform_sd_controller1.slot[0].private_caps |=
			        (AMBA_SD_PRIVATE_CAPS_VDD_18 |
			         AMBA_SD_PRIVATE_CAPS_DDR);

			ambarella_platform_sd_controller1.slot[0].ext_power.active_level = GPIO_HIGH;
			ambarella_platform_sd_controller1.slot[0].ext_power.active_delay = 300;
			ambarella_platform_sd_controller1.slot[0].fixed_cd = 1;
			ambarella_platform_sd_controller1.slot[0].set_bus_timing =
			        ginkgo_ipcam_sdio_set_bus_timing;

			platform_device_register(&ambarella_rtc0);

			break;

		default:
			pr_warn("%s: Unknown EVK Rev[%d]\n", __func__,
			        AMBARELLA_BOARD_REV(system_rev));
			break;
		}
	}

	platform_add_devices(ambarella_devices, ARRAY_SIZE(ambarella_devices));
	for (i = 0; i < ARRAY_SIZE(ambarella_devices); i++) {
		device_set_wakeup_capable(&ambarella_devices[i]->dev, 1);
		device_set_wakeup_enable(&ambarella_devices[i]->dev, 0);
	}
}

#ifdef CONFIG_ARM_GIC
static void gic_irq_disable(struct irq_data *d)
{
#ifdef CONFIG_PLAT_AMBARELLA_BOSS
	u32 line = d->hwirq;

	/* Disable in BOSS. */
	boss_disable_irq(line);
#endif
}

static void gic_irq_enable(struct irq_data *d)
{
#ifdef CONFIG_PLAT_AMBARELLA_BOSS
	u32 line = d->hwirq;

	/* Enable in BOSS. */
	boss_enable_irq(line);
#endif
}

static void ginkgo_amp_mask(struct irq_data *d)
{
	void __iomem *addr;
	u32 line = d->hwirq;
	u32 base = (u32)__io(AMBARELLA_VA_GIC_DIST_BASE);
	u32 mask;

	// set distribution to core0
	//printk("{{{{ gic mask line %d }}}}\n", line);
	addr = (void __iomem *) (base + GIC_DIST_TARGET + (line >> 2) * 4);
	mask = readl_relaxed(addr);
	mask &= ~(0xFF << ((line % 4) * 8));

	/* Direct the interrupt to Core-0. */
	mask |= (0x1 << ((line % 4) * 8));
	writel_relaxed(mask, addr);
}

u32 gic_resume_mask[32];

static void ginkgo_amp_unmask(struct irq_data *d)
{
	void __iomem *addr;
	u32 line = d->hwirq;
	u32 base = (u32)__io(AMBARELLA_VA_GIC_DIST_BASE);
	u32 mask;

	gic_resume_mask[line >> 5] |= (1 << (line % 32));

	// set distribution to core1
	//printk("{{{{ gic unmask line %d }}}}\n", line);
	addr = (void __iomem *) (base + GIC_DIST_TARGET + (line >> 2) * 4);
	mask = readl_relaxed(addr);
	mask &= ~(0xFF << ((line % 4) * 8));
	mask |= (0x2 << ((line % 4) * 8));
	writel_relaxed(mask, addr);
}
#endif

static void __init ginkgo_init_irq(void)
{
#ifdef CONFIG_ARM_GIC
	memset(gic_resume_mask, 0x0, sizeof(u32) * 32);

	// In case of AMP, we disable general gic_dist_init in gic.c
	// Instead, we distribute irq to core-1 on the fly when an irq
	// is unmasked in ginkgo_amp_unmask.
	gic_arch_extn.irq_disable = gic_irq_disable;
	gic_arch_extn.irq_enable = gic_irq_enable;

	gic_arch_extn.irq_mask = ginkgo_amp_mask;
	gic_arch_extn.irq_unmask = ginkgo_amp_unmask;
#endif
	ambarella_init_irq();
}

/* ==========================================================================*/
MACHINE_START(GINKGO, "GoPro HAWAII banzailink!")
.atag_offset	= 0x100,
.restart_mode	= 's',
.smp		= smp_ops(ambarella_smp_ops),
.reserve	= ambarella_memblock_reserve,
.map_io		= ambarella_map_io,
.init_irq	= ginkgo_init_irq,
.timer		= &ambarella_timer,
.init_machine	= ambarella_init_ginkgo,
#if defined(CONFIG_ARM_GIC)
.handle_irq	= gic_handle_irq,
#endif
.restart	= ambarella_restart_machine,
MACHINE_END

