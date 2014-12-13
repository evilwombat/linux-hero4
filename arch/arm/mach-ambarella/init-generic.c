/*
 * arch/arm/mach-ambarella/init-generic.c
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

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>
#include <asm/gpio.h>
#include <asm/system_info.h>

#include <mach/hardware.h>
#include <mach/init.h>
#include <mach/board.h>
#include <mach/common.h>

#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>

#include <linux/i2c.h>

#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <plat/ambinput.h>
#include <plat/ambcache.h>

#include "board-device.h"

#include <plat/dma.h>

#include <linux/input.h>

/* ==========================================================================*/
static struct platform_device *ambarella_devices[] __initdata = {
	&ambarella_adc0,
#ifdef CONFIG_PLAT_AMBARELLA_SUPPORT_SATA
	&ambarella_ahci0,
#endif
#ifdef CONFIG_PLAT_AMBARELLA_SUPPORT_HW_CRYPTO
	&ambarella_crypto,
#endif
	&ambarella_dummy_codec0,
#ifdef CONFIG_PLAT_AMBARELLA_SUPPORT_UHC
	&ambarella_ehci0,
#endif
#if (ETH_INSTANCES >= 1)
	&ambarella_eth0,
#endif
#if (ETH_INSTANCES >= 2)
	&ambarella_eth1,
#endif
	&ambarella_fb0,
	&ambarella_fb1,
	&ambarella_i2s0,
#ifdef CONFIG_PLAT_AMBARELLA_SUPPORT_I2C_MUX
	&ambarella_idc0_mux,
#endif
	&ambarella_idc0,
#if (IDC_INSTANCES >= 2)
	&ambarella_idc1,
#endif
#ifdef CONFIG_INPUT_AMBARELLA_IR
	&ambarella_ir0,
#endif
#ifdef CONFIG_PLAT_AMBARELLA_SUPPORT_UHC
	&ambarella_ohci0,
#endif
	&ambarella_pcm0,
	&ambarella_rtc0,
	&ambarella_sd0,
#if (SD_INSTANCES >= 2)
	&ambarella_sd1,
#endif
	&ambarella_spi0,
#if (SPI_INSTANCES >= 2)
	&ambarella_spi1,
#endif
#if (SPI_INSTANCES >= 3)
	&ambarella_spi2,
#endif
#if (SPI_INSTANCES >= 4)
	&ambarella_spi3,
#endif
	&ambarella_uart,
#if (UART_INSTANCES >= 2)
	&ambarella_uart1,
#endif
#if (UART_INSTANCES >= 3)
	&ambarella_uart2,
#endif
#if (UART_INSTANCES >= 4)
	&ambarella_uart3,
#endif
#ifdef CONFIG_PLAT_AMBARELLA_SUPPORT_UDC
	&ambarella_udc0,
#endif
	&ambarella_wdt0,
	&ambarella_dma,
	&ambarella_nand,
};

/* ==========================================================================*/
static struct ambarella_key_table generic_keymap[AMBINPUT_TABLE_SIZE] = {
	{AMBINPUT_VI_KEY,	{.vi_key	= {0,	0,	0}}},
	{AMBINPUT_VI_REL,	{.vi_rel	= {0,	0,	0}}},
	{AMBINPUT_VI_ABS,	{.vi_abs	= {0,	0,	0}}},
	{AMBINPUT_VI_SW,	{.vi_sw		= {0,	0,	0}}},

	{AMBINPUT_END},
};

static struct ambarella_input_board_info generic_board_input_info = {
	.pkeymap		= generic_keymap,
	.pinput_dev		= NULL,
	.pdev			= NULL,

	.abx_max_x		= 4095,
	.abx_max_y		= 4095,
	.abx_max_pressure	= 4095,
	.abx_max_width		= 16,
};

struct platform_device generic_board_input = {
	.name		= "ambarella-input",
	.id		= -1,
	.resource	= NULL,
	.num_resources	= 0,
	.dev		= {
		.platform_data		= &generic_board_input_info,
		.dma_mask		= &ambarella_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	}
};


/* ==========================================================================*/
static void __init ambarella_init_generic(void)
{
	int					i;

	ambarella_init_machine("Generic");

	platform_add_devices(ambarella_devices, ARRAY_SIZE(ambarella_devices));
	for (i = 0; i < ARRAY_SIZE(ambarella_devices); i++) {
		device_set_wakeup_capable(&ambarella_devices[i]->dev, 1);
		device_set_wakeup_enable(&ambarella_devices[i]->dev, 0);
	}

	i2c_register_board_info(0, ambarella_board_vin_infos,
		ARRAY_SIZE(ambarella_board_vin_infos));

#if (IDC_SUPPORT_PIN_MUXING_FOR_HDMI == 1)
	i2c_register_board_info(0, &ambarella_board_hdmi_info, 1);
#else
	i2c_register_board_info(1, &ambarella_board_hdmi_info, 1);
#endif

	platform_device_register(&generic_board_input);
}

/* ==========================================================================*/
MACHINE_START(AMBARELLA, "Ambarella Media SoC")
	.atag_offset	= 0x100,
	.restart_mode	= 's',
	.smp		= smp_ops(ambarella_smp_ops),
	.reserve	= ambarella_memblock_reserve,
	.map_io		= ambarella_map_io,
	.init_irq	= ambarella_init_irq,
	.timer		= &ambarella_timer,
	.init_machine	= ambarella_init_generic,
#if defined(CONFIG_ARM_GIC)
	.handle_irq	= gic_handle_irq,
#endif
	.restart	= ambarella_restart_machine,
MACHINE_END

