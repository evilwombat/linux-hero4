/**
 * system/src/rpclnt/rpclnt.h
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

#ifndef __RPCLNT_H__
#define __RPCLNT_H__

struct rpclnt
{
	struct work_struct	svq_work;
	struct work_struct	rvq_work;

	int			inited;
	char			*name;

	void			*svq;
	int			svq_tx_irq;
	int			svq_rx_irq;
	int			svq_num_bufs;
	u32			svq_buf_phys;
	u32			svq_vring_phys;
	int			svq_vring_algn;

	void			*rvq;
	int			rvq_tx_irq;
	int			rvq_rx_irq;
	int			rvq_num_bufs;
	u32			rvq_buf_phys;
	u32			rvq_vring_phys;
	int			rvq_vring_algn;
};

extern struct rpclnt *rpclnt_sync(const char *bus_name);

#endif /* __RPCLNT_H__ */
