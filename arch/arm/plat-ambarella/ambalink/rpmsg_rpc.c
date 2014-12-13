/*
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/err.h>
#include <linux/remoteproc.h>
#include <linux/aipc_msg.h>
#if RPC_DEBUG
#include <asm/uaccess.h>
#include <linux/proc_fs.h>
#include <plat/ambalink_cfg.h>
#endif
#include "aipc_priv.h"

#define chnl_tx_name "aipc_rpc"

static struct rpmsg_channel *chnl_tx;

#if RPC_DEBUG
#define RPMSG_RPC_TIMER_PROC
#endif

#ifdef RPMSG_RPC_TIMER_PROC
static const char proc_name[] = "rpc_profiler";
static struct proc_dir_entry *rpc_file = NULL;

/*
 * calculate the time
 */
static unsigned int calc_timer_diff(unsigned int start, unsigned int end){
	unsigned int diff;
	if(end <= start) {
		diff = start - end;
	}
	else{
		diff = 0xFFFFFFFF - end + 1 + start;
	}
	return diff;
}

/*
 * get time
 */
static int rpmsg_rpc_proc_read(char *page, char **start,
	off_t off, int count, int *eof, void *data)
{
	sprintf(page, "%u", amba_readl(PROFILE_TIMER));
	return strlen(page)+1;
}
/*
 * access the rpc statistics in shared memory
 */
static int rpmsg_rpc_proc_write(struct file *file,
	const char __user *buffer, unsigned long count, void *data)
{
	char buf[50];
	unsigned int addr, result, cur_time, diff;
	unsigned int* value, *sec_value;
	int retval, cond;

	if (count > sizeof(buf)) {
		pr_err("%s: count %d out of size!\n", __func__, (u32)count);
		retval = -ENOSPC;
		goto rpmsg_rpc_write_exit;
	}

	memset(buf, 0x0, sizeof(buf));

	if(copy_from_user(buf, buffer, count)){
		pr_err("%s: copy_from_user fail!\n", __func__);
		retval = -EFAULT;
		goto rpmsg_rpc_write_exit;
	}

	/* access to the statistics in shared memory*/
	sscanf(buf,"%d %u %u", &cond, &addr, &result);
	value = (unsigned int *) ambarella_phys_to_virt(addr + RPC_PROFILE_ADDR);
	switch (cond){
		case 1:	//add the result
			*value += result;
			break;
		case 2:	//identify whether the result is larger than value
			if(*value < result){
				*value = result;
			}
			break;
		case 3:	//identify whether the result is smaller than value
			if(*value > result){
				*value = result;
			}
			break;
		case 4:	//calculate injection time
			//value is LuLastInjectTime & sec_value is LuTotalInjectTime
			sec_value = (unsigned int *) ambarella_phys_to_virt(result + RPC_PROFILE_ADDR);
			cur_time = amba_readl(PROFILE_TIMER);
			if( *value != 0){
				//calculate the duration from last to current injection.
				diff = calc_timer_diff(*value, cur_time);
			}
			else {
				diff = 0;
			}
			*sec_value += diff; //sum up the injection time
			*value = cur_time;
			break;
	}
	retval = count;

rpmsg_rpc_write_exit:
	return retval;
}
#endif
/*
 * forward incoming packet from remote to router
 */
static void rpmsg_rpc_recv(struct rpmsg_channel *rpdev, void *data, int len,
			void *priv, u32 src)
{
#if RPC_DEBUG
	struct aipc_pkt *pkt = (struct aipc_pkt *)data;
	pkt->xprt.lk_to_lu_start = amba_readl(PROFILE_TIMER);
#endif
	DMSG("rpmsg_rpc recv %d bytes\n", len);
	aipc_router_send((struct aipc_pkt*)data, len);
}

/*
 * send out packet targeting ThreadX
 */
static void rpmsg_rpc_send_tx(struct aipc_pkt *pkt, int len, int port)
{
	if (chnl_tx)
	{
#if RPC_DEBUG
    	pkt->xprt.lu_to_lk_end = amba_readl(PROFILE_TIMER);
#endif
		rpmsg_send(chnl_tx, pkt, len);
	}
}

static int rpmsg_rpc_probe(struct rpmsg_channel *rpdev)
{
	int ret = 0;
	struct rpmsg_ns_msg nsm;
#ifdef RPMSG_RPC_TIMER_PROC
	rpc_file = create_proc_entry(proc_name, S_IRUGO | S_IWUSR, get_ambarella_proc_dir());
	if(rpc_file == NULL){
		pr_err("%s: %s fail!\n", __func__, proc_name);
		ret = -ENOMEM;
	} else{
		rpc_file->read_proc = rpmsg_rpc_proc_read;
		rpc_file->write_proc = rpmsg_rpc_proc_write;
	}
#endif
	if (!strcmp(rpdev->id.name, chnl_tx_name))
		chnl_tx = rpdev;
	nsm.addr = rpdev->dst;
	memcpy(nsm.name, rpdev->id.name, RPMSG_NAME_SIZE);
	nsm.flags = 0;

	rpmsg_send(rpdev, &nsm, sizeof(nsm));

	return ret;
}

static void rpmsg_rpc_remove(struct rpmsg_channel *rpdev)
{
}

static struct rpmsg_device_id rpmsg_rpc_id_table[] = {
	{ .name	= chnl_tx_name, },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_rpc_id_table);

static struct rpmsg_driver rpmsg_rpc_driver = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_rpc_id_table,
	.probe		= rpmsg_rpc_probe,
	.callback	= rpmsg_rpc_recv,
	.remove		= rpmsg_rpc_remove,
};

static int __init rpmsg_rpc_init(void)
{
	struct xprt_ops ops_tx = {
		.send = rpmsg_rpc_send_tx,
	};
	aipc_router_register_xprt(AIPC_HOST_THREADX, &ops_tx);
	return register_rpmsg_driver(&rpmsg_rpc_driver);
}

static void __exit rpmsg_rpc_fini(void)
{
	unregister_rpmsg_driver(&rpmsg_rpc_driver);
}

module_init(rpmsg_rpc_init);
module_exit(rpmsg_rpc_fini);

MODULE_DESCRIPTION("RPMSG RPC Server");
