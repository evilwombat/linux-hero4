/*
 * arch/arm/plat-ambarella/misc/aipc_slock.c
 *
 * Authors:
 *	Joey Li <jli@ambarella.com>
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
 * Copyright (C) 2013-2015, Ambarella Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <asm/uaccess.h>

#include <mach/hardware.h>
#include <plat/ambalink_cfg.h>
#include <linux/aipc/ipc_slock.h>

typedef struct {
	unsigned long lock;
	char          padding[4];
} aspinlock_t;

typedef struct {
	int size;
	aspinlock_t *lock;
} aspinlock_db;

static aspinlock_db lock_set;

static int procfs_spinlock_read(char *page, char **start,
				off_t off, int count, int *eof, void *data)
{
	int len;

	len = scnprintf(page+off, count,
			"\n"
			"usage: echo id [t] > /proc/ambarella/spinlock\n"
			"    id is the spinlock id\n"
			"    t is the duration(ms) which spinlock is locked. Default is 0\n"
			"\n");
	if (eof)
		*eof = 1;

	return len;
}

static int procfs_spinlock_write(struct file *file,
				const char __user *buffer, unsigned long count, void *data)
{
	char str[128];
	int  id = 0, delay = 0;
	unsigned long flags;

	//memset(str, 0, sizeof(str));
	if (copy_from_user(str, buffer, count))
	{
		printk(KERN_ERR "copy_from_user failed, aborting\n");
		return -EFAULT;
	}
	sscanf(str, "%d %d", &id, &delay);
	printk("try spinlock %d, holding for %d ms\n", id, delay);

	aipc_spin_lock_irqsave(id, &flags);
	mdelay(delay);
	aipc_spin_unlock_irqrestore(id, flags);

	printk("done\n");
	return count;
}

static void init_procfs(void)
{
	struct proc_dir_entry *aipc_dir;
	struct proc_dir_entry *aipc_dev;

	aipc_dir = get_ambarella_proc_dir();
	aipc_dev = create_proc_entry("spinlock", S_IRUGO | S_IWUSR, aipc_dir);
	aipc_dev->write_proc = procfs_spinlock_write;
	aipc_dev->read_proc = procfs_spinlock_read;
}

int aipc_spin_lock_setup(u32 addr)
{
#ifdef CONFIG_PLAT_AMBARELLA_BOSS
	lock_set.lock = (aspinlock_t*) addr;
#else
	lock_set.lock = (aspinlock_t*) ambarella_phys_to_virt(AIPC_SLOCK_ADDR);
#endif
	lock_set.size = AIPC_SLOCK_SIZE / sizeof(aspinlock_t);

	/* Reserve one spinlock space for BCH NAND controller workaround. */
	lock_set.size -= 1;
	//memset(lock_set.lock, 0, lock_set.size);

	printk("%s done\n", __func__);

	return 0;
}
EXPORT_SYMBOL(aipc_spin_lock_setup);

static int aipc_spin_lock_init(void)
{
#if !defined(CONFIG_PLAT_AMBARELLA_BOSS)
	/* Call aipc_spin_lock_setup from atag.c eraly_boss() if BOSS. */
	aipc_spin_lock_setup(AIPC_SLOCK_ADDR);
#endif
	init_procfs();

	return 0;
}

static void aipc_spin_lock_exit(void)
{
}

void __aipc_spin_lock(unsigned long *lock)
{
	unsigned int tmp;

	//spin to get the lock
	__asm__ __volatile__(
		"1:	ldrex	%0, [%1]\n"
		"	teq	%0, #0\n"
		"	strexeq	%0, %2, [%1]\n"
		"	teqeq	%0, #0\n"
		"	bne	1b"
		: "=&r" (tmp)
		: "r" (lock), "r" (1)
		: "cc");

	// set memory barrier here
	dmb();
}

void __aipc_spin_unlock(unsigned long *lock)
{
	dmb();

	// release the lock
	__asm__ __volatile__(
		"	str	%1, [%0]\n"
		:
		: "r" (lock), "r" (0)
		: "cc");
}

void __aipc_spin_lock_irqsave(unsigned long *lock, unsigned long *flags)
{
	unsigned int tmp;

	local_irq_save(*flags);
	preempt_disable();

	//spin to get the lock
	__asm__ __volatile__(
		"1:	ldrex	%0, [%1]\n"
		"	teq	%0, #0\n"
		"	strexeq	%0, %2, [%1]\n"
		"	teqeq	%0, #0\n"
		"	bne	1b"
		: "=&r" (tmp)
		: "r" (lock), "r" (1)
		: "cc");

	// set memory barrier here
	dmb();
}

void __aipc_spin_unlock_irqrestore(unsigned long *lock, unsigned long flags)
{
	dmb();

	// release the lock
	__asm__ __volatile__(
		"	str	%1, [%0]\n"
		:
		: "r" (lock), "r" (0)
		: "cc");

	preempt_enable();
	local_irq_restore(flags);
}

void aipc_spin_lock(int id)
{
	if (id < 0 || id >= lock_set.size)
	{
		printk(KERN_ERR "%s: invalid id %d\n", __FUNCTION__, id);
		return;
	}
	__aipc_spin_lock(&lock_set.lock[id].lock);
}

void aipc_spin_unlock(int id)
{
	if (id < 0 || id >= lock_set.size)
	{
		printk(KERN_ERR "%s: invalid id %d\n", __FUNCTION__, id);
		return;
	}
	__aipc_spin_unlock(&lock_set.lock[id].lock);
}

void aipc_spin_lock_irqsave(int id, unsigned long *flags)
{
	if (id < 0 || id >= lock_set.size)
	{
		printk(KERN_ERR "%s: invalid id %d\n", __FUNCTION__, id);
		return;
	}
	__aipc_spin_lock_irqsave(&lock_set.lock[id].lock, flags);
}

void aipc_spin_unlock_irqrestore(int id, unsigned long flags)
{
	if (id < 0 || id >= lock_set.size)
	{
		printk(KERN_ERR "%s: invalid id %d\n", __FUNCTION__, id);
		return;
	}
	__aipc_spin_unlock_irqrestore(&lock_set.lock[id].lock, flags);
}

subsys_initcall(aipc_spin_lock_init);
module_exit(aipc_spin_lock_exit);
MODULE_DESCRIPTION("Ambarella IPC spinlock");
