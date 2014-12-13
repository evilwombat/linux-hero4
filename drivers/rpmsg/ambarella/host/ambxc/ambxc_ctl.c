/**
 * History:
 *    2012/09/15 - [Tzu-Jung Lee] created file
 *
 * Copyright (C) 2012-2012, Ambarella, Inc.
 *
 * All rights reserved. No Part of this file may be reproduced, stored
 * in a retrieval system, or transmitted, in any form, or by any means,
 * electronic, mechanical, photocopying, recording, or otherwise,
 * without the prior consent of Ambarella, Inc.
 */

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/remoteproc.h>
#include <linux/rpmsg.h>

static struct rpmsg_channel *rpdev_ambxc_ctl;

int ambxc_ctl_send(const char *buf, int count)
{
	struct rpmsg_channel *rpdev = rpdev_ambxc_ctl;

	while (rpmsg_trysend(rpdev, (char*)buf, count))
		;

	return count;
}

static void rpmsg_ambxc_ctl_cb(struct rpmsg_channel *rpdev, void *data,
				int len, void *priv, u32 src)
{
	//ambxc_ctl_cb((int)priv, data, len);
}

static int rpmsg_ambxc_ctl_probe(struct rpmsg_channel *rpdev)
{
	int ret = 0;
	struct rpmsg_ns_msg nsm;
	int index;

	nsm.addr = rpdev->dst;
	memcpy(nsm.name, rpdev->id.name, RPMSG_NAME_SIZE);
	nsm.flags = 0;

	rpdev->ept->priv = (void*)index;
	rpdev_ambxc_ctl = rpdev;
	rpmsg_sendto(rpdev, &nsm, sizeof(nsm), rpdev->dst);

	return ret;
}

static void rpmsg_ambxc_ctl_remove(struct rpmsg_channel *rpdev)
{
}

static struct rpmsg_device_id rpmsg_ambxc_ctl_id_table[] = {
	{ .name	= "ambxc_ctl_arm11", }, /* H: CA9-A, C: ARM11 */
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_ambxc_ctl_id_table);

struct rpmsg_driver rpmsg_ambxc_ctl_driver =
{
	.drv.name	= "rpmsg_ambxc_ctl",
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_ambxc_ctl_id_table,
	.probe		= rpmsg_ambxc_ctl_probe,
	.callback	= rpmsg_ambxc_ctl_cb,
	.remove		= rpmsg_ambxc_ctl_remove,
};
