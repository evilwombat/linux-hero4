/*
 * drivers/mtd/ambarella_nand.c
 *
 * History:
 *	2008/04/11 - [Cao Rongrong & Chien-Yang Chen] created file
 *	2009/01/04 - [Anthony Ginger] Port to 2.6.28
 *	2012/05/23 - [Ken He] Add the dma engine driver method support
 *	2012/07/03 - [Ken He] use the individual FDMA for nand
 *
 * Copyright (C) 2004-2009, Ambarella, Inc.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/bitrev.h>
#include <linux/bch.h>
#include <linux/gpio.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>
#include <asm/dma.h>
#include <mach/dma.h>
#include <mach/board.h>
#include <plat/nand.h>
#include <plat/ptb.h>
#include <plat/fio.h>
#include <plat/event.h>

#if defined(CONFIG_AMBALINK_LOCK)
#include <plat/ambalink_cfg.h>
/*
 * NAND DSM bug: doing copyback after a read command will be failed.
 * We need to save the last NAND command of Linux to notify ThreadX to
 * workaround this bug.
 */
u32 ambalink_nand_last_cmd_addr;
#endif

#define AMBARELLA_NAND_DMA_BUFFER_SIZE	4096

#define AMB_SOFT_BCH_EXTRA_SIZE		6

#ifdef CONFIG_MTD_NAND_AMBARELLA_SWECC
static bool amba_nand_sw_ecc = true;
#else
static bool amba_nand_sw_ecc = false;
#endif
/* nand_check_wp will be checked before write, so wait MTD fix */
#undef AMBARELLA_NAND_WP

struct ambarella_nand_info {
	struct nand_chip		chip;
	struct mtd_info			mtd;
	struct nand_hw_control		controller;

	struct device			*dev;
	wait_queue_head_t		wq;

	struct ambarella_platform_nand	*plat_nand;
	int				suspend;
	/* dma irq for transferring data between Nand and FIFO */
	int				dma_irq;
	int				cmd_irq;
	/* fdma irq for transferring data between FIFO and Dram */
	int				fdma_irq;
	int				wp_gpio;
	unsigned char __iomem		*regbase;
	unsigned char __iomem		*fdmaregbase;
	u32				dmabase;
	bool				soft_ecc;
	/* used for software BCH */
	struct bch_control		*bch;
	u32				*errloc;
	u8				*bch_data;
	u8				read_ecc_rev[13];
	u8				calc_ecc_rev[13];

	dma_addr_t			dmaaddr;
	u8				*dmabuf;
	int				dma_bufpos;
	u32				dma_status;
	u32				fio_dma_sta;
	u32				fio_ecc_sta;
	atomic_t			irq_flag;

	/* saved column/page_addr during CMD_SEQIN */
	int				seqin_column;
	int				seqin_page_addr;

	/* Operation parameters for nand controller register */
	int				err_code;
	u32				cmd;
	u32				control_reg;
	u32				addr_hi;
	u32				addr;
	u32				dst;
	dma_addr_t			buf_phys;
	dma_addr_t			spare_buf_phys;
	u32				len;
	u32				slen;
	u32				area;
	u32				ecc;

	struct ambarella_nand_timing	*origin_timing;
	struct ambarella_nand_timing	current_timing;

	struct notifier_block		system_event;
	struct semaphore		system_event_sem;

};

static struct nand_ecclayout amb_oobinfo_512 = {
	.eccbytes = 5,
	.eccpos = {6, 7, 8, 9, 10},
	.oobfree = {{0, 5}, {11, 5}}
};

static struct nand_ecclayout amb_oobinfo_2048 = {
	.eccbytes = 20,
	.eccpos = {8, 9, 10,11, 12,
		24, 25, 26, 27, 28,
		40, 41, 42, 43, 44,
		56, 57, 58, 59, 60},
	.oobfree = {{1, 7}, {13, 3},
		{17, 7}, {29, 3},
		{33, 7}, {45, 3},
		{49, 7}, {61, 3}}
};

static struct nand_ecclayout amb_oobinfo_2048_dsm_ecc6 = {
	.eccbytes = 40,
	.eccpos = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
		22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
		38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
		54, 55, 56, 57, 58, 59, 60, 61, 62, 63},
	.oobfree = {{1, 5}, {17, 5},
			{33, 5}, {49, 5}}
};

static struct nand_ecclayout amb_oobinfo_2048_dsm_ecc8 = {
	.eccbytes = 52,
	.eccpos = {19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
		51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
		83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,
		115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127},
	.oobfree = {{2, 17}, {34, 17},
			{66, 17}, {98, 17}}
};

#ifdef CONFIG_PLAT_AMBARELLA_AMBALINK
/*
 * The generic flash bbt decriptors overlap with our ecc
 * hardware, so define some Ambarella specific ones.
 */
static uint8_t bbt_pattern[] = { 'B', 'b', 't', '0' };
static uint8_t mirror_pattern[] = { '1', 't', 'b', 'B' };

static struct nand_bbt_descr bbt_main_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		| NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs = 1,
	.len = 4,
	.veroffs = 13,
	.maxblocks = 4,
	.pattern = bbt_pattern,
};

static struct nand_bbt_descr bbt_mirror_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		| NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs = 1,
	.len = 4,
	.veroffs = 13,
	.maxblocks = 4,
	.pattern = mirror_pattern,
};

/* bbt pattern for dual space mode */
static u8 bbt_pattern_dsm[] = { 'B', 'b', 't', '2' };
static u8 mirror_pattern_dsm[] = { '3', 't', 'b', 'B' };

static struct nand_bbt_descr bbt_main_descr_dsm = {
       .options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
           | NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
       .offs = 1,
       .len = 4,
       .veroffs = 5,
       .maxblocks = 4,
       .pattern = bbt_pattern_dsm,
};

static struct nand_bbt_descr bbt_mirror_descr_dsm = {
       .options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
           | NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
       .offs = 1,
       .len = 4,
       .veroffs = 5,
       .maxblocks = 4,
       .pattern = mirror_pattern_dsm,
};
#endif

/* ==========================================================================*/
static char part_name[PART_MAX][PART_NAME_LEN];

/* ==========================================================================*/
#define NAND_TIMING_RSHIFT24BIT(x)	(((x) & 0xff000000) >> 24)
#define NAND_TIMING_RSHIFT16BIT(x)	(((x) & 0x00ff0000) >> 16)
#define NAND_TIMING_RSHIFT8BIT(x)	(((x) & 0x0000ff00) >> 8)
#define NAND_TIMING_RSHIFT0BIT(x)	(((x) & 0x000000ff) >> 0)

#define NAND_TIMING_LSHIFT24BIT(x)	((x) << 24)
#define NAND_TIMING_LSHIFT16BIT(x)	((x) << 16)
#define NAND_TIMING_LSHIFT8BIT(x)	((x) << 8)
#define NAND_TIMING_LSHIFT0BIT(x)	((x) << 0)

static int nand_timing_calc(u32 clk, int minmax, int val)
{
	u32 x;
	int n,r;

	x = val * clk;
	n = x / 1000;
	r = x % 1000;

	if (r != 0)
		n++;

	if (minmax)
		n--;
	return n < 1 ? 1 : n;
}

static inline int nand_amb_is_hw_bch(struct ambarella_nand_info *nand_info)
{
	return !nand_info->soft_ecc && nand_info->plat_nand->sets->ecc_bits > 1;
}

static inline int nand_amb_is_sw_bch(struct ambarella_nand_info *nand_info)
{
	return nand_info->soft_ecc && nand_info->plat_nand->sets->ecc_bits > 1;
}

static void nand_amb_corrected_recovery(struct ambarella_nand_info *nand_info)
{
	u32 fio_ctr_reg, fio_dmactr_reg;

	/* FIO reset will just reset FIO registers, but will not affect
	 * Nand controller. */
	fio_ctr_reg = amba_readl(nand_info->regbase + FIO_CTR_OFFSET);
	fio_dmactr_reg = amba_readl(nand_info->regbase + FIO_DMACTR_OFFSET);
	ambarella_fio_rct_reset();
	amba_writel(nand_info->regbase + FIO_CTR_OFFSET, fio_ctr_reg);
	amba_writel(nand_info->regbase + FIO_DMACTR_OFFSET, fio_dmactr_reg);
}

static void nand_amb_enable_dsm(struct ambarella_nand_info *nand_info)
{
	u32 fio_dsm_ctr = 0, fio_ctr_reg = 0, dma_dsm_ctr = 0;
	fio_ctr_reg = amba_readl(nand_info->regbase + FIO_CTR_OFFSET);

	fio_dsm_ctr |= (FIO_DSM_EN | FIO_DSM_MAJP_2KB);
	dma_dsm_ctr |= (DMA_DSM_EN | DMA_DSM_MAJP_2KB);
	fio_ctr_reg |= FIO_CTR_RS;
	fio_ctr_reg &= ~(FIO_CTR_CO | FIO_CTR_SE | FIO_CTR_ECC_6BIT
					| FIO_CTR_ECC_8BIT);

	if (nand_info->plat_nand->sets->ecc_bits == 6) {
		fio_dsm_ctr |= FIO_DSM_SPJP_64B;
		dma_dsm_ctr |= DMA_DSM_SPJP_64B;
	} else {
		u32 nand_ext_ctr_reg = 0;
		fio_dsm_ctr |= FIO_DSM_SPJP_128B;
		dma_dsm_ctr |= DMA_DSM_SPJP_128B;

		nand_ext_ctr_reg = amba_readl(nand_info->regbase +
				FLASH_EX_CTR_OFFSET);
		nand_ext_ctr_reg |= NAND_EXT_CTR_SP_2X;
		amba_writel(nand_info->regbase + FLASH_EX_CTR_OFFSET,
					nand_ext_ctr_reg);
	}

	amba_writel(nand_info->regbase + FIO_CTR_OFFSET, fio_ctr_reg | FIO_CTR_RR);
	amba_writel(nand_info->regbase + FIO_DSM_CTR_OFFSET, fio_dsm_ctr);
	amba_writel(nand_info->regbase + FIO_CTR_OFFSET, fio_ctr_reg);
	amba_writel(nand_info->fdmaregbase + FDMA_DSM_CTR_OFFSET, dma_dsm_ctr);

}

static void nand_amb_enable_bch(struct ambarella_nand_info *nand_info)
{
	u32 fio_dsm_ctr = 0, fio_ctr_reg = 0, dma_dsm_ctr = 0;
	fio_ctr_reg = amba_readl(nand_info->regbase + FIO_CTR_OFFSET);

	if (nand_info->plat_nand->sets->ecc_bits > 1) {
			fio_dsm_ctr |= (FIO_DSM_EN | FIO_DSM_MAJP_2KB);
			dma_dsm_ctr |= (DMA_DSM_EN | DMA_DSM_MAJP_2KB);
			fio_ctr_reg |= (FIO_CTR_RS | FIO_CTR_CO);

			if (nand_info->plat_nand->sets->ecc_bits == 6) {
			  fio_dsm_ctr |= FIO_DSM_SPJP_64B;
				dma_dsm_ctr |= DMA_DSM_SPJP_64B;
				fio_ctr_reg |= FIO_CTR_ECC_6BIT;
			} else {
				u32 nand_ext_ctr_reg = 0;
				fio_dsm_ctr |= FIO_DSM_SPJP_128B;
				dma_dsm_ctr |= DMA_DSM_SPJP_128B;
				fio_ctr_reg |= FIO_CTR_ECC_8BIT;
				nand_ext_ctr_reg = amba_readl(nand_info->regbase +
						FLASH_EX_CTR_OFFSET);
				nand_ext_ctr_reg |= NAND_EXT_CTR_SP_2X;
				amba_writel(nand_info->regbase + FLASH_EX_CTR_OFFSET,
							nand_ext_ctr_reg);
			}
	}

	amba_writel(nand_info->regbase + FIO_CTR_OFFSET, fio_ctr_reg | FIO_CTR_RR);
	amba_writel(nand_info->regbase + FIO_DSM_CTR_OFFSET, fio_dsm_ctr);
	amba_writel(nand_info->regbase + FIO_CTR_OFFSET, fio_ctr_reg);
	amba_writel(nand_info->fdmaregbase + FDMA_DSM_CTR_OFFSET, dma_dsm_ctr);

}

static void nand_amb_disable_bch(struct ambarella_nand_info *nand_info)
{
	u32 fio_ctr_reg = 0;

	fio_ctr_reg = amba_readl(nand_info->regbase + FIO_CTR_OFFSET);
	/* Setup FIO Dual Space Mode Control Register */
	fio_ctr_reg |= FIO_CTR_RS;
	fio_ctr_reg &= ~(FIO_CTR_CO |
			 FIO_CTR_ECC_6BIT |
			 FIO_CTR_ECC_8BIT);

#ifndef CONFIG_PLAT_AMBARELLA_AMBALINK
	if (nand_info->plat_nand->sets->ecc_bits == 8) {
		u32 nand_ext_ctr_reg = 0;
		nand_ext_ctr_reg = amba_readl(nand_info->regbase +
					FLASH_EX_CTR_OFFSET);
		nand_ext_ctr_reg &= ~NAND_EXT_CTR_SP_2X;
		amba_writel(nand_info->regbase + FLASH_EX_CTR_OFFSET,
					nand_ext_ctr_reg);
	}
	amba_writel(nand_info->regbase + FIO_CTR_OFFSET, fio_ctr_reg);

	amba_writel(nand_info->regbase + FIO_DSM_CTR_OFFSET, 0);
	amba_writel(nand_info->fdmaregbase + FDMA_DSM_CTR_OFFSET, 0);
#endif
}

static int nand_bch_spare_cmp(struct ambarella_nand_info *nand_info)
{
	u32 i;
	u8 *bsp;

	bsp = nand_info->dmabuf + nand_info->mtd.writesize;
	if (nand_info->plat_nand->sets->ecc_bits > 0x1) {
		for (i = 0; i < nand_info->mtd.oobsize; i++) {
				if (bsp[i] != 0xff)
					break;
		}
		if (i == nand_info->mtd.oobsize)
			return 0;
		}
	return -1;
}

static void amb_nand_set_timing(struct ambarella_nand_info *nand_info,
	struct ambarella_nand_timing *timing);

static void ambnand_calc_timing(struct ambarella_nand_info *nand_info)
{
	struct ambarella_nand_timing *nand_time_para;
	u32 timing_para;
	u8 tcls, tals, tcs, tds;
	u8 tclh, talh, tch, tdh;
	u8 twp, twh, twb, trr;
	u8 trp, treh, trb, tceh;
	u8 trdelay, tclr, twhr, tir;
	u8 tww, trhz, tar;
	u32 clk;

	clk =  nand_info->plat_nand->get_pll();
	nand_time_para = nand_info->plat_nand->timing;
	/* timing 0 */
	timing_para = nand_time_para->timing0;
	tcls = NAND_TIMING_RSHIFT24BIT(timing_para);
	tals = NAND_TIMING_RSHIFT16BIT(timing_para);
	tcs = NAND_TIMING_RSHIFT8BIT(timing_para);
	tds = NAND_TIMING_RSHIFT0BIT(timing_para);

	tcls = nand_timing_calc(clk, 0, tcls);
	tals = nand_timing_calc(clk, 0, tals);
	tcs = nand_timing_calc(clk, 0, tcs);
	tds = nand_timing_calc(clk, 0, tds);

	nand_info->current_timing.timing0 = NAND_TIMING_LSHIFT24BIT(tcls) |
			NAND_TIMING_LSHIFT16BIT(tals) |
			NAND_TIMING_LSHIFT8BIT(tcs) |
			NAND_TIMING_LSHIFT0BIT(tds);

	/* timing 1 */
	timing_para = nand_time_para->timing1;
	tclh = NAND_TIMING_RSHIFT24BIT(timing_para);
	talh = NAND_TIMING_RSHIFT16BIT(timing_para);
	tch = NAND_TIMING_RSHIFT8BIT(timing_para);
	tdh = NAND_TIMING_RSHIFT0BIT(timing_para);

	tclh = nand_timing_calc(clk, 0, tclh);
	talh = nand_timing_calc(clk, 0, talh);
	tch = nand_timing_calc(clk, 0, tch);
	tdh = nand_timing_calc(clk, 0, tdh);

	nand_info->current_timing.timing1 = NAND_TIMING_LSHIFT24BIT(tclh) |
			NAND_TIMING_LSHIFT16BIT(talh) |
			NAND_TIMING_LSHIFT8BIT(tch) |
			NAND_TIMING_LSHIFT0BIT(tdh);

	/* timing 2 */
	timing_para = nand_time_para->timing2;
	twp = NAND_TIMING_RSHIFT24BIT(timing_para);
	twh = NAND_TIMING_RSHIFT16BIT(timing_para);
	twb = NAND_TIMING_RSHIFT8BIT(timing_para);
	trr = NAND_TIMING_RSHIFT0BIT(timing_para);

	twp = nand_timing_calc(clk, 0, twp);
	twh = nand_timing_calc(clk, 0, twh);
	twb = nand_timing_calc(clk, 1, twb);
	trr = nand_timing_calc(clk, 0, trr);

	nand_info->current_timing.timing2 = NAND_TIMING_LSHIFT24BIT(twp) |
			NAND_TIMING_LSHIFT16BIT(twh) |
			NAND_TIMING_LSHIFT8BIT(twb) |
			NAND_TIMING_LSHIFT0BIT(trr);

	/* timing 3 */
	timing_para = nand_time_para->timing3;
	trp = NAND_TIMING_RSHIFT24BIT(timing_para);
	treh = NAND_TIMING_RSHIFT16BIT(timing_para);
	trb = NAND_TIMING_RSHIFT8BIT(timing_para);
	tceh = NAND_TIMING_RSHIFT0BIT(timing_para);

	trp = nand_timing_calc(clk, 0, trp);
	treh = nand_timing_calc(clk, 0, treh);
	trb = nand_timing_calc(clk, 1, trb);
	tceh = nand_timing_calc(clk, 1, tceh);

	nand_info->current_timing.timing3 = NAND_TIMING_LSHIFT24BIT(trp) |
			NAND_TIMING_LSHIFT16BIT(treh) |
			NAND_TIMING_LSHIFT8BIT(trb) |
			NAND_TIMING_LSHIFT0BIT(tceh);

	/* timing 4 */
	timing_para = nand_time_para->timing4;
	trdelay = NAND_TIMING_RSHIFT24BIT(timing_para);
	tclr = NAND_TIMING_RSHIFT16BIT(timing_para);
	twhr = NAND_TIMING_RSHIFT8BIT(timing_para);
	tir = NAND_TIMING_RSHIFT0BIT(timing_para);

	trdelay = nand_timing_calc(clk, 1, trdelay);
	tclr = nand_timing_calc(clk, 0, tclr);
	twhr = nand_timing_calc(clk, 0, twhr);
	tir = nand_timing_calc(clk, 0, tir);

	nand_info->current_timing.timing4 = NAND_TIMING_LSHIFT24BIT(trdelay) |
			NAND_TIMING_LSHIFT16BIT(tclr) |
			NAND_TIMING_LSHIFT8BIT(twhr) |
			NAND_TIMING_LSHIFT0BIT(tir);

	/* timing 5 */
	timing_para = nand_time_para->timing5;
	tww = NAND_TIMING_RSHIFT16BIT(timing_para);
	trhz = NAND_TIMING_RSHIFT8BIT(timing_para);
	tar = NAND_TIMING_RSHIFT0BIT(timing_para);

	tww = nand_timing_calc(clk, 0, tww);
	trhz = nand_timing_calc(clk, 1, trhz);
	tar = nand_timing_calc(clk, 0, tar);


	nand_info->current_timing.timing5 = NAND_TIMING_LSHIFT16BIT(tww) |
			NAND_TIMING_LSHIFT8BIT(trhz) |
			NAND_TIMING_LSHIFT0BIT(tar);
}

static int pre_notified = 0;

static int ambarella_nand_system_event(struct notifier_block *nb,
	unsigned long val, void *data)
{
	int					errorCode = NOTIFY_OK;
	struct ambarella_nand_info		*nand_info;

	nand_info = container_of(nb, struct ambarella_nand_info, system_event);

	switch (val) {
	case AMBA_EVENT_PRE_CPUFREQ:
		pr_debug("%s: Pre Change\n", __func__);
		if (!nand_info->suspend) {
			down(&nand_info->system_event_sem);
			pre_notified = 1;
		}
		break;

	case AMBA_EVENT_POST_CPUFREQ:
		pr_debug("%s: Post Change\n", __func__);
		/* NAND timing is re-calculated by ThreadX.
		 * Linux does not need to calculate it again */
#if !defined(CONFIG_AMBALINK_LOCK)
		/* The timming register is default value,
		 * it's big enough to operate
		 * with NAND, so no need to change it. */
		if (nand_info->origin_timing->control != 0) {
			ambnand_calc_timing(nand_info);
			amb_nand_set_timing(nand_info, &nand_info->current_timing);

			pr_debug("new reg:\t0x%08x 0x%08x"
				" 0x%08x 0x%08x 0x%08x 0x%08x\n",
				nand_info->current_timing.timing0,
				nand_info->current_timing.timing1,
				nand_info->current_timing.timing2,
				nand_info->current_timing.timing3,
				nand_info->current_timing.timing4,
				nand_info->current_timing.timing5);
		}
#endif
		if (pre_notified) {
			up(&nand_info->system_event_sem);
			pre_notified = 0;
		}
		break;

	default:
		break;
	}

	return errorCode;
}

static irqreturn_t nand_fiocmd_isr_handler(int irq, void *dev_id)
{
	irqreturn_t				rval = IRQ_NONE;
	struct ambarella_nand_info		*nand_info;
	u32					val;

	nand_info = (struct ambarella_nand_info *)dev_id;

	val = amba_readl(nand_info->regbase + FIO_STA_OFFSET);

	if (val & FIO_STA_FI) {
		amba_clrbitsl(nand_info->regbase + FIO_CTR_OFFSET,
			(FIO_CTR_RS | FIO_CTR_SE | FIO_CTR_CO));

		amba_writel(nand_info->regbase + FLASH_INT_OFFSET, 0x0);

		atomic_clear_mask(0x1, (unsigned long *)&nand_info->irq_flag);
		wake_up(&nand_info->wq);

		rval = IRQ_HANDLED;
	}

	return rval;
}

/* this dma is used to transfer data between Nand and FIO FIFO. */
static irqreturn_t nand_fiodma_isr_handler(int irq, void *dev_id)
{
	struct ambarella_nand_info		*nand_info;
	u32					val, fio_dma_sta;

	nand_info = (struct ambarella_nand_info *)dev_id;

	val = amba_readl(nand_info->regbase + FIO_DMACTR_OFFSET);

	if ((val & (FIO_DMACTR_SD | FIO_DMACTR_CF |
		FIO_DMACTR_XD | FIO_DMACTR_FL)) ==  FIO_DMACTR_FL) {
		fio_dma_sta = amba_readl(nand_info->regbase + FIO_DMASTA_OFFSET);
		/* dummy IRQ by S2 chip */
		if (fio_dma_sta == 0x0)
			return IRQ_HANDLED;

		nand_info->fio_dma_sta = fio_dma_sta;

		amba_writel(nand_info->regbase + FIO_DMASTA_OFFSET, 0x0);

		if (nand_amb_is_hw_bch(nand_info)) {
			nand_info->fio_ecc_sta =
				amba_readl(nand_info->regbase + FIO_ECC_RPT_STA_OFFSET);
			amba_writel(nand_info->regbase + FIO_ECC_RPT_STA_OFFSET, 0x0);
		}
		atomic_clear_mask(0x2, (unsigned long *)&nand_info->irq_flag);
		wake_up(&nand_info->wq);
	}

	return IRQ_HANDLED;
}

/* this dma is used to transfer data between FIO FIFO and Memory. */
static irqreturn_t ambarella_fdma_isr_handler(int irq, void *dev_id)
{
	irqreturn_t				rval = IRQ_NONE;
	struct ambarella_nand_info		*nand_info;
	u32					int_src;

	nand_info = (struct ambarella_nand_info *)dev_id;

#if defined(CONFIG_AMBALINK_LOCK)
	if (nand_info->cmd == 0)
		return IRQ_HANDLED;
#endif

	int_src = amba_readl(nand_info->fdmaregbase + FDMA_INT_OFFSET);

	if (int_src & (1 << FIO_DMA_CHAN)) {
		nand_info->dma_status =
			amba_readl(nand_info->fdmaregbase + FDMA_STA_OFFSET);
		amba_writel(nand_info->fdmaregbase + FDMA_STA_OFFSET, 0);
		amba_writel(nand_info->fdmaregbase + FDMA_SPR_STA_OFFSET, 0);

		atomic_clear_mask(0x4, (unsigned long *)&nand_info->irq_flag);
		wake_up(&nand_info->wq);

		rval = IRQ_HANDLED;
	}

	return rval;
}

static void nand_amb_setup_dma_devmem(struct ambarella_nand_info *nand_info)
{
	u32					ctrl_val;
	u32					size = 0;

	/* init and enable fdma to transfer data betwee FIFO and Memory */
	if (nand_info->len > 16)
		ctrl_val = DMA_CHANX_CTR_WM | DMA_CHANX_CTR_NI | DMA_NODC_MN_BURST_SIZE;
	else
		ctrl_val = DMA_CHANX_CTR_WM | DMA_CHANX_CTR_NI | DMA_NODC_SP_BURST_SIZE;

	ctrl_val |= nand_info->len | DMA_CHANX_CTR_EN;
	ctrl_val &= ~DMA_CHANX_CTR_D;

	/* Setup main external DMA engine transfer */
	amba_writel(nand_info->fdmaregbase + FDMA_STA_OFFSET, 0);

	amba_writel(nand_info->fdmaregbase + FDMA_SRC_OFFSET, nand_info->dmabase);
	amba_writel(nand_info->fdmaregbase + FDMA_DST_OFFSET, nand_info->buf_phys);

	if (nand_info->plat_nand->sets->ecc_bits > 1) {
		/* Setup spare external DMA engine transfer */
		amba_writel(nand_info->fdmaregbase + FDMA_SPR_STA_OFFSET, 0x0);
		amba_writel(nand_info->fdmaregbase + FDMA_SPR_SRC_OFFSET, nand_info->dmabase);
		amba_writel(nand_info->fdmaregbase + FDMA_SPR_DST_OFFSET, nand_info->spare_buf_phys);
		amba_writel(nand_info->fdmaregbase + FDMA_SPR_CNT_OFFSET, nand_info->slen);
	}

	amba_writel(nand_info->fdmaregbase + FDMA_CTR_OFFSET, ctrl_val);

	/* init and enable fio-dma to transfer data between Nand and FIFO */
	amba_writel(nand_info->regbase + FIO_DMAADR_OFFSET, nand_info->addr);

	size = nand_info->len + nand_info->slen;
	if (size > 16) {
		ctrl_val = FIO_DMACTR_EN |
			FIO_DMACTR_FL |
			FIO_MN_BURST_SIZE |
			size;
	} else {
		ctrl_val = FIO_DMACTR_EN |
			FIO_DMACTR_FL |
			FIO_SP_BURST_SIZE |
			size;
	}
	amba_writel(nand_info->regbase + FIO_DMACTR_OFFSET, ctrl_val);
}

static void nand_amb_setup_dma_memdev(struct ambarella_nand_info *nand_info)
{
	u32					ctrl_val, dma_burst_val, fio_burst_val;
	u32					size = 0;

	if (nand_info->plat_nand->sets->ecc_bits > 1) {
		dma_burst_val = DMA_NODC_MN_BURST_SIZE8;
		fio_burst_val = FIO_MN_BURST_SIZE8;
	} else {
		dma_burst_val = DMA_NODC_MN_BURST_SIZE;
		fio_burst_val = FIO_MN_BURST_SIZE;
	}

	/* init and enable fdma to transfer data betwee FIFO and Memory */
	if (nand_info->len > 16)
		ctrl_val = DMA_CHANX_CTR_RM | DMA_CHANX_CTR_NI | dma_burst_val;
	else
		ctrl_val = DMA_CHANX_CTR_RM | DMA_CHANX_CTR_NI | DMA_NODC_SP_BURST_SIZE;

	ctrl_val |= nand_info->len | DMA_CHANX_CTR_EN;
	ctrl_val &= ~DMA_CHANX_CTR_D;

	/* Setup main external DMA engine transfer */
	amba_writel(nand_info->fdmaregbase + FDMA_STA_OFFSET, 0);

	amba_writel(nand_info->fdmaregbase + FDMA_SRC_OFFSET, nand_info->buf_phys);
	amba_writel(nand_info->fdmaregbase + FDMA_DST_OFFSET, nand_info->dmabase);

	if (nand_info->plat_nand->sets->ecc_bits > 1) {
		/* Setup spare external DMA engine transfer */
		amba_writel(nand_info->fdmaregbase + FDMA_SPR_STA_OFFSET, 0x0);
		amba_writel(nand_info->fdmaregbase + FDMA_SPR_SRC_OFFSET, nand_info->spare_buf_phys);
		amba_writel(nand_info->fdmaregbase + FDMA_SPR_DST_OFFSET, nand_info->dmabase);
		amba_writel(nand_info->fdmaregbase + FDMA_SPR_CNT_OFFSET, nand_info->slen);
	}

	amba_writel(nand_info->fdmaregbase + FDMA_CTR_OFFSET, ctrl_val);

	/* init and enable fio-dma to transfer data between Nand and FIFO */
	amba_writel(nand_info->regbase + FIO_DMAADR_OFFSET, nand_info->addr);

	size = nand_info->len + nand_info->slen;
	if (size > 16) {
		ctrl_val = FIO_DMACTR_EN | FIO_DMACTR_FL | fio_burst_val |
			FIO_DMACTR_RM | size;
	} else {
		ctrl_val = FIO_DMACTR_EN | FIO_DMACTR_FL | FIO_SP_BURST_SIZE |
			FIO_DMACTR_RM | size;
	}
	amba_writel(nand_info->regbase + FIO_DMACTR_OFFSET, ctrl_val);
}

static int nand_amb_request(struct ambarella_nand_info *nand_info)
{
	int					errorCode = 0;
	u32					cmd;
	u32					nand_ctr_reg = 0;
	u32					nand_cmd_reg = 0;
	u32					fio_ctr_reg = 0;
	long					timeout;

	if (unlikely(nand_info->suspend == 1)) {
		dev_err(nand_info->dev, "%s: suspend!\n", __func__);
		errorCode = -EPERM;
		goto nand_amb_request_exit;
	}

	cmd = nand_info->cmd;

	nand_ctr_reg = nand_info->control_reg | NAND_CTR_WAS;

	down(&nand_info->system_event_sem);

	nand_info->plat_nand->request();

#if defined(CONFIG_AMBALINK_LOCK)
#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
	boss_set_irq_owner(nand_info->cmd_irq, BOSS_IRQ_OWNER_LINUX, 1);
	boss_set_irq_owner(nand_info->dma_irq, BOSS_IRQ_OWNER_LINUX, 1);
	boss_set_irq_owner(nand_info->fdma_irq, BOSS_IRQ_OWNER_LINUX, 1);
#endif
	enable_irq(nand_info->cmd_irq);
	enable_irq(nand_info->dma_irq);
	enable_irq(nand_info->fdma_irq);
	/* Write current command to the magic address to notify ThreadX. */
	amba_writel(ambalink_nand_last_cmd_addr, cmd);
#endif

#ifdef AMBARELLA_NAND_WP
	if ((cmd == NAND_AMB_CMD_ERASE ||
		cmd == NAND_AMB_CMD_COPYBACK ||
		cmd == NAND_AMB_CMD_PROGRAM ||
		cmd == NAND_AMB_CMD_READSTATUS) &&
		nand_info->wp_gpio >= 0)
		gpio_direction_output(nand_info->wp_gpio, GPIO_HIGH);
#endif

	switch (cmd) {
	case NAND_AMB_CMD_RESET:
		nand_cmd_reg = NAND_AMB_CMD_RESET;
		amba_writel(nand_info->regbase + FLASH_CMD_OFFSET,
			nand_cmd_reg);
		break;

	case NAND_AMB_CMD_READID:
		nand_ctr_reg |= NAND_CTR_A(nand_info->addr_hi);
		nand_cmd_reg = nand_info->addr | NAND_AMB_CMD_READID;

		if (nand_info->plat_nand->id_cycles == 5) {
			u32	nand_ext_ctr_reg = 0;

			nand_ext_ctr_reg = amba_readl(nand_info->regbase + FLASH_EX_CTR_OFFSET);
			nand_ext_ctr_reg |= NAND_EXT_CTR_I5;
			nand_ctr_reg &= ~(NAND_CTR_I4);
			amba_writel(nand_info->regbase + FLASH_EX_CTR_OFFSET,
				nand_ext_ctr_reg);
		}

		amba_writel(nand_info->regbase + FLASH_CTR_OFFSET,
			nand_ctr_reg);
		amba_writel(nand_info->regbase + FLASH_CMD_OFFSET,
			nand_cmd_reg);
		break;

	case NAND_AMB_CMD_READSTATUS:
		nand_ctr_reg |= NAND_CTR_A(nand_info->addr_hi);
		nand_cmd_reg = nand_info->addr | NAND_AMB_CMD_READSTATUS;
		amba_writel(nand_info->regbase + FLASH_CTR_OFFSET,
			nand_ctr_reg);
		amba_writel(nand_info->regbase + FLASH_CMD_OFFSET,
			nand_cmd_reg);
		break;

	case NAND_AMB_CMD_ERASE:
		/* Fix dual space mode bug */
		if (nand_info->plat_nand->sets->ecc_bits > 1)
			amba_writel(nand_info->regbase + FIO_DMAADR_OFFSET, nand_info->addr);

		nand_ctr_reg |= NAND_CTR_A(nand_info->addr_hi);
		nand_cmd_reg = nand_info->addr | NAND_AMB_CMD_ERASE;
		amba_writel(nand_info->regbase + FLASH_CTR_OFFSET,
			nand_ctr_reg);
		amba_writel(nand_info->regbase + FLASH_CMD_OFFSET,
			nand_cmd_reg);
		break;

	case NAND_AMB_CMD_COPYBACK:
		nand_ctr_reg |= NAND_CTR_A(nand_info->addr_hi);
		nand_ctr_reg |= NAND_CTR_CE;
		nand_cmd_reg = nand_info->addr | NAND_AMB_CMD_COPYBACK;
		amba_writel(nand_info->regbase + FLASH_CFI_OFFSET,
			nand_info->dst);
		amba_writel(nand_info->regbase + FLASH_CTR_OFFSET,
			nand_ctr_reg);
		amba_writel(nand_info->regbase + FLASH_CMD_OFFSET,
			nand_cmd_reg);
		break;

	case NAND_AMB_CMD_READ:
		nand_ctr_reg |= NAND_CTR_A(nand_info->addr_hi);

		if (nand_amb_is_hw_bch(nand_info)) {
			/* Setup FIO DMA Control Register */
			nand_amb_enable_bch(nand_info);
			/* in dual space mode,enable the SE bit */
			nand_ctr_reg |= NAND_CTR_SE;

			/* Clean Flash_IO_ecc_rpt_status Register */
			amba_writel(nand_info->regbase + FIO_ECC_RPT_STA_OFFSET, 0x0);
		} else if (nand_amb_is_sw_bch(nand_info)) {
			/* Setup FIO DMA Control Register */
			nand_amb_enable_dsm(nand_info);
			/* in dual space mode,enable the SE bit */
			nand_ctr_reg |= NAND_CTR_SE;
		} else {
			if (nand_info->area == MAIN_ECC)
				nand_ctr_reg |= (NAND_CTR_SE);
			else if (nand_info->area == SPARE_ONLY ||
				nand_info->area == SPARE_ECC)
				nand_ctr_reg |= (NAND_CTR_SE | NAND_CTR_SA);

			fio_ctr_reg = amba_readl(nand_info->regbase + FIO_CTR_OFFSET);

			if (nand_info->area == SPARE_ONLY ||
				nand_info->area == SPARE_ECC  ||
				nand_info->area == MAIN_ECC)
				fio_ctr_reg |= (FIO_CTR_RS);

			switch (nand_info->ecc) {
			case EC_MDSE:
				nand_ctr_reg |= NAND_CTR_EC_SPARE;
				fio_ctr_reg |= FIO_CTR_CO;
				break;
			case EC_MESD:
				nand_ctr_reg |= NAND_CTR_EC_MAIN;
				fio_ctr_reg |= FIO_CTR_CO;
				break;
			case EC_MESE:
				nand_ctr_reg |=	(NAND_CTR_EC_MAIN | NAND_CTR_EC_SPARE);
				fio_ctr_reg |= FIO_CTR_CO;
				break;
			case EC_MDSD:
			default:
				break;
			}

			amba_writel(nand_info->regbase + FIO_CTR_OFFSET,
						fio_ctr_reg);
		}

		amba_writel(nand_info->regbase + FLASH_CTR_OFFSET, nand_ctr_reg);
		nand_amb_setup_dma_devmem(nand_info);

		break;

	case NAND_AMB_CMD_PROGRAM:
		nand_ctr_reg |= NAND_CTR_A(nand_info->addr_hi);

		if (nand_amb_is_hw_bch(nand_info)) {
			/* Setup FIO DMA Control Register */
			nand_amb_enable_bch(nand_info);
			/* in dual space mode,enable the SE bit */
			nand_ctr_reg |= NAND_CTR_SE;

			/* Clean Flash_IO_ecc_rpt_status Register */
			amba_writel(nand_info->regbase + FIO_ECC_RPT_STA_OFFSET, 0x0);
		} else if (nand_amb_is_sw_bch(nand_info)) {
			/* Setup FIO DMA Control Register */
			nand_amb_enable_dsm(nand_info);
			/* in dual space mode,enable the SE bit */
			nand_ctr_reg |= NAND_CTR_SE;
		} else {
			if (nand_info->area == MAIN_ECC)
				nand_ctr_reg |= (NAND_CTR_SE);
			else if (nand_info->area == SPARE_ONLY ||
				nand_info->area == SPARE_ECC)
				nand_ctr_reg |= (NAND_CTR_SE | NAND_CTR_SA);

			fio_ctr_reg = amba_readl(nand_info->regbase + FIO_CTR_OFFSET);

			if (nand_info->area == SPARE_ONLY ||
				nand_info->area == SPARE_ECC  ||
				nand_info->area == MAIN_ECC)
				fio_ctr_reg |= (FIO_CTR_RS);

			switch (nand_info->ecc) {
			case EG_MDSE :
				nand_ctr_reg |= NAND_CTR_EG_SPARE;
				break;
			case EG_MESD :
				nand_ctr_reg |= NAND_CTR_EG_MAIN;
				break;
			case EG_MESE :
				nand_ctr_reg |= (NAND_CTR_EG_MAIN | NAND_CTR_EG_SPARE);
				break;
			case EG_MDSD:
			default:
				break;
			}

			amba_writel(nand_info->regbase + FIO_CTR_OFFSET,
				fio_ctr_reg);
		}
		amba_writel(nand_info->regbase + FLASH_CTR_OFFSET, nand_ctr_reg);
		nand_amb_setup_dma_memdev(nand_info);

		break;

	default:
		dev_warn(nand_info->dev,
			"%s: wrong command %d!\n", __func__, cmd);
		errorCode = -EINVAL;
		goto nand_amb_request_done;
		break;
	}

	if (cmd == NAND_AMB_CMD_READ || cmd == NAND_AMB_CMD_PROGRAM) {
		timeout = wait_event_timeout(nand_info->wq,
			atomic_read(&nand_info->irq_flag) == 0x0, 1 * HZ);
		if (timeout <= 0) {
			errorCode = -EBUSY;
			dev_err(nand_info->dev, "%s: cmd=0x%x timeout 0x%08x\n",
				__func__, cmd, atomic_read(&nand_info->irq_flag));
		} else {
			dev_dbg(nand_info->dev, "%ld jiffies left.\n", timeout);
		}

		if (nand_info->dma_status & (DMA_CHANX_STA_OE | DMA_CHANX_STA_ME |
			DMA_CHANX_STA_BE | DMA_CHANX_STA_RWE |
			DMA_CHANX_STA_AE)) {
			dev_err(nand_info->dev,
				"%s: Errors happend in DMA transaction %d!\n",
				__func__, nand_info->dma_status);
			errorCode = -EIO;
			goto nand_amb_request_done;
		}

		if (nand_amb_is_hw_bch(nand_info)) {
			if (cmd == NAND_AMB_CMD_READ) {
				if (nand_info->fio_ecc_sta & FIO_ECC_RPT_FAIL) {
					int ret = 0;
					/* Workaround for page never used, BCH will be failed */
					if (nand_info->area == MAIN_ECC || nand_info->area == SPARE_ECC)
						ret = nand_bch_spare_cmp(nand_info);

					if (ret < 0) {
						nand_info->mtd.ecc_stats.failed++;
						dev_err(nand_info->dev,
							"BCH corrected failed (0x%08x), addr is 0x[%x]!\n",
							nand_info->fio_ecc_sta, nand_info->addr);
					}
				} else if (nand_info->fio_ecc_sta & FIO_ECC_RPT_ERR) {
					nand_info->mtd.ecc_stats.corrected++;
					/* once bitflip and data corrected happened, BCH will keep on
					 * to report bitflip in following read operations, even though
					 * there is no bitflip happened really. So this is a workaround
					 * to get it back. */
					nand_amb_corrected_recovery(nand_info);
				}
			} else if (cmd == NAND_AMB_CMD_PROGRAM) {
				if (nand_info->fio_ecc_sta & FIO_ECC_RPT_FAIL) {
					dev_err(nand_info->dev,
						"BCH program program failed (0x%08x)!\n",
						nand_info->fio_ecc_sta);
				}
			}
		}
		errorCode = nand_info->plat_nand->parse_error(
			nand_info->fio_dma_sta);
		if (errorCode) {
			u32 block_addr;
			block_addr = nand_info->addr /
					nand_info->mtd.erasesize *
					nand_info->mtd.erasesize;
			dev_err(nand_info->dev,
				"%s: cmd=0x%x, addr_hi=0x%x, "
				"addr=0x%x, dst=0x%x, buf=0x%x, "
				"len=0x%x, area=0x%x, ecc=0x%x, "
				"block addr=0x%x!\n",
				__func__, cmd,
				nand_info->addr_hi,
				nand_info->addr,
				nand_info->dst,
				nand_info->buf_phys,
				nand_info->len,
				nand_info->area,
				nand_info->ecc,
				block_addr);
			goto nand_amb_request_done;
		}
	} else {
		/* just wait cmd irq, no care about both DMA irqs */
		timeout = wait_event_timeout(nand_info->wq,
			(atomic_read(&nand_info->irq_flag) & 0x1) == 0x0, 1 * HZ);
		if (timeout <= 0) {
			errorCode = -EBUSY;
			dev_err(nand_info->dev, "%s: cmd=0x%x timeout 0x%08x\n",
				__func__, cmd, atomic_read(&nand_info->irq_flag));
			goto nand_amb_request_done;
		} else {
			dev_dbg(nand_info->dev, "%ld jiffies left.\n", timeout);

			if (cmd == NAND_AMB_CMD_READID) {
				u32 id = amba_readl(nand_info->regbase +
					FLASH_ID_OFFSET);
				if (nand_info->plat_nand->id_cycles == 5) {
					u32 id_5 = amba_readl(nand_info->regbase +
					FLASH_EX_ID_OFFSET);
					nand_info->dmabuf[4] = (unsigned char) (id_5 & 0xFF);
				}
				nand_info->dmabuf[0] = (unsigned char) (id >> 24);
				nand_info->dmabuf[1] = (unsigned char) (id >> 16);
				nand_info->dmabuf[2] = (unsigned char) (id >> 8);
				nand_info->dmabuf[3] = (unsigned char) id;
			} else if (cmd == NAND_AMB_CMD_READSTATUS) {
				*nand_info->dmabuf = amba_readl(nand_info->regbase +
					FLASH_STA_OFFSET);
			}
		}
	}

nand_amb_request_done:
	atomic_set(&nand_info->irq_flag, 0x7);
	nand_info->dma_status = 0;
	/* Avoid to flush previous error info */
	if (nand_info->err_code == 0)
		nand_info->err_code = errorCode;

#ifdef AMBARELLA_NAND_WP
	if ((cmd == NAND_AMB_CMD_ERASE ||
		cmd == NAND_AMB_CMD_COPYBACK ||
		cmd == NAND_AMB_CMD_PROGRAM ||
		cmd == NAND_AMB_CMD_READSTATUS) &&
		nand_info->wp_gpio >= 0)
		gpio_direction_output(nand_info->wp_gpio, GPIO_LOW);
#endif

	if ((cmd == NAND_AMB_CMD_READ || cmd == NAND_AMB_CMD_PROGRAM)
		&& nand_amb_is_hw_bch(nand_info))
		nand_amb_disable_bch(nand_info);

#if defined(CONFIG_AMBALINK_LOCK)
	nand_info->cmd = 0;
	disable_irq(nand_info->cmd_irq);
	disable_irq(nand_info->dma_irq);
	disable_irq(nand_info->fdma_irq);
#endif

	nand_info->plat_nand->release();

	up(&nand_info->system_event_sem);

nand_amb_request_exit:
	return errorCode;
}

int nand_amb_reset(struct ambarella_nand_info *nand_info)
{
	nand_info->cmd = NAND_AMB_CMD_RESET;

	return nand_amb_request(nand_info);
}

int nand_amb_read_id(struct ambarella_nand_info *nand_info)
{
	nand_info->cmd = NAND_AMB_CMD_READID;
	nand_info->addr_hi = 0;
	nand_info->addr = 0;

	return nand_amb_request(nand_info);
}

int nand_amb_read_status(struct ambarella_nand_info *nand_info)
{
	nand_info->cmd = NAND_AMB_CMD_READSTATUS;
	nand_info->addr_hi = 0;
	nand_info->addr = 0;

	return nand_amb_request(nand_info);
}

int nand_amb_erase(struct ambarella_nand_info *nand_info, u32 page_addr)
{
	int					errorCode = 0;
	u32					addr_hi;
	u32					addr;
	u64					addr64;

	addr64 = (u64)(page_addr * nand_info->mtd.writesize);
	addr_hi = (u32)(addr64 >> 32);
	addr = (u32)addr64;

	nand_info->cmd = NAND_AMB_CMD_ERASE;
	nand_info->addr_hi = addr_hi;
	nand_info->addr = addr;

	errorCode = nand_amb_request(nand_info);

	return errorCode;
}

int nand_amb_read_data(struct ambarella_nand_info *nand_info,
	u32 page_addr, dma_addr_t buf_dma, u8 area)
{
	int					errorCode = 0;
	u32					addr_hi;
	u32					addr;
	u32					len;
	u64					addr64;
	u8					ecc = 0;

	addr64 = (u64)(page_addr * nand_info->mtd.writesize);
	addr_hi = (u32)(addr64 >> 32);
	addr = (u32)addr64;

	switch (area) {
	case MAIN_ONLY:
		ecc = EC_MDSD;
		len = nand_info->mtd.writesize;
		break;
	case MAIN_ECC:
		ecc = EC_MESD;
		len = nand_info->mtd.writesize;
		break;
	case SPARE_ONLY:
		ecc = EC_MDSD;
		len = nand_info->mtd.oobsize;
		break;
	case SPARE_ECC:
		ecc = EC_MDSE;
		len = nand_info->mtd.oobsize;
		break;
	default:
		dev_err(nand_info->dev, "%s: Wrong area.\n", __func__);
		errorCode = -EINVAL;
		goto nand_amb_read_page_exit;
		break;
	}

	nand_info->slen = 0;
	if(nand_info->plat_nand->sets->ecc_bits > 1) {
		/* when use BCH, the EG and EC should be 0 */
		ecc = 0;
		len = nand_info->mtd.writesize;
		nand_info->slen = nand_info->mtd.oobsize;
		nand_info->spare_buf_phys = buf_dma + len;
	}

	nand_info->cmd = NAND_AMB_CMD_READ;
	nand_info->addr_hi = addr_hi;
	nand_info->addr = addr;
	nand_info->buf_phys = buf_dma;
	nand_info->len = len;
	nand_info->area = area;
	nand_info->ecc = ecc;

	errorCode = nand_amb_request(nand_info);

nand_amb_read_page_exit:
	return errorCode;
}

int nand_amb_write_data(struct ambarella_nand_info *nand_info,
	u32 page_addr, dma_addr_t buf_dma, u8 area)
{
	int					errorCode = 0;
	u32					addr_hi;
	u32					addr;
	u32					len;
	u64					addr64;
	u8					ecc;

	addr64 = (u64)(page_addr * nand_info->mtd.writesize);
	addr_hi = (u32)(addr64 >> 32);
	addr = (u32)addr64;

	switch (area) {
	case MAIN_ONLY:
		ecc = EG_MDSD;
		len = nand_info->mtd.writesize;
		break;
	case MAIN_ECC:
		ecc = EG_MESD;
		len = nand_info->mtd.writesize;
		break;
	case SPARE_ONLY:
		ecc = EG_MDSD;
		len = nand_info->mtd.oobsize;
		break;
	case SPARE_ECC:
		ecc = EG_MDSE;
		len = nand_info->mtd.oobsize;
		break;
	default:
		dev_err(nand_info->dev, "%s: Wrong area.\n", __func__);
		errorCode = -EINVAL;
		goto nand_amb_write_page_exit;
		break;
	}

	nand_info->slen = 0;
	if(nand_info->plat_nand->sets->ecc_bits > 1) {
		/* when use BCH, the EG and EC should be 0 */
		ecc = 0;
		len = nand_info->mtd.writesize;
		nand_info->slen = nand_info->mtd.oobsize;
		nand_info->spare_buf_phys = buf_dma + len;
	}
	nand_info->cmd = NAND_AMB_CMD_PROGRAM;
	nand_info->addr_hi = addr_hi;
	nand_info->addr = addr;
	nand_info->buf_phys = buf_dma;
	nand_info->len = len;
	nand_info->area = area;
	nand_info->ecc = ecc;

	errorCode = nand_amb_request(nand_info);

nand_amb_write_page_exit:
	return errorCode;
}


/* ==========================================================================*/
static uint8_t amb_nand_read_byte(struct mtd_info *mtd)
{
	struct ambarella_nand_info		*nand_info;
	uint8_t					*data;

	nand_info = (struct ambarella_nand_info *)mtd->priv;

	data = nand_info->dmabuf + nand_info->dma_bufpos;
	nand_info->dma_bufpos++;

	return *data;
}

static u16 amb_nand_read_word(struct mtd_info *mtd)
{
	struct ambarella_nand_info		*nand_info;
	u16					*data;

	nand_info = (struct ambarella_nand_info *)mtd->priv;

	data = (u16 *)(nand_info->dmabuf + nand_info->dma_bufpos);
	nand_info->dma_bufpos += 2;

	return *data;
}

static void amb_nand_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct ambarella_nand_info		*nand_info;

	nand_info = (struct ambarella_nand_info *)mtd->priv;

	if ((nand_info->dma_bufpos >= AMBARELLA_NAND_DMA_BUFFER_SIZE) ||
		((nand_info->dma_bufpos + len) > AMBARELLA_NAND_DMA_BUFFER_SIZE))
		BUG();

	memcpy(buf, nand_info->dmabuf + nand_info->dma_bufpos, len);
	nand_info->dma_bufpos += len;
}

static void amb_nand_write_buf(struct mtd_info *mtd,
	const uint8_t *buf, int len)
{
	struct ambarella_nand_info		*nand_info;

	nand_info = (struct ambarella_nand_info *)mtd->priv;

	if ((nand_info->dma_bufpos >= AMBARELLA_NAND_DMA_BUFFER_SIZE) ||
		((nand_info->dma_bufpos + len) > AMBARELLA_NAND_DMA_BUFFER_SIZE))
		BUG();

	memcpy(nand_info->dmabuf + nand_info->dma_bufpos, buf, len);
	nand_info->dma_bufpos += len;
}

static void amb_nand_select_chip(struct mtd_info *mtd, int chip)
{
	struct ambarella_nand_info		*nand_info;

	nand_info = (struct ambarella_nand_info *)mtd->priv;

	if (chip > 0) {
		dev_err(nand_info->dev,
			"%s: Multi-Chip isn't supported yet.\n", __func__);
	}
}

static void amb_nand_cmd_ctrl(struct mtd_info *mtd, int dat, unsigned int ctrl)
{

}

static int amb_nand_dev_ready(struct mtd_info *mtd)
{
	struct nand_chip 			*chip = mtd->priv;

	chip->cmdfunc(mtd, NAND_CMD_STATUS, -1, -1);

	return (chip->read_byte(mtd) & NAND_STATUS_READY) ? 1 : 0;
}

static int amb_nand_waitfunc(struct mtd_info *mtd, struct nand_chip *chip)
{
	int					status = 0;
	struct ambarella_nand_info		*nand_info;

	nand_info = (struct ambarella_nand_info *)mtd->priv;

	/* ambarella nand controller has waited for the command completion,
	  * but still need to check the nand chip's status
	  */
	if (nand_info->err_code)
		status = NAND_STATUS_FAIL;
	else {
		chip->cmdfunc(mtd, NAND_CMD_STATUS, -1, -1);
		status = chip->read_byte(mtd);
	}

	return status;
}

static void amb_nand_cmdfunc(struct mtd_info *mtd, unsigned command,
	int column, int page_addr)
{
	struct ambarella_nand_info		*nand_info;

	nand_info = (struct ambarella_nand_info *)mtd->priv;
	nand_info->err_code = 0;

	switch(command) {
	case NAND_CMD_RESET:
		nand_amb_reset(nand_info);
		break;
	case NAND_CMD_READID:
		nand_info->dma_bufpos = 0;
		nand_amb_read_id(nand_info);
		break;
	case NAND_CMD_STATUS:
		nand_info->dma_bufpos = 0;
		nand_amb_read_status(nand_info);
		break;
	case NAND_CMD_ERASE1:
		nand_amb_erase(nand_info, page_addr);
		break;
	case NAND_CMD_ERASE2:
		break;
	case NAND_CMD_READOOB:
		nand_info->dma_bufpos = column;
		if (nand_info->plat_nand->sets->ecc_bits > 1) {
			u8 mn_area_ecc = nand_info->soft_ecc ? MAIN_ONLY : MAIN_ECC;
			nand_info->dma_bufpos = mtd->writesize;
			nand_amb_read_data(nand_info, page_addr,
					nand_info->dmaaddr, mn_area_ecc);
		} else {
			nand_amb_read_data(nand_info, page_addr,
					nand_info->dmaaddr, SPARE_ONLY);
		}
		break;
	case NAND_CMD_READ0:
		nand_info->dma_bufpos = column;
		{
			u8 area = nand_info->soft_ecc ? MAIN_ONLY : MAIN_ECC;
			nand_amb_read_data(nand_info, page_addr,
					nand_info->dmaaddr, area);
			if (nand_info->plat_nand->sets->ecc_bits == 1)
				nand_amb_read_data(nand_info, page_addr,
					nand_info->dmaaddr + mtd->writesize, SPARE_ONLY);
		}
		break;
	case NAND_CMD_SEQIN:
		nand_info->dma_bufpos = column;
		nand_info->seqin_column = column;
		nand_info->seqin_page_addr = page_addr;
		break;
	case NAND_CMD_PAGEPROG:
		{
			u8 mn_area, sp_area;
			u32 offset;
			mn_area = nand_info->soft_ecc ? MAIN_ONLY : MAIN_ECC;
			sp_area = nand_amb_is_hw_bch(nand_info) ? SPARE_ECC : SPARE_ONLY;
			offset = (nand_info->plat_nand->sets->ecc_bits > 1) ? 0 :
					mtd->writesize;

			if (nand_info->seqin_column < mtd->writesize) {
				nand_amb_write_data(nand_info,
					nand_info->seqin_page_addr,
					nand_info->dmaaddr, mn_area);
				if (nand_info->soft_ecc
					&&nand_info->plat_nand->sets->ecc_bits == 1) {
					nand_amb_write_data(nand_info,
						nand_info->seqin_page_addr,
						nand_info->dmaaddr + mtd->writesize,
						sp_area);
				}
			} else {
				nand_amb_write_data(nand_info,
					nand_info->seqin_page_addr,
					nand_info->dmaaddr + offset,
					sp_area);
			}
		}
		break;
	default:
		dev_err(nand_info->dev, "%s: 0x%x, %d, %d\n",
				__func__, command, column, page_addr);
		BUG();
		break;
	}
}

static void amb_nand_hwctl(struct mtd_info *mtd, int mode)
{
}

static int amb_nand_calculate_ecc(struct mtd_info *mtd,
		const u_char *buf, u_char *code)
{
	struct ambarella_nand_info		*nand_info;

	nand_info = (struct ambarella_nand_info *)mtd->priv;

	if (!nand_info->soft_ecc) {
		memset(code, 0xff, nand_info->chip.ecc.bytes);
	} else if (nand_info->plat_nand->sets->ecc_bits == 1) {
		nand_calculate_ecc(mtd, buf, code);
		/* FIXME: the first two bytes ecc codes are swaped comparing
		 * to the ecc codes generated by our hardware, so we swap them
		 * here manually. But I don't know why they were swapped. */
		swap(code[0], code[1]);
	} else {
		u32 i, amb_eccsize;

		/* make it be compatible with hw bch */
		for (i = 0; i < nand_info->chip.ecc.size; i++)
			nand_info->bch_data[i] = bitrev8(buf[i]);

		memset(code, 0, nand_info->chip.ecc.bytes);

		amb_eccsize = nand_info->chip.ecc.size + AMB_SOFT_BCH_EXTRA_SIZE;
		encode_bch(nand_info->bch, nand_info->bch_data, amb_eccsize, code);

		/* make it be compatible with hw bch */
		for (i = 0; i < nand_info->chip.ecc.bytes; i++)
			code[i] = bitrev8(code[i]);
	}

	return 0;
}

static int amb_nand_correct_data(struct mtd_info *mtd, u_char *buf,
	u_char *read_ecc, u_char *calc_ecc)
{
	struct ambarella_nand_info		*nand_info;
	int errorCode = 0;

	nand_info = (struct ambarella_nand_info *)mtd->priv;

	/* if we use hardware ecc, any errors include DMA error and
	 * FIO DMA error, we consider it as a ecc error which will tell
	 * the caller the read fail. We have distinguish all the errors,
	 * but the nand_read_ecc only check the return value by this
	 * function. */
	if (!nand_info->soft_ecc)
		errorCode = nand_info->err_code;
	else if (nand_info->plat_nand->sets->ecc_bits == 1)
		errorCode = nand_correct_data(mtd, buf, read_ecc, calc_ecc);
	else {
		struct nand_chip *chip = &nand_info->chip;
		u32 *errloc = nand_info->errloc;
		int amb_eccsize, i, count;

		for (i = 0; i < chip->ecc.bytes; i++) {
			nand_info->read_ecc_rev[i] = bitrev8(read_ecc[i]);
			nand_info->calc_ecc_rev[i] = bitrev8(calc_ecc[i]);
		}

		amb_eccsize = chip->ecc.size + AMB_SOFT_BCH_EXTRA_SIZE;
		count = decode_bch(nand_info->bch, NULL,
					amb_eccsize,
					nand_info->read_ecc_rev,
					nand_info->calc_ecc_rev,
					NULL, errloc);
		if (count > 0) {
			for (i = 0; i < count; i++) {
				if (errloc[i] < (amb_eccsize * 8)) {
					/* error is located in data, correct it */
					buf[errloc[i] >> 3] ^= (128 >> (errloc[i] & 7));
				}
				/* else error in ecc, no action needed */

				dev_dbg(nand_info->dev,
					"corrected bitflip %u\n", errloc[i]);
			}
		} else if (count < 0) {
			count = nand_bch_spare_cmp(nand_info);
			if (count < 0)
				dev_err(nand_info->dev, "ecc unrecoverable error\n");
		}

		errorCode = count;
	}

	return errorCode;
}

static int amb_nand_write_oob_std(struct mtd_info *mtd,
	struct nand_chip *chip, int page)
{
	struct ambarella_nand_info		*nand_info;
	int					i, status;

	nand_info = (struct ambarella_nand_info *)mtd->priv;
	/* Our nand controller will write the generated ECC code into spare
	  * area automatically, so we should mark the ECC code which located
	  * in the eccpos.
	  */
	if (!nand_info->soft_ecc) {
		for (i = 0; i < chip->ecc.total; i++)
			chip->oob_poi[chip->ecc.layout->eccpos[i]] = 0xFF;
	}

	chip->cmdfunc(mtd, NAND_CMD_SEQIN, mtd->writesize, page);
	chip->write_buf(mtd, chip->oob_poi, mtd->oobsize);
	chip->cmdfunc(mtd, NAND_CMD_PAGEPROG, -1, -1);
	status = chip->waitfunc(mtd, chip);

	return status & NAND_STATUS_FAIL ? -EIO : 0;
}

/* ==========================================================================*/
static void amb_nand_set_timing(struct ambarella_nand_info *nand_info,
	struct ambarella_nand_timing *timing)
{
#ifndef CONFIG_MTD_NAND_AMBARELLA_DEFAULT_TIMINGS
	amba_writel(nand_info->regbase + FLASH_TIM0_OFFSET, timing->timing0);
	amba_writel(nand_info->regbase + FLASH_TIM1_OFFSET, timing->timing1);
	amba_writel(nand_info->regbase + FLASH_TIM2_OFFSET, timing->timing2);
	amba_writel(nand_info->regbase + FLASH_TIM3_OFFSET, timing->timing3);
	amba_writel(nand_info->regbase + FLASH_TIM4_OFFSET, timing->timing4);
	amba_writel(nand_info->regbase + FLASH_TIM5_OFFSET, timing->timing5);
#endif
}


/*
 * The encoding sequence in a byte is "LSB first".
 *
 * For each 2K page, there will be 2048 byte main data (B0 ~ B2047) and 64 byte
 * spare data (B2048 ~ B2111). Thus, each page is divided into 4 BCH blocks.
 * For example, B0~B511 and B2048~B2063 are grouped as the first BCH block.
 * B0 will be encoded first and B2053 will be encoded last.
 *
 * B2054 ~B2063 are used to store 10B parity data (precisely to say, 78 bits)
 * The 2 dummy bits are filled as 0 and located at the msb of B2063.
*/
static int ambarella_nand_init_soft_bch(struct ambarella_nand_info *nand_info)
{
	struct nand_chip *chip = &nand_info->chip;
	u32 amb_eccsize, eccbytes, m, t;

	amb_eccsize = chip->ecc.size + AMB_SOFT_BCH_EXTRA_SIZE;
	eccbytes = chip->ecc.bytes;

	m = fls(1 + 8 * amb_eccsize);
	t = (eccbytes * 8) / m;

	nand_info->bch = init_bch(m, t, 0);
	if (!nand_info->bch)
		return -EINVAL;

	nand_info->errloc = devm_kzalloc(nand_info->dev,
				t * sizeof(*nand_info->errloc), GFP_KERNEL);
	if (!nand_info->errloc)
		return -ENOMEM;

	nand_info->bch_data = devm_kzalloc(nand_info->dev,
					amb_eccsize, GFP_KERNEL);
	if (nand_info->bch_data == NULL)
		return -ENOMEM;

	/* asumming the 6 bytes data in spare area are all 0xff, in other
	 * words, we don't support to write anything except for ECC code
	 * into spare are. */
	memset(nand_info->bch_data + chip->ecc.size,
				0xff, AMB_SOFT_BCH_EXTRA_SIZE);

	return 0;
}

static int ambarella_nand_config_flash(
	struct ambarella_nand_info *nand_info)
{
	int					errorCode = 0;

	ambnand_calc_timing(nand_info);
	amb_nand_set_timing(nand_info, &nand_info->current_timing);

	/* control_reg will be uesd when real operation to NAND is performed */

	/* Calculate row address cycyle according to whether the page number
	  * of the nand is greater than 65536 */
	if ((nand_info->chip.chip_shift - nand_info->chip.page_shift) > 16)
		nand_info->control_reg |= NAND_CTR_P3;
	else
		nand_info->control_reg &= ~NAND_CTR_P3;

	nand_info->control_reg &= ~NAND_CTR_SZ_8G;
	switch (nand_info->chip.chipsize) {
	case 8 * 1024 * 1024:
		nand_info->control_reg |= NAND_CTR_SZ_64M;
		break;
	case 16 * 1024 * 1024:
		nand_info->control_reg |= NAND_CTR_SZ_128M;
		break;
	case 32 * 1024 * 1024:
		nand_info->control_reg |= NAND_CTR_SZ_256M;
		break;
	case 64 * 1024 * 1024:
		nand_info->control_reg |= NAND_CTR_SZ_512M;
		break;
	case 128 * 1024 * 1024:
		nand_info->control_reg |= NAND_CTR_SZ_1G;
		break;
	case 256 * 1024 * 1024:
		nand_info->control_reg |= NAND_CTR_SZ_2G;
		break;
	case 512 * 1024 * 1024:
		nand_info->control_reg |= NAND_CTR_SZ_4G;
		break;
	case 1024 * 1024 * 1024:
		nand_info->control_reg |= NAND_CTR_SZ_8G;
		break;
	default:
		dev_err(nand_info->dev,
			"Unexpected NAND flash chipsize %lld. Aborting\n",
			nand_info->chip.chipsize);
		errorCode = -ENXIO;
		break;
	}

	printk("%s: 0x%08x, 0x%08x\n", __func__,
		nand_info->plat_nand->timing->control, nand_info->control_reg);

	return errorCode;
}

static int ambarella_nand_init_chip(struct ambarella_nand_info *nand_info)
{
	u32 sys_config, cfg_page_size, cfg_read_confirm;
	struct nand_chip *chip = &nand_info->chip;

	sys_config = ambarella_board_generic.board_poc;

	if (sys_config & SYS_CONFIG_NAND_ECC_BCH_EN) {
		if (sys_config & SYS_CONFIG_NAND_ECC_SPARE_2X)
			nand_info->plat_nand->sets->ecc_bits = 8;
		else
			nand_info->plat_nand->sets->ecc_bits = 6;
	} else {
		nand_info->plat_nand->sets->ecc_bits = 1;
	}

	dev_info(nand_info->dev, "in %secc-[%d]bit mode\n",
		nand_info->soft_ecc ? "soft " : "", nand_info->plat_nand->sets->ecc_bits);

	cfg_page_size = sys_config & SYS_CONFIG_NAND_FLASH_PAGE ? 2048 : 512;
	cfg_read_confirm = sys_config & SYS_CONFIG_NAND_READ_CONFIRM ? 1 : 0;

	if (cfg_read_confirm)
		nand_info->control_reg = NAND_CTR_RC;
	if (cfg_page_size == 2048)
		nand_info->control_reg |= (NAND_CTR_C2 | NAND_CTR_SZ_8G);
	/*
	  * Always use P3 and I4 to support all NAND,
	  * but we will adjust them after read ID from NAND.
	  */
	nand_info->control_reg |= (NAND_CTR_P3 | NAND_CTR_I4 | NAND_CTR_IE);

	chip->chip_delay = 0;
	chip->controller = &nand_info->controller;
	chip->read_byte = amb_nand_read_byte;
	chip->read_word = amb_nand_read_word;
	chip->write_buf = amb_nand_write_buf;
	chip->read_buf = amb_nand_read_buf;
	chip->select_chip = amb_nand_select_chip;
	chip->cmd_ctrl = amb_nand_cmd_ctrl;
	chip->dev_ready = amb_nand_dev_ready;
	chip->waitfunc = amb_nand_waitfunc;
	chip->cmdfunc = amb_nand_cmdfunc;
	chip->options |= NAND_NO_SUBPAGE_WRITE;

	if (nand_info->plat_nand->flash_bbt) {
		printk(KERN_INFO "ambarella_nand: Use On Flash BBT\n");
#ifdef CONFIG_PLAT_AMBARELLA_AMBALINK
		chip->bbt_options |= NAND_BBT_USE_FLASH;
		if (nand_info->plat_nand->sets->ecc_bits > 1) {
			chip->bbt_td = &bbt_main_descr_dsm;
			chip->bbt_md = &bbt_mirror_descr_dsm;
		} else {
			chip->bbt_td = &bbt_main_descr;
			chip->bbt_md = &bbt_mirror_descr;
		}
#else
		chip->bbt_options |= NAND_BBT_USE_FLASH | NAND_BBT_NO_OOB;
#endif
	}

	nand_info->mtd.priv = chip;
	nand_info->mtd.owner = THIS_MODULE;

	return 0;
}

static int ambarella_nand_init_chipecc(
	struct ambarella_nand_info *nand_info)
{
	struct nand_chip *chip = &nand_info->chip;
	struct mtd_info	*mtd = &nand_info->mtd;
	int errorCode = 0;

	/* sanity check */
	BUG_ON(nand_info->plat_nand->sets->ecc_bits != 1
		&& nand_info->plat_nand->sets->ecc_bits != 6
		&& nand_info->plat_nand->sets->ecc_bits != 8);
	BUG_ON(mtd->writesize != 2048 && mtd->writesize != 512);
	/* We support 1-bit and 6-bit ecc,recently. */
	BUG_ON(nand_info->soft_ecc && nand_info->plat_nand->sets->ecc_bits > 6);
	BUG_ON(nand_info->plat_nand->sets->ecc_bits == 8 && mtd->oobsize < 128);

	chip->ecc.mode = NAND_ECC_HW;
	chip->ecc.strength = nand_info->plat_nand->sets->ecc_bits;

	switch (nand_info->plat_nand->sets->ecc_bits) {
	case 8:
		chip->ecc.size = 512;
		chip->ecc.bytes = 13;
		chip->ecc.layout = &amb_oobinfo_2048_dsm_ecc8;
		break;
	case 6:
		chip->ecc.size = 512;
		chip->ecc.bytes = 10;
		chip->ecc.layout = &amb_oobinfo_2048_dsm_ecc6;
		break;
	case 1:
		chip->ecc.size = 512;
		chip->ecc.bytes = 5;
		if (mtd->writesize == 2048)
			chip->ecc.layout = &amb_oobinfo_2048;
		else
			chip->ecc.layout = &amb_oobinfo_512;
		break;
	}

	if (nand_amb_is_sw_bch(nand_info)) {
		errorCode = ambarella_nand_init_soft_bch(nand_info);
		if (errorCode < 0)
			return errorCode;
		/* bootloader may have enabled hw BCH, we must disable it here */
		nand_amb_enable_dsm(nand_info);
	}
	chip->ecc.hwctl = amb_nand_hwctl;
	chip->ecc.calculate = amb_nand_calculate_ecc;
	chip->ecc.correct = amb_nand_correct_data;
	chip->ecc.write_oob = amb_nand_write_oob_std;

	return 0;
}

static int ambarella_nand_get_resource(
	struct ambarella_nand_info *nand_info, struct platform_device *pdev)
{
	int					errorCode = 0;
	struct resource				*res;

	res = platform_get_resource_byname(pdev,
		IORESOURCE_MEM, "fio_reg");
	if (res == NULL) {
		dev_err(&pdev->dev, "Get reg_res failed!\n");
		errorCode = -ENXIO;
		goto nand_get_resource_err_exit;
	}
	nand_info->regbase = (unsigned char __iomem *)res->start;

	res = platform_get_resource_byname(pdev,
		IORESOURCE_MEM, "fio_fifo");
	if (res == NULL) {
		dev_err(&pdev->dev, "Get dma_res failed!\n");
		errorCode = -ENXIO;
		goto nand_get_resource_err_exit;
	}
	nand_info->dmabase = ambarella_virt_to_phys(res->start);

	res = platform_get_resource_byname(pdev,
		IORESOURCE_MEM, "fdma_reg");
	if (res == NULL) {
		dev_err(&pdev->dev, "Get dmareg_res failed!\n");
		errorCode = -ENXIO;
		goto nand_get_resource_err_exit;
	}
	nand_info->fdmaregbase = (unsigned char __iomem *)res->start;

	nand_info->cmd_irq = platform_get_irq_byname(pdev, "fio_cmd_irq");
	if (nand_info->cmd_irq <= 0) {
		dev_err(&pdev->dev, "Get cmd_irq failed!\n");
		errorCode = -ENXIO;
		goto nand_get_resource_err_exit;
	}

	nand_info->dma_irq = platform_get_irq_byname(pdev, "fio_dma_irq");
	if (nand_info->dma_irq <= 0) {
		dev_err(&pdev->dev, "Get dma_irq failed!\n");
		errorCode = -ENXIO;
		goto nand_get_resource_err_exit;
	}

	nand_info->fdma_irq = platform_get_irq_byname(pdev, "fdma_irq");
	if (nand_info->fdma_irq <= 0) {
		dev_err(&pdev->dev, "Get fdma_irq failed!\n");
		errorCode = -ENXIO;
		goto nand_get_resource_err_exit;
	}

	res = platform_get_resource_byname(pdev,
		IORESOURCE_IO, "wp_gpio");
	if (res == NULL) {
		nand_info->wp_gpio = -1;
	} else {
		nand_info->wp_gpio = res->start;

		errorCode = gpio_request_one(nand_info->wp_gpio,
				GPIOF_OUT_INIT_HIGH, pdev->name);
		if (errorCode < 0) {
			dev_err(&pdev->dev, "Could not get WP GPIO %d\n",
				nand_info->wp_gpio);
			goto nand_get_resource_err_exit;
		}
	}
	/*
	* You only can set soft_ecc to true,
	* if you want to use soft ecc.
	*/
	nand_info->soft_ecc = amba_nand_sw_ecc;

	errorCode = request_irq(nand_info->cmd_irq, nand_fiocmd_isr_handler,
			IRQF_SHARED | IRQF_TRIGGER_HIGH,
			"fio_cmd_irq", nand_info);
	if (errorCode < 0) {
		dev_err(&pdev->dev, "Could not register fio_cmd_irq %d!\n",
			nand_info->cmd_irq);
		goto nand_get_resource_free_wp_gpio;
	}

	errorCode = request_irq(nand_info->dma_irq, nand_fiodma_isr_handler,
			IRQF_SHARED | IRQF_TRIGGER_HIGH,
			"fio_dma_irq", nand_info);
	if (errorCode < 0) {
		dev_err(&pdev->dev, "Could not register fio_dma_irq %d!\n",
			nand_info->dma_irq);
		goto nand_get_resource_free_fiocmd_irq;
	}

#if !defined(CONFIG_AMBALINK_LOCK)
	/* init fdma to avoid dummy irq */
	amba_writel(nand_info->fdmaregbase + FDMA_STA_OFFSET, 0);
	amba_writel(nand_info->fdmaregbase + FDMA_SPR_STA_OFFSET, 0);
	amba_writel(nand_info->fdmaregbase + FDMA_CTR_OFFSET,
		DMA_CHANX_CTR_WM | DMA_CHANX_CTR_RM | DMA_CHANX_CTR_NI);
#endif

	errorCode = request_irq(nand_info->fdma_irq, ambarella_fdma_isr_handler,
			IRQF_SHARED | IRQF_TRIGGER_HIGH,
			"fdma_irq", nand_info);
	if (errorCode < 0) {
		dev_err(&pdev->dev, "Could not register fdma_irq %d!\n",
			nand_info->dma_irq);
		goto nand_get_resource_free_fiodma_irq;
	}

	return 0;

nand_get_resource_free_fiodma_irq:
	free_irq(nand_info->dma_irq, nand_info);

nand_get_resource_free_fiocmd_irq:
	free_irq(nand_info->cmd_irq, nand_info);

nand_get_resource_free_wp_gpio:
	if (nand_info->wp_gpio >= 0)
		gpio_free(nand_info->wp_gpio);

nand_get_resource_err_exit:
	return errorCode;
}

static void ambarella_nand_put_resource(struct ambarella_nand_info *nand_info)
{
	free_irq(nand_info->fdma_irq, nand_info);
	free_irq(nand_info->dma_irq, nand_info);
	free_irq(nand_info->cmd_irq, nand_info);

	if (nand_info->wp_gpio >= 0)
		gpio_free(nand_info->wp_gpio);
}

static int ambarella_nand_scan_partitions(struct ambarella_nand_info *nand_info)
{
	int					errorCode = 0;

	struct mtd_info				*mtd;
	struct mtd_partition			*amboot_partitions;
	int					amboot_nr_partitions = 0;
	flpart_meta_t				*meta_table;
	int					meta_numpages, meta_offpage;
	int					from, retlen, found, i;
	int					cmd_nr_partitions = 0;

	mtd = &nand_info->mtd;

	/* struct flpart_table_t contains the meta data containing partition info.
	 * meta_offpage indicate the offset from each block
	 * meta_numpages indicate the size of the meta data */
	meta_offpage = sizeof(flpart_table_t) / mtd->writesize;
	meta_numpages = sizeof(flpart_meta_t) / mtd->writesize;
	if (sizeof(flpart_meta_t) % mtd->writesize)
		meta_numpages++;

	if (meta_numpages * mtd->writesize > AMBARELLA_NAND_DMA_BUFFER_SIZE) {
		dev_err(nand_info->dev,
			"%s: The size of meta data is too large\n", __func__);
		errorCode = -ENOMEM;
		goto ambarella_nand_scan_error1;
	}

	/* find the meta data, start from the second block */
	meta_table = kzalloc(sizeof(flpart_meta_t), GFP_KERNEL);
	if (meta_table == NULL)
		goto ambarella_nand_scan_error1;

	found = 0;
	for (from = mtd->erasesize; from < mtd->size; from += mtd->erasesize) {
		if (mtd->_block_isbad(mtd, from)) {
			continue;
		}

		errorCode = mtd->_read(mtd,
			from + meta_offpage * mtd->writesize,
			meta_numpages * mtd->writesize,
			&retlen, (u8 *)meta_table);
		if (errorCode < 0) {
			dev_info(nand_info->dev,
				"%s: Can't meta data!\n", __func__);
			goto ambarella_nand_scan_error2;
		}

		if (meta_table->magic == PTB_META_MAGIC) {
			found = 1;
			break;
		} else if (meta_table->magic == PTB_META_MAGIC2) {
			found = 2;
			break;
		}
	}

	if (found == 0) {
		dev_err(nand_info->dev, "%s: meta appears damaged...\n", __func__);
		errorCode = -EINVAL;
		goto ambarella_nand_scan_error2;
	}

	dev_info(nand_info->dev, "%s: Partition infomation found!\n", __func__);

	amboot_partitions = kzalloc(
		PART_MAX + CMDLINE_PART_MAX * sizeof(struct mtd_partition),
		GFP_KERNEL);
	if (amboot_partitions == NULL)
		goto ambarella_nand_scan_error2;

	/* if this partition isn't located in NAND, fake its nblk to 0, this
	 * feature start from the second version of flpart_meta_t. */
	if (found > 1) {
		for (i = 0; i < PART_MAX; i++) {
			if (meta_table->part_dev[i] != BOOT_DEV_NAND)
				meta_table->part_info[i].nblk = 0;
		}
	}

	amboot_nr_partitions = 0;
	found = 0;
	for (i = 0; i < PART_MAX; i++) {
#ifdef CONFIG_MTD_NAND_AMBARELLA_ENABLE_FULL_PTB
		/*
		 * find swp partition, and then take it as a benchmark to adjust
		 * each partitions's offset.
		 * adjust rules:
		 * 1. before swp partition:
		 *	if the partition's nblk (size) is 0, make its sblk and
		 *	nblk equal to the previous one;
		 * 2. after swp partition:
		 *	if the partition's nblk (size) is 0, skip the partition;
		 */
		if (!found && !strncmp(meta_table->part_info[i].name, "swp", 3)) {
			found = 1;
		}
		if (found) {
			if (meta_table->part_info[i].nblk == 0)
				continue;
		} else {
			/* bst partition should be always exist */
			if (i > 0 && meta_table->part_info[i].nblk == 0) {
				meta_table->part_info[i].sblk =
					meta_table->part_info[i-1].sblk;
				meta_table->part_info[i].nblk =
					meta_table->part_info[i-1].nblk;
			}
		}
#else
		if (meta_table->part_info[i].nblk == 0)
			continue;
#endif
		strcpy(part_name[i], meta_table->part_info[i].name);
		amboot_partitions[amboot_nr_partitions].name = part_name[i];
		amboot_partitions[amboot_nr_partitions].offset = meta_table->part_info[i].sblk * mtd->erasesize;
		amboot_partitions[amboot_nr_partitions].size = meta_table->part_info[i].nblk * mtd->erasesize;
		amboot_nr_partitions++;
	}

	errorCode = 0;

	i = 0;
	if (cmd_nr_partitions >= 0)
		i = cmd_nr_partitions;

	mtd_device_parse_register(mtd, NULL, 0, amboot_partitions, amboot_nr_partitions);

	kfree(amboot_partitions);
ambarella_nand_scan_error2:
	kfree(meta_table);

ambarella_nand_scan_error1:

	return errorCode;
}

static int ambarella_nand_probe(struct platform_device *pdev)
{
	int					errorCode = 0;
	struct ambarella_nand_info		*nand_info;
	struct mtd_info				*mtd;
	struct ambarella_platform_nand		*plat_nand;

	plat_nand = (struct ambarella_platform_nand *)pdev->dev.platform_data;
	if ((plat_nand == NULL) || (plat_nand->timing == NULL) ||
		(plat_nand->sets == NULL) || (plat_nand->parse_error == NULL) ||
		(plat_nand->request == NULL) || (plat_nand->release == NULL) ||
		(plat_nand->get_pll == NULL)) {
		dev_err(&pdev->dev, "Can't get platform_data!\n");
		errorCode = - EPERM;
		goto ambarella_nand_probe_exit;
	}

	nand_info = kzalloc(sizeof(struct ambarella_nand_info), GFP_KERNEL);
	if (nand_info == NULL) {
		dev_err(&pdev->dev, "kzalloc for nand nand_info failed!\n");
		errorCode = - ENOMEM;
		goto ambarella_nand_probe_exit;
	}

	nand_info->plat_nand = plat_nand;
	nand_info->dev = &pdev->dev;
	spin_lock_init(&nand_info->controller.lock);
	init_waitqueue_head(&nand_info->controller.wq);
	init_waitqueue_head(&nand_info->wq);
	sema_init(&nand_info->system_event_sem, 1);
	atomic_set(&nand_info->irq_flag, 0x7);

	nand_info->dmabuf = dma_alloc_coherent(nand_info->dev,
		AMBARELLA_NAND_DMA_BUFFER_SIZE,
		&nand_info->dmaaddr, GFP_KERNEL);
	if (nand_info->dmabuf == NULL) {
		dev_err(&pdev->dev, "dma_alloc_coherent failed!\n");
		errorCode = -ENOMEM;
		goto ambarella_nand_probe_free_info;
	}
	BUG_ON(nand_info->dmaaddr & 0x7);

#ifdef CONFIG_PLAT_AMBARELLA_AMBALINK
	nand_info->plat_nand->request();
#endif

	errorCode = ambarella_nand_get_resource(nand_info, pdev);
	if (errorCode < 0)
		goto ambarella_nand_probe_free_dma;

#if defined(CONFIG_AMBALINK_LOCK)
	/*
	 * Disablle the IRQs, which by default are enabled when requested.
	 * Otherwise, it causes the unbalenced IRQ enable/disable checking
	 * failed when the first time nand_amb_request() is called.
	 */
	disable_irq(nand_info->cmd_irq);
	disable_irq(nand_info->dma_irq);
	disable_irq(nand_info->fdma_irq);

	nand_info->plat_nand->release();

	/*
	 * NAND DSM bug: doing copyback after a read command will be failed.
	 * We need to save the last NAND command of Linux to notify ThreadX to
	 * workaround this bug.
	 */
#ifdef CONFIG_PLAT_AMBARELLA_BOSS
	ambalink_nand_last_cmd_addr = AIPC_MUTEX_ADDR - 0x4;
#else
	ambalink_nand_last_cmd_addr = ambarella_phys_to_virt(AIPC_MUTEX_ADDR) - 0x4;
#endif
#endif

	ambarella_nand_init_chip(nand_info);

	mtd = &nand_info->mtd;
	mtd->name = "ambarella_nand";

	errorCode = nand_scan_ident(mtd, plat_nand->sets->nr_chips, NULL);
	if (errorCode)
		goto ambarella_nand_probe_mtd_error;

	errorCode = ambarella_nand_init_chipecc(nand_info);
	if (errorCode)
		goto ambarella_nand_probe_mtd_error;

	errorCode = ambarella_nand_config_flash(nand_info);
	if (errorCode)
		goto ambarella_nand_probe_mtd_error;

	errorCode = nand_scan_tail(mtd);
	if (errorCode)
		goto ambarella_nand_probe_mtd_error;
#ifdef CONFIG_AMBPTB_PARTITION
	errorCode = ambarella_nand_scan_partitions(nand_info);
	if (errorCode)
		goto ambarella_nand_probe_mtd_error;
#else
	mtd_device_parse_register(mtd, NULL, NULL, NULL, 0);
#endif


	platform_set_drvdata(pdev, nand_info);

	nand_info->origin_timing = plat_nand->timing;

	nand_info->system_event.notifier_call = ambarella_nand_system_event;
	ambarella_register_event_notifier(&nand_info->system_event);

	goto ambarella_nand_probe_exit;

ambarella_nand_probe_mtd_error:
	ambarella_nand_put_resource(nand_info);

ambarella_nand_probe_free_dma:
	dma_free_coherent(nand_info->dev,
		AMBARELLA_NAND_DMA_BUFFER_SIZE,
		nand_info->dmabuf, nand_info->dmaaddr);

ambarella_nand_probe_free_info:
	kfree(nand_info);

ambarella_nand_probe_exit:

	return errorCode;
}

static int ambarella_nand_remove(struct platform_device *pdev)
{
	int					errorCode = 0;
	struct ambarella_nand_info		*nand_info;

	nand_info = (struct ambarella_nand_info *)platform_get_drvdata(pdev);

	if (nand_info) {
		ambarella_unregister_event_notifier(&nand_info->system_event);

		nand_release(&nand_info->mtd);

		ambarella_nand_put_resource(nand_info);

		dma_free_coherent(nand_info->dev,
			AMBARELLA_NAND_DMA_BUFFER_SIZE,
			nand_info->dmabuf, nand_info->dmaaddr);

		kfree(nand_info);
	}

	return errorCode;
}

#ifdef CONFIG_PM
static int ambarella_nand_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	int					errorCode = 0;
	struct ambarella_nand_info		*nand_info;

	nand_info = platform_get_drvdata(pdev);
	nand_info->suspend = 1;

	down(&nand_info->system_event_sem);

#if !defined(CONFIG_PLAT_AMBARELLA_BOSS)
	disable_irq(nand_info->dma_irq);
	disable_irq(nand_info->cmd_irq);
	disable_irq(nand_info->fdma_irq);
#endif
	dev_dbg(&pdev->dev, "%s exit with %d @ %d\n",
		__func__, errorCode, state.event);

	return errorCode;
}

static int ambarella_nand_resume(struct platform_device *pdev)
{
	int					errorCode = 0;
	struct ambarella_nand_info		*nand_info;

	nand_info = platform_get_drvdata(pdev);
	amb_nand_set_timing(nand_info, &nand_info->current_timing);

#if defined(CONFIG_AMBALINK_LOCK)
#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
	boss_set_irq_owner(nand_info->cmd_irq, BOSS_IRQ_OWNER_LINUX, 1);
	boss_set_irq_owner(nand_info->dma_irq, BOSS_IRQ_OWNER_LINUX, 1);
	boss_set_irq_owner(nand_info->fdma_irq, BOSS_IRQ_OWNER_LINUX, 1);
#endif
	enable_irq(nand_info->cmd_irq);
	enable_irq(nand_info->dma_irq);
	enable_irq(nand_info->fdma_irq);
#endif

	up(&nand_info->system_event_sem);

	nand_info->suspend = 0;

	dev_dbg(&pdev->dev, "%s exit with %d\n", __func__, errorCode);

	return errorCode;
}
#endif

static struct platform_driver amb_nand_driver = {
	.probe		= ambarella_nand_probe,
	.remove		= ambarella_nand_remove,
#ifdef CONFIG_PM
	.suspend	= ambarella_nand_suspend,
	.resume		= ambarella_nand_resume,
#endif
	.driver = {
		.name	= "ambarella-nand",
		.owner	= THIS_MODULE,
	},
};
module_platform_driver(amb_nand_driver);

MODULE_AUTHOR("Cao Rongrong & Chien-Yang Chen");
MODULE_DESCRIPTION("Ambarella Media processor NAND Controller Driver");
MODULE_LICENSE("GPL");

