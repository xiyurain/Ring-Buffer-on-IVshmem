/*
 * drivers/char/ringbuf_ivshmem.c - driver of ring buffer based on Inter-VM shared memory PCI device
 *
 * Xiangyu Ren <180110718@mail.hit.edu.cn>
 *
 * Based on kvm_ivshmem.c:
 *         Copyright 2009 Cam Macdonell
 * 
 */

#include <linux/kfifo.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pci_regs.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/spinlock_types.h>
#include <linux/spinlock.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Xiangyu Ren <180110718@mail.hit.edu.cn>");
MODULE_DESCRIPTION("ring buffer based on Inter-VM shared memory module");
MODULE_VERSION("1.0");

#define RINGBUF_SZ 512
#define RINGBUF_MSG_SZ sizeof(rbmsg_hd)
#define BUF_INFO_SZ sizeof(ringbuf_info)
#define TRUE 1
#define FALSE 0
#define RINGBUF_DEVICE_MINOR_NR 0
#define QEMU_PROCESS_ID 1
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define SLEEP_PERIOD_MSEC 10

#define IOCTL_MAGIC		('f')
#define IOCTL_RING		_IOW(IOCTL_MAGIC, 1, u32)
#define IOCTL_WAIT		_IO(IOCTL_MAGIC, 2)
#define IOCTL_IVPOSITION	_IOR(IOCTL_MAGIC, 3, u32)
#define IVPOSITION_REG_OFF	0x08
#define DOORBELL_REG_OFF	0x0c

static int ROLE = 1;
MODULE_PARM_DESC(ROLE, "Role of this ringbuf device.");
module_param(ROLE, int, 0400);

/* KVM Inter-VM shared memory device register offsets */
enum {
	IntrMask        = 0x00,    /* Interrupt Mask */
	IntrStatus      = 0x04,    /* Interrupt Status */
	IVPosition      = 0x08,    /* VM ID */
	Doorbell        = 0x0c,    /* Doorbell */
};

/* Consumer(reader) or Producer(writer) role of ring buffer*/
enum {
	Consumer	= 	0,
	Producer	=	1,
};

/*
 * message sent via ring buffer, as header of the payloads
*/
typedef struct ringbuf_msg_hd {
	unsigned int src_qid;

	unsigned int payload_off;
	ssize_t payload_len;
} rbmsg_hd;

typedef STRUCT_KFIFO(char, RINGBUF_SZ) fifo;

/*
 * @ivposition: device ID in IVshmem
 * @regaddr: physical address of shmem PCIe dev regs
 * @base_addr: mapped start address of IVshmem space
 * @bar#_addr/size: address or size of IVshmem BAR
 * @fifo_addr: address of the Kfifo struct
 * @payloads_st: start address of the payloads area
 * write_lock: multiple writer lock
*/

typedef struct ringbuf_device {
	struct pci_dev	*dev;
	int		minor;

	u8 		revision;
	unsigned int 	ivposition;

	void __iomem 	*regs_addr;
	void __iomem 	*base_addr;

	unsigned int 	bar0_addr;
	unsigned int 	bar0_size;
	unsigned int 	bar1_addr;
	unsigned int 	bar1_size;

	char            (*msix_names)[256];
	int             nvectors;

	unsigned int 	bar2_addr;
	unsigned int 	bar2_size;

	fifo*		fifo_addr;
	unsigned int 	bufsize;
	void __iomem	*payloads_st;
	spinlock_t	*write_lock;
	
	unsigned int 	role;
} ringbuf_device;


static int __init ringbuf_init(void);
static void __exit ringbuf_cleanup(void);
static int ringbuf_open(struct inode *, struct file *);
static int ringbuf_release(struct inode *, struct file *);
static ssize_t ringbuf_read(struct file *, char *, size_t, loff_t *);
static ssize_t ringbuf_write(struct file *, const char *, size_t, loff_t *);
static void ringbuf_remove_device(struct pci_dev* pdev);
static int ringbuf_probe_device(struct pci_dev *pdev,
				const struct pci_device_id * ent);
static long ringbuf_ioctl(struct file *fp, unsigned int cmd,  long unsigned int value);
static void ringbuf_poll(struct work_struct *work);
static void ringbuf_notify(unsigned int value);
static void ringbuf_readmsg(struct tasklet_struct* data);

static int event_toggle;
DECLARE_WAIT_QUEUE_HEAD(wait_queue);

DECLARE_TASKLET(read_msg_tasklet, ringbuf_readmsg);

static ringbuf_device ringbuf_dev;
static unsigned int payload_pt;
static int device_major_nr;


static const struct file_operations ringbuf_ops = {
	.owner		= 	THIS_MODULE,
	.open		= 	ringbuf_open,
	.read		= 	ringbuf_read,
	.write   	= 	ringbuf_write,
	.release 	= 	ringbuf_release,
	.unlocked_ioctl   = 	ringbuf_ioctl,
};

static struct pci_device_id ringbuf_id_table[] = {
	{ 0x1af4, 0x1110, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0 },
};
MODULE_DEVICE_TABLE(pci, ringbuf_id_table);

static struct pci_driver ringbuf_pci_driver = {
	.name		= 	"ringbuf",
	.id_table	= 	ringbuf_id_table,
	.probe	   	= 	ringbuf_probe_device,
	.remove	  	= 	ringbuf_remove_device,
};


static long ringbuf_ioctl(struct file *fp, unsigned int cmd,  long unsigned int value)
{
    	unsigned int ivposition;
    	unsigned int vector;

	ringbuf_device *dev = &ringbuf_dev;
    	BUG_ON(dev->base_addr == NULL);

    	switch (cmd) {
    	case IOCTL_RING:
        	vector = value & 0xffff;
        	ivposition = (value & 0xffff0000) >> 16;

        	writel(value & 0xffffffff, dev->regs_addr + DOORBELL_REG_OFF);
        break;

	case IOCTL_WAIT:
		printk(KERN_INFO "wait for interrupt\n");
		// todo: poll
		return 0;

	case IOCTL_IVPOSITION:
		printk(KERN_INFO "get ivposition: %u\n", dev->ivposition);
		return dev->ivposition;

	default:
		printk(KERN_INFO "bad ioctl command: %d\n", cmd);
		return -1;
	}

	return 0;
}

/* 
 * interrupt handler, to receive message
 */
static irqreturn_t ringbuf_interrupt (int irq, void *dev_instance)
{
	struct ringbuf_device * dev = dev_instance;

	if (unlikely(dev == NULL))
		return IRQ_NONE;

	// printk(KERN_INFO "RINGBUF: interrupt: %d\n", irq);
	tasklet_schedule(&read_msg_tasklet);

	return IRQ_HANDLED;
}

static int request_msix_vectors(struct ringbuf_device *dev, int n)
{
	int ret, irq_number, alloc_nums;
	unsigned int i;
	ret = -EINVAL;

	printk(KERN_INFO "request msi-x vectors: %d\n", n);
	dev->nvectors = n;

	dev->msix_names = kmalloc(n * sizeof(*dev->msix_names), GFP_KERNEL);
	if (dev->msix_names == NULL) {
		ret = -ENOMEM;
		goto error;
	}

	alloc_nums = pci_alloc_irq_vectors(dev->dev, 1, n, PCI_IRQ_MSIX);
	if(alloc_nums < 0) {
		printk(KERN_INFO "Fail to alloc pci MSI-X irq\n");
		goto free_names;
	}

	for (i = 0; i < alloc_nums; i++) {
		snprintf(dev->msix_names[i], sizeof(*dev->msix_names),
			"%s%d-%d", "ringbuf", dev->minor, i);

		irq_number = pci_irq_vector(dev->dev, i);
		ret = request_irq(irq_number, ringbuf_interrupt,
				IRQF_SHARED, dev->msix_names[i], dev);

		if (ret) {
			printk(KERN_ERR "unable to alloc irq for msixentry %d vec %d\n",
							i, irq_number);
			goto release_irqs;
		}

		printk(KERN_INFO "irq for msix entry: %d, vector: %d\n",
			i, irq_number);
	}

	return 0;

release_irqs:
    	pci_free_irq_vectors(dev->dev);

free_names:
    	kfree(dev->msix_names);

error:
    	return ret;
}

static void ringbuf_fifo_init(void) 
{
	fifo fifo_indevice;
	spinlock_t lock;

	printk(KERN_INFO "Check if the ring buffer is already init");
	if(kfifo_size(ringbuf_dev.fifo_addr) != RINGBUF_SZ) {
		printk(KERN_INFO "Start to init the ring buffer\n");

		memcpy(ringbuf_dev.fifo_addr, &fifo_indevice, 
					sizeof(fifo_indevice));
		INIT_KFIFO(*(ringbuf_dev.fifo_addr));

		memcpy(ringbuf_dev.write_lock, &lock, sizeof(lock));
		spin_lock_init(ringbuf_dev.write_lock);
	}

	printk(KERN_INFO "Check if the payloads area is already init");
	//TODO: init the payloads linklist
	payload_pt = 0;
}

static void free_msix_vectors(struct ringbuf_device *dev)
{
	pci_free_irq_vectors(dev->dev);
	kfree(dev->msix_names);
}

static void ringbuf_readmsg(struct tasklet_struct* data)
{
	char recv[512];

	ringbuf_read(NULL, recv, 512, 0);
	printk(KERN_INFO "recv msg: %s\n", recv);
}

static ssize_t ringbuf_read(struct file * filp, char * buffer, size_t len, 
							loff_t *offset)
{
	rbmsg_hd hd;
	unsigned int msgread_len;
	fifo *fifo_addr = ringbuf_dev.fifo_addr;

	/* if the device role is not Consumer, than not allowed to read */
	if(ringbuf_dev.role != Consumer) {
		printk(KERN_ERR "ringbuf: not allowed to read \n");
		return 0;
	}
	if(!ringbuf_dev.base_addr || !fifo_addr) {
		printk(KERN_ERR "ringbuf: cannot read from addr (NULL)\n");
		return 0;
	}

	if(kfifo_len(fifo_addr) < RINGBUF_MSG_SZ) {
		printk(KERN_ERR "no msg in ring buffer\n");
		return 0;
	}

	fifo_addr->kfifo.data = (void*)fifo_addr + 0x18;

	mb();

	msgread_len = kfifo_out(fifo_addr, (char*)&hd, RINGBUF_MSG_SZ);
	if(hd.src_qid != QEMU_PROCESS_ID) {
		printk(KERN_ERR "invalid ring buffer msg\n");
		goto err;
	}

	rmb();

	memcpy(buffer, ringbuf_dev.payloads_st + hd.payload_off, 
			MIN(len, hd.payload_len));
	return 0;

err:
	return -EFAULT;
}



static ssize_t ringbuf_write(struct file * filp, const char * buffer, 
					size_t len, loff_t *offset)
{
	rbmsg_hd hd;
	unsigned int msgsent_len;
	fifo* fifo_addr = ringbuf_dev.fifo_addr;

	if(ringbuf_dev.role != Producer) {
		printk(KERN_ERR "ringbuf: not allowed to write \n");
		return 0;
	}
	if(!ringbuf_dev.base_addr || !fifo_addr) {
		printk(KERN_ERR "ringbuf: cannot read from addr (NULL)\n");
		return 0;
	}
	if(kfifo_avail(fifo_addr) < RINGBUF_MSG_SZ) {
		printk(KERN_ERR "not enough space in ring buffer\n");
		return 0;
	}

	hd.src_qid = QEMU_PROCESS_ID;
	hd.payload_off = payload_pt;
	hd.payload_len = len;
	memcpy(ringbuf_dev.payloads_st + hd.payload_off, buffer, len);

	wmb();

	spin_lock(ringbuf_dev.write_lock);
	fifo_addr->kfifo.data = (void*)fifo_addr + 0x18;

	mb();

	msgsent_len = kfifo_in(fifo_addr, (char*)&hd, RINGBUF_MSG_SZ);
	spin_unlock(ringbuf_dev.write_lock);

	if(msgsent_len != RINGBUF_MSG_SZ) {
		printk(KERN_ERR "ring buffer msg incomplete! only %d sent\n", msgsent_len);
		goto err;
	}

	ringbuf_ioctl(NULL, IOCTL_RING, 1);
	payload_pt += len;
	return 0;

err:
	return -EFAULT;
}



static int ringbuf_open(struct inode * inode, struct file * filp)
{

	printk(KERN_INFO "Opening ringbuf device\n");

	if (MINOR(inode->i_rdev) != RINGBUF_DEVICE_MINOR_NR) {
		printk(KERN_INFO "minor number is %d\n", 
				RINGBUF_DEVICE_MINOR_NR);
		return -ENODEV;
	}
	filp->private_data = (void*)(&ringbuf_dev);
	ringbuf_dev.minor = RINGBUF_DEVICE_MINOR_NR;

   return 0;
}



static int ringbuf_release(struct inode * inode, struct file * filp)
{
	if(ringbuf_dev.fifo_addr != NULL) {
		//TODO: free the payloads linklist
		payload_pt = 0;
		printk(KERN_INFO "ring buffer is being freed");
		kfifo_free(ringbuf_dev.fifo_addr);
	}

	printk(KERN_INFO "release ringbuf_device\n");

   	return 0;
}


static int ringbuf_probe_device (struct pci_dev *pdev,
				const struct pci_device_id * ent) 
{

	int ret;
	struct ringbuf_device *dev = &ringbuf_dev;
	printk(KERN_INFO "probing for device\n");

	ret = pci_enable_device(pdev);
	if (ret < 0) {
		printk(KERN_INFO "unable to enable device: %d\n", ret);
		goto out;
	}

	ret = pci_request_regions(pdev, "ringbuf");
	if (ret < 0) {
		printk(KERN_INFO "unable to reserve resources: %d\n", ret);
		goto disable_device;
	}

	pci_read_config_byte(pdev, PCI_REVISION_ID, &(dev->revision));

	printk(KERN_INFO "device %d:%d, revision: %d\n", device_major_nr,
		dev->minor, dev->revision);

	/* Pysical address of BAR0, BAR1, BAR2 */
	dev->bar0_addr = pci_resource_start(pdev, 0);
	dev->bar0_size = pci_resource_len(pdev, 0);
	dev->bar1_addr = pci_resource_start(pdev, 1);
	dev->bar1_size = pci_resource_len(pdev, 1);
	dev->bar2_addr = pci_resource_start(pdev, 2);
	dev->bar2_size = pci_resource_len(pdev, 2);

	printk(KERN_INFO "BAR0: 0x%0x, %d\n", dev->bar0_addr,
		dev->bar0_size);
	printk(KERN_INFO "BAR1: 0x%0x, %d\n", dev->bar1_addr,
		dev->bar1_size);
	printk(KERN_INFO "BAR2: 0x%0x, %d\n", dev->bar2_addr,
		dev->bar2_size);

	dev->regs_addr = ioremap(dev->bar0_addr, dev->bar0_size);
	if (!dev->regs_addr) {
		printk(KERN_INFO "unable to ioremap bar0, sz: %d\n", 
						dev->bar0_size);
		goto release_regions;
	}

	dev->vec_tb = ioremap(dev->bar1_addr, dev->bar1_size);
	if (!dev->vec_tb) {
		printk(KERN_INFO "unable to ioremap bar1, sz: %d\n", 
						dev->bar1_size);
		goto release_regions;
	}

	dev->base_addr = ioremap(dev->bar2_addr, dev->bar2_size);
	if (!dev->base_addr) {
		printk(KERN_INFO "unable to ioremap bar2, sz: %d\n", 
						dev->bar2_size);
		goto iounmap_bar0;
	}
	printk(KERN_INFO "BAR1 map: %p\n", dev->base_addr);
	printk(KERN_INFO "BAR2 map: %p\n", dev->base_addr);

	ringbuf_dev.fifo_addr = (fifo*)ringbuf_dev.base_addr;
	ringbuf_dev.payloads_st = ringbuf_dev.base_addr 
					+ sizeof(fifo) + RINGBUF_SZ;	

	ringbuf_dev.write_lock =
		(spinlock_t *)(ringbuf_dev.base_addr + sizeof(fifo) + RINGBUF_SZ - 16);

	dev->dev = pdev;
	dev->role = ROLE;

	if (dev->revision == 1) {
		dev->ivposition = ioread32(
			dev->regs_addr + IVPOSITION_REG_OFF);

		printk(KERN_INFO "device ivposition: %u, MSI-X: %s\n", 
			dev->ivposition,
			(dev->ivposition == 0) ? "no": "yes");

		if (dev->ivposition != 0) {
			ret = request_msix_vectors(dev, 4);
			if (ret != 0) {
				goto destroy_device;
			}
		}
	}
	printk(KERN_INFO "device probed\n");

	ringbuf_fifo_init();

	return 0;

destroy_device:
    	dev->dev = NULL;
    	iounmap(dev->base_addr);

iounmap_bar0:
    	iounmap(dev->regs_addr);

release_regions:
    	pci_release_regions(pdev);

disable_device:
    	pci_disable_device(pdev);

out:
    	return ret;
}



static void ringbuf_remove_device(struct pci_dev* pdev)
{
	struct ringbuf_device *dev = &ringbuf_dev;

	printk(KERN_INFO "removing ivshmem device\n");

	free_msix_vectors(dev);

	dev->dev = NULL;

	iounmap(dev->base_addr);
	iounmap(dev->regs_addr);

	pci_release_regions(pdev);
	pci_disable_device(pdev);

	destroy_workqueue(poll_workqueue);
}



static void __exit ringbuf_cleanup(void)
{
	pci_unregister_driver(&ringbuf_pci_driver);
	unregister_chrdev(device_major_nr, "ringbuf");
}

static int __init ringbuf_init(void)
{
    	int err = -ENOMEM;

	err = register_chrdev(0, "ringbuf", &ringbuf_ops);
	if (err < 0) {
		printk(KERN_ERR "Unable to register ringbuf device\n");
		return err;
	}
	device_major_nr = err;
	printk("RINGBUF: Major device number is: %d\n", device_major_nr);

    	err = pci_register_driver(&ringbuf_pci_driver);
	if (err < 0) {
		goto error;
	}

	return 0;

error:
	unregister_chrdev(device_major_nr, "ringbuf");
	return err;
}

module_init(ringbuf_init);
module_exit(ringbuf_cleanup);