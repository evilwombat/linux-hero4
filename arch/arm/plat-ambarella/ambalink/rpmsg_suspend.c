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

/*----------------------------------------------------------------------------*/
typedef u32 UINT32;

typedef enum _RPDEV_SUSP_HIBER_CMD_e_ {
    HIBER_CMD_CHECK_FROM_LINUX = 0,
    HIBER_CMD_SUSPEND_FROM_LINUX,
    HIBER_CMD_RESUME_FROM_LINUX
} RPDEV_SUSP_HIBER_CMD_e;

typedef struct _RpdevSusp_Hiber_s_ {
    UINT32  Cmd;
    UINT32  Param1;
    UINT32  Param2;
} RpdevSusp_Hiber_s;

struct rpmsg_channel *rpdev_susp;

int rpmsg_susp_hiber_cmd_check(u32 info)
{
	RpdevSusp_Hiber_s hiber;

	memset(&hiber, 0x0, sizeof(hiber));
	hiber.Cmd = HIBER_CMD_CHECK_FROM_LINUX;
	hiber.Param1 = info;
	
	rpmsg_send(rpdev_susp, &hiber, sizeof(hiber));
}
EXPORT_SYMBOL(rpmsg_susp_hiber_cmd_check);

int rpmsg_susp_hiber_cmd_suspend(int flag)
{
	RpdevSusp_Hiber_s hiber;

	memset(&hiber, 0x0, sizeof(hiber));
	hiber.Cmd = HIBER_CMD_SUSPEND_FROM_LINUX;
	
	rpmsg_send(rpdev_susp, &hiber, sizeof(hiber));
}
EXPORT_SYMBOL(rpmsg_susp_hiber_cmd_suspend);

int rpmsg_susp_hiber_cmd_resume(void)
{
	RpdevSusp_Hiber_s hiber;

	memset(&hiber, 0x0, sizeof(hiber));
	hiber.Cmd = HIBER_CMD_RESUME_FROM_LINUX;
	
	rpmsg_send(rpdev_susp, &hiber, sizeof(hiber));
}
EXPORT_SYMBOL(rpmsg_susp_hiber_cmd_resume);

/*----------------------------------------------------------------------------*/

static void rpmsg_suspend_cb(struct rpmsg_channel *rpdev, void *data, int len,
			void *priv, u32 src)
{
	printk("[ %20s ] recv msg: src = 0x%x and len = %d\n", __func__ , src, len);
}

static int rpmsg_suspend_probe(struct rpmsg_channel *rpdev)
{
	int ret = 0;
	struct rpmsg_ns_msg nsm;

	rpdev_susp = rpdev;
	nsm.addr = rpdev->dst;
	memcpy(nsm.name, rpdev->id.name, RPMSG_NAME_SIZE);
	nsm.flags = 0;

	rpmsg_send(rpdev, &nsm, sizeof(nsm));

	return ret;
}

static void rpmsg_suspend_remove(struct rpmsg_channel *rpdev)
{
}

static struct rpmsg_device_id rpmsg_suspend_id_table[] = {
	{ .name	= "rpdev_suspend", },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_suspend_id_table);

static struct rpmsg_driver rpmsg_suspend_driver = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_suspend_id_table,
	.probe		= rpmsg_suspend_probe,
	.callback	= rpmsg_suspend_cb,
	.remove		= rpmsg_suspend_remove,
};

static int __init rpmsg_suspend_init(void)
{
	return register_rpmsg_driver(&rpmsg_suspend_driver);
}

static void __exit rpmsg_suspend_fini(void)
{
	unregister_rpmsg_driver(&rpmsg_suspend_driver);
}

module_init(rpmsg_suspend_init);
module_exit(rpmsg_suspend_fini);

MODULE_DESCRIPTION("RPMSG Echo Server");
