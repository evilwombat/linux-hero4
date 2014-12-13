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
#include <linux/remoteproc.h>

#include <linux/aipc/rpmsg_sd.h>
#include <plat/ambcache.h>
#include <plat/ambalink_cfg.h>

#ifndef UINT32
typedef u32 UINT32;
#endif

typedef struct _AMBA_RPDEV_SD_MSG_s_ {
    UINT32  Cmd;
    UINT32  Param;
} AMBA_RPDEV_SD_MSG_s;

DECLARE_COMPLETION(sd0_comp);
DECLARE_COMPLETION(sd1_comp);
struct rpmsg_channel *rpdev_sd;

//#define AMBARELLA_RPMSG_SD_PROC
#ifdef AMBARELLA_RPMSG_SD_PROC

static const char sd_proc_name[] = "sd";
static struct proc_dir_entry *rpmsg_proc_dir = NULL;
static struct proc_dir_entry *sd_file = NULL;

static int rpmsg_sd_proc_read(char *page, char **start,
	off_t off, int count, int *eof, void *data)
{
	return 0;
}

static int rpmsg_sd_proc_write(struct file *file,
	const char __user *buffer, unsigned long count, void *data)
{
	int retval, i;
	char sd_buf[256];
	struct rpmsg_channel *rpdev = data;

	if (count > sizeof(sd_buf)) {
		pr_err("%s: count %d out of size!\n", __func__, (u32)count);
		retval = -ENOSPC;
		goto rpmsg_sd_write_exit;
	}

	memset(sd_buf, 0x0, sizeof(sd_buf));

	if (copy_from_user(sd_buf, buffer, count)) {
		pr_err("%s: copy_from_user fail!\n", __func__);
		retval = -EFAULT;
		goto rpmsg_sd_write_exit;
	}

	/* Strip trailing \r or \n */
	for (i = count - 1 ; i >= 0; i--) {
		if (sd_buf[i] == '\r' || sd_buf[i] == '\n') {
			sd_buf[i] = '\0';
			count--;
		} else {
			break;
		}
	}

	count ++;

	//printk("[%s]: sd_buf: %s, count = %d\n", __func__, sd_buf, (int) count);

	rpmsg_send(rpdev, sd_buf, count);

	retval = count;

rpmsg_sd_write_exit:
	return count;
}
#endif

/* -------------------------------------------------------------------------- */
int rpmsg_sdinfo_get(void *data)
{
	AMBA_RPDEV_SD_MSG_s sd_ctrl_cmd;
	struct rpdev_sdinfo *sdinfo;

	BUG_ON(!rpdev_sd);

	sdinfo = (struct rpdev_sdinfo *) data;

	memset(&sd_ctrl_cmd, 0x0, sizeof(sd_ctrl_cmd));
	sd_ctrl_cmd.Cmd = SD_INFO_GET;
	sd_ctrl_cmd.Param = (u32) ambalink_virt_to_phys((u32) data);

	ambcache_flush_range((void *) sdinfo, sizeof(struct rpdev_sdinfo));

	rpmsg_send(rpdev_sd, &sd_ctrl_cmd, sizeof(sd_ctrl_cmd));

	if (sdinfo->slot_id == SD_HOST0_SD || sdinfo->slot_id == SD_HOST0_SDIO) {
		wait_for_completion(&sd0_comp);
	} else {
		wait_for_completion(&sd1_comp);
	}

	ambcache_inv_range((void *) sdinfo, sizeof(struct rpdev_sdinfo));

	return 0;
}
EXPORT_SYMBOL(rpmsg_sdinfo_get);

int rpmsg_sdresp_get(void *data)
{
	AMBA_RPDEV_SD_MSG_s sd_ctrl_cmd;
	struct rpdev_sdresp *sdresp;

	BUG_ON(!rpdev_sd);

	sdresp = (struct rpdev_sdresp *) data;

	memset(&sd_ctrl_cmd, 0x0, sizeof(sd_ctrl_cmd));
	sd_ctrl_cmd.Cmd = SD_RESP_GET;
	sd_ctrl_cmd.Param = (u32) ambalink_virt_to_phys((u32) data);

	ambcache_flush_range((void *) sdresp, sizeof(struct rpdev_sdresp));

	rpmsg_send(rpdev_sd, &sd_ctrl_cmd, sizeof(sd_ctrl_cmd));

	if (sdresp->slot_id == SD_HOST0_SD || sdresp->slot_id == SD_HOST0_SDIO) {
		wait_for_completion(&sd0_comp);
	} else {
		wait_for_completion(&sd1_comp);
	}

	ambcache_inv_range((void *) sdresp, sizeof(struct rpdev_sdresp));

	return 0;
}
EXPORT_SYMBOL(rpmsg_sdresp_get);

int rpmsg_sd_detect_insert(u32 slot_id)
{
	AMBA_RPDEV_SD_MSG_s sd_ctrl_cmd;

	memset(&sd_ctrl_cmd, 0x0, sizeof(sd_ctrl_cmd));
	sd_ctrl_cmd.Cmd = SD_DETECT_INSERT;
	sd_ctrl_cmd.Param = slot_id;

	rpmsg_send(rpdev_sd, &sd_ctrl_cmd, sizeof(sd_ctrl_cmd));

	return 0;
}
EXPORT_SYMBOL(rpmsg_sd_detect_insert);

int rpmsg_sd_detect_eject(u32 slot_id)
{
	AMBA_RPDEV_SD_MSG_s sd_ctrl_cmd;

	memset(&sd_ctrl_cmd, 0x0, sizeof(sd_ctrl_cmd));
	sd_ctrl_cmd.Cmd = SD_DETECT_EJECT;
	sd_ctrl_cmd.Param = slot_id;

	rpmsg_send(rpdev_sd, &sd_ctrl_cmd, sizeof(sd_ctrl_cmd));

	return 0;
}
EXPORT_SYMBOL(rpmsg_sd_detect_eject);

/* -------------------------------------------------------------------------- */

static int rpmsg_sdresp_detect_change(void *data)
{
#ifdef CONFIG_MMC_AMBARELLA
	AMBA_RPDEV_SD_MSG_s *msg = (AMBA_RPDEV_SD_MSG_s *) data;
	extern void ambarella_sd_rpmsg_cd(int slot_id);

	ambarella_sd_rpmsg_cd((int) msg->Param);
#endif

	return 0;
}

static int rpmsg_sd_ack(void *data)
{
	AMBA_RPDEV_SD_MSG_s *msg = (AMBA_RPDEV_SD_MSG_s *) data;

	if (msg->Param == SD_HOST0_SD || msg->Param == SD_HOST0_SDIO) {
		complete(&sd0_comp);
	} else {
		complete(&sd1_comp);
	}

	return 0;
}

/* -------------------------------------------------------------------------- */
typedef int (*PROC_FUNC)(void *data);
static PROC_FUNC proc_list[] = {
	rpmsg_sdresp_detect_change,
	rpmsg_sd_ack,
};

static void rpmsg_sd_cb(struct rpmsg_channel *rpdev, void *data, int len,
			void *priv, u32 src)
{
	AMBA_RPDEV_SD_MSG_s *msg = (AMBA_RPDEV_SD_MSG_s *) data;

#if 0
	printk("recv: cmd = [%d], data = [0x%08x]", msg->Cmd, msg->Param);
#endif
	switch (msg->Cmd) {
	case SD_DETECT_CHANGE:
		proc_list[0](data);
		break;
	case SD_RPMSG_ACK:
		proc_list[1](data);
		break;
	default:
		printk("%s err: cmd = [%d], data = [0x%08x]",
			__func__, msg->Cmd, msg->Param);
		break;
	}
}

static int rpmsg_sd_probe(struct rpmsg_channel *rpdev)
{
	int ret = 0;
	struct rpmsg_ns_msg nsm;

	//printk("%s: probed", __func__);

#ifdef AMBARELLA_RPMSG_SD_PROC
	rpmsg_proc_dir = proc_mkdir("rpmsg_sd", get_ambarella_proc_dir());
	sd_file = create_proc_entry(sd_proc_name, S_IRUGO | S_IWUSR, rpmsg_proc_dir);
	if (sd_file == NULL) {
		pr_err("%s: %s fail!\n", __func__, sd_proc_name);
		ret = -ENOMEM;
	} else {
		sd_file->read_proc = rpmsg_sd_proc_read;
		sd_file->write_proc = rpmsg_sd_proc_write;
		sd_file->data = (void *) rpdev;
	}
#endif

	rpdev_sd = rpdev;
	nsm.addr = rpdev->dst;
	memcpy(nsm.name, rpdev->id.name, RPMSG_NAME_SIZE);
	nsm.flags = 0;

	rpmsg_send(rpdev, &nsm, sizeof(nsm));

	return ret;
}

static void rpmsg_sd_remove(struct rpmsg_channel *rpdev)
{
}

static struct rpmsg_device_id rpmsg_sd_id_table[] = {
	{ .name	= "AmbaRpdev_SD", },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_sd_id_table);

static struct rpmsg_driver rpmsg_sd_driver = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_sd_id_table,
	.probe		= rpmsg_sd_probe,
	.callback	= rpmsg_sd_cb,
	.remove		= rpmsg_sd_remove,
};

static int __init rpmsg_sd_init(void)
{
	return register_rpmsg_driver(&rpmsg_sd_driver);
}

static void __exit rpmsg_sd_fini(void)
{
	unregister_rpmsg_driver(&rpmsg_sd_driver);
}

fs_initcall(rpmsg_sd_init);
module_exit(rpmsg_sd_fini);

MODULE_DESCRIPTION("RPMSG SD Server");
