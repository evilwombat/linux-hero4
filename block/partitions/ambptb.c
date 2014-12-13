/*
 *  fs/partitions/ambptb.c
 *
 * Author: Anthony Ginger <hfjiang@ambarella.com>
 *
 * Copyright (C) 2004-2010, Ambarella, Inc.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include "check.h"
#include "ambptb.h"
#include <plat/ptb.h>

int ambptb_partition(struct parsed_partitions *state)
{
	int					i;
	int					slot = 1;
	unsigned char				*data;
	Sector					sect;
	u32					sect_size;
	u32					sect_address;
	u32					sect_offset;
	flpart_meta_t				*ptb_meta;
	char					ptb_tmp[1 + BDEVNAME_SIZE + 1];
	int					result = 0;

	sect_size = bdev_logical_block_size(state->bdev);
	sect_address = sizeof(flpart_table_t) / sect_size;
	sect_offset = sizeof(flpart_table_t) % sect_size;

	data = read_part_sector(state, sect_address, &sect);
	if (!data) {
		result = -1;
		goto ambptb_partition_exit;
	}
	ptb_meta = (flpart_meta_t *)(data + sect_offset);
	if ((ptb_meta->magic == PTB_META_MAGIC) ||
		(ptb_meta->magic == PTB_META_MAGIC2))
		goto ambptb_partition_report_ptb;

	put_dev_sector(sect);
	data = read_part_sector(state, sect_address + 4, &sect);
	if (!data) {
		result = -1;
		goto ambptb_partition_exit;
	}
	ptb_meta = (flpart_meta_t *)(data + sect_offset);
	if ((ptb_meta->magic != PTB_META_MAGIC) &&
		(ptb_meta->magic != PTB_META_MAGIC2))
		goto ambptb_partition_exit;

ambptb_partition_report_ptb:
	for (i = 0; i < TOTAL_FW_PARTS; i++) {
		if (slot >= state->limit)
			break;

		if (ptb_meta->part_dev[i] == BOOT_DEV_SM) {
			state->parts[slot].from = ptb_meta->part_info[i].sblk;
			state->parts[slot].size = ptb_meta->part_info[i].nblk;
			snprintf(ptb_tmp, sizeof(ptb_tmp), " %s",
				ptb_meta->part_info[i].name);
			strlcat(state->pp_buf, ptb_tmp, PAGE_SIZE);
			slot++;
		}
	}

#if 0
	for (i = 0; i < ARRAY_SIZE(ptb_meta->sm_stg); i++) {
		if (slot >= state->limit)
			break;

		if (ptb_meta->sm_stg[i].nblk != 0) {
			state->parts[slot].from = ptb_meta->sm_stg[i].sblk;
			state->parts[slot].size = ptb_meta->sm_stg[i].nblk;
			snprintf(ptb_tmp, sizeof(ptb_tmp), " stg%d", i + 1);
			strlcat(state->pp_buf, ptb_tmp, PAGE_SIZE);
			slot++;
		}
	}
#endif
	strlcat(state->pp_buf, "\n", PAGE_SIZE);
	put_dev_sector(sect);
	result = 1;

ambptb_partition_exit:
	return result;
}

