/*
 * arch/arm/plat-ambarella/include/mach/entry-macro.S
 *
 * History:
 *	2006/12/27 - [Charles Chiou] created file
 *	2007/12/27 - [Grady Chen] Added VIC2, GPIO0, GPIO1, GPIO2
 *	2009/06/15 - [Anthony Ginger] Optimize with clz.
 *
 * Copyright (C) 2004-2009, Ambarella, Inc.
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

#include <mach/hardware.h>
#include <asm/irq.h>
#ifdef CONFIG_PLAT_AMBARELLA_BOSS
#define __ASM__
#include <mach/hardware.h>
#include <mach/boss.h>
#endif

		.macro	stat2nr, stat, nr, tmp
		rsbs    \tmp, \stat, #0
		and     \nr, \tmp, \stat
		clzcc   \nr, \nr
		rsc     \nr, \nr, #32
		teq     \nr, #32
		.endm

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_preamble, base, tmp
		.endm

#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
#if defined(CONFIG_ARM_GIC)
		.macro boss_int_update, reg1, reg2, reg3, reg4
		tst     \reg1, #0x80
		ldr     \reg1, =boss            @ reg1: boss
		ldr     \reg1, [\reg1]
		bne     2f

1:
		mov     \reg3, #BOSS_GIC0MASK_OFFSET
		add     \reg3, \reg3, \reg4
		ldr     \reg2, [\reg1, \reg3]
		mov     \reg3, #BOSS_GUEST_GIC0_EN_OFFSET
		add     \reg3, \reg3, \reg4
		ldr     \reg3, [\reg1, \reg3]
		and     \reg2, \reg2, \reg3

		ldr     \reg3, =AMBARELLA_VA_GIC_DIST_BASE
		orr     \reg3, \reg3, #GIC_SET_ENABLE_OFFSET
		str     \reg2, [\reg3, \reg4]

		add     \reg4, \reg4, #4
		cmp     \reg4, #32
		bne     1b

		mov     \reg2, #0
		str     \reg2, [\reg1, #BOSS_GUEST_IRQ_MASK_OFFSET]
		b       3f
2:
		mov     \reg3, #BOSS_GIC0MASK_OFFSET
		add     \reg3, \reg3, \reg4
		ldr     \reg2, [\reg1, \reg3]
		mov     \reg3, #BOSS_GUEST_GIC0_EN_OFFSET
		add     \reg3, \reg3, \reg4
		ldr     \reg3, [\reg1, \reg3]
		and     \reg2, \reg2, \reg3

		ldr     \reg3, =AMBARELLA_VA_GIC_DIST_BASE
		orr     \reg3, \reg3, #GIC_CLEAR_ENABLE_OFFSET
		str     \reg2, [\reg3, \reg4]
		add     \reg4, \reg4, #4
		cmp     \reg4, #32
		bne     2b

		mov     \reg2, #1
		str     \reg2, [\reg1, #BOSS_GUEST_IRQ_MASK_OFFSET]
3:
		.endm

#else   /* !CONFIG_ARM_GIC */
		.macro boss_int_update, reg1, reg2, reg3
		tst \reg1, #0x80
		ldr \reg1, =boss            @ reg1: boss
		ldr \reg1, [\reg1]
		bne 1f

		ldr \reg2, [\reg1, #BOSS_VIC1MASK_OFFSET]
		ldr \reg3, [\reg1, #BOSS_GUEST_VIC1_EN_OFFSET]
		and \reg2, \reg2, \reg3
		ldr \reg3, =VIC_BASE
		str \reg2, [\reg3, #VIC_INTEN_OFFSET]

		ldr \reg2, [\reg1, #BOSS_VIC2MASK_OFFSET]
		ldr \reg3, [\reg1, #BOSS_GUEST_VIC2_EN_OFFSET]
		and \reg2, \reg2, \reg3
		ldr \reg3, =VIC2_BASE
		str \reg2, [\reg3, #VIC_INTEN_OFFSET]

		mov \reg2, #0
		str \reg2, [\reg1, #BOSS_GUEST_IRQ_MASK_OFFSET]
		b   2f
1:

		ldr \reg2, [\reg1, #BOSS_VIC1MASK_OFFSET]
		ldr \reg3, [\reg1, #BOSS_GUEST_VIC1_EN_OFFSET]
		and \reg2, \reg2, \reg3
		ldr \reg3, =VIC_BASE
		str \reg2, [\reg3, #VIC_INTEN_CLR_OFFSET]

		ldr \reg2, [\reg1, #BOSS_VIC2MASK_OFFSET]
		ldr \reg3, [\reg1, #BOSS_GUEST_VIC2_EN_OFFSET]
		and \reg2, \reg2, \reg3
		ldr \reg3, =VIC2_BASE
		str \reg2, [\reg3, #VIC_INTEN_CLR_OFFSET]

		mov \reg2, #1
		str \reg2, [\reg1, #BOSS_GUEST_IRQ_MASK_OFFSET]
2:
		.endm
#endif  /* CONFIG_ARM_GIC */
#endif  /* CONFIG_PLAT_AMBARELLA_BOSS */

		.macro	arch_ret_to_user, tmp1, tmp2
#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
		stmfd   sp!, {r0-r3}
		mrs     r0, spsr
		mov     r3, #0
		boss_int_update r0, r1, r2, r3
		ldmfd   sp!, {r0-r3}
#endif
		.endm

#if !defined(CONFIG_ARM_GIC)

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	\base, =(VIC_BASE)
		ldr	\irqstat, [\base, #VIC_IRQ_STA_OFFSET]
		stat2nr	\irqstat, \irqnr, \tmp
#if (VIC_INSTANCES >= 2)
		bne	1000f
		ldr	\base, =(VIC2_BASE)
		ldr	\irqstat, [\base, #VIC_IRQ_STA_OFFSET]
		stat2nr	\irqstat, \irqnr, \tmp
		addne	\irqnr, \irqnr, #VIC2_INT_VEC(0)
#endif
#if (VIC_INSTANCES >= 3)
		bne	1000f
		ldr	\base, =(VIC3_BASE)
		ldr	\irqstat, [\base, #VIC_IRQ_STA_OFFSET]
		stat2nr	\irqstat, \irqnr, \tmp
		addne	\irqnr, \irqnr, #VIC3_INT_VEC(0)
#endif
1000:
		.endm
#else   /* CONFIG_ARM_GIC */
#include <plat/cortex.h>
#include <asm/hardware/gic.h>

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	\base, =AMBARELLA_VA_GIC_CPU_BASE
		ldr	\irqstat, [\base, #GIC_CPU_INTACK]
		ldr	\tmp, =1021
		bic	\irqnr, \irqstat, #0x1c00
		cmp	\irqnr, #29
		cmpcc	\irqnr, \irqnr
		cmpne	\irqnr, \tmp
		cmpcs	\irqnr, \irqnr
		.endm

		.macro	test_for_ipi, irqnr, irqstat, base, tmp
		bic	\irqnr, \irqstat, #0x1c00
		cmp	\irqnr, #16
		it	cc
		strcc	\irqstat, [\base, #GIC_CPU_EOI]
		it	cs
		cmpcs	\irqnr, \irqnr
		.endm

		.macro	test_for_ltirq, irqnr, irqstat, base, tmp
		bic	\irqnr, \irqstat, #0x1c00
		mov	\tmp, #0
		cmp	\irqnr, #29
		itt	eq
		moveq	\tmp, #1
		streq	\irqstat, [\base, #GIC_CPU_EOI]
		cmp	\tmp, #0
		.endm
#endif  /* CONFIG_ARM_GIC */

