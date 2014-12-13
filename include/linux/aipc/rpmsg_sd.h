/*
 * include/linux/aipc/rpmsg_sd.h
 *
 * Authors:
 *	Kerson Chen <cychenc@ambarella.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * Copyright (C) 2011-2015, Ambarella Inc.
 */

#ifndef __AIPC_RPMSG_SD_H__
#define __AIPC_RPMSG_SD_H__

#ifdef __KERNEL__

typedef enum _AMBA_RPDEV_SD_CMD_e_ {
	SD_INFO_GET = 0,
	SD_RESP_GET,
	SD_DETECT_INSERT,
	SD_DETECT_EJECT,
	SD_DETECT_CHANGE,
	SD_RPMSG_ACK
} AMBA_RPDEV_SD_CMD_e;

/**
 * The slot IDs
 */
typedef enum _ambarella_slot_id {
	SD_HOST0_SD = 0,
	SD_HOST1_SD,
	SD_HOST0_SDIO,

	SD_MAX_CARD      /* Total number of slot IDs */
} ambarella_slot_id;

struct rpdev_sdinfo {

	u32 slot_id;	/**< from LK */
	u8  from_rpmsg;	/**< from LK */
	u8  padding[3];

	u8  is_init;
	u8  is_sdmem;
	u8  is_mmc;
	u8  is_sdio;
	u32 rca;
	u32 hcs;
	u32 ocr;
	u32 clk;
	u32 bus_width;
} __attribute__((aligned(32), packed));

struct rpdev_sdresp {

	u32 slot_id;
	u32 opcode;
	int ret;
	u32 padding;
	u32 resp[4];
	char buf[512];
} __attribute__((aligned(32), packed));

int rpmsg_sdinfo_get(void *data);
int rpmsg_sdresp_get(void *data);
int rpmsg_sd_detect_insert(u32 slot_id);
int rpmsg_sd_detect_eject(u32 slot_id);

#endif	/* __KERNEL__ */

#endif
