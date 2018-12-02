/*
 * ker_r_w.c: read/write at the kernel mode
 * to read/write registers, cp0 registers
 */

#include <linux/autoconf.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/time.h>
#include <linux/mman.h>


#include <linux/timer.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/bitops.h>            /* for generic_ffs */
#include <linux/fs.h>			/* everything... */
#include <linux/errno.h>		/* error codes */
#include <linux/types.h>		/* size_t */
#include <linux/pci.h>

#include <asm/page.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/system.h>

#define KERRW_IOC_READ      0
#define KERRW_IOC_WRITE     1


#define KERRW_IOC_READ16      10
#define KERRW_IOC_WRITE16     11

#define KERRW_IOC_READ8      20
#define KERRW_IOC_WRITE8     21

#define KERRW_IOC_DIRECTREAD8   70
#define KERRW_IOC_DIRECTWRITE8  71

#define KERRW_IOC_READ_CP0  2
#define KERRW_IOC_WRITE_CP0 3

#define KERRW_IOC_READ_PCICONF_8    30
#define KERRW_IOC_WRITE_PCICONF_8   31
#define KERRW_IOC_READ_PCICONF_16   40
#define KERRW_IOC_WRITE_PCICONF_16  41
#define KERRW_IOC_READ_PCICONF_32   50
#define KERRW_IOC_WRITE_PCICONF_32  51

#define KERRW_IOC_READ_PCIBASE      60
#define KERRW_IOC_WRITE_PCIBASE     61


#define KER_R_W_MAJOR       109

static int ker_r_w_open (struct inode *inode, struct file *filp)
{
    return 0;
}

static int ker_r_w_close (struct inode *inode, struct file *filp)
{
    return 0;
}

static int ker_r_w_ioctl (struct inode *inode, struct file *filp,unsigned int cmd, unsigned long arg)
{
    unsigned int adwParams[2];
    unsigned int mapaddr;
    volatile unsigned int *pdwAddr = NULL;
    volatile unsigned short *pwAddr = NULL;
    volatile unsigned char *pucAddr = NULL;

    if (copy_from_user(adwParams, (void *)arg, sizeof(adwParams)))
    {
        return -EFAULT;
    }

    mapaddr = ioremap(adwParams[0], 4);

    pdwAddr = (volatile unsigned int*)mapaddr;
    pwAddr = (volatile unsigned short*)mapaddr;
    pucAddr = (volatile unsigned char*)mapaddr;
      
    switch(cmd) 
    {
        case KERRW_IOC_READ:
        {
            adwParams[1] = readl(pdwAddr);
            if (copy_to_user((void *)(arg + 4), &adwParams[1], sizeof(adwParams[1])))
            {
                return -EFAULT;
            }            
            break; 
        }

        case KERRW_IOC_WRITE:
        {
            writel(adwParams[1], pdwAddr);
            break; 
        }

        case KERRW_IOC_READ16:
        {
            adwParams[1] = readw(pwAddr);
            if (copy_to_user((void *)(arg + 4), &adwParams[1], sizeof(adwParams[1])))
            {
                return -EFAULT;
            }
            break;
        }

        case KERRW_IOC_WRITE16:
        {
            writew((unsigned short)adwParams[1], pwAddr);
            break;
        }
        
        case KERRW_IOC_READ8:
        {
            adwParams[1] = readb(pucAddr);
            if (copy_to_user((void *)(arg + 4), &adwParams[1], sizeof(adwParams[1])))
            {
                return -EFAULT;
            }
            break;
        }

        case KERRW_IOC_WRITE8:
        {
            writeb((unsigned char)adwParams[1], pucAddr);
            msleep(50);
            printk("val read out = 0x%x\n", readb(pucAddr));
            break;
        }
        
        
        case KERRW_IOC_DIRECTREAD8:
        {
            adwParams[1] = readb(adwParams[0]);
            if (copy_to_user((void *)(arg + 4), &adwParams[1], sizeof(adwParams[1])))
            {
                return -EFAULT;
            }
            break;
        }

        case KERRW_IOC_DIRECTWRITE8:
        {
            writeb((unsigned char)adwParams[1], adwParams[0]);
            msleep(50);
            printk("val read out = 0x%x\n", readb(adwParams[0]));
            break;
        }
		default:
        {
            break; 
        }
    }

    iounmap(mapaddr);
    
	return 0;
}

static struct file_operations ker_r_w_ops = {
    .open       =	ker_r_w_open,
    .release    =   ker_r_w_close,
	.ioctl      =   ker_r_w_ioctl,
};

static int __init ker_r_w_init (void) 
{
    int res;

    printk("ker_rw driver, to read/write all registers, V1.00\n");
    printk("Complite time: %s %s\n", __DATE__, __TIME__);

    res = register_chrdev(KER_R_W_MAJOR, "ker_rw", &ker_r_w_ops);	
    if (res < 0)
    {
        printk("unable to get major %d\n", KER_R_W_MAJOR);
        return res;
    }

   
    return 0;	
}

static void __exit ker_r_w_cleanup (void) 
{	
    unregister_chrdev(KER_R_W_MAJOR, "ker_rw");
    printk("Remove driver ker_rw ok!\n");
}

module_init (ker_r_w_init);
module_exit (ker_r_w_cleanup);

MODULE_DESCRIPTION("kernel read/write, to read/write all registers");
MODULE_AUTHOR("wei.dongshan@zte.com.cn");

