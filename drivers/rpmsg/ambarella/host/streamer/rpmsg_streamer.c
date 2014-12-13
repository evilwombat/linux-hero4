/*
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/err.h>
#include <linux/remoteproc.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>

#define VIDEO_MINOR 0
#define AUDIO_MINOR 1

#define MAX_FRAME_SIZE             8
#define GET_FRAME_INFO(dev, index) &(dev)->frames[(index)&(MAX_FRAME_SIZE-1)]

#define STREAM_VIDEO_INPUT   1
#define STREAM_VIDEO_OUTPUT  2

#if 1
#define LOCK(dev)            mutex_lock(&dev->lock)
#define UNLOCK(dev)          mutex_unlock(&dev->lock)
#else
#define LOCK(dev)
#define UNLOCK(dev)
#endif

struct stream_info {
    u32 addr;
    u32 size;
};

struct stream_video_dev {
    unsigned int          flag;
    struct stream_info    frames[MAX_FRAME_SIZE];
    int                   write_index;
    int                   read_index;
    int                   offset;
    struct semaphore      frame_sem;
    struct mutex          lock;
    struct cdev           cdev;
};

static int stream_major;

static struct stream_video_dev stream_video_device;

static int video_open(struct inode *inode, struct file *filp)
{
    struct stream_video_dev *dev;
    dev = container_of(inode->i_cdev, struct stream_video_dev, cdev);
    filp->private_data = dev;
    
    if (!(dev->flag & STREAM_VIDEO_INPUT))
    {
        printk("\n!!!!!!!!!!!!ERROR!!!!!!!!!!! no input stream yet.\n");
        return -1;
    }

    LOCK(dev);
    dev->write_index = 0;
    dev->read_index = -1;
    dev->offset = 0;
    sema_init(&dev->frame_sem, 0);
    UNLOCK(dev);
    printk("video_open, minor is %d\n", iminor(inode));
    return 0;
}

static int video_release(struct inode *inode, struct file *filp)
{
    struct stream_video_dev *dev = (struct stream_video_dev*)filp->private_data;

    LOCK(dev);
    dev->flag &= ~STREAM_VIDEO_OUTPUT;
    UNLOCK(dev);
    printk("video_release\n");
    return 0;
}

static ssize_t video_read(struct file *filp, char __user *buf, size_t count, 
                          loff_t *f_pos)
{
    struct stream_video_dev *dev = (struct stream_video_dev*)filp->private_data;
    struct stream_info *info;
    int bytes_to_copy, ret;

    dev->flag |= STREAM_VIDEO_OUTPUT;
    
    if (!dev->offset) 
    {
        // wait for the next frame
        down(&dev->frame_sem);
        dev->read_index++;
        //printk("get frame %d\n", dev->read_index);
    }

    info = GET_FRAME_INFO(dev, dev->read_index);
    bytes_to_copy = info->size - dev->offset;
    if (bytes_to_copy > count)
        bytes_to_copy = count;
    ret = copy_to_user(buf, (void*)info->addr+dev->offset, bytes_to_copy);
    dev->offset += bytes_to_copy;

    // reset offset if a frame is completely copied
    if (dev->offset == info->size) 
    {
        dev->offset = 0;
        printk(" <--- frame %d, bytes %d\n", dev->read_index, info->size);
    }

    *f_pos += bytes_to_copy;
    return bytes_to_copy;
}

static struct file_operations video_fops = {
    .owner      = THIS_MODULE,
    .read       = video_read,
    .open       = video_open,
    .release    = video_release
};

static void setup_video_dev(struct stream_video_dev *dev, int major)
{
    int err;

    cdev_init(&dev->cdev, &video_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &video_fops;
    mutex_init(&dev->lock);
    err = cdev_add(&dev->cdev, MKDEV(major, VIDEO_MINOR), 1);
    if (err)
        printk(KERN_ERR "Error %d adding stream video device\n", err); 
}    

static void setup_stream_dev(void)
{
    dev_t dev = 0;

    alloc_chrdev_region(&dev, 0, 1, "amba_streaming");
    stream_major = MAJOR(dev);
    setup_video_dev(&stream_video_device, stream_major);
}

static void rpmsg_streamer_cb(struct rpmsg_channel *rpdev, void *data, int len,
			void *priv, u32 src)
{
    struct stream_video_dev *dev = &stream_video_device;
    struct stream_info *in = (struct stream_info*)data;
    struct stream_info *out;

    dev->flag |= STREAM_VIDEO_INPUT;

    if (!(dev->flag & STREAM_VIDEO_OUTPUT))
    {
        printk("no client yet, skip\n");
        return;
    }

    if (dev->write_index - dev->read_index >= MAX_FRAME_SIZE)
    {
        printk("streamer buf queue is full, skip.\n");
        return;
    }

    LOCK(dev);
    out = GET_FRAME_INFO(dev, dev->write_index);
    out->size = in->size;
    out->addr = ambarella_phys_to_virt(in->addr);
    dev->write_index++;
    up(&dev->frame_sem);
    UNLOCK(dev);

	printk("--->  frame %d, bytes %d\n",  dev->write_index-1, out->size);
}

static int rpmsg_streamer_probe(struct rpmsg_channel *rpdev)
{
	int ret = 0;
	struct rpmsg_ns_msg nsm;

	nsm.addr = rpdev->dst;
	memcpy(nsm.name, rpdev->id.name, RPMSG_NAME_SIZE);
	nsm.flags = 0;

	rpmsg_send(rpdev, &nsm, sizeof(nsm));

	return ret;
}

static void rpmsg_streamer_remove(struct rpmsg_channel *rpdev)
{
}

static struct rpmsg_device_id rpmsg_streamer_id_table[] = {
	{ .name	= "streamer_cortex", },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_streamer_id_table);

static struct rpmsg_driver rpmsg_streamer_driver = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_streamer_id_table,
	.probe		= rpmsg_streamer_probe,
	.callback	= rpmsg_streamer_cb,
	.remove		= rpmsg_streamer_remove,
};

static int __init rpmsg_streamer_init(void)
{
    setup_stream_dev();
	return register_rpmsg_driver(&rpmsg_streamer_driver);
}

static void __exit rpmsg_streamer_fini(void)
{
	unregister_rpmsg_driver(&rpmsg_streamer_driver);
}

module_init(rpmsg_streamer_init);
module_exit(rpmsg_streamer_fini);

MODULE_DESCRIPTION("RPMSG Streamer Server");
