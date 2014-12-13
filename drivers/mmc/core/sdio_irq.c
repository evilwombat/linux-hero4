/*
 * linux/drivers/mmc/core/sdio_irq.c
 *
 * Author:      Nicolas Pitre
 * Created:     June 18, 2007
 * Copyright:   MontaVista Software Inc.
 *
 * Copyright 2008 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/export.h>
#include <linux/wait.h>
#include <linux/delay.h>

#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>

#include "sdio_ops.h"

static int process_sdio_pending_irqs(struct mmc_host *host)
{
	struct mmc_card *card = host->card;
	int i, ret, count;
#if !(defined(CONFIG_TI_WL18XX))
	unsigned char pending;
#endif
	struct sdio_func *func;

	/*
	 * Optimization, if there is only 1 function interrupt registered
	 * and we know an IRQ was signaled then call irq handler directly.
	 * Otherwise do the full probe.
	 */
	func = card->sdio_single_irq;
	if (func && host->sdio_irq_pending) {
		func->irq_handler(func);
		return 1;
	}

#if defined(CONFIG_TI_WL18XX)
	card->pending_int = 0;
	ret = mmc_io_rw_direct(card, 0, 0, SDIO_CCCR_INTx, 0,
		&card->pending_int);
#else
	ret = mmc_io_rw_direct(card, 0, 0, SDIO_CCCR_INTx, 0, &pending);
#endif
	if (ret) {
		pr_debug("%s: error %d reading SDIO_CCCR_INTx\n",
		       mmc_card_id(card), ret);
		return ret;
	}

	count = 0;
	for (i = 1; i <= 7; i++) {
#if defined(CONFIG_TI_WL18XX)
		if (card->pending_int & (1 << i)) {
#else
		if (pending & (1 << i)) {
#endif
			func = card->sdio_func[i - 1];
			if (!func) {
				pr_warning("%s: pending IRQ for "
					"non-existent function\n",
					mmc_card_id(card));
				ret = -EINVAL;
			} else if (func->irq_handler) {
				func->irq_handler(func);
				count++;
#if defined(CONFIG_TI_WL18XX)
			} else if (!func->irq_handler_ll) {
#else
			} else {
#endif
				pr_warning("%s: pending IRQ with no handler\n",
				       sdio_func_id(func));
				ret = -EINVAL;
			}
		}
	}

	if (count)
		return count;

	return ret;
}

#if defined(CONFIG_TI_WL18XX)
static void call_sdio_lockless_irqs(struct mmc_card *card)
{
	int i;

	for (i = 1; i <= 7; i++) {
		if (card->pending_int & (1 << i)) {
			struct sdio_func *func = card->sdio_func[i - 1];
			if (func && func->irq_handler_ll) {
				func->irq_handler_ll(func);
			}
		}
	}
}
#endif

static int sdio_irq_thread(void *_host)
{
	struct mmc_host *host = _host;
	struct sched_param param = { .sched_priority = 1 };
	unsigned long period, idle_period;
#if defined(CONFIG_TI_WL18XX)
	unsigned long flags;
#endif
	int ret;

	sched_setscheduler(current, SCHED_FIFO, &param);

	/*
	 * We want to allow for SDIO cards to work even on non SDIO
	 * aware hosts.  One thing that non SDIO host cannot do is
	 * asynchronous notification of pending SDIO card interrupts
	 * hence we poll for them in that case.
	 */
	idle_period = msecs_to_jiffies(10);
	period = (host->caps & MMC_CAP_SDIO_IRQ) ?
		MAX_SCHEDULE_TIMEOUT : idle_period;

	pr_debug("%s: IRQ thread started (poll period = %lu jiffies)\n",
		 mmc_hostname(host), period);

	do {
		/*
		 * We claim the host here on drivers behalf for a couple
		 * reasons:
		 *
		 * 1) it is already needed to retrieve the CCCR_INTx;
		 * 2) we want the driver(s) to clear the IRQ condition ASAP;
		 * 3) we need to control the abort condition locally.
		 *
		 * Just like traditional hard IRQ handlers, we expect SDIO
		 * IRQ handlers to be quick and to the point, so that the
		 * holding of the host lock does not cover too much work
		 * that doesn't require that lock to be held.
		 */
		ret = __mmc_claim_host(host, &host->sdio_irq_thread_abort);
		if (ret)
			break;
		ret = process_sdio_pending_irqs(host);
		host->sdio_irq_pending = false;
		mmc_release_host(host);
#if defined(CONFIG_TI_WL18XX)
		call_sdio_lockless_irqs(host->card);
#endif

		/*
		 * Give other threads a chance to run in the presence of
		 * errors.
		 */
		if (ret < 0) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (!kthread_should_stop())
				schedule_timeout(HZ);
			set_current_state(TASK_RUNNING);
		}

#if defined(CONFIG_TI_WL18XX)
		host->sdio_irq_running = false;
		spin_lock_irqsave(&host->sdio_irq_running_lock, flags);
		wake_up_all(&host->sdio_wq);
		spin_unlock_irqrestore(&host->sdio_irq_running_lock, flags);
#endif
		/*
		 * Adaptive polling frequency based on the assumption
		 * that an interrupt will be closely followed by more.
		 * This has a substantial benefit for network devices.
		 */
		if (!(host->caps & MMC_CAP_SDIO_IRQ)) {
			if (ret > 0)
				period /= 2;
			else {
				period++;
				if (period > idle_period)
					period = idle_period;
			}
		}

		set_current_state(TASK_INTERRUPTIBLE);
		if (host->caps & MMC_CAP_SDIO_IRQ) {
#if defined(CONFIG_TI_WL18XX)
			spin_lock_irqsave(&host->sdio_irq_running_lock, flags);
			host->sdio_irq_running = false;
			wake_up_all(&host->sdio_wq);
			spin_unlock_irqrestore(&host->sdio_irq_running_lock, flags);
#endif

			mmc_host_clk_hold(host);
			host->ops->enable_sdio_irq(host, 1);
			mmc_host_clk_release(host);
		}
		if (!kthread_should_stop())
			schedule_timeout(period);
		set_current_state(TASK_RUNNING);
	} while (!kthread_should_stop());

	if (host->caps & MMC_CAP_SDIO_IRQ) {
		mmc_host_clk_hold(host);
		host->ops->enable_sdio_irq(host, 0);
		mmc_host_clk_release(host);

#if defined(CONFIG_TI_WL18XX)
		spin_lock_irqsave(&host->sdio_irq_running_lock, flags);
		host->sdio_irq_running = false;
		wake_up_all(&host->sdio_wq);
		spin_unlock_irqrestore(&host->sdio_irq_running_lock, flags);
#endif
	}

	pr_debug("%s: IRQ thread exiting with code %d\n",
		 mmc_hostname(host), ret);

	return ret;
}

static int sdio_card_irq_get(struct mmc_card *card)
{
	struct mmc_host *host = card->host;

	WARN_ON(!host->claimed);

	if (!host->sdio_irqs++) {
		atomic_set(&host->sdio_irq_thread_abort, 0);
		host->sdio_irq_thread =
			kthread_run(sdio_irq_thread, host, "ksdioirqd/%s",
				mmc_hostname(host));
		if (IS_ERR(host->sdio_irq_thread)) {
			int err = PTR_ERR(host->sdio_irq_thread);
			host->sdio_irqs--;
			return err;
		}
	}

	return 0;
}

static int sdio_card_irq_put(struct mmc_card *card)
{
	struct mmc_host *host = card->host;

	WARN_ON(!host->claimed);
	BUG_ON(host->sdio_irqs < 1);

	if (!--host->sdio_irqs) {
		atomic_set(&host->sdio_irq_thread_abort, 1);
		kthread_stop(host->sdio_irq_thread);
	}

	return 0;
}

/* If there is only 1 function registered set sdio_single_irq */
static void sdio_single_irq_set(struct mmc_card *card)
{
	struct sdio_func *func;
	int i;

	card->sdio_single_irq = NULL;
	if ((card->host->caps & MMC_CAP_SDIO_IRQ) &&
	    card->host->sdio_irqs == 1)
		for (i = 0; i < card->sdio_funcs; i++) {
		       func = card->sdio_func[i];
		       if (func && func->irq_handler) {
			       card->sdio_single_irq = func;
			       break;
		       }
	       }
}

/**
 *	sdio_claim_irq - claim the IRQ for a SDIO function
 *	@func: SDIO function
 *	@handler: IRQ handler callback
 *
 *	Claim and activate the IRQ for the given SDIO function. The provided
 *	handler will be called when that IRQ is asserted.  The host is always
 *	claimed already when the handler is called so the handler must not
 *	call sdio_claim_host() nor sdio_release_host().
 */
int sdio_claim_irq(struct sdio_func *func, sdio_irq_handler_t *handler)
{
	int ret;
	unsigned char reg;

	BUG_ON(!func);
	BUG_ON(!func->card);

	pr_debug("SDIO: Enabling IRQ for %s...\n", sdio_func_id(func));

	if (func->irq_handler) {
		pr_debug("SDIO: IRQ for %s already in use.\n", sdio_func_id(func));
		return -EBUSY;
	}

	ret = mmc_io_rw_direct(func->card, 0, 0, SDIO_CCCR_IENx, 0, &reg);
	if (ret)
		return ret;

	reg |= 1 << func->num;

	reg |= 1; /* Master interrupt enable */

	ret = mmc_io_rw_direct(func->card, 1, 0, SDIO_CCCR_IENx, reg, NULL);
	if (ret)
		return ret;

	func->irq_handler = handler;
	ret = sdio_card_irq_get(func->card);
	if (ret)
		func->irq_handler = NULL;
	sdio_single_irq_set(func->card);

	return ret;
}
EXPORT_SYMBOL_GPL(sdio_claim_irq);

#if defined(CONFIG_TI_WL18XX)
/**
 *	sdio_claim_irq_lockless - claim the IRQ for a SDIO function
 *	@func: SDIO function
 *	@handler: IRQ handler callback
 *
 *	Claim and activate the IRQ for the given SDIO function. The provided
 *	handler will be called when that IRQ is asserted. The host is not
 *	claimed when the handler is called so the handler might need to call
 *	sdio_claim_host() and sdio_release_host().
 */
int sdio_claim_irq_lockless(struct sdio_func *func, sdio_irq_handler_t *handler)
{
	int ret;

	if (func->irq_handler_ll) {
		pr_debug("SDIO: IRQ for %s already in use.\n", sdio_func_id(func));
		return -EBUSY;
	}

	func->irq_handler_ll = handler;
	ret = sdio_claim_irq(func, NULL);
	if (ret)
		func->irq_handler_ll = NULL;

	return ret;
}
EXPORT_SYMBOL_GPL(sdio_claim_irq_lockless);
#endif

/**
 *	sdio_release_irq - release the IRQ for a SDIO function
 *	@func: SDIO function
 *
 *	Disable and release the IRQ for the given SDIO function.
 */
int sdio_release_irq(struct sdio_func *func)
{
	int ret;
	unsigned char reg;

	BUG_ON(!func);
	BUG_ON(!func->card);

	pr_debug("SDIO: Disabling IRQ for %s...\n", sdio_func_id(func));

#if defined(CONFIG_TI_WL18XX)
	if (func->irq_handler || func->irq_handler_ll) {
#else
	if (func->irq_handler) {
#endif
		func->irq_handler = NULL;
#if defined(CONFIG_TI_WL18XX)
		func->irq_handler_ll = NULL;
#endif
		sdio_card_irq_put(func->card);
		sdio_single_irq_set(func->card);
	}

	ret = mmc_io_rw_direct(func->card, 0, 0, SDIO_CCCR_IENx, 0, &reg);
	if (ret)
		return ret;

	reg &= ~(1 << func->num);

	/* Disable master interrupt with the last function interrupt */
	if (!(reg & 0xFE))
		reg = 0;

	ret = mmc_io_rw_direct(func->card, 1, 0, SDIO_CCCR_IENx, reg, NULL);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(sdio_release_irq);

#if defined(CONFIG_TI_WL18XX)
int sdio_enable_irq(struct sdio_func *func)
{
	int ret;
	unsigned char reg;

	BUG_ON(!func);
	BUG_ON(!func->card);

	pr_debug("SDIO: Enabling IRQ for %s...\n", sdio_func_id(func));

	ret = mmc_io_rw_direct(func->card, 0, 0, SDIO_CCCR_IENx, 0, &reg);
	if (ret)
		return ret;

	reg |= 1 << func->num;

	reg |= 1; /* Master interrupt enable */

	ret = mmc_io_rw_direct(func->card, 1, 0, SDIO_CCCR_IENx, reg, NULL);
	if (ret)
		return ret;

	/* TODO: add async stuff */

	return ret;
}
EXPORT_SYMBOL_GPL(sdio_enable_irq);

int sdio_disable_irq(struct sdio_func *func)
{
	int ret;
	unsigned char reg;

	BUG_ON(!func);
	BUG_ON(!func->card);

	pr_debug("SDIO: Disabling IRQ for %s...\n", sdio_func_id(func));

	ret = mmc_io_rw_direct(func->card, 0, 0, SDIO_CCCR_IENx, 0, &reg);
	if (ret)
		return ret;

	reg &= ~(1 << func->num);

	/* Disable master interrupt with the last function interrupt */
	if (!(reg & 0xFE))
		reg = 0;

	ret = mmc_io_rw_direct(func->card, 1, 0, SDIO_CCCR_IENx, reg, NULL);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(sdio_disable_irq);

static bool sdio_irq_running(struct mmc_host *host)
{
	unsigned long flags;
	bool running;

	spin_lock_irqsave(&host->sdio_irq_running_lock, flags);
	running = host->sdio_irq_running;
	spin_unlock_irqrestore(&host->sdio_irq_running_lock, flags);

	return running;
}

int sdio_flush_irq(struct sdio_func *func)
{
	struct mmc_host *host;

	BUG_ON(!func);
	BUG_ON(!func->card);

	host = func->card->host;

	while (sdio_irq_running(host)) {
		DEFINE_WAIT(wait);

		prepare_to_wait(&host->sdio_wq, &wait, TASK_UNINTERRUPTIBLE);
		if (sdio_irq_running(host))
			schedule();

		/*
		 * we should check the condition again, but this might be
		 * a problem as a new irq might have come in the meantime
		 */
		finish_wait(&host->sdio_wq, &wait);

		break;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(sdio_flush_irq);
#endif

