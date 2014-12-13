/*
 * drivers/dma/ambarella_dma.c  --  Ambarella DMA engine driver
 *
 * History:
 *	2012/05/10 - Ken He <jianhe@ambarella.com> created file
 *
 * Coryright (c) 2008-2012, Ambarella, Inc.
 * http://www.ambarella.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/dmapool.h>
#include <linux/delay.h>

#include <mach/hardware.h>
#include <plat/dma.h>

#include "ambarella_dma.h"

int ambarella_dma_channel_id(void *chan)
{
	return to_ambdma_chan((struct dma_chan *)chan)->id;
}
EXPORT_SYMBOL(ambarella_dma_channel_id);

static struct ambdma_desc *ambdma_alloc_desc(struct dma_chan *chan, gfp_t gfp_flags)
{
	struct ambdma_chan *amb_chan = to_ambdma_chan(chan);
	struct ambdma_device *amb_dma = amb_chan->amb_dma;
	struct ambdma_desc *amb_desc = NULL;
	dma_addr_t phys;

	amb_desc = kzalloc(sizeof(struct ambdma_desc), gfp_flags);
	if (!amb_desc)
		return NULL;

	amb_desc->lli = dma_pool_alloc(amb_dma->lli_pool, gfp_flags, &phys);
	if (!amb_desc->lli) {
		kfree(amb_desc);
		return NULL;
	}

	INIT_LIST_HEAD(&amb_desc->tx_list);
	dma_async_tx_descriptor_init(&amb_desc->txd, chan);
	/* txd.flags will be overwritten in prep functions */
	amb_desc->txd.flags = DMA_CTRL_ACK;
	amb_desc->txd.tx_submit = ambdma_tx_submit;
	amb_desc->txd.phys = phys;
	amb_desc->is_cyclic = 0;

	return amb_desc;
}

static void ambdma_free_desc(struct dma_chan *chan, struct ambdma_desc *amb_desc)
{
	struct ambdma_chan *amb_chan = to_ambdma_chan(chan);
	struct ambdma_device *amb_dma = amb_chan->amb_dma;

	dma_pool_free(amb_dma->lli_pool, amb_desc->lli, amb_desc->txd.phys);
	kfree(amb_desc);
}

static struct ambdma_desc *ambdma_get_desc(struct ambdma_chan *amb_chan)
{
	struct ambdma_desc *desc, *_desc;
	struct ambdma_desc *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&amb_chan->lock, flags);
	list_for_each_entry_safe(desc, _desc, &amb_chan->free_list, desc_node) {
		if (async_tx_test_ack(&desc->txd)) {
			list_del_init(&desc->desc_node);
			ret = desc;
			break;
		}
	}
	spin_unlock_irqrestore(&amb_chan->lock, flags);

	/* no more descriptor available in initial pool: create one more */
	if (!ret) {
		ret = ambdma_alloc_desc(&amb_chan->chan, GFP_ATOMIC);
		if (ret) {
			spin_lock_irqsave(&amb_chan->lock, flags);
			amb_chan->descs_allocated++;
			spin_unlock_irqrestore(&amb_chan->lock, flags);
		} else {
			pr_err("%s: no more descriptors available\n", __func__);
		}
	}

	return ret;
}


static void ambdma_put_desc(struct ambdma_chan *amb_chan,
		struct ambdma_desc *amb_desc)
{
	unsigned long flags;

	if (amb_desc) {
		spin_lock_irqsave(&amb_chan->lock, flags);
		list_splice_init(&amb_desc->tx_list, &amb_chan->free_list);
		list_add_tail(&amb_desc->desc_node, &amb_chan->free_list);
		spin_unlock_irqrestore(&amb_chan->lock, flags);
	}
}

static void ambdma_return_desc(struct ambdma_chan *amb_chan,
		struct ambdma_desc *amb_desc)
{
	struct dma_async_tx_descriptor	*txd = &amb_desc->txd;

	/* move children to free_list */
	list_splice_init(&amb_desc->tx_list, &amb_chan->free_list);
	/* move myself to free_list */
	list_move_tail(&amb_desc->desc_node, &amb_chan->free_list);

	/* unmap dma addresses if required */
	if (!amb_chan->chan.private) {
		if (!(txd->flags & DMA_COMPL_SKIP_DEST_UNMAP)) {
			struct device *dev = amb_chan->chan.device->dev;
			if (txd->flags & DMA_COMPL_DEST_UNMAP_SINGLE)
				dma_unmap_single(dev,
						amb_desc->lli->dst,
						amb_desc->len,
						DMA_FROM_DEVICE);
			else
				dma_unmap_page(dev,
						amb_desc->lli->dst,
						amb_desc->len,
						DMA_FROM_DEVICE);
		}
		if (!(txd->flags & DMA_COMPL_SKIP_SRC_UNMAP)) {
			struct device *dev = amb_chan->chan.device->dev;
			if (txd->flags & DMA_COMPL_SRC_UNMAP_SINGLE)
				dma_unmap_single(dev,
						amb_desc->lli->src,
						amb_desc->len,
						DMA_TO_DEVICE);
			else
				dma_unmap_page(dev,
						amb_desc->lli->src,
						amb_desc->len,
						DMA_TO_DEVICE);
		}
	}
}

static void ambdma_chain_complete(struct ambdma_chan *amb_chan,
		struct ambdma_desc *amb_desc)
{
	struct dma_async_tx_descriptor	*txd = &amb_desc->txd;

	amb_chan->completed_cookie = txd->cookie;

	ambdma_return_desc(amb_chan, amb_desc);

	spin_unlock(&amb_chan->lock);
	if (txd->callback && (txd->flags & DMA_PREP_INTERRUPT))
		txd->callback(txd->callback_param);
	spin_lock(&amb_chan->lock);

	dma_run_dependencies(txd);
}


static void ambdma_complete_all(struct ambdma_chan *amb_chan)
{
	struct ambdma_desc *amb_desc, *_desc;
	LIST_HEAD(list);

	list_splice_init(&amb_chan->active_list, &list);

	/* submit queued descriptors ASAP, i.e. before we go through
	 * the completed ones. */
	if (!list_empty(&amb_chan->queue)) {
		list_splice_init(&amb_chan->queue, &amb_chan->active_list);
		ambdma_dostart(amb_chan, ambdma_first_active(amb_chan));
	}

	list_for_each_entry_safe(amb_desc, _desc, &list, desc_node)
		ambdma_chain_complete(amb_chan, amb_desc);
}

static void ambdma_advance_work(struct ambdma_chan *amb_chan)
{
	if (list_empty(&amb_chan->active_list) || list_is_singular(&amb_chan->active_list)) {
		ambdma_complete_all(amb_chan);
	} else {
		ambdma_chain_complete(amb_chan, ambdma_first_active(amb_chan));
		/* active_list has been updated by ambdma_chain_complete(),
		 * so ambdma_first_active() will get another amb_desc. */
		ambdma_dostart(amb_chan, ambdma_first_active(amb_chan));
	}
}

static void ambdma_handle_error(struct ambdma_chan *amb_chan,
		struct ambdma_desc *bad_desc)
{
	list_del_init(&bad_desc->desc_node);

	/* try to submit queued descriptors to restart dma */
	list_splice_init(&amb_chan->queue, amb_chan->active_list.prev);
	if (!list_empty(&amb_chan->active_list))
		ambdma_dostart(amb_chan, ambdma_first_active(amb_chan));

	pr_crit("%s: DMA error on channel %d: 0x%08x\n",
		__func__, amb_chan->id, bad_desc->lli->rpt);

	/* pretend the descriptor completed successfully */
	ambdma_chain_complete(amb_chan, bad_desc);
}

static void ambdma_tasklet(unsigned long data)
{
	struct ambdma_chan *amb_chan = (struct ambdma_chan *)data;
	struct ambdma_desc *amb_desc = NULL;
	enum ambdma_status old_status;
	unsigned long flags;

	spin_lock_irqsave(&amb_chan->lock, flags);

	old_status = amb_chan->status;
	if (!ambdma_chan_is_enabled(amb_chan)) {
		amb_chan->status = AMBDMA_STATUS_IDLE;
		if (!list_empty(&amb_chan->stopping_list))
			ambdma_return_desc(amb_chan, ambdma_first_stopping(amb_chan));
	}

	/* someone might have called terminate all */
	if (list_empty(&amb_chan->active_list))
		goto tasklet_out;

	/* note: if the DMA channel is stopped by DMA_TERMINATE_ALL rather
	 * than naturally end, then ambdma_first_active() will return the next
	 * descriptor that need to be started, but not the descriptor that
	 * invoke this tasklet (IRQ) */
	amb_desc = ambdma_first_active(amb_chan);

	if (!amb_desc->is_cyclic && amb_chan->status != AMBDMA_STATUS_IDLE) {
		pr_err("%s: channel(%d) invalid status\n", __func__, amb_chan->id);
		goto tasklet_out;
	}

	if (old_status == AMBDMA_STATUS_BUSY) {
		/* the IRQ is triggered by DMA stopping naturally or by errors.*/
		if (ambdma_desc_is_error(amb_desc)) {
			ambdma_handle_error(amb_chan, amb_desc);
		} else if (amb_desc->is_cyclic) {
			spin_unlock(&amb_chan->lock);
			if (amb_desc->txd.callback)
				amb_desc->txd.callback(amb_desc->txd.callback_param);
			spin_lock(&amb_chan->lock);
		} else {
			ambdma_advance_work(amb_chan);
		}
	} else if (old_status == AMBDMA_STATUS_STOPPING) {
		/* the DMA channel is stopped by DMA_TERMINATE_ALL.  */
		ambdma_dostart(amb_chan, amb_desc);
	}

tasklet_out:
	spin_unlock_irqrestore(&amb_chan->lock, flags);
}

static irqreturn_t ambdma_dma_irq_handler(int irq, void *dev_data)
{
	struct ambdma_device *amb_dma = dev_data;
	u32 i, int_src;
	irqreturn_t ret = IRQ_NONE;

	int_src = amba_readl(DMA_INT_REG);

	if (int_src == 0)
		return IRQ_HANDLED;

	for (i = 0; i < NUM_DMA_CHANNELS; i++) {

		if (i == FIO_DMA_CHAN)
			continue;

		spin_lock(&amb_dma->amb_chan[i].lock);
		if (int_src & (1 << i)) {
			amba_writel(DMA_CHAN_STA_REG(i), 0);
			tasklet_schedule(&amb_dma->amb_chan[i].tasklet);
			ret = IRQ_HANDLED;
		}
		spin_unlock(&amb_dma->amb_chan[i].lock);
	}

	return ret;
}

static int ambdma_stop_channel(struct ambdma_chan *amb_chan)
{
	struct ambdma_device *amb_dma = amb_chan->amb_dma;
	struct ambdma_desc *first, *amb_desc;
	int id = amb_chan->id;

	if (amb_chan->status == AMBDMA_STATUS_BUSY) {
		if (amb_chan->force_stop) {
			/* Disable DMA: this sequence is not mentioned at APM.*/
			amba_writel(DMA_CHAN_STA_REG(id), DMA_CHANX_STA_OD);
			amba_writel(DMA_CHAN_DA_REG(id), amb_dma->dummy_lli_phys);
			amba_writel(DMA_CHAN_CTR_REG(id),
				DMA_CHANX_CTR_WM | DMA_CHANX_CTR_NI);
			udelay(1);
			/* avoid to trigger dummy IRQ.*/
			amba_writel(DMA_CHAN_STA_REG(id), 0x0);
			if (ambdma_chan_is_enabled(amb_chan)) {
				pr_err("%s: stop dma channel(%d) failed\n",
					__func__, id);
				return -EIO;
			}
			amb_chan->status = AMBDMA_STATUS_IDLE;
		} else {
			/* if force_stop == 0, the DMA channel is still running
			 * at this moment. And normally there are still two IRQs
			 * will be triggered untill DMA channel stops. */
			first = ambdma_first_active(amb_chan);
			first->lli->attr |= DMA_DESC_EOC;
			list_for_each_entry(amb_desc, &first->tx_list, desc_node) {
				amb_desc->lli->attr |= DMA_DESC_EOC;
			}
			amb_chan->status = AMBDMA_STATUS_STOPPING;
			/* active_list is still being used by DMA controller,
			 * so move it to stopping_list to avoid being
			 * initialized by next transfer */
			list_move_tail(&first->desc_node, &amb_chan->stopping_list);
		}
	}

	return 0;
}

static dma_cookie_t ambdma_assign_cookie(struct ambdma_chan *amb_chan,
		struct ambdma_desc *amb_desc)
{
	dma_cookie_t cookie = amb_chan->chan.cookie;

	if (++cookie < 0)
		cookie = 1;

	amb_chan->chan.cookie = cookie;
	amb_desc->txd.cookie = cookie;

	return cookie;
}

static void ambdma_dostart(struct ambdma_chan *amb_chan, struct ambdma_desc *first)
{
	int id = amb_chan->id;

	/* if DMA channel is not idle right now, the DMA descriptor
	 * will be started at ambdma_tasklet(). */
	if (amb_chan->status > AMBDMA_STATUS_IDLE)
		return;

	if (ambdma_chan_is_enabled(amb_chan)) {
		pr_err("%s: channel(%d) should be idle here\n", __func__, id);
		return;
	}

	amba_writel(DMA_CHAN_STA_REG(id), 0x0);
	amba_writel(DMA_CHAN_DA_REG(id), first->txd.phys);
	amba_writel(DMA_CHAN_CTR_REG(id), DMA_CHANX_CTR_EN | DMA_CHANX_CTR_D);
	amb_chan->status = AMBDMA_STATUS_BUSY;
}

static dma_cookie_t ambdma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct ambdma_desc *amb_desc;
	struct ambdma_chan *amb_chan;
	dma_cookie_t cookie = 0;
	unsigned long flags;

	amb_desc = to_ambdma_desc(tx);
	amb_chan = to_ambdma_chan(tx->chan);

	spin_lock_irqsave(&amb_chan->lock, flags);

	cookie = ambdma_assign_cookie(amb_chan, amb_desc);

	if (list_empty(&amb_chan->active_list)) {
		list_add_tail(&amb_desc->desc_node, &amb_chan->active_list);
		ambdma_dostart(amb_chan, amb_desc);
	} else {
		list_add_tail(&amb_desc->desc_node, &amb_chan->queue);
	}

	spin_unlock_irqrestore(&amb_chan->lock, flags);

	return cookie;
}

/* DMA Engine API begin */
static int ambdma_alloc_chan_resources(struct dma_chan *chan)
{
	struct ambdma_chan *amb_chan;
	struct ambdma_desc *amb_desc;
	LIST_HEAD(tmp_list);
	unsigned long flags;
	int i;

	amb_chan = to_ambdma_chan(chan);

	if (amb_chan->status == AMBDMA_STATUS_BUSY) {
		pr_err("%s: channel(%d) not idle!\n", __func__, amb_chan->id);
		return -EIO;
	}

	/* Alloc descriptors for this channel */
	for (i = 0; i < NR_DESCS_PER_CHANNEL; i++) {
		amb_desc = ambdma_alloc_desc(chan, GFP_KERNEL);
		if (amb_desc == NULL) {
			struct ambdma_desc *d, *_d;
			list_for_each_entry_safe(d, _d, &tmp_list, desc_node) {
				ambdma_free_desc(chan, d);
			}
			pr_err("%s: failed to allocate descriptor\n", __func__);
			return -ENOMEM;
		}

		list_add_tail(&amb_desc->desc_node, &tmp_list);
	}

	spin_lock_irqsave(&amb_chan->lock, flags);
	amb_chan->descs_allocated = i;
	list_splice_init(&tmp_list, &amb_chan->free_list);
	amb_chan->completed_cookie = chan->cookie = 1;
	if (!amb_chan->chan.private)
		amb_chan->force_stop = 1;
	else
		amb_chan->force_stop = *(u32 *)amb_chan->chan.private;
	spin_unlock_irqrestore(&amb_chan->lock, flags);

	return amb_chan->descs_allocated;
}

static void ambdma_free_chan_resources(struct dma_chan *chan)
{
	struct ambdma_chan *amb_chan;
	struct ambdma_desc *amb_desc, *_desc;
	LIST_HEAD(list);
	unsigned long flags;

	amb_chan = to_ambdma_chan(chan);

	spin_lock_irqsave(&amb_chan->lock, flags);
	BUG_ON(!list_empty(&amb_chan->active_list));
	BUG_ON(!list_empty(&amb_chan->queue));
	BUG_ON(amb_chan->status == AMBDMA_STATUS_BUSY);

	list_splice_init(&amb_chan->free_list, &list);
	amb_chan->descs_allocated = 0;
	spin_unlock_irqrestore(&amb_chan->lock, flags);

	list_for_each_entry_safe(amb_desc, _desc, &list, desc_node)
		ambdma_free_desc(chan, amb_desc);
}

static enum dma_status ambdma_tx_status(struct dma_chan *chan,
		dma_cookie_t cookie, struct dma_tx_state *txstate)
{
	struct ambdma_chan *amb_chan = to_ambdma_chan(chan);
	dma_cookie_t last_used, last_complete;
	unsigned long flags;

	spin_lock_irqsave(&amb_chan->lock, flags);
	last_used = chan->cookie;
	last_complete = amb_chan->completed_cookie;
	spin_unlock_irqrestore(&amb_chan->lock, flags);

	dma_set_tx_state(txstate, last_complete, last_used, 0);

	return dma_async_is_complete(cookie, last_complete, last_used);
}

static void ambdma_issue_pending(struct dma_chan *chan)
{
	struct ambdma_chan *amb_chan = to_ambdma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&amb_chan->lock, flags);

	/* if dma channel is not idle, will active queue list in tasklet. */
	if (amb_chan->status == AMBDMA_STATUS_IDLE) {
		if (!list_empty(&amb_chan->active_list))
			pr_err("%s: active_list should be empty here\n", __func__);
		else
			ambdma_advance_work(amb_chan);
	}

	spin_unlock_irqrestore(&amb_chan->lock, flags);
}

static int ambdma_device_control(struct dma_chan *chan,
		enum dma_ctrl_cmd cmd, 	unsigned long arg)
{
	struct ambdma_chan *amb_chan = to_ambdma_chan(chan);
	struct dma_slave_config *config = (void *)arg;
	enum dma_slave_buswidth width = 0;
	int ret = 0, maxburst;
	struct ambdma_desc *amb_desc, *_desc;
	LIST_HEAD(list);
	unsigned long flags;

	switch (cmd) {
	case DMA_TERMINATE_ALL:
		spin_lock_irqsave(&amb_chan->lock, flags);
		ambdma_stop_channel(amb_chan);

		/* active_list entries will end up before queued entries */
		list_splice_init(&amb_chan->queue, &list);
		list_splice_init(&amb_chan->active_list, &list);

		/* Flush all pending and queued descriptors */
		list_for_each_entry_safe(amb_desc, _desc, &list, desc_node) {
			/* move children to free_list */
			list_splice_init(&amb_desc->tx_list, &amb_chan->free_list);
			/* move myself to free_list */
			list_move_tail(&amb_desc->desc_node, &amb_chan->free_list);
		}
		spin_unlock_irqrestore(&amb_chan->lock, flags);

		break;
	case DMA_SLAVE_CONFIG:
		/* We only support mem to dev or dev to mem transfers */
		switch (config->direction) {
		case DMA_MEM_TO_DEV:
			width = config->dst_addr_width;
			maxburst = config->dst_maxburst;
			amb_chan->rt_addr = config->dst_addr;
			amb_chan->rt_attr = DMA_DESC_RM | DMA_DESC_NI |
					DMA_DESC_IE | DMA_DESC_ST;
			break;
		case DMA_DEV_TO_MEM:
			width = config->src_addr_width;
			maxburst = config->src_maxburst;
			amb_chan->rt_addr = config->src_addr;
			amb_chan->rt_attr = DMA_DESC_WM | DMA_DESC_NI |
					DMA_DESC_IE | DMA_DESC_ST;
			break;
		default:
			return -ENXIO;
		}

		/* bus width for descriptor mode control_info [ts fileds] */
		switch (width) {
		case DMA_SLAVE_BUSWIDTH_8_BYTES:
			amb_chan->rt_attr |= DMA_DESC_TS_8B;
			break;
		case DMA_SLAVE_BUSWIDTH_4_BYTES:
			amb_chan->rt_attr |= DMA_DESC_TS_4B;
			break;
		case DMA_SLAVE_BUSWIDTH_2_BYTES:
			amb_chan->rt_attr |= DMA_DESC_TS_2B;
			break;
		case DMA_SLAVE_BUSWIDTH_1_BYTE:
			amb_chan->rt_attr |= DMA_DESC_TS_1B;
			break;
		default:
			break;
		}

		/* burst for descriptor mode control_info [blk fileds] */
		switch (maxburst) {
		case 1024:
			amb_chan->rt_attr |= DMA_DESC_BLK_1024B;
			break;
		case 512:
			amb_chan->rt_attr |= DMA_DESC_BLK_512B;
			break;
		case 256:
			amb_chan->rt_attr |= DMA_DESC_BLK_256B;
			break;
		case 128:
			amb_chan->rt_attr |= DMA_DESC_BLK_128B;
			break;
		case 64:
			amb_chan->rt_attr |= DMA_DESC_BLK_64B;
			break;
		case 32:
			amb_chan->rt_attr |= DMA_DESC_BLK_32B;
			break;
		case 16:
			amb_chan->rt_attr |= DMA_DESC_BLK_16B;
			break;
		case 8:
			amb_chan->rt_attr |= DMA_DESC_BLK_8B;
			break;
		default:
			break;
		}

		break;
	default:
		ret = -ENXIO;
	}

	return ret;
}

static struct dma_async_tx_descriptor *ambdma_prep_dma_cyclic(
		struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
		size_t period_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
	struct ambdma_chan *amb_chan = to_ambdma_chan(chan);
	struct ambdma_desc *amb_desc, *first = NULL, *prev = NULL;
	int left_len = buf_len;

	if (buf_len == 0 || period_len == 0) {
		pr_err("%s: buf/period length is zero!\n", __func__);
		return NULL;
	}

	if (!IS_ALIGNED(buf_addr, 8) || !IS_ALIGNED(period_len, 8)) {
		pr_err("%s: buf_addr/period_len is not 8bytes aligned! (%d,%d)\n",
			__func__, buf_addr, period_len);
		return NULL;
	}

	do {
		amb_desc = ambdma_get_desc(amb_chan);
		if (!amb_desc)
			goto dma_cyclic_err;

		amb_desc->is_cyclic = 1;

		if (period_len > left_len)
			period_len = left_len;

		if (direction == DMA_MEM_TO_DEV) {
			amb_desc->lli->src = buf_addr;
			amb_desc->lli->dst = amb_chan->rt_addr;
		} else if (direction == DMA_DEV_TO_MEM) {
			amb_desc->lli->src = amb_chan->rt_addr;
			amb_desc->lli->dst = buf_addr;
		} else {
			goto dma_cyclic_err;
		}
		/* trigger interrupt after each dma transaction ends. */
		amb_desc->lli->attr = amb_chan->rt_attr | DMA_DESC_ID;
		amb_desc->lli->xfr_count = period_len;
		/* rpt_addr points to amb_desc->lli->rpt */
		amb_desc->lli->rpt_addr =
			amb_desc->txd.phys + sizeof(struct ambdma_lli) - 4;
		/* here we initialize rpt to 0 */
		amb_desc->lli->rpt = 0;

		if (first == NULL)
			first = amb_desc;
		else {
			prev->lli->next_desc = amb_desc->txd.phys;
			list_add_tail(&amb_desc->desc_node, &first->tx_list);
		}

		prev = amb_desc;

		left_len -= period_len;
		buf_addr += period_len;

		/* our dma controller can't transfer data larger than 4M Bytes,
		 * but it seems that no use case will transfer so large data,
		 * so we just trigger a BUG here for reminder. */
		BUG_ON(amb_desc->lli->xfr_count > AMBARELLA_DMA_MAX_LENGTH);
	} while (left_len > 0);

	/* lets make a cyclic list */
	amb_desc->lli->next_desc = first->txd.phys;

	/* First descriptor of the chain embedds additional information */
	first->txd.cookie = -EBUSY;
	first->len = buf_len;

	return &first->txd;

dma_cyclic_err:
	dev_err(&chan->dev->device, "prep_dma_cyclic error: %p\n", amb_desc);
	ambdma_put_desc(amb_chan, first);
	return NULL;
}


static struct dma_async_tx_descriptor *ambdma_prep_slave_sg(
		struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
	struct ambdma_chan *amb_chan = to_ambdma_chan(chan);
	struct ambdma_desc *amb_desc, *first = NULL, *prev = NULL;
	struct scatterlist *sgent;
	size_t total_len = 0;
	unsigned i = 0;

	if (sg_len == 0) {
		pr_err("%s: sg length is zero!\n", __func__);
		return NULL;
	}

	for_each_sg(sgl, sgent, sg_len, i) {
		amb_desc = ambdma_get_desc(amb_chan);
		if (!amb_desc)
			goto slave_sg_err;

		amb_desc->is_cyclic = 0;

		if (direction == DMA_MEM_TO_DEV) {
			amb_desc->lli->src = sg_dma_address(sgent);
			amb_desc->lli->dst = amb_chan->rt_addr;
		} else if (direction == DMA_DEV_TO_MEM) {
			amb_desc->lli->src = amb_chan->rt_addr;
			amb_desc->lli->dst = sg_dma_address(sgent);
		} else {
			goto slave_sg_err;
		}
		amb_desc->lli->attr = amb_chan->rt_attr;
		amb_desc->lli->xfr_count = sg_dma_len(sgent);
		/* rpt_addr points to amb_desc->lli->rpt */
		amb_desc->lli->rpt_addr =
			amb_desc->txd.phys + sizeof(struct ambdma_lli) - 4;
		/* here we initialize rpt to 0 */
		amb_desc->lli->rpt = 0;

		if (first == NULL)
			first = amb_desc;
		else {
			prev->lli->next_desc = amb_desc->txd.phys;
			list_add_tail(&amb_desc->desc_node, &first->tx_list);
		}

		prev = amb_desc;
		total_len += amb_desc->lli->xfr_count;

		/* our dma controller can't transfer data larger than 4M Bytes,
		 * but it seems that no use case will transfer so large data,
		 * so we just trigger a BUG here for reminder. */
		BUG_ON(amb_desc->lli->xfr_count > AMBARELLA_DMA_MAX_LENGTH);
	}

	/* set EOC flag to specify the last descriptor */
	amb_desc->lli->attr |= DMA_DESC_EOC;

	/* First descriptor of the chain embedds additional information */
	first->txd.cookie = -EBUSY;
	first->txd.flags = flags; /* client is in control of this ack */
	first->len = total_len;

	return &first->txd;

slave_sg_err:
	dev_err(&chan->dev->device, "prep_slave_sg error: %p\n", amb_desc);
	ambdma_put_desc(amb_chan, first);
	return NULL;
}

static struct dma_async_tx_descriptor *ambdma_prep_dma_memcpy(
		struct dma_chan *chan, dma_addr_t dst, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct ambdma_chan *amb_chan = to_ambdma_chan(chan);
	struct ambdma_desc *amb_desc = NULL, *first = NULL, *prev = NULL;
	size_t left_len = len, xfer_count;

	if (unlikely(!len)) {
		pr_info("%s: length is zero!\n", __func__);
		return NULL;
	}

	if (!IS_ALIGNED(dst, 8) || !IS_ALIGNED(src, 8)) {
		pr_err("%s: dst/src is not 8bytes aligned! (%d,%d)\n",
			__func__, dst, src);
		return NULL;
	}

	do{
		amb_desc = ambdma_get_desc(amb_chan);
		if (!amb_desc)
			goto dma_memcpy_err;

		amb_desc->is_cyclic = 0;

		amb_desc->lli->src = src;
		amb_desc->lli->dst = dst;
		amb_desc->lli->attr = DMA_DESC_RM | DMA_DESC_WM | DMA_DESC_IE |
				DMA_DESC_ST | DMA_DESC_BLK_32B | DMA_DESC_TS_4B;
		xfer_count = min(left_len, (size_t)AMBARELLA_DMA_MAX_LENGTH);
		amb_desc->lli->xfr_count = xfer_count;
		/* rpt_addr points to amb_desc->lli->rpt */
		amb_desc->lli->rpt_addr =
			amb_desc->txd.phys + sizeof(struct ambdma_lli) - 4;
		/* here we initialize rpt to 0 */
		amb_desc->lli->rpt = 0;

		if (first == NULL)
			first = amb_desc;
		else {
			prev->lli->next_desc = amb_desc->txd.phys;
			list_add_tail(&amb_desc->desc_node, &first->tx_list);
		}

		prev = amb_desc;

		src += xfer_count;
		dst += xfer_count;
		left_len -= xfer_count;

	} while (left_len > 0);

	/* set EOC flag to specify the last descriptor */
	amb_desc->lli->attr |= DMA_DESC_EOC;

	/* First descriptor of the chain embedds additional information */
	first->txd.cookie = -EBUSY;
	first->txd.flags = flags; /* client is in control of this ack */
	first->len = len;

	return &first->txd;

dma_memcpy_err:
	dev_err(&chan->dev->device, "prep_dma_memcpy error: %p\n", amb_desc);
	ambdma_put_desc(amb_chan, first);

	return NULL;
}

static int ambarella_dma_probe(struct platform_device *pdev)
{
	struct ambdma_device *amb_dma;
	struct ambdma_chan *amb_chan;
	int i, ret = 0;

	/* alloc the amba dma engine struct */
	amb_dma = kzalloc(sizeof(*amb_dma), GFP_KERNEL);
	if (amb_dma == NULL) {
		ret = -ENOMEM;
		goto ambdma_dma_probe_exit;
	}

	amb_dma->dma_irq = platform_get_irq(pdev, 0);
	if (amb_dma->dma_irq < 0) {
		ret = -EINVAL;
		goto ambdma_dma_probe_exit1;
	}

	/* create a pool of consistent memory blocks for hardware descriptors */
	amb_dma->lli_pool = dma_pool_create("ambdma_lli_pool",
			&pdev->dev, sizeof(struct ambdma_lli), 16, 0);
	if (!amb_dma->lli_pool) {
		dev_err(&pdev->dev, "No memory for descriptors dma pool\n");
		ret = -ENOMEM;
		goto ambdma_dma_probe_exit1;
	}

	/* alloc dummy_lli and dummy_data for terminate usage. */
	amb_dma->dummy_lli = dma_pool_alloc(amb_dma->lli_pool,
			GFP_KERNEL, &amb_dma->dummy_lli_phys);
	if (amb_dma->dummy_lli == NULL) {
		ret = -ENOMEM;
		goto ambdma_dma_probe_exit2;
	}

	amb_dma->dummy_data = dma_pool_alloc(amb_dma->lli_pool,
			GFP_KERNEL, &amb_dma->dummy_data_phys);
	if (amb_dma->dummy_data == NULL) {
		ret = -ENOMEM;
		goto ambdma_dma_probe_exit3;
	}

	amb_dma->dummy_lli->attr = DMA_DESC_EOC | DMA_DESC_WM |
					DMA_DESC_NI | DMA_DESC_IE |
					DMA_DESC_ST | DMA_DESC_ID;
	amb_dma->dummy_lli->next_desc = amb_dma->dummy_lli_phys;
	amb_dma->dummy_lli->xfr_count = 0;
	amb_dma->dummy_lli->src = 0;
	amb_dma->dummy_lli->dst = amb_dma->dummy_data_phys;
	/* rpt_addr points to ambdma_lli->rpt field */
	amb_dma->dummy_lli->rpt_addr =
		amb_dma->dummy_lli_phys + sizeof(struct ambdma_lli) - 4;

	/* Init dma_device struct */
	dma_cap_zero(amb_dma->dma_slave.cap_mask);
	dma_cap_set(DMA_PRIVATE, amb_dma->dma_slave.cap_mask);
	dma_cap_set(DMA_SLAVE, amb_dma->dma_slave.cap_mask);
	dma_cap_set(DMA_CYCLIC, amb_dma->dma_slave.cap_mask);
	INIT_LIST_HEAD(&amb_dma->dma_slave.channels);
	amb_dma->dma_slave.device_alloc_chan_resources = ambdma_alloc_chan_resources;
	amb_dma->dma_slave.device_free_chan_resources = ambdma_free_chan_resources;
	amb_dma->dma_slave.device_tx_status = ambdma_tx_status;
	amb_dma->dma_slave.device_issue_pending = ambdma_issue_pending;
	amb_dma->dma_slave.device_prep_dma_cyclic = ambdma_prep_dma_cyclic;
	amb_dma->dma_slave.device_prep_slave_sg = ambdma_prep_slave_sg;
	amb_dma->dma_slave.device_control = ambdma_device_control;
	amb_dma->dma_slave.dev = &pdev->dev;

	dma_cap_zero(amb_dma->dma_memcpy.cap_mask);
	dma_cap_set(DMA_MEMCPY, amb_dma->dma_memcpy.cap_mask);
	INIT_LIST_HEAD(&amb_dma->dma_memcpy.channels);
	amb_dma->dma_memcpy.device_alloc_chan_resources = ambdma_alloc_chan_resources;
	amb_dma->dma_memcpy.device_free_chan_resources = ambdma_free_chan_resources;
	amb_dma->dma_memcpy.device_tx_status = ambdma_tx_status;
	amb_dma->dma_memcpy.device_issue_pending = ambdma_issue_pending;
	amb_dma->dma_memcpy.device_prep_dma_memcpy = ambdma_prep_dma_memcpy;
	amb_dma->dma_memcpy.dev = &pdev->dev;
	amb_dma->dma_memcpy.copy_align = 3; /* 2^3 = 8 bytes for copy alignment */

	/* init dma_chan struct */
	for (i = 0; i < NUM_DMA_CHANNELS; i++) {

		if (i == FIO_DMA_CHAN)
			continue;

		amb_chan = &amb_dma->amb_chan[i];

		spin_lock_init(&amb_chan->lock);
		amb_chan->amb_dma = amb_dma;
		amb_chan->id = i;
		amb_chan->status = AMBDMA_STATUS_IDLE;
		INIT_LIST_HEAD(&amb_chan->active_list);
		INIT_LIST_HEAD(&amb_chan->queue);
		INIT_LIST_HEAD(&amb_chan->free_list);
		INIT_LIST_HEAD(&amb_chan->stopping_list);

		tasklet_init(&amb_chan->tasklet, ambdma_tasklet,
				(unsigned long)amb_chan);

		amb_chan->chan.cookie = amb_chan->completed_cookie = 1;
		if (i == I2S_TX_DMA_CHAN || i == I2S_RX_DMA_CHAN) {
			amb_chan->chan.device = &amb_dma->dma_slave;
			list_add_tail(&amb_chan->chan.device_node,
					&amb_dma->dma_slave.channels);
		} else {
			amb_chan->chan.device = &amb_dma->dma_memcpy;
			list_add_tail(&amb_chan->chan.device_node,
					&amb_dma->dma_memcpy.channels);
		}
	}

	/* although FIOS DMA has its own driver, we also init FIOS DMA
	 * status here, orelse dummy FIOS DMA interrupts may occurred
	 * without its driver installed. */
	for (i = 0; i < NUM_DMA_CHANNELS; i++) {
		amba_writel(DMA_CHAN_STA_REG(i), 0);
		amba_writel(DMA_CHAN_CTR_REG(i),
			DMA_CHANX_CTR_WM | DMA_CHANX_CTR_RM | DMA_CHANX_CTR_NI);
	}

	ret = request_irq(amb_dma->dma_irq, ambdma_dma_irq_handler,
			IRQF_SHARED | IRQF_TRIGGER_HIGH,
			dev_name(&pdev->dev), amb_dma);
	if (ret)
		goto ambdma_dma_probe_exit4;

	ret = dma_async_device_register(&amb_dma->dma_slave);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to register slave DMA device: %d\n", ret);
		goto ambdma_dma_probe_exit5;
	}

	ret = dma_async_device_register(&amb_dma->dma_memcpy);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to register memcpy DMA device: %d\n", ret);
		goto ambdma_dma_probe_exit6;
	}

	platform_set_drvdata(pdev, amb_dma);

	dev_info(&pdev->dev, "Ambarella DMA Engine \n");

	return 0;

ambdma_dma_probe_exit6:
	dma_async_device_unregister(&amb_dma->dma_slave);

ambdma_dma_probe_exit5:
	free_irq(amb_dma->dma_irq, amb_dma);

ambdma_dma_probe_exit4:
	dma_pool_free(amb_dma->lli_pool, amb_dma->dummy_data,
			amb_dma->dummy_data_phys);

ambdma_dma_probe_exit3:
	dma_pool_free(amb_dma->lli_pool, amb_dma->dummy_lli,
			amb_dma->dummy_lli_phys);

ambdma_dma_probe_exit2:
	dma_pool_destroy(amb_dma->lli_pool);

ambdma_dma_probe_exit1:
	kfree(amb_dma);

ambdma_dma_probe_exit:
	return ret;
}

static int ambarella_dma_remove(struct platform_device *pdev)
{
	struct ambdma_device *amb_dma = platform_get_drvdata(pdev);
	struct ambdma_chan *amb_chan;
	int i;

	for (i = 0; i < NUM_DMA_CHANNELS; i++) {

		if (i == FIO_DMA_CHAN)
			continue;

		amb_chan = &amb_dma->amb_chan[i];
		ambdma_stop_channel(amb_chan);

		tasklet_disable(&amb_chan->tasklet);
		tasklet_kill(&amb_chan->tasklet);
	}

	dma_async_device_unregister(&amb_dma->dma_memcpy);
	dma_async_device_unregister(&amb_dma->dma_slave);

	free_irq(amb_dma->dma_irq, amb_dma);
	dma_pool_free(amb_dma->lli_pool, amb_dma->dummy_lli,
			amb_dma->dummy_lli_phys);
	dma_pool_free(amb_dma->lli_pool, amb_dma->dummy_data,
			amb_dma->dummy_data_phys);
	dma_pool_destroy(amb_dma->lli_pool);

	kfree(amb_dma);

	return 0;
}

static struct platform_driver ambarella_dma_driver = {
	.probe		= ambarella_dma_probe,
	.remove		= ambarella_dma_remove,

	.driver		= {
		.name	= "ambarella-dma",
		.owner	= THIS_MODULE,
	},
};

static int __init ambarella_dma_init(void)
{
	return platform_driver_register(&ambarella_dma_driver);
}

static void __exit ambarella_dma_exit(void)
{
	platform_driver_unregister(&ambarella_dma_driver);
}

subsys_initcall(ambarella_dma_init);
module_exit(ambarella_dma_exit);

MODULE_DESCRIPTION("Ambarella DMA Engine System Driver");
MODULE_AUTHOR("Jian He <jianhe@ambarella.com>");
MODULE_LICENSE("GPL");

