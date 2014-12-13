/**
 * system/src/rpclnt/rpdev.h
 *
 * History:
 *    2012/08/15 - [Tzu-Jung Lee] created file
 *
 * Copyright (C) 2012-2012, Ambarella, Inc.
 *
 * All rights reserved. No Part of this file may be reproduced, stored
 * in a retrieval system, or transmitted, in any form, or by any means,
 * electronic, mechanical, photocopying, recording, or otherwise,
 * without the prior consent of Ambarella, Inc.
 */

#ifndef __RPDEV_H__
#define __RPDEV_H__

#define RPMSG_VRING_ALIGN		(4096)
#define RPMSG_RESERVED_ADDRESSES	(1024)
#define RPMSG_ADDRESS_ANY		(0xffffffff)
#define RPMSG_NS_ADDR			(53)
#define RPMSG_NAME_LEN			(32)

#if defined (__KERNEL__)
#include <plat/remoteproc_cfg.h>
#include <linux/sched.h>
#include <linux/rpmsg.h>
#else
#include "rpmsg.h"
#endif

struct rpdev;
typedef void (*rpdev_cb)(struct rpdev* rpdev, void *data, int len,
			 void *priv, u32 src);
struct rpdev {

#if defined(__KERNEL__)
	struct completion	comp;
#else
	ID			flgid;
#endif

	struct rpclnt		*rpclnt;
	char			name[RPMSG_NAME_LEN];
	u32     		src;
	u32     		dst;
	u32			flags;

	rpdev_cb		cb;
	void			*priv;
};

extern int rpdev_register(struct rpdev *rpdev, const char *bus_name);

extern struct rpdev *rpdev_alloc(const char *name, int flags,
                                 rpdev_cb cb, void *priv);

extern int rpdev_send_offchannel(struct rpdev *rpdev, u32 src, u32 dst,
                                 void *data, int len);

extern int rpdev_send(struct rpdev *rpdev, void *data, int len);
extern int rpdev_trysend(struct rpdev *rpdev, void *data, int len);

#endif /* __RPDEV_H__ */
