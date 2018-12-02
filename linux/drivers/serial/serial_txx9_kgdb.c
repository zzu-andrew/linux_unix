/*
 * drivers/serial/serial_txx9_kgdb.c
 *
 * kgdb interface for gdb
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * Copyright (C) 2005-2006 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kgdb.h>
#include <linux/io.h>

/* Speed of the UART. */
static unsigned int kgdb_txx9_baud = CONFIG_KGDB_BAUDRATE;

#define TXX9_NPORT 4		/* TX4939 has 4 UARTs, others only have 2 */

static struct uart_port  kgdb_txx9_ports[TXX9_NPORT];
static struct uart_port *kgdb_port;

/* TXX9 Serial Registers */
#define TXX9_SILCR	0x00
#define TXX9_SIDISR	0x08
#define TXX9_SISCISR	0x0c
#define TXX9_SIFCR	0x10
#define TXX9_SIFLCR	0x14
#define TXX9_SIBGR	0x18
#define TXX9_SITFIFO	0x1c
#define TXX9_SIRFIFO	0x20

/* SILCR : Line Control */
#define TXX9_SILCR_SCS_IMCLK_BG	0x00000020
#define TXX9_SILCR_SCS_SCLK_BG	0x00000060
#define TXX9_SILCR_USBL_1BIT	0x00000000
#define TXX9_SILCR_UMODE_8BIT	0x00000000

/* SIDISR : DMA/Int. Status */
#define TXX9_SIDISR_RFDN_MASK	0x0000001f

/* SISCISR : Status Change Int. Status */
#define TXX9_SISCISR_TRDY	0x00000004

/* SIFCR : FIFO Control */
#define TXX9_SIFCR_SWRST	0x00008000

/* SIBGR : Baud Rate Control */
#define TXX9_SIBGR_BCLK_T0	0x00000000
#define TXX9_SIBGR_BCLK_T2	0x00000100
#define TXX9_SIBGR_BCLK_T4	0x00000200
#define TXX9_SIBGR_BCLK_T6	0x00000300

static inline unsigned int sio_in(struct uart_port *port, int offset)
{
	return *(volatile u32 *)(port->membase + offset);
}

static inline void sio_out(struct uart_port *port, int offset,
	unsigned int value)
{
	*(volatile u32 *)(port->membase + offset) = value;
}

void __init txx9_kgdb_add_port(int n, struct uart_port *port)
{
	memcpy(&kgdb_txx9_ports[n], port, sizeof(struct uart_port));
}

static int txx9_kgdb_init(void)
{
	unsigned int quot, sibgr;

	kgdb_port = &kgdb_txx9_ports[CONFIG_KGDB_PORT_NUM];

	if (kgdb_port->iotype != UPIO_MEM &&
	    kgdb_port->iotype != UPIO_MEM32)
		return -1;

	/* Reset the UART. */
	sio_out(kgdb_port, TXX9_SIFCR, TXX9_SIFCR_SWRST);
#ifdef CONFIG_CPU_TX49XX
	/*
	 * TX4925 BUG WORKAROUND.  Accessing SIOC register
	 * immediately after soft reset causes bus error.
	 */
	iob();
	udelay(1);
#endif
	/* Wait until reset is complete. */
	while (sio_in(kgdb_port, TXX9_SIFCR) & TXX9_SIFCR_SWRST);

	/* Select the frame format and input clock. */
	sio_out(kgdb_port, TXX9_SILCR,
		TXX9_SILCR_UMODE_8BIT | TXX9_SILCR_USBL_1BIT |
		((kgdb_port->flags & UPF_MAGIC_MULTIPLIER) ?
		TXX9_SILCR_SCS_SCLK_BG : TXX9_SILCR_SCS_IMCLK_BG));

	/* Select the input clock prescaler that fits the baud rate. */
	quot = (kgdb_port->uartclk + 8 * kgdb_txx9_baud) /
		(16 * kgdb_txx9_baud);
	if (quot < (256 << 1))
		sibgr = (quot >> 1) | TXX9_SIBGR_BCLK_T0;
	else if (quot < ( 256 << 3))
		sibgr = (quot >> 3) | TXX9_SIBGR_BCLK_T2;
	else if (quot < ( 256 << 5))
		sibgr = (quot >> 5) | TXX9_SIBGR_BCLK_T4;
	else if (quot < ( 256 << 7))
		sibgr = (quot >> 7) | TXX9_SIBGR_BCLK_T6;
	else
		sibgr = 0xff | TXX9_SIBGR_BCLK_T6;

	sio_out(kgdb_port, TXX9_SIBGR, sibgr);

	/* Enable receiver and transmitter. */
	sio_out(kgdb_port, TXX9_SIFLCR, 0);

	return 0;
}

static void txx9_kgdb_late_init(void)
{
	request_mem_region(kgdb_port->mapbase, 0x40, "serial_txx9(debug)");
}

static int txx9_kgdb_read(void)
{
	while (!(sio_in(kgdb_port, TXX9_SIDISR) & TXX9_SIDISR_RFDN_MASK));

	return sio_in(kgdb_port, TXX9_SIRFIFO);
}

static void txx9_kgdb_write(u8 ch)
{
	while (!(sio_in(kgdb_port, TXX9_SISCISR) & TXX9_SISCISR_TRDY));

	sio_out(kgdb_port, TXX9_SITFIFO, ch);
}

struct kgdb_io kgdb_io_ops = {
	.read_char	= txx9_kgdb_read,
	.write_char	= txx9_kgdb_write,
	.init		= txx9_kgdb_init,
	.late_init	= txx9_kgdb_late_init
};
