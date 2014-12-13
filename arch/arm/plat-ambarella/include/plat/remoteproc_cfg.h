/**
 * History:
 *    2012/09/17 - [Tzu-Jung Lee] created file
 *
 * Copyright (C) 2012-2012, Ambarella, Inc.
 *
 * All rights reserved. No Part of this file may be reproduced, stored
 * in a retrieval system, or transmitted, in any form, or by any means,
 * electronic, mechanical, photocopying, recording, or otherwise,
 * without the prior consent of Ambarella, Inc.
 */

#ifndef __PLAT_AMBARELLA_REMOTEPROC_CFG_H
#define __PLAT_AMBARELLA_REMOTEPROC_CFG_H

#ifdef CONFIG_MACH_GINKGO

#include <plat/ambalink_cfg.h>

#else

#define RPMSG_NUM_BUFS			(4096)
#define RPMSG_BUF_SIZE			(4096)
#define RPMSG_TOTAL_BUF_SPACE		(RPMSG_NUM_BUFS * RPMSG_BUF_SIZE)

#define VRING_CA9_B_TO_A		(0x50000000)
#define VRING_CA9_A_TO_B		(0x50020000)

#define VRING_BUF_CA9_A_AND_B		(0x51000000)

#define VRING_CA9_B_TO_A_CLNT_IRQ	(108)
#define VRING_CA9_B_TO_A_HOST_IRQ	(109)

#define VRING_CA9_A_TO_B_CLNT_IRQ	(106)
#define VRING_CA9_A_TO_B_HOST_IRQ	(107)

#define VRING_ARM11_TO_CA9_A		(0x50040000)
#define VRING_CA9_A_TO_ARM11		(0x50060000)

#define VRING_BUF_CA9_A_AND_ARM11	(0x52000000)

#define VRING_CA9_A_TO_ARM11_CLNT_IRQ	(110)
#define VRING_CA9_A_TO_ARM11_HOST_IRQ	(111)

#define VRING_ARM11_TO_CA9_A_CLNT_IRQ	(112)
#define VRING_ARM11_TO_CA9_A_HOST_IRQ	(113)

#define VRING_ARM11_TO_CA9_B		(0x50080000)
#define VRING_CA9_B_TO_ARM11		(0x500A0000)

#define VRING_BUF_CA9_B_AND_ARM11	(0x53000000)

#define VRING_CA9_B_TO_ARM11_CLNT_IRQ	(114)
#define VRING_CA9_B_TO_ARM11_HOST_IRQ	(115)

#define VRING_ARM11_TO_CA9_B_CLNT_IRQ	(116)
#define VRING_ARM11_TO_CA9_B_HOST_IRQ	(117)
#endif // CONFIG_MACH_XXX

#endif /* __PLAT_AMBARELLA_REMOTEPROC_CFG_H */
