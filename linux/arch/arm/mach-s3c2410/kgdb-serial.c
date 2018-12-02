/*
 * linux/arch/arm/mach-pxa/kgdb-serial.c
 *
 * Provides low level kgdb serial support hooks for s3c2410/s3c2440 boards
 *
 * Author:	thisway.diy
 * Copyright:	(C) 2008-2010 www.100ask.net, thisway.diy@163.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kgdb.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/sysrq.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include <asm/io.h>
#include <asm/irq.h>

#include <asm/hardware.h>

#include <asm/arch/regs-serial.h>
#include <asm/arch/regs-gpio.h>

#define portaddr(port, reg) (port_base[port] + (reg))

#define rd_regb(port, reg) (__raw_readb(portaddr(port, reg)))
#define rd_regl(port, reg) (__raw_readl(portaddr(port, reg)))

#define wr_regb(port, reg, val) \
  do { __raw_writeb(val, portaddr(port, reg)); } while(0)

#define wr_regl(port, reg, val) \
  do { __raw_writel(val, portaddr(port, reg)); } while(0)


#define MAX_PORT        3
#define UART_BAUDRATE	(CONFIG_KGDB_BAUDRATE)
static u32 port_base[] = {S3C24XX_VA_UART0, S3C24XX_VA_UART1, S3C24XX_VA_UART2};


static int kgdb_serial_init(void)
{
    struct clk *clock_p;
    u32 pclk;
    u32 ubrdiv;
    u32 val;
    u32 index = CONFIG_KGDB_PORT_NUM;

    clock_p = clk_get(NULL, "pclk");
    pclk = clk_get_rate(clock_p);

    ubrdiv = (pclk / (UART_BAUDRATE * 16)) - 1;

    /* 设置GPIO用作串口, 并且禁止内部上拉
     * GPH2,GPH3用作TXD0,RXD0
     * GPH4,GPH5用作TXD1,RXD1
     * GPH6,GPH7用作TXD2,RXD2
     */
    if (index < MAX_PORT)
    {
        index = 2 + index * 2;
        
        val = inl(S3C2410_GPHUP) | (0x3 << index);
        outl(val, S3C2410_GPHUP);

        index *= 2;
        val = (inl(S3C2410_GPHCON) & (~(0xF << index))) | \
              (0xA << index);
        outl(val, S3C2410_GPHCON);
    }
    else
    {
        return -1;
    }
    
    // 8N1(8个数据位，无较验，1个停止位)
    wr_regl(CONFIG_KGDB_PORT_NUM, S3C2410_ULCON, 0x03);

    // 中断/查询方式，UART时钟源为PCLK
    wr_regl(CONFIG_KGDB_PORT_NUM, S3C2410_UCON, 0x3c5);

    // 使用FIFO
    wr_regl(CONFIG_KGDB_PORT_NUM, S3C2410_UFCON, 0x51);

    // 不使用流控
    wr_regl(CONFIG_KGDB_PORT_NUM, S3C2410_UMCON, 0x00);

    // 设置波特率
    wr_regl(CONFIG_KGDB_PORT_NUM, S3C2410_UBRDIV, ubrdiv);    
    
	return 0;
}

static void kgdb_serial_putchar(u8 c)
{
    /* 等待，直到发送缓冲区中的数据已经全部发送出去 */   
    while (!(rd_regb(CONFIG_KGDB_PORT_NUM, S3C2410_UTRSTAT) & S3C2410_UTRSTAT_TXE));        

    /* 向UTXH寄存器中写入数据，UART即自动将它发送出去 */    
    wr_regb(CONFIG_KGDB_PORT_NUM, S3C2410_UTXH, c);
}

static int kgdb_serial_getchar(void)
{
    /* 等待，直到接收缓冲区中的有数据 */    
    while (!(rd_regb(CONFIG_KGDB_PORT_NUM, S3C2410_UTRSTAT) & S3C2410_UTRSTAT_RXDR));        

    /* 直接读取URXH寄存器，即可获得接收到的数据 */    
    return rd_regb(CONFIG_KGDB_PORT_NUM, S3C2410_URXH);
}

struct kgdb_io kgdb_io_ops = {
	.init = kgdb_serial_init,
	.read_char = kgdb_serial_getchar,
	.write_char = kgdb_serial_putchar,
};
