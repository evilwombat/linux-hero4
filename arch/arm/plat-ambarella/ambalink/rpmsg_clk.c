/*
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 */

#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/remoteproc.h>

#include <plat/ambcache.h>
#include <plat/ambalink_cfg.h>
#include <plat/timer.h>
#include <plat/clk.h>
#include <plat/event.h>

#ifndef UINT32
typedef u32 UINT32;
#endif

typedef struct clk_name_s {
	int clk_idx;
	char clk_name[64];
} clk_name;

typedef enum _AMBA_CLK_IDX_e_ {
        AMBA_CLK_IDSP = 0,
        AMBA_CLK_CORE,
        AMBA_CLK_CORTEX,
        AMBA_CLK_SDIO,
        AMBA_CLK_SD48,
        AMBA_CLK_UART,
        AMBA_CLK_ARM,
        AMBA_CLK_AHB,
        AMBA_CLK_APB,
        AMBA_CLK_AXI,

        AMBA_NUM_CLK                                /* Total number of CLKs */
} AMBA_CLK_IDX_e;

typedef enum _AMBA_RPDEV_CLK_CMD_e_ {
        CLK_SET = 0,
        CLK_GET,
        CLK_RPMSG_ACK_THREADX,
        CLK_RPMSG_ACK_LINUX,
        CLK_CHANGED_PRE_NOTIFY,
        CLK_CHANGED_POST_NOTIFY,
} AMBA_RPDEV_CLK_CMD_e;

typedef struct _AMBA_RPDEV_CLK_MSG_s_ {
	UINT32  Cmd;
	UINT32  Param;
} AMBA_RPDEV_CLK_MSG_s;

struct rpdev_clk_info {
	u32 clk_idx;
	u32 rate;
	u32 padding[6];
} __attribute__((aligned(32), packed));

DECLARE_COMPLETION(clk_comp);
struct rpmsg_channel *rpdev_clk;
static u32 rpmsg_clk_inited = 0;

static clk_name ambarella_clk[] = {
	{AMBA_CLK_IDSP, "gclk_idsp"},
	{AMBA_CLK_CORE, "gclk_core"},
	{AMBA_CLK_CORTEX, "gclk_cortex"},
	{AMBA_CLK_SDIO, "gclk_sdio"},
	{AMBA_CLK_SD48, "gclk_sd"},
	{AMBA_CLK_UART, "gclk_uart"},
	{AMBA_CLK_ARM, "gclk_arm"},
	{AMBA_CLK_AHB, "gclk_ahb"},
	{AMBA_CLK_APB, "gclk_apb"},
	{AMBA_CLK_AXI, "gclk_axi"},
};

extern int hibernation_start;
u32 oldfreq, newfreq;

/* -------------------------------------------------------------------------- */
static inline unsigned long cpufreq_scale_copy(unsigned long old,
                                               u_int div, u_int mult)
{
#if BITS_PER_LONG == 32

	u64 result = ((u64) old) * ((u64) mult);
	do_div(result, div);
	return (unsigned long) result;

#elif BITS_PER_LONG == 64

	unsigned long result = old * ((u64) mult);
	result /= div;
	return result;

#endif
};

extern unsigned long loops_per_jiffy;
static inline unsigned int ambarella_adjust_jiffies(unsigned long val,
                                                    unsigned int oldfreq, unsigned int newfreq)
{
	if (((val == AMBA_EVENT_PRE_CPUFREQ) && (oldfreq < newfreq)) ||
	    ((val == AMBA_EVENT_POST_CPUFREQ) && (oldfreq != newfreq))) {
		loops_per_jiffy = cpufreq_scale_copy(loops_per_jiffy,
		                                     oldfreq, newfreq);

		return newfreq;
	}

	return oldfreq;
}

/* -------------------------------------------------------------------------- */
int rpmsg_clk_set(void *data, unsigned long rate)
{
	AMBA_RPDEV_CLK_MSG_s clk_ctrl_cmd;
	struct rpdev_clk_info clk_pkg = {0};
	struct clk *clk = (struct clk *) data;
	int i;

	if (!rpmsg_clk_inited || hibernation_start) {
		//printk("%s: rpmsg is not ready (%s)!\n", __func__, clk->name);
		return 0;
	}

	for (i = 0; i < AMBA_NUM_CLK; i++) {
		if (strcmp(clk->name, (const char *) ambarella_clk[i].clk_name) == 0) {
			clk_pkg.clk_idx = i;
			clk_pkg.rate = rate;
			break;
		}
	}

	if (i == AMBA_NUM_CLK) {
		if ((strcmp(clk->name, "pll_out_idsp") != 0) &&
		    (strcmp(clk->name, "pll_out_core") != 0)) {
			printk("%s %s clock is not supported!\n", __func__,
			       clk->name);
		}
		return 0;
	}

	clk_ctrl_cmd.Cmd = CLK_SET;
	clk_ctrl_cmd.Param = (u32) ambalink_virt_to_phys((u32) &clk_pkg);

	ambcache_flush_range((void *) &clk_pkg, sizeof(clk_pkg));

	rpmsg_send(rpdev_clk, &clk_ctrl_cmd, sizeof(clk_ctrl_cmd));

	wait_for_completion(&clk_comp);

	return 0;
}
EXPORT_SYMBOL(rpmsg_clk_set);

int rpmsg_clk_get(void *data)
{
	AMBA_RPDEV_CLK_MSG_s clk_ctrl_cmd;
	static volatile struct rpdev_clk_info clk_pkg = {0};
	struct clk *clk = (struct clk *) data;
	u32 i;

	if (!rpmsg_clk_inited || hibernation_start) {
		//printk("%s: rpmsg is not ready (%s)!\n", __func__, clk->name);
		return 0;
	}

	for (i = 0; i < AMBA_NUM_CLK; i++) {
		if (strcmp(clk->name, (const char *) ambarella_clk[i].clk_name) == 0) {
			clk_pkg.clk_idx = i;
			clk_pkg.rate = 0;
			break;
		}
	}

	if (i == AMBA_NUM_CLK) {
		if ((strcmp(clk->name, "pll_out_idsp") != 0) &&
		    (strcmp(clk->name, "pll_out_core") != 0)) {
			printk("%s %s clock is not supported!\n", __func__,
			       clk->name);
		}
		return 0;
	}

	clk_ctrl_cmd.Cmd = CLK_GET;
	clk_ctrl_cmd.Param = (u32) ambalink_virt_to_phys((u32) &clk_pkg);

	ambcache_clean_range((void *) &clk_pkg, sizeof(clk_pkg));

	rpmsg_send(rpdev_clk, &clk_ctrl_cmd, sizeof(clk_ctrl_cmd));

	wait_for_completion(&clk_comp);

	ambcache_inv_range((void *) &clk_pkg, sizeof(clk_pkg));

	return clk_pkg.rate;
}
EXPORT_SYMBOL(rpmsg_clk_get);
/* -------------------------------------------------------------------------- */
/* Received the ack from ThreadX. */
static int rpmsg_clk_ack_linux(void *data)
{
	complete(&clk_comp);

	return 0;
}

/* Ack Threadx, Linux is done. */
static int rpmsg_clk_ack_threadx(void *data)
{
	AMBA_RPDEV_CLK_MSG_s clk_ctrl_cmd = {0};

	clk_ctrl_cmd.Cmd = CLK_RPMSG_ACK_THREADX;

	return rpmsg_send(rpdev_clk, &clk_ctrl_cmd, sizeof(clk_ctrl_cmd));
}

/* Pre-notify Linux, some clock will be changed. */
static int rpmsg_clk_changed_pre_notify(void *data)
{
	int retval = 0;

	retval = notifier_to_errno(
	                 ambarella_set_event(AMBA_EVENT_PRE_CPUFREQ, NULL));
	if (retval) {
		pr_err("%s: AMBA_EVENT_PRE_CPUFREQ failed(%d)\n",
		       __func__, retval);
	}

	/* Disable rpmsg_clk because some driver will get the clock again. */
	rpmsg_clk_inited = 0;

	oldfreq = (unsigned int)clk_get_rate(clk_get(NULL, "gclk_cortex"));

	ambarella_timer_suspend(1);

	rpmsg_clk_ack_threadx(data);

	return 0;
}

/* Post-notify Linux, some clock has been changed. */
static int rpmsg_clk_changed_post_notify(void *data)
{
	int retval = 0;

	ambarella_timer_resume(1);

	newfreq = (unsigned int)clk_get_rate(clk_get(NULL, "gclk_cortex"));

	ambarella_adjust_jiffies(AMBA_EVENT_POST_CPUFREQ, oldfreq, newfreq);

	retval = notifier_to_errno(
	                 ambarella_set_event(AMBA_EVENT_POST_CPUFREQ, NULL));
	if (retval) {
		pr_err("%s: AMBA_EVENT_POST_CPUFREQ failed(%d)\n",
		       __func__, retval);
	}

	/* Enable rpmsg_clk here because some driver will get the clock. */
	rpmsg_clk_inited = 1;

	rpmsg_clk_ack_threadx(data);

	return 0;
}

/* -------------------------------------------------------------------------- */
typedef int (*PROC_FUNC)(void *data);
static PROC_FUNC proc_list[] = {
	rpmsg_clk_ack_linux,
	rpmsg_clk_changed_pre_notify,
	rpmsg_clk_changed_post_notify,
};

static void rpmsg_clk_cb(struct rpmsg_channel *rpdev, void *data, int len,
                         void *priv, u32 src)
{
	AMBA_RPDEV_CLK_MSG_s *msg = (AMBA_RPDEV_CLK_MSG_s *) data;

#if 0
	printk("recv: cmd = [%d], data = [0x%08x]", msg->Cmd, msg->Param);
#endif

	switch (msg->Cmd) {
	case CLK_RPMSG_ACK_LINUX:
		proc_list[0](data);
		break;
	case CLK_CHANGED_PRE_NOTIFY:
		proc_list[1](data);
		break;
	case CLK_CHANGED_POST_NOTIFY:
		proc_list[2](data);
		break;
	default:
		break;
	}
}

static int rpmsg_clk_probe(struct rpmsg_channel *rpdev)
{
	int ret = 0;
	struct rpmsg_ns_msg nsm;

	//printk("%s: probed", __func__);

	rpdev_clk = rpdev;
	nsm.addr = rpdev->dst;
	memcpy(nsm.name, rpdev->id.name, RPMSG_NAME_SIZE);
	nsm.flags = 0;

	rpmsg_send(rpdev, &nsm, sizeof(nsm));

	rpmsg_clk_inited = 1;

	return ret;
}

static void rpmsg_clk_remove(struct rpmsg_channel *rpdev)
{
}

static struct rpmsg_device_id rpmsg_clk_id_table[] = {
	{ .name	= "AmbaRpdev_CLK", },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_clk_id_table);

static struct rpmsg_driver rpmsg_clk_driver = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_clk_id_table,
	.probe		= rpmsg_clk_probe,
	.callback	= rpmsg_clk_cb,
	.remove		= rpmsg_clk_remove,
};

static int __init rpmsg_clk_init(void)
{
	return register_rpmsg_driver(&rpmsg_clk_driver);
}

static void __exit rpmsg_clk_fini(void)
{
	unregister_rpmsg_driver(&rpmsg_clk_driver);
}

fs_initcall(rpmsg_clk_init);
module_exit(rpmsg_clk_fini);

MODULE_DESCRIPTION("RPMSG CLK Server");
