/*
 * arch/arm/plat-ambarella/generic/pm.c
 * Power Management Routines
 * Author: Anthony Ginger <hfjiang@ambarella.com>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/cpu.h>
#include <linux/power_supply.h>

#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/suspend.h>

#include <mach/board.h>
#include <mach/hardware.h>
#include <mach/init.h>
#include <mach/system.h>
#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
#include <mach/boss.h>
#endif

#include <plat/bapi.h>
#include <plat/ambcache.h>
#include <plat/ambalink_cfg.h>

/* ==========================================================================*/
/* Below data structures have to sync with aoss.S and RTOS side, AmbaHiber.c. */
struct ambernation_page_info {
        u_int src;
        u_int dst;
        u_int size;
};
typedef struct ambernation_page_info ambernation_page_info;
#define AMBERNATION_AOSS_MAGIC 0x19531110

struct ambernation_aoss_info {
        u_int fn_pri[252];
        u_int magic;
        u_int total_pages;
        u_int copy_pages;
        struct ambernation_page_info *page_info;
};
typedef struct ambernation_aoss_info ambernation_aoss_info;

struct ambernation_check_info {
        struct ambernation_aoss_info *aoss_info;
	u32 padding[7];
} __attribute__((aligned(32), packed));
typedef struct ambernation_check_info ambernation_check_info;

/* ==========================================================================*/

static struct ambernation_check_info pm_abcheck_info;
static u32 pm_abcopy_page = 0;

typedef unsigned int (*ambnation_aoss_call_t)(u32, u32, u32, u32);
extern int in_suspend;
//extern char in_suspend_ipc_svc;

/* ==========================================================================*/
#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX	"ambarella_config."

/* ==========================================================================*/
static int pm_check_power_supply = 1;
static int pm_force_power_on = 0;
int pm_hibernation_recover = 0;
#if defined(CONFIG_AMBARELLA_SYS_PM_CALL)
module_param(pm_check_power_supply, int, 0644);
module_param(pm_force_power_on, int, 0644);
#endif

int rpmsg_linkctrl_cmd_hiber_prepare(u32 info);
int rpmsg_linkctrl_cmd_hiber_enter(int flag);
int rpmsg_linkctrl_cmd_hiber_exit(void);

extern int hibernation_start;
extern int wowlan_resume_from_ram;

/* ==========================================================================*/
void ambarella_power_off(void)
{
	if (ambarella_board_generic.power_control.gpio_id >= 0) {
		ambarella_set_gpio_output(
			&ambarella_board_generic.power_control, 0);
		ambarella_set_gpio_output(
			&ambarella_board_generic.power_control, 1);
	} else {
		amba_writel((amba_readl(ANA_PWR_REG) | ANA_PWR_POWER_DOWN),
			ANA_PWR_REG);
	}
}

void ambarella_power_off_prepare(void)
{
}

/* ==========================================================================*/
static int ambarella_pm_notify(unsigned long val)
{
	int					retval = 0;

	retval = notifier_to_errno(ambarella_set_event(val, NULL));
	if (retval)
		pr_debug("%s: 0x%08lx fail(%d)\n",__func__, val, retval);

	return retval;
}

static int ambarella_pm_notify_raw(unsigned long val)
{
	int					retval = 0;

	retval = notifier_to_errno(ambarella_set_raw_event(val, NULL));
	if (retval)
		pr_debug("%s: 0x%08lx fail(%d)\n",__func__, val, retval);

	return retval;
}

static int ambarella_pm_pre(unsigned long *irqflag, u32 bsuspend, u32 tm_level)
{
	int					retval = 0;

	if (irqflag)
		local_irq_save(*irqflag);

	if (bsuspend) {
		ambarella_timer_suspend(tm_level);
		ambarella_irq_suspend(0);
		ambarella_gpio_suspend(0);
	}

	retval = ambarella_pm_notify_raw(AMBA_EVENT_PRE_PM);

	return retval;
}

static int ambarella_pm_post(unsigned long *irqflag, u32 bresume, u32 tm_level)
{
	int					retval = 0;

	retval = ambarella_pm_notify_raw(AMBA_EVENT_POST_PM);

	if (bresume) {
		ambarella_gpio_resume(0);
		ambarella_irq_resume(0);
		ambarella_timer_resume(tm_level);
	}

	if (irqflag)
		local_irq_restore(*irqflag);

	return retval;
}

static int ambarella_pm_check(suspend_state_t state)
{
	int					retval = 0;

	retval = ambarella_pm_notify_raw(AMBA_EVENT_CHECK_PM);
	if (retval)
		goto ambarella_pm_check_exit;

	retval = ambarella_pm_notify(AMBA_EVENT_CHECK_PM);
	if (retval)
		goto ambarella_pm_check_exit;

ambarella_pm_check_exit:
	return retval;
}

/* ==========================================================================*/
int ambarella_pm_linkctrl_prepare(void)
{
	int retval = 0;
#if defined(CONFIG_RPMSG_LINKCTRL)
	u32 phy_addr;

	pm_abcopy_page = 0;

	phy_addr = ambalink_virt_to_phys((u32) &pm_abcheck_info);
	ambcache_flush_range(&pm_abcheck_info, sizeof(pm_abcheck_info));

	rpmsg_linkctrl_cmd_hiber_prepare(phy_addr);

	pm_abcheck_info.aoss_info = (struct ambernation_aoss_info *)
				ambalink_phys_to_virt((u32) pm_abcheck_info.aoss_info);
#endif
	return retval;

}


int notify_suspend_done(int suspend_mode)
{
	if (suspend_mode > 1) {
		// standby and suspend to mem
		amba_writel(AHB_SCRATCHPAD_REG(0x10), 0x1 << (AMBALINK_AMP_SUSPEND_KICK - AXI_SOFT_IRQ(0)));
	} else {
		// hiber to NAND and hiber to ram
#if defined(CONFIG_RPMSG_LINKCTRL)
		rpmsg_linkctrl_cmd_hiber_enter(suspend_mode);
#endif
	}

	return 0;
}

int ambarella_pm_linkctrl_enter(void)
{
	int					retval = 0;
	int					i;
#ifdef CONFIG_OUTER_CACHE
	int					l2_mode = 0;
#endif
	ambnation_aoss_call_t			pm_abaoss_entry = NULL;
	ambnation_aoss_call_t			pm_abaoss_outcoming = NULL;
	u32					pm_abaoss_arg[4];
	u32					pm_fn_pri;

	if (pm_abcheck_info.aoss_info) {
		for (i = 0; i < 4; i++) {
			pm_abaoss_arg[i] = ambalink_phys_to_virt(
				pm_abcheck_info.aoss_info->fn_pri[i]);
		}
		pm_abaoss_entry = (ambnation_aoss_call_t)pm_abaoss_arg[0];
		pm_abaoss_outcoming = (ambnation_aoss_call_t)pm_abaoss_arg[2];
		pm_fn_pri = (u32)pm_abcheck_info.aoss_info->fn_pri;

#if defined(CONFIG_PLAT_AMBARELLA_CORTEX) && defined(CONFIG_SMP)
		arch_smp_suspend(0);
#endif

#ifdef CONFIG_OUTER_CACHE
		l2_mode = outer_is_enabled();
		if (l2_mode)
			ambcache_l2_disable_raw();
#endif
		flush_cache_all();
		retval = pm_abaoss_entry(pm_fn_pri, pm_abaoss_arg[1],
			pm_abaoss_arg[2], pm_abaoss_arg[3]);
		printk("pm_abaoss_entry returned 0x%x\n",retval);

		if (retval != 0x01) {
			notify_suspend_done(wowlan_resume_from_ram);
#if defined(CONFIG_AMBALINK_MULTIPLE_CORE)
			// Linux on cortex core 1
			sev();
			wfe();
			wfe();
			pm_abaoss_outcoming(pm_fn_pri, 0x0, 0x0, 0x0);
#elif defined (CONFIG_AMBALINK_AMP_MULTIPLE_CORE)
			/* ARM11 */
			/* Enable AMBALINK_AMP_SUSPEND_KICK IRQ for wake up event. */
			enable_irq(AMBALINK_AMP_SUSPEND_KICK);
			local_irq_disable();
			cpu_do_idle();
			local_irq_enable();
			disable_irq(AMBALINK_AMP_SUSPEND_KICK);
			pm_abaoss_outcoming(pm_fn_pri, 0x0, 0x0, 0x0);
#else
			/* Single core. BOSS */
			local_irq_disable();
			boss->state = BOSS_STATE_SUSPENDED;
			arch_idle();
#endif
		}

#if defined(CONFIG_SMP)
		arch_smp_resume(0);
#endif

#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_HAL)
		set_ambarella_hal_invalid();
#endif
#ifdef CONFIG_OUTER_CACHE
		if (l2_mode)
			ambcache_l2_enable_raw();
#endif
	}

	return retval;
}

/* ==========================================================================*/

static int ambarella_pm_cpu_do_idle(unsigned long unused)
{
	cpu_do_idle();

	return 0;
}

static int ambarella_pm_enter_standby(void)
{
	int					retval = 0;
	unsigned long				flags;

	if (ambarella_pm_pre(&flags, 1, 1))
		BUG();

    // interrupt the other OS suspend is done.
    notify_suspend_done(2);
    
    // any interrupt to wake up linux.
	cpu_suspend(0, ambarella_pm_cpu_do_idle);

	if (ambarella_pm_post(&flags, 1, 1))
		BUG();

	return retval;
}

static int ambarella_pm_enter_mem(void)
{
	int					retval = 0;
	unsigned long				flags;

	if (ambarella_pm_pre(&flags, 1, 1))
		BUG();

    ambarella_pm_linkctrl_enter();

	if (ambarella_pm_post(&flags, 1, 1))
		BUG();

	return retval;
}

static int ambarella_pm_suspend_valid(suspend_state_t state)
{
	int					retval = 0;
	int					valid = 0;

	retval = ambarella_pm_check(state);
	if (retval)
		goto ambarella_pm_valid_exit;

	switch (state) {
	case PM_SUSPEND_ON:
		valid = 1;
		break;

	case PM_SUSPEND_STANDBY:
        hibernation_start = 1;
		valid = 1;
		break;

	case PM_SUSPEND_MEM:
			valid = 1;
        retval = ambarella_pm_linkctrl_prepare();
		break;

	default:
		break;
	}

ambarella_pm_valid_exit:
	pr_debug("%s: state[%d]=%d\n", __func__, state, valid);

	return valid;
}

static int ambarella_pm_suspend_begin(suspend_state_t state)
{
	int					retval = 0;

	switch (state) {
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		retval = ambarella_pm_notify(AMBA_EVENT_PRE_PM);
		break;
	default:
		break;
	}

	return retval;
}

static int ambarella_pm_suspend_enter(suspend_state_t state)
{
	int					retval = 0;

	switch (state) {
	case PM_SUSPEND_ON:
		break;

	case PM_SUSPEND_STANDBY:
		retval = ambarella_pm_enter_standby();
		break;

	case PM_SUSPEND_MEM:
		retval = ambarella_pm_enter_mem();
		break;

	default:
		break;
	}

	return retval;
}

static void ambarella_pm_suspend_end(void)
{
	ambarella_pm_notify(AMBA_EVENT_POST_PM);
}

static struct platform_suspend_ops ambarella_pm_suspend_ops = {
	.valid		= ambarella_pm_suspend_valid,
	.begin		= ambarella_pm_suspend_begin,
	.enter		= ambarella_pm_suspend_enter,
	.end		= ambarella_pm_suspend_end,
};

/* ==========================================================================*/
static int ambarella_pm_hibernation_begin(void)
{
    return ambarella_pm_linkctrl_prepare();
}

static void ambarella_pm_hibernation_end(void)
{
	int					retval = 0;

    if (pm_hibernation_recover) {
        pm_hibernation_recover = 0;
        printk("Skip hibernation_end to hibernation_recover\r\n");
        return;
    }

	retval = ambarella_pm_post(NULL, 0, 0);
#if defined(CONFIG_RPMSG_LINKCTRL)
	rpmsg_linkctrl_cmd_hiber_exit();
#endif
}

static int ambarella_pm_hibernation_pre_snapshot(void)
{
	int					retval = 0;

	retval = ambarella_pm_pre(NULL, 1, 0);

	return retval;
}

static void ambarella_pm_hibernation_finish(void)
{
}

static int ambarella_pm_hibernation_prepare(void)
{
	int					retval = 0;
	return retval;
}

static int ambarella_pm_hibernation_enter(void)
{
	int					retval = 0;

	return retval;
}

static void ambarella_pm_hibernation_leave(void)
{
	ambarella_pm_post(NULL, 1, 0);
}

static int ambarella_pm_hibernation_pre_restore(void)
{
	int					retval = 0;
	return retval;
}

static void ambarella_pm_hibernation_restore_cleanup(void)
{
}

static void ambarella_pm_hibernation_restore_recover(void)
{
	ambarella_pm_post(NULL, 0, 0);
    pm_hibernation_recover = 1;
}

static struct platform_hibernation_ops ambarella_pm_hibernation_ops = {
	.begin = ambarella_pm_hibernation_begin,
	.end = ambarella_pm_hibernation_end,
	.pre_snapshot = ambarella_pm_hibernation_pre_snapshot,
	.finish = ambarella_pm_hibernation_finish,
	.prepare = ambarella_pm_hibernation_prepare,
	.enter = ambarella_pm_hibernation_enter,
	.leave = ambarella_pm_hibernation_leave,
	.pre_restore = ambarella_pm_hibernation_pre_restore,
	.restore_cleanup = ambarella_pm_hibernation_restore_cleanup,
	.recover = ambarella_pm_hibernation_restore_recover,
};

int arch_swsusp_write(unsigned int flags)
{
	int					retval = 0;

    retval = ambarella_pm_linkctrl_enter();
		pm_abcopy_page = 0;
		pm_abcheck_info.aoss_info->copy_pages = 0;
		in_suspend = 0;
		//in_suspend_ipc_svc = 0;
		swsusp_arch_restore_cpu();

	return retval;
}

static int ambernation_increase_page_info(
	struct ambernation_aoss_info *aoss_info,
	struct ambernation_page_info *src_page_info)
{
	int						retval = 0;
	struct ambernation_page_info			*page_info;

	if (pm_abcopy_page == 0) {
		aoss_info->copy_pages = 0;
	}
	if (aoss_info->copy_pages >= aoss_info->total_pages) {
		pr_debug("%s: copy_pages[%d] >= total_pages[%d].\n", __func__,
			aoss_info->copy_pages, aoss_info->total_pages);
		retval = -EPERM;
		goto ambernation_increase_page_info_exit;
	}

	page_info = (struct ambernation_page_info *)
		ambalink_phys_to_virt((u32)aoss_info->page_info);
	pr_debug("%s: page_info %p offset %d, cur %p \n", __func__,
		page_info, aoss_info->copy_pages,
		&page_info[aoss_info->copy_pages]);
	if (pm_abcopy_page == 0) {
		pm_abcopy_page = 1;
		aoss_info->copy_pages = 0;
		page_info[0].src = src_page_info->src;
		page_info[0].dst = src_page_info->dst;
		page_info[0].size = src_page_info->size;
	} else {
		if ((src_page_info->src == (page_info[aoss_info->copy_pages].src + page_info[aoss_info->copy_pages].size)) &&
			(src_page_info->dst == (page_info[aoss_info->copy_pages].dst + page_info[aoss_info->copy_pages].size))) {
			page_info[aoss_info->copy_pages].size += src_page_info->size;
		} else {
			aoss_info->copy_pages++;
			page_info[aoss_info->copy_pages].src = src_page_info->src;
			page_info[aoss_info->copy_pages].dst = src_page_info->dst;
			page_info[aoss_info->copy_pages].size = src_page_info->size;
		}
	}
	pr_debug("%s: copy [0x%08x] to [0x%08x], size [0x%08x] %d\n", __func__,
		page_info[aoss_info->copy_pages].src,
		page_info[aoss_info->copy_pages].dst,
		page_info[aoss_info->copy_pages].size,
		aoss_info->copy_pages);

ambernation_increase_page_info_exit:

	return retval;
}

void arch_copy_data_page(unsigned long dst_pfn, unsigned long src_pfn)
{
	struct ambernation_page_info		src_page_info;

	if (pm_abcheck_info.aoss_info) {
		src_page_info.src = ambalink_page_to_phys(pfn_to_page(src_pfn));
		src_page_info.dst = ambalink_page_to_phys(pfn_to_page(dst_pfn));

		src_page_info.size = PAGE_SIZE;
		ambernation_increase_page_info(pm_abcheck_info.aoss_info,
			&src_page_info);
	}
}


/* ==========================================================================*/
int __init ambarella_init_pm(void)
{
	pm_power_off = ambarella_power_off;
	pm_power_off_prepare = ambarella_power_off_prepare;

	suspend_set_ops(&ambarella_pm_suspend_ops);
	hibernation_set_ops(&ambarella_pm_hibernation_ops);

	pm_abcheck_info.aoss_info = NULL;

	return 0;
}

