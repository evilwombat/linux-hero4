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

#define AMBARELLA_RPMSG_ECHO_PROC
#ifdef AMBARELLA_RPMSG_ECHO_PROC

static const char echo_proc_name[] = "echo";
static struct proc_dir_entry *rpmsg_proc_dir = NULL;
static struct proc_dir_entry *echo_file = NULL;

static int rpmsg_echo_proc_read(char *page, char **start,
	off_t off, int count, int *eof, void *data)
{
	return 0;
}

static int rpmsg_echo_proc_write(struct file *file,
	const char __user *buffer, unsigned long count, void *data)
{
	int retval, i;
	char echo_buf[256];
	struct rpmsg_channel *rpdev = data;

	if (count > sizeof(echo_buf)) {
		pr_err("%s: count %d out of size!\n", __func__, (u32)count);
		retval = -ENOSPC;
		goto rpmsg_echo_write_exit;
	}

	memset(echo_buf, 0x0, sizeof(echo_buf));

	if (copy_from_user(echo_buf, buffer, count)) {
		pr_err("%s: copy_from_user fail!\n", __func__);
		retval = -EFAULT;
		goto rpmsg_echo_write_exit;
	}

	/* Strip trailing \r or \n */
	for (i = count - 1 ; i >= 0; i--) {
		if (echo_buf[i] == '\r' || echo_buf[i] == '\n') {
			echo_buf[i] = '\0';
			count--;
		} else {
			break;
		}
	}

	count ++;

	//printk("[%s]: echo_buf: %s, count = %d\n", __func__, echo_buf, (int) count);

	rpmsg_send(rpdev, echo_buf, count);

	retval = count;

rpmsg_echo_write_exit:
	return count;
}
#endif

static void rpmsg_echo_cb(struct rpmsg_channel *rpdev, void *data, int len,
			void *priv, u32 src)
{
	printk("[ %20s ] recv msg: [%s] from 0x%x and len %d\n",
	       __func__, (const char*)data, src, len);

	/* Echo the recved message back */
	rpmsg_send(rpdev, data, len);
}

static int rpmsg_echo_probe(struct rpmsg_channel *rpdev)
{
	int ret = 0;
	struct rpmsg_ns_msg nsm;

#ifdef AMBARELLA_RPMSG_ECHO_PROC
	rpmsg_proc_dir = proc_mkdir("rpmsg", get_ambarella_proc_dir());
	echo_file = create_proc_entry(echo_proc_name, S_IRUGO | S_IWUSR, rpmsg_proc_dir);
	if (echo_file == NULL) {
		pr_err("%s: %s fail!\n", __func__, echo_proc_name);
		ret = -ENOMEM;
	} else {
		echo_file->read_proc = rpmsg_echo_proc_read;
		echo_file->write_proc = rpmsg_echo_proc_write;
		echo_file->data = (void *) rpdev;
	}
#endif

	nsm.addr = rpdev->dst;
	memcpy(nsm.name, rpdev->id.name, RPMSG_NAME_SIZE);
	nsm.flags = 0;

	rpmsg_send(rpdev, &nsm, sizeof(nsm));

	return ret;
}

static void rpmsg_echo_remove(struct rpmsg_channel *rpdev)
{
}

static struct rpmsg_device_id rpmsg_echo_id_table[] = {
	{ .name	= "echo_ca9_b", },
	{ .name	= "echo_arm11", },
	{ .name	= "echo_cortex", },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_echo_id_table);

static struct rpmsg_driver rpmsg_echo_driver = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_echo_id_table,
	.probe		= rpmsg_echo_probe,
	.callback	= rpmsg_echo_cb,
	.remove		= rpmsg_echo_remove,
};

static int __init rpmsg_echo_init(void)
{
	return register_rpmsg_driver(&rpmsg_echo_driver);
}

static void __exit rpmsg_echo_fini(void)
{
	unregister_rpmsg_driver(&rpmsg_echo_driver);
}

module_init(rpmsg_echo_init);
module_exit(rpmsg_echo_fini);

MODULE_DESCRIPTION("RPMSG Echo Server");
