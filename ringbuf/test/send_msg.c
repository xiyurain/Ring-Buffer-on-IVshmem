#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/syscalls.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/jiffies.h>

extern unsigned long volatile jiffies;

#define IOCTL_MAGIC		('f')
#define IOCTL_RING		_IOW(IOCTL_MAGIC, 1, u32)
#define IOCTL_WAIT		_IO(IOCTL_MAGIC, 2)
#define IOCTL_IVPOSITION	_IOR(IOCTL_MAGIC, 3, u32)
 
int __init sendmsg_init(void)
{
        struct file *fp = NULL;
        loff_t pos = 0;
        int i, cyc = 20;
        long ivposition;
        char msg[256];

        fp = filp_open("/dev/ringbuf", O_RDWR, 0644);
        ivposition = fp->f_op->unlocked_ioctl(fp, IOCTL_IVPOSITION, 0);

        printk("send_message test case start.\n");
        for(i = 0; i < cyc; i++) {
                sprintf(msg, "MSG #%d   from peer%ld   (jiffies: %lu)", i, ivposition, jiffies);
                fp->f_op->write(fp, msg, strlen(msg) + 1, &pos);
                printk(KERN_INFO "msg sent: %s", msg);
                msleep(3000);
        }
        
        filp_close(fp, NULL);
        fp = NULL;
        return 0;
}

void __exit sendmsg_exit(void)
{
    printk("send_message test case exit/n");
}
 
module_init(sendmsg_init);
module_exit(sendmsg_exit);
 
MODULE_LICENSE("GPL");