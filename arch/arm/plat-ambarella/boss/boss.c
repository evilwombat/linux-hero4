/*
 * arch/arm/plat-ambarella/boss/boss.c
 *
 * Author: Henry Lin <hllin@ambarella.com>
 *
 * Copyright (C) 2004-2011, Ambarella, Inc.
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
#include <linux/module.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/aipc/ipc_slock.h>

#include <asm/io.h>

#include <mach/boss.h>

extern u32 ipc_tick_get(void);

#define BOSS_IRQ_IS_ENABLED(f)	((f & PSR_I_BIT) == 0)

struct boss_obj_s G_boss_obj = {
	.ready		= 0,
	.count		= 0,
	.irq_enable_count = 0,
	.irq_disable_count = 0,
	.irq_save_count = 0,
	.irq_restore_count = 0,
};

struct boss_obj_s *boss_obj = &G_boss_obj;
EXPORT_SYMBOL(boss_obj);

/*
 * Set BOSS to ready state
 */
void boss_set_ready(int ready)
{
	boss_obj->ready = ready;
	if (ready) {
		boss->state = BOSS_STATE_READY;
		boss_obj->count = 0;
	}
}
EXPORT_SYMBOL(boss_set_ready);

#if defined(CONFIG_PLAT_AMBARELLA_BOSS)

/*
 * BOSS: arch_local_irq_enable
 */
void boss_local_irq_enable(void)
{
#ifdef CONFIG_ARM_GIC
	int i;
#endif

	arm_irq_disable();

	boss->guest_irq_mask = 0;
	boss_obj->irq_enable_count++;
	//boss_obj->irq_enable_time = ipc_tick_get();

#ifdef CONFIG_ARM_GIC
	for (i = 0; i < 8; i++) {
		amba_writel(GIC_SET_ENABLE_REG(i << 2),
			    boss->gic_mask[i] & boss->guest_gic_en[i]);
	}
#else
	amba_writel(VIC_INTEN_REG, boss->vic1mask & boss->guest_vic1_en);
	amba_writel(VIC2_INTEN_REG, boss->vic2mask & boss->guest_vic2_en);
#endif

	arm_irq_enable();
}
EXPORT_SYMBOL(boss_local_irq_enable);

/*
 * BOSS: arch_local_irq_enable
 */
void boss_local_irq_disable(void)
{
	unsigned long flags;
#ifdef CONFIG_ARM_GIC
	int i;
#endif

	flags = arm_irq_save();

#ifdef CONFIG_ARM_GIC
	for (i = 0; i < 8; i++) {
		amba_writel(GIC_CLEAR_ENABLE_REG(i << 2), boss->gic_mask[i]);
	}
#else
	amba_writel(VIC_INTEN_CLR_REG, boss->vic1mask);
	amba_writel(VIC2_INTEN_CLR_REG, boss->vic2mask);
#endif

	boss->guest_irq_mask = 1;

	boss_obj->irq_disable_count++;
	//boss_obj->irq_disable_time = ipc_tick_get();

	arm_irq_restore(flags);
}
EXPORT_SYMBOL(boss_local_irq_disable);

/*
 * BOSS: arch_local_save_flags
 */
unsigned long boss_local_save_flags(void)
{
	unsigned long flags;
	asm volatile(
		"	mrs	%0, cpsr	@ local_save_flags"
		: "=r" (flags) : : "memory", "cc");

	if (boss->guest_irq_mask) {
		flags |= PSR_I_BIT;
	}

	return flags;
}
EXPORT_SYMBOL(boss_local_save_flags);

/*
 * BOSS: arch_local_irq_save
 */
unsigned long boss_local_irq_save(void)
{
	unsigned long flags;

	flags = arm_irq_save();
	if (boss_obj->count == 0) {
		//boss_obj->irq_start_time = ipc_tick_get();
	}
	boss_obj->count++;
	boss_obj->irq_save_count++;
	//boss_obj->irq_save_time = ipc_tick_get();
	arm_irq_restore(flags);

	flags = arch_local_save_flags();

	if (BOSS_IRQ_IS_ENABLED(flags)) {
		boss_local_irq_disable();
	}

	return flags;
}
EXPORT_SYMBOL(boss_local_irq_save);

/*
 * BOSS: arch_local_irq_restore
 */
void boss_local_irq_restore(unsigned long flags)
{
	if (boss_obj->count == 0) {
		arm_irq_restore(flags);
		return;
	}

	if (BOSS_IRQ_IS_ENABLED(flags)) {
		boss_local_irq_enable();
	}

	flags = arm_irq_save();
	boss_obj->irq_restore_count++;
	//boss_obj->irq_restore_time = ipc_tick_get();
	arm_irq_restore(flags);
}
EXPORT_SYMBOL(boss_local_irq_restore);

#endif	/* CONFIG_BOSS_SINGLE_CORE */

#if 0
/*
 * Called when ipc reports ready status
 */
void boss_on_ipc_report_ready(int ready)
{
	if (ready) {
		boss->log_buf_ptr = ipc_virt_to_phys (boss_log_buf_ptr);
		boss->log_buf_len_ptr = ipc_virt_to_phys (boss_log_buf_len_ptr);
		boss->log_buf_last_ptr = ipc_virt_to_phys (boss_log_buf_last_ptr);
	}
}
EXPORT_SYMBOL(boss_on_ipc_report_ready);
#endif

/*
 * Get the owner of a BOSS IRQ
 */
int boss_get_irq_owner(int irq)
{
	int owner = BOSS_IRQ_OWNER_RTOS;
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();

#if defined(CONFIG_ARM_GIC)
	if (boss->gic_mask[irq >> 5] & (0x1 << (irq % 32))) {
		owner = BOSS_IRQ_OWNER_LINUX;
	}
#else
	if (irq < 32) {
		if (boss->vic1mask & (0x1 << irq)) {
			owner = BOSS_IRQ_OWNER_LINUX;
		}
	}
	else if (irq < 64) {
		if (boss->vic2mask & (0x1 << (irq - 32))) {
			owner = BOSS_IRQ_OWNER_LINUX;
		}
	}
	else if (irq < 96) {
		if (boss->vic3mask & (0x1 << (irq - 64))) {
			owner = BOSS_IRQ_OWNER_LINUX;
		}
	}
#endif

	preempt_enable();
	local_irq_restore(flags);

	return owner;
}
EXPORT_SYMBOL(boss_get_irq_owner);

/*
 * Set the owner of a BOSS IRQ
 */
void boss_set_irq_owner(int irq, int owner, int update)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();

#if defined(CONFIG_ARM_GIC)
#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
	if (owner == BOSS_IRQ_OWNER_RTOS) {
		BOSS_INT_SET_RTOS(boss->gic_mask[irq >> 5], (irq % 32));
		BOSS_INT_SET(boss->root_gic_en[irq >> 5], (irq % 32));
		if (update) {
			amba_writel(GIC_SET_ENABLE_REG((irq >> 5) << 2), 1 << (irq % 32));
		}
	} else {
		BOSS_INT_SET_LINUX(boss->gic_mask[irq >> 5], (irq % 32));
		BOSS_INT_SET(boss->guest_gic_en[irq >> 5], (irq % 32));
		if (update) {
			if (boss->guest_irq_mask) {
				amba_writel(GIC_CLEAR_ENABLE_REG((irq >> 5) << 2), 1 << (irq % 32));
			} else {
				amba_writel(GIC_SET_ENABLE_REG((irq >> 5) << 2), 1 << (irq % 32));
			}
		}
	}
#else   /* !CONFIG_PLAT_AMBARELLA_BOSS */
#endif  /* CONFIG_PLAT_AMBARELLA_BOSS */
#else   /* !CONFIG_ARM_GIC */
    if (irq < 32) {
        if (owner == BOSS_IRQ_OWNER_RTOS) {
            BOSS_INT_SET_RTOS(boss->vic1mask, irq);
            BOSS_INT_SET(boss->root_vic1_en, irq);
            if (update) {
                amba_writel(VIC_INTEN_REG, 1 << irq);
            }
        } else {
            BOSS_INT_SET_LINUX(boss->vic1mask, irq);
            BOSS_INT_SET(boss->guest_vic1_en, irq);
            if (update) {
                if (boss->guest_irq_mask) {
                    amba_writel(VIC_INTEN_CLR_REG, 1 << irq);
                } else {
                    amba_writel(VIC_INTEN_REG, 1 << irq);
                }
            }
        }
    }
    else if (irq < 64) {
        if (owner == BOSS_IRQ_OWNER_RTOS) {
            BOSS_INT_SET_RTOS(boss->vic2mask, irq - 32);
            BOSS_INT_SET(boss->root_vic2_en, irq - 32);
            if (update) {
                amba_writel(VIC2_INTEN_REG, 1 << (irq - 32));
            }
        } else {
            BOSS_INT_SET_LINUX(boss->vic2mask, irq - 32);
            BOSS_INT_SET(boss->guest_vic2_en, irq - 32);
            if (update) {
                if (boss->guest_irq_mask) {
                    amba_writel(VIC2_INTEN_CLR_REG, 1 << (irq - 32));
                } else {
                    amba_writel(VIC2_INTEN_REG, 1 << (irq - 32));
                }
            }
        }
    }
#if (VIC_INSTANCES >= 3)
    else if (irq < 96) {
        if (owner == BOSS_IRQ_OWNER_RTOS) {
            BOSS_INT_SET_RTOS(boss->vic3mask, irq - 64);
            BOSS_INT_SET(boss->root_vic3_en, irq - 64);
            if (update) {
                amba_writel(VIC3_INTEN_REG, 1 << (irq - 64));
            }
        } else {
            BOSS_INT_SET_LINUX(boss->vic3mask, irq - 64);
            BOSS_INT_SET(boss->guest_vic3_en, irq - 64);
            if (update) {
                if (boss->guest_irq_mask) {
                    amba_writel(VIC3_INTEN_CLR_REG, 1 << (irq - 64));
                } else {
                    amba_writel(VIC3_INTEN_REG, 1 << (irq - 64));
                }
            }
        }

    }
#endif  /* VIC_INSTANCES >= 3 */
#endif  /* CONFIG_ARM_GIC */

	preempt_enable();
	local_irq_restore(flags);
}
EXPORT_SYMBOL(boss_set_irq_owner);

/*
 * Set Linux as the owner of an BOSS IRQ
 */
void boss_enable_irq(int irq)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();

#if defined(CONFIG_ARM_GIC)
	if (boss->gic_mask[LOCAL_TIMER_IRQ >> 5] & (LOCAL_TIMER_IRQ % 32)) {
		for (;;);
	}

	BOSS_INT_SET(boss->guest_gic_en[irq >> 5], (irq % 32));
#else   /* !CONFIG_ARM_GIC */
	if (boss->vic1mask & (1 << 12)) {
		for (;;);
	}

	if (irq < 32) {
		BOSS_INT_SET(boss->guest_vic1_en, irq);
	}
	else if (irq < 64) {
		BOSS_INT_SET(boss->guest_vic2_en, irq - 32);
	}
#if (VIC_INSTANCES >= 3)
	else if (irq < 96) {
		BOSS_INT_SET(boss->guest_vic3_en, irq - 64);
	}
#endif
#endif  /* CONFIG_ARM_GIC */

	preempt_enable();
	local_irq_restore(flags);
}
EXPORT_SYMBOL(boss_enable_irq);

/*
 * Set uITRON as the owner of an BOSS IRQ
 */
void boss_disable_irq(int irq)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();

#if defined(CONFIG_ARM_GIC)
	if (boss->gic_mask[LOCAL_TIMER_IRQ >> 5] & (LOCAL_TIMER_IRQ % 32)) {
		for (;;);
	}

	BOSS_INT_CLR(boss->guest_gic_en[irq >> 5], (irq % 32));
#else   /* !CONFIG_ARM_GIC */
	if (boss->vic1mask & (1 << 12)) {
		for (;;);
	}

	if (irq < 32) {
		BOSS_INT_CLR(boss->guest_vic1_en, irq);
	}
	else if (irq < 64) {
		BOSS_INT_CLR(boss->guest_vic2_en, irq - 32);
	}
#if (VIC_INSTANCES >= 3)
	else if (irq < 96) {
		BOSS_INT_CLR(boss->guest_vic3_en, irq - 64);
	}
#endif
#endif  /* CONFIG_ARM_GIC */

	preempt_enable();
	local_irq_restore(flags);
}
EXPORT_SYMBOL(boss_disable_irq);

int boss_get_device_owner(int device)
{
	if (boss == NULL) {
        /* Should not be here!! */
        printk(KERN_ERR "%s: boss is NULL.", __func__);
        return 0;
	}

	if (device >= BOSS_DEVICE_NUM) {
		return -1;
	}

	return ((boss->device_owner_mask >> device) & 1);
}
EXPORT_SYMBOL(boss_get_device_owner);
