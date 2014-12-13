/*
 * arch/arm/mach-ambarella/touch-tm1510.c
 *
 * Author: Zhenwu Xue <zwxue@ambarella.com>
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
#include <linux/i2c.h>
#include <linux/i2c/tm1510.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/hardware.h>
#include <mach/init.h>
#include <mach/board.h>

#include "touch.h"

/* ==========================================================================*/
static int ambarella_tm1510_get_pendown_state(void)
{
	if (ambarella_gpio_get(
		ambarella_board_generic.touch_panel_irq.irq_gpio))
		return 0;
	else
		return 1;
}

static void ambarella_tm1510_clear_penirq(void)
{
	ambarella_touch_common_ack_irq();
}

static int ambarella_tm1510_init_platform_hw(void)
{
	return ambarella_touch_common_init_hw();
}

static void ambarella_tm1510_exit_platform_hw(void)
{
}

static struct tm1510_platform_data ambarella_tm1510_pdata = {
	.fix = {
		[TM1510_FAMILY_0] = {
			.x_invert	= 0,
			.y_invert	= 1,
			.x_rescale	= 0,
			.y_rescale	= 0,
			.x_min		= 11,
			.x_max		= 3873,
			.y_min		= 0,
			.y_max		= 2248,

			.family_code	= 0x00,
		},

		[TM1510_FAMILY_1] = {
			.x_invert	= 1,
			.y_invert	= 0,
			.x_rescale	= 0,
			.y_rescale	= 0,
			.x_min		= 11,
			.x_max		= 3236,
			.y_min		= 0,
			.y_max		= 2022,

			.family_code	= 0x26,
		},

		[TM1510_FAMILY_2] = {
			.x_invert	= 1,
			.y_invert	= 0,
			.x_rescale	= 0,
			.y_rescale	= 0,
			.x_min		= 11,
			.x_max		= 3236,
			.y_min		= 0,
			.y_max		= 2022,

			.family_code	= 0xc1,
		},
	},
	.get_pendown_state	= ambarella_tm1510_get_pendown_state,
	.clear_penirq		= ambarella_tm1510_clear_penirq,
	.init_platform_hw	= ambarella_tm1510_init_platform_hw,
	.exit_platform_hw	= ambarella_tm1510_exit_platform_hw
};

struct i2c_board_info ambarella_tm1510_board_info = {
	.type		= "tm1510",
	.addr		= 0x20,
	.platform_data	= &ambarella_tm1510_pdata,
};

