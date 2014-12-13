#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/err.h>
#include <linux/remoteproc.h>

#include <linux/kfifo.h>
#include <linux/delay.h>

#include <plat/ambcache.h>

typedef struct xnfs_info_s {

	char		*addr;
	int		size;
	int		count;
	void		*priv;
} xnfs_info_t;

struct xnfs_struct {

	int			id;
	struct kfifo		fifo;
	spinlock_t		lock;
	struct rpmsg_channel	*rpdev;
};

static struct xnfs_struct g_wnfs;
static struct xnfs_struct g_rnfs;

ssize_t xnfs_read(char *buf, size_t len)
{
	struct xnfs_struct *xnfs = &g_wnfs;
	xnfs_info_t info;
	int n = 0;
	char *va;
	int ret;

	ret = kfifo_out_locked(&xnfs->fifo, &info, sizeof(info), &xnfs->lock);
	if (ret < sizeof(info))
		return -EAGAIN;

	if (!info.size) {
		printk(KERN_DEBUG "rpmsg: got EOS from itron, and ack it back with NULL buffer\n");
		rpmsg_send(xnfs->rpdev, &n, sizeof(n));
		return 0;
	}

	va = (void *)ambarella_phys_to_virt((unsigned long)info.addr);
	n = info.size * info.count;
	printk(KERN_DEBUG "rpmsg: linux request: 0x%08x, got: 0x%08x\n", len, info.count);

	ambcache_flush_range((void *)va, n);
	memcpy(buf, va, n);

	/* ack to release fread of iTRON side */
	rpmsg_send(xnfs->rpdev, &n, sizeof(n));

	return n;
}

extern int output_callback(char **addr, int *size, int *count, void **priv);
extern void output_callback_done(void *);

static void rpmsg_rnfs_cb(struct rpmsg_channel *rpdev, void *data,
			  int len, void *priv)
{
	struct xnfs_struct *xnfs = priv;
	xnfs_info_t *req = data;
	xnfs_info_t avail;
	char *va;
	int n;

	if (!req->size) {
		printk(KERN_DEBUG "rpmsg: remote file stream closed\n");
		return;
	}
	printk(KERN_DEBUG "rpmsg: itron reuest: 0x%x\n", req->size * req->count);
	va = (void *)ambarella_phys_to_virt((unsigned long)req->addr);

	while (!output_callback(&avail.addr, &avail.size, &avail.count, &avail.priv)) {
		printk(KERN_DEBUG "rpmsg: waiting for kfifo\n");
		msleep(300);
	}

	n = avail.size * avail.count;
	if (n) {
		if (n > (req->size * req->count)) {
			printk(KERN_DEBUG "rpmsg: buffer size between Linux and itron mismatch!\n");
			n = req->size * req->count;
		}
		memcpy(va, avail.addr, n);
		output_callback_done(avail.priv);
		ambcache_clean_range((void *)va, n);
	}

	printk(KERN_DEBUG "rpmsg: return: 0x%x to itron\n", n);
	rpmsg_send(xnfs->rpdev, &n, sizeof(n));
}

static void rpmsg_wnfs_cb(struct rpmsg_channel *rpdev, void *data,
			  int len, void *priv)
{
	struct xnfs_struct *xnfs = priv;

	kfifo_in_locked(&xnfs->fifo, (char *)data, len, &xnfs->lock);
}

static void rpmsg_xnfs_cb(struct rpmsg_channel *rpdev, void *data,
			  int len, void *priv, u32 src)
{
	struct xnfs_struct *xnfs = priv;

	if (xnfs->id == 0)
		return rpmsg_rnfs_cb(rpdev, data, len, priv);
	else if (xnfs->id == 1)
		return rpmsg_wnfs_cb(rpdev, data, len, priv);
}

static int xnfs_init(struct xnfs_struct *xnfs)
{
	int ret;

	spin_lock_init(&xnfs->lock);

	ret = kfifo_alloc(&xnfs->fifo, 4096 * 16, GFP_KERNEL);
	if (ret)
		return ret;

	return 0;
}

static struct rpmsg_device_id rpmsg_xnfs_id_table[] = {
	{ .name = "rnfs_arm11", },
	{ .name	= "wnfs_arm11", },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_xnfs_id_table);

static int rpmsg_xnfs_probe(struct rpmsg_channel *rpdev)
{
	int ret = 0;
	struct rpmsg_ns_msg nsm;
	struct xnfs_struct *xnfs = NULL;

	nsm.addr = rpdev->dst;
	memcpy(nsm.name, rpdev->id.name, RPMSG_NAME_SIZE);
	nsm.flags = 0;

	if (!strcmp(rpdev->id.name, rpmsg_xnfs_id_table[0].name)) {
		xnfs = &g_rnfs;
		xnfs->id = 0;
		printk("RPMSG Ready from NFS Server [ARM11] is ready\n");

	} else if (!strcmp(rpdev->id.name, rpmsg_xnfs_id_table[1].name)) {
		xnfs = &g_wnfs;
		xnfs->id = 1;
		printk("RPMSG Write to NFS Server [ARM11] is ready\n");
	}

	xnfs_init(xnfs);
	xnfs->rpdev = rpdev;

	rpdev->ept->priv = xnfs;

	rpmsg_send(rpdev, &nsm, sizeof(nsm));

	return ret;
}

static void rpmsg_xnfs_remove(struct rpmsg_channel *rpdev)
{
}

struct rpmsg_driver rpmsg_xnfs_driver = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_xnfs_id_table,
	.probe		= rpmsg_xnfs_probe,
	.callback	= rpmsg_xnfs_cb,
	.remove		= rpmsg_xnfs_remove,
};
