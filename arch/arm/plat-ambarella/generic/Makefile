#
# arch/arm/plat-ambarella/generic/Makefile
#
# Author: Anthony Ginger <hfjiang@ambarella.com>
#
# Copyright (C) 2004-2011, Ambarella, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
#

obj-y						+= adc.o
obj-$(CONFIG_PLAT_AMBARELLA_SUPPORT_SATA)	+= ahci.o
obj-y						+= audio.o
obj-y						+= crypto.o
obj-y						+= dma.o
obj-y						+= eth.o
obj-y						+= fio.o
obj-$(CONFIG_PLAT_AMBARELLA_SUPPORT_GDMA)	+= gdma.o
obj-y						+= gpio.o
obj-y						+= idc.o
obj-$(CONFIG_PLAT_AMBARELLA_SUPPORT_IR)		+= ir.o
ifeq ($(CONFIG_PLAT_AMBARELLA_SUPPORT_HAL), y)
obj-y						+= pll_hal.o
else
obj-y						+= clk.o
endif
ifneq ($(CONFIG_PLAT_AMBARELLA_BOSS), y)
obj-y						+= irq.o
obj-y						+= timer.o
endif
ifneq ($(CONFIG_PLAT_AMBARELLA_AMBALINK), y)
obj-y						+= init.o
obj-y						+= pm.o
endif
obj-$(CONFIG_HAVE_PWM)				+= pwm.o
obj-y						+= hwlock.o
obj-y						+= rtc.o
obj-y						+= sd.o
obj-y						+= spi.o
obj-y						+= uart.o
obj-y						+= wdt.o
obj-$(CONFIG_PLAT_AMBARELLA_SUPPORT_UPORT)	+= uport.o
obj-$(CONFIG_PLAT_AMBARELLA_SUPPORT_UDC)	+= udc.o
obj-$(CONFIG_PLAT_AMBARELLA_SUPPORT_UHC)	+= uhc.o
obj-$(CONFIG_CPU_FREQ)				+= cpufreq.o

