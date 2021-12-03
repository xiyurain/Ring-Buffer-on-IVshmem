#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/syscalls.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
 
int __init sendmsg_init(void)
{
        struct file *fp = NULL;
        loff_t pos = 0;
        int i, cyc = 20;
        char msg = ""

        fp = filp_open("/dev/ringbuf", O_RDWR, 0644);

        printk("send_file test case start.\n");
        testfp = filp_open("../payload/uoe.txt", O_RDONLY, 0644);


        fp->f_op->write(fp, , , &pos);
        
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