/*
 * arch/arm/plat-ambarella/misc/aipc_nl.c
 *
 * Author: Joey Li <jli@ambarella.com>
 *
 * Copyright (C) 2013, Ambarella, Inc.
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
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <net/sock.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/aipc_msg.h>
#include "aipc_priv.h"

struct sock *nl_sock = NULL;

/*
 * forward incoming msg to binder
 */
static void data_handler(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	struct aipc_pkt *pkt;
	int len;

	nlh = (struct nlmsghdr*)skb->data;
	pkt = (struct aipc_pkt*)NLMSG_DATA(nlh);
	len = NLMSG_PAYLOAD(nlh, 0);
	aipc_router_send(pkt, len);	
}

static void aipc_nl_send(struct aipc_pkt *pkt, int len, int port)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;

	DMSG("aipc_nl_send to port %u, len %d\n", port, len); 
	skb = nlmsg_new(len, 0);
	nlh = nlmsg_put(skb, 0, 0, NLMSG_DONE, len, 0);
	NETLINK_CB(skb).dst_group = 0;
	memcpy(nlmsg_data(nlh), pkt, len);
	nlmsg_unicast(nl_sock, skb, port);
	/* we dont't need call nlmsg_free, kernel will take care of this */
}

	
int __init aipc_nl_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.input = data_handler,
	};
	struct xprt_ops ops = {
		.send = aipc_nl_send,
	};
	nl_sock = netlink_kernel_create(&init_net, NL_PROTO_AMBAIPC, &cfg);
	aipc_router_register_xprt(AIPC_HOST_LINUX, &ops);
	return 0;
}

void __exit aipc_nl_exit(void)
{
	netlink_kernel_release(nl_sock);
}

