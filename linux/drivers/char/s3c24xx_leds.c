#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/irq.h>
#include <asm/arch/regs-gpio.h>
#include <asm/hardware.h>

#define DEVICE_NAME     "leds"  /* ����ģʽ��ִ�С�cat /proc/devices����������豸���� */
#define LED_MAJOR       231     /* ���豸�� */

/* Ӧ�ó���ִ��ioctl(fd, cmd, arg)ʱ�ĵ�2������ */
#define IOCTL_LED_ON    0
#define IOCTL_LED_OFF   1

/* ����ָ��LED���õ�GPIO���� */
static unsigned long led_table [] = {
    S3C2410_GPB5,
    S3C2410_GPB6,
    S3C2410_GPB7,
    S3C2410_GPB8,
};

/* ����ָ��GPIO���ŵĹ��ܣ���� */
static unsigned int led_cfg_table [] = {
    S3C2410_GPB5_OUTP,
    S3C2410_GPB6_OUTP,
    S3C2410_GPB7_OUTP,
    S3C2410_GPB8_OUTP,
};

/* Ӧ�ó�����豸�ļ�/dev/ledsִ��open(...)ʱ��
 * �ͻ����s3c24xx_leds_open����
 */
static int s3c24xx_leds_open(struct inode *inode, struct file *file)
{
    int i;
    
    for (i = 0; i < 4; i++) {
        // ����GPIO���ŵĹ��ܣ���������LED���漰��GPIO������Ϊ�������
        s3c2410_gpio_cfgpin(led_table[i], led_cfg_table[i]);
    }
    return 0;
}

/* Ӧ�ó�����豸�ļ�/dev/ledsִ��ioclt(...)ʱ��
 * �ͻ����s3c24xx_leds_ioctl����
 */
static int s3c24xx_leds_ioctl(
    struct inode *inode, 
    struct file *file, 
    unsigned int cmd, 
    unsigned long arg)
{
    if (arg > 4) {
        return -EINVAL;
    }
    
    switch(cmd) {
    case IOCTL_LED_ON:
        // ����ָ�����ŵ������ƽΪ0
        s3c2410_gpio_setpin(led_table[arg], 0);
        return 0;

    case IOCTL_LED_OFF:
        // ����ָ�����ŵ������ƽΪ1
        s3c2410_gpio_setpin(led_table[arg], 1);
        return 0;

    default:
        return -EINVAL;
    }
}

/* ����ṹ���ַ��豸��������ĺ���
 * ��Ӧ�ó�������豸�ļ�ʱ�����õ�open��read��write�Ⱥ�����
 * ���ջ��������ṹ��ָ���Ķ�Ӧ����
 */
static struct file_operations s3c24xx_leds_fops = {
    .owner  =   THIS_MODULE,    /* ����һ���꣬�������ģ��ʱ�Զ�������__this_module���� */
    .open   =   s3c24xx_leds_open,     
    .ioctl  =   s3c24xx_leds_ioctl,
};

/*
 * ִ��insmod����ʱ�ͻ����������� 
 */
static int __init s3c24xx_leds_init(void)
{
    int ret;

    /* ע���ַ��豸
     * ����Ϊ���豸�š��豸���֡�file_operations�ṹ��
     * ���������豸�žͺ;����file_operations�ṹ��ϵ�����ˣ�
     * �������豸ΪLED_MAJOR���豸�ļ�ʱ���ͻ����s3c24xx_leds_fops�е���س�Ա����
     * LED_MAJOR������Ϊ0����ʾ���ں��Զ��������豸��
     */
    ret = register_chrdev(LED_MAJOR, DEVICE_NAME, &s3c24xx_leds_fops);
    if (ret < 0) {
      printk(DEVICE_NAME " can't register major number\n");
      return ret;
    }
    
    printk(DEVICE_NAME " initialized\n");
    return 0;
}

/*
 * ִ��rmmod����ʱ�ͻ����������� 
 */
static void __exit s3c24xx_leds_exit(void)
{
    /* ж���������� */
    unregister_chrdev(LED_MAJOR, DEVICE_NAME);
}

/* ������ָ����������ĳ�ʼ��������ж�غ��� */
module_init(s3c24xx_leds_init);
module_exit(s3c24xx_leds_exit);

/* �������������һЩ��Ϣ�����Ǳ���� */
MODULE_AUTHOR("http://www.100ask.net");
MODULE_DESCRIPTION("S3C2410/S3C2440 LED Driver");
MODULE_LICENSE("GPL");

