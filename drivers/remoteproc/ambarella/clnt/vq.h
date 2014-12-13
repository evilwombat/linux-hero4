/**
 * system/src/rpclnt/vq.h
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

#ifndef __VQ_H__
#define __VQ_H__

#if defined(__KERNEL__)
#include <linux/virtio_ring.h>
#include <linux/sched.h>
#else
#include <itron.h>
#include "virtio_ring.h"

static inline void *ambarella_virt_to_phys(void *addr)
{
        return addr;
}

static inline void *ambarella_phys_to_virt(void *addr)
{
        return addr;
}
#endif

struct vq {
#if defined(__KERNEL__)
	spinlock_t		lock;
	struct completion	comp;
#else
	ID			mtxid;
	ID			flgid;
#endif
	void			(*cb)(struct vq *vq);
	void			(*kick)(struct vq *vq);
	struct vring		vring;
	u16			last_avail_idx;
	u16			last_used_idx;
};

extern struct vq *vq_create(void (*cb)(struct vq *vq),
			    void (*kick)(struct vq *vq),
			    int num_bufs,
			    u32 vring_virt, int vring_algn);

extern void vq_wait_for_completion(struct vq *vq);
extern void vq_complete(struct vq *vq);

extern int vq_kick_prepare(struct vq *vq);
extern void vq_enable_used_notify(struct vq *vq);
extern void vq_disable_used_notify(struct vq *vq);
extern int vq_more_avail_buf(struct vq *vq);
extern int vq_get_avail_buf(struct vq *vq, void **buf, int *len);
extern int vq_add_used_buf(struct vq *vq, int idx, int len);
extern int vq_init_unused_bufs(struct vq *vq, void *buf, int len);

#endif /* __VQ_H__ */
