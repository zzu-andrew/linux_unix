/*
 *  linux/drivers/mmc/s3cmci.h - Samsung S3C MCI driver
 *
 *  Copyright (C) 2004-2006 maintech GmbH, Thomas Kleffel <tk@maintech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>
#include <linux/platform_device.h>
#include <linux/irq.h>

#include <asm/dma.h>
#include <asm/dma-mapping.h>

#include <asm/io.h>
#include <asm/arch/regs-sdi.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/mci.h>

#include "mmc_debug.h"
#include "s3cmci.h"

#define DRIVER_NAME "s3c-mci"

enum dbg_channels {
	dbg_err   = (1 << 0),
	dbg_debug = (1 << 1),
	dbg_info  = (1 << 2),
	dbg_irq   = (1 << 3),
	dbg_sg    = (1 << 4),
	dbg_dma   = (1 << 5),
	dbg_pio   = (1 << 6),
	dbg_fail  = (1 << 7),
	dbg_conf  = (1 << 8),
};

static const int dbgmap_err   = dbg_err | dbg_fail;
static const int dbgmap_info  = dbg_info | dbg_conf;
static const int dbgmap_debug = dbg_debug;

#define dbg(host, channels, args...)		 \
	if (dbgmap_err & channels) 		 \
		dev_err(&host->pdev->dev, args); \
	else if (dbgmap_info & channels)	 \
		dev_info(&host->pdev->dev, args);\
	else if (dbgmap_debug & channels)	 \
		dev_dbg(&host->pdev->dev, args);

#define RESSIZE(ressource) (((ressource)->end - (ressource)->start)+1)

static struct s3c2410_dma_client s3cmci_dma_client = {
	.name		= "s3c-mci",
};

static void finalize_request(struct s3cmci_host *host);
static void s3cmci_send_request(struct mmc_host *mmc);
static void s3cmci_reset(struct s3cmci_host *host);

#ifdef CONFIG_MMC_DEBUG

static inline void dbg_dumpregs(struct s3cmci_host *host, char *prefix)
{
	u32 con, pre, cmdarg, cmdcon, cmdsta, r0, r1, r2, r3, timer, bsize;
	u32 datcon, datcnt, datsta, fsta, imask;

	con 	= readl(host->base + S3C2410_SDICON);
	pre 	= readl(host->base + S3C2410_SDIPRE);
	cmdarg 	= readl(host->base + S3C2410_SDICMDARG);
	cmdcon 	= readl(host->base + S3C2410_SDICMDCON);
	cmdsta 	= readl(host->base + S3C2410_SDICMDSTAT);
	r0 	= readl(host->base + S3C2410_SDIRSP0);
	r1 	= readl(host->base + S3C2410_SDIRSP1);
	r2 	= readl(host->base + S3C2410_SDIRSP2);
	r3 	= readl(host->base + S3C2410_SDIRSP3);
	timer 	= readl(host->base + S3C2410_SDITIMER);
	bsize 	= readl(host->base + S3C2410_SDIBSIZE);
	datcon 	= readl(host->base + S3C2410_SDIDCON);
	datcnt 	= readl(host->base + S3C2410_SDIDCNT);
	datsta 	= readl(host->base + S3C2410_SDIDSTA);
	fsta 	= readl(host->base + S3C2410_SDIFSTA);
	imask   = readl(host->base + host->sdiimsk);

	dbg(host, dbg_debug, "%s  CON:[%08x]  PRE:[%08x]  TMR:[%08x]\n",
				prefix, con, pre, timer);

	dbg(host, dbg_debug, "%s CCON:[%08x] CARG:[%08x] CSTA:[%08x]\n",
				prefix, cmdcon, cmdarg, cmdsta);

	dbg(host, dbg_debug, "%s DCON:[%08x] FSTA:[%08x]"
			       " DSTA:[%08x] DCNT:[%08x]\n",
				prefix, datcon, fsta, datsta, datcnt);

	dbg(host, dbg_debug, "%s   R0:[%08x]   R1:[%08x]"
			       "   R2:[%08x]   R3:[%08x]\n",
				prefix, r0, r1, r2, r3);
}

static void prepare_dbgmsg(struct s3cmci_host *host, struct mmc_command *cmd,
								int stop)
{
 	snprintf(host->dbgmsg_cmd, 300,
		"#%u%s op:%s(%i) arg:0x%08x flags:0x08%x retries:%u",
		host->ccnt, (stop?" (STOP)":""), mmc_cmd2str(cmd->opcode),
		cmd->opcode, cmd->arg, cmd->flags, cmd->retries);

	if (cmd->data) {
		snprintf(host->dbgmsg_dat, 300,
			"#%u bsize:%u blocks:%u bytes:%u",
			host->dcnt, cmd->data->blksz,
			cmd->data->blocks,
			cmd->data->blocks * cmd->data->blksz);
	} else {
		host->dbgmsg_dat[0] = '\0';
	}
}

static void dbg_dumpcmd(struct s3cmci_host *host, struct mmc_command *cmd,
								int fail)
{
	unsigned int dbglvl = fail?dbg_fail:dbg_debug;

	if (!cmd)
		return;

	if (cmd->error == MMC_ERR_NONE) {

		dbg(host, dbglvl, "CMD[OK] %s R0:0x%08x\n",
			host->dbgmsg_cmd, cmd->resp[0]);
	} else {
		dbg(host, dbglvl, "CMD[%s] %s Status:%s\n",
			mmc_err2str(cmd->error), host->dbgmsg_cmd,
			host->status);
	}

	if (!cmd->data)
		return;

	if (cmd->data->error == MMC_ERR_NONE) {
		dbg(host, dbglvl, "DAT[%s] %s\n",
			mmc_err2str(cmd->data->error), host->dbgmsg_dat);
	} else {
		dbg(host, dbglvl, "DAT[%s] %s DCNT:0x%08x\n",
			mmc_err2str(cmd->data->error), host->dbgmsg_dat,
			readl(host->base + S3C2410_SDIDCNT));
	}
}
#endif

static inline u32 enable_imask(struct s3cmci_host *host, u32 imask)
{
	u32 newmask;

	newmask = readl(host->base + host->sdiimsk);
	newmask|= imask;

	writel(newmask, host->base + host->sdiimsk);

	return newmask;
}

static inline u32 disable_imask(struct s3cmci_host *host, u32 imask)
{
	u32 newmask;

	newmask = readl(host->base + host->sdiimsk);
	newmask&= ~imask;

	writel(newmask, host->base + host->sdiimsk);

	return newmask;
}

static inline void clear_imask(struct s3cmci_host *host)
{
	writel(0, host->base + host->sdiimsk);
}

static inline int get_data_buffer(struct s3cmci_host *host,
			volatile u32 *words, volatile u32 **pointer)
{
	struct scatterlist *sg;

	if (host->pio_active == XFER_NONE)
		return -EINVAL;

	if ((!host->mrq) || (!host->mrq->data))
		return -EINVAL;

	if (host->pio_sgptr >= host->mrq->data->sg_len) {
		dbg(host, dbg_debug, "no more buffers (%i/%i)\n",
		      host->pio_sgptr, host->mrq->data->sg_len);
		return -EBUSY;
	}
	sg = &host->mrq->data->sg[host->pio_sgptr];

	*words	= sg->length >> 2;
	*pointer= page_address(sg->page) + sg->offset;

	host->pio_sgptr++;

	dbg(host, dbg_sg, "new buffer (%i/%i)\n",
	      host->pio_sgptr, host->mrq->data->sg_len);

	return 0;
}

#define FIFO_FILL(host) ((readl(host->base + S3C2410_SDIFSTA) & S3C2410_SDIFSTA_COUNTMASK) >> 2)
#define FIFO_FREE(host) ((63 - (readl(host->base + S3C2410_SDIFSTA) & S3C2410_SDIFSTA_COUNTMASK)) >> 2)

static inline void do_pio_read(struct s3cmci_host *host)
{
	int res;
	u32 fifo;
	void __iomem *from_ptr;

	//write real prescaler to host, it might be set slow to fix
	writel(host->prescaler, host->base + S3C2410_SDIPRE);

	from_ptr = host->base + host->sdidata;

	while ((fifo = FIFO_FILL(host))) {
		if (!host->pio_words) {
			res = get_data_buffer(host, &host->pio_words,
							&host->pio_ptr);
			if (res) {
				host->pio_active = XFER_NONE;
				host->complete_what = COMPLETION_FINALIZE;

				dbg(host, dbg_pio, "pio_read(): "
					"complete (no more data).\n");
				return;
			}

			dbg(host, dbg_pio, "pio_read(): new target: [%i]@[%p]\n",
			       host->pio_words, host->pio_ptr);
		}

		dbg(host, dbg_pio, "pio_read(): fifo:[%02i] "
				   "buffer:[%03i] dcnt:[%08X]\n",
				   fifo, host->pio_words,
				   readl(host->base + S3C2410_SDIDCNT));

		if (fifo > host->pio_words)
			fifo = host->pio_words;

		host->pio_words-= fifo;
		host->pio_count+= fifo;

		while(fifo--) {
			*(host->pio_ptr++) = readl(from_ptr);
		}
	}

	if (!host->pio_words) {
		res = get_data_buffer(host, &host->pio_words, &host->pio_ptr);
		if (res) {
			dbg(host, dbg_pio, "pio_read(): "
				"complete (no more buffers).\n");
			host->pio_active = XFER_NONE;
			host->complete_what = COMPLETION_FINALIZE;

			return;
		}
	}

	enable_imask(host, S3C2410_SDIIMSK_RXFIFOHALF | S3C2410_SDIIMSK_RXFIFOLAST);
}

static inline void do_pio_write(struct s3cmci_host *host)
{
	int res;
	u32 fifo;

	void __iomem *to_ptr;

	to_ptr = host->base + host->sdidata;

	while ((fifo = FIFO_FREE(host))) {
		if (!host->pio_words) {
			res = get_data_buffer(host, &host->pio_words,
							&host->pio_ptr);
			if (res) {
				dbg(host, dbg_pio, "pio_write(): "
					"complete (no more data).\n");
				host->pio_active = XFER_NONE;

				return;
			}

			dbg(host, dbg_pio, "pio_write(): "
				"new source: [%i]@[%p]\n",
				host->pio_words, host->pio_ptr);

		}

		if (fifo > host->pio_words)
			fifo = host->pio_words;

		host->pio_words-= fifo;
		host->pio_count+= fifo;

		while(fifo--) {
			writel(*(host->pio_ptr++), to_ptr);
		}
	}

	enable_imask(host, S3C2410_SDIIMSK_TXFIFOHALF);
}

static void pio_tasklet(unsigned long data)
{
	struct s3cmci_host *host = (struct s3cmci_host *) data;

	disable_irq(host->irq);

	if (host->pio_active == XFER_WRITE)
		do_pio_write(host);

	if (host->pio_active == XFER_READ)
		do_pio_read(host);

	if (host->complete_what == COMPLETION_FINALIZE) {
		clear_imask(host);
		if (host->pio_active != XFER_NONE) {
			dbg(host, dbg_err, "unfinished %s "
				"- pio_count:[%u] pio_words:[%u]\n",
				(host->pio_active == XFER_READ)?"read":"write",
				host->pio_count, host->pio_words);

			host->mrq->data->error = MMC_ERR_DMA;
		}

		finalize_request(host);
	} else
		enable_irq(host->irq);
}

/*
 * ISR for SDI Interface IRQ
 * Communication between driver and ISR works as follows:
 *   host->mrq 			points to current request
 *   host->complete_what	tells the ISR when the request is considered done
 *     COMPLETION_CMDSENT	  when the command was sent
 *     COMPLETION_RSPFIN          when a response was received
 *     COMPLETION_XFERFINISH	  when the data transfer is finished
 *     COMPLETION_XFERFINISH_RSPFIN both of the above.
 *   host->complete_request	is the completion-object the driver waits for
 *
 * 1) Driver sets up host->mrq and host->complete_what
 * 2) Driver prepares the transfer
 * 3) Driver enables interrupts
 * 4) Driver starts transfer
 * 5) Driver waits for host->complete_rquest
 * 6) ISR checks for request status (errors and success)
 * 6) ISR sets host->mrq->cmd->error and host->mrq->data->error
 * 7) ISR completes host->complete_request
 * 8) ISR disables interrupts
 * 9) Driver wakes up and takes care of the request
 *
 * Note: "->error"-fields are expected to be set to 0 before the request
 *       was issued by mmc.c - therefore they are only set, when an error
 *       contition comes up
 */

static irqreturn_t s3cmci_irq(int irq, void *dev_id)
{
	struct s3cmci_host *host;
	struct mmc_command *cmd;
	u32 mci_csta, mci_dsta, mci_fsta, mci_dcnt, mci_imsk;
	u32 mci_cclear, mci_dclear;
	unsigned long iflags;

	host = (struct s3cmci_host *)dev_id;

	spin_lock_irqsave(&host->complete_lock, iflags);

	mci_csta 	= readl(host->base + S3C2410_SDICMDSTAT);
	mci_dsta 	= readl(host->base + S3C2410_SDIDSTA);
	mci_dcnt 	= readl(host->base + S3C2410_SDIDCNT);
	mci_fsta 	= readl(host->base + S3C2410_SDIFSTA);
	mci_imsk	= readl(host->base + host->sdiimsk);
	mci_cclear	= 0;
	mci_dclear	= 0;

	if ((host->complete_what == COMPLETION_NONE) ||
			(host->complete_what == COMPLETION_FINALIZE)) {
		host->status = "nothing to complete";
		clear_imask(host);
		goto irq_out;
	}

	if (!host->mrq) {
		host->status = "no active mrq";
		clear_imask(host);
		goto irq_out;
	}

	cmd = host->cmd_is_stop?host->mrq->stop:host->mrq->cmd;

	if (!cmd) {
		host->status = "no active cmd";
		clear_imask(host);
		goto irq_out;
	}

	if (!host->dodma) {
		if ((host->pio_active == XFER_WRITE) &&
				(mci_fsta & S3C2410_SDIFSTA_TFDET)) {

			disable_imask(host, S3C2410_SDIIMSK_TXFIFOHALF);
			tasklet_schedule(&host->pio_tasklet);
			host->status = "pio tx";
		}

		if ((host->pio_active == XFER_READ) &&
				(mci_fsta & S3C2410_SDIFSTA_RFDET)) {

			disable_imask(host,
				S3C2410_SDIIMSK_RXFIFOHALF | S3C2410_SDIIMSK_RXFIFOLAST);

			tasklet_schedule(&host->pio_tasklet);
			host->status = "pio rx";
		}
	}

	if (mci_csta & S3C2410_SDICMDSTAT_CMDTIMEOUT) {
		cmd->error = MMC_ERR_TIMEOUT;
		host->status = "error: command timeout";
		goto fail_transfer;
	}

	if (mci_csta & S3C2410_SDICMDSTAT_CMDSENT) {
		if (host->complete_what == COMPLETION_CMDSENT) {
			host->status = "ok: command sent";
			goto close_transfer;
		}

		mci_cclear |= S3C2410_SDICMDSTAT_CMDSENT;
	}

	if (mci_csta & S3C2410_SDICMDSTAT_CRCFAIL) {
		if (cmd->flags & MMC_RSP_CRC) {
			if (host->mrq->cmd->flags & MMC_RSP_136) {
				dbg(host, dbg_irq, 
				    "fixup: ignore CRC fail with long rsp\n");
			} else {
#if 0
				cmd->error = MMC_ERR_BADCRC;
				host->status = "error: bad command crc";
				goto fail_transfer;
#endif
			}
		}
		mci_cclear |= S3C2410_SDICMDSTAT_CRCFAIL;
	}

	if (mci_csta & S3C2410_SDICMDSTAT_RSPFIN) {
		if (host->complete_what == COMPLETION_RSPFIN) {
			host->status = "ok: command response received";
			goto close_transfer;
		}

		if (host->complete_what == COMPLETION_XFERFINISH_RSPFIN)
			host->complete_what = COMPLETION_XFERFINISH;

		mci_cclear |= S3C2410_SDICMDSTAT_RSPFIN;
	}

	/* errors handled after this point are only relevant
	   when a data transfer is in progress */

	if (!cmd->data)
		goto clear_status_bits;

	/* Check for FIFO failure */
	if (host->is2440) {
		if (mci_fsta & S3C2440_SDIFSTA_FIFOFAIL) {
			host->mrq->data->error = MMC_ERR_FIFO;
			host->status = "error: 2440 fifo failure";
			goto fail_transfer;
		}
	} else {
		if (mci_dsta & S3C2410_SDIDSTA_FIFOFAIL) {
			cmd->data->error = MMC_ERR_FIFO;
			host->status = "error:  fifo failure";
			goto fail_transfer;
		}
	}

	if (mci_dsta & S3C2410_SDIDSTA_RXCRCFAIL) {
		cmd->data->error = MMC_ERR_BADCRC;
		host->status = "error: bad data crc (outgoing)";
		goto fail_transfer;
	}

	if (mci_dsta & S3C2410_SDIDSTA_CRCFAIL) {
		cmd->data->error = MMC_ERR_BADCRC;
		host->status = "error: bad data crc (incoming)";
		goto fail_transfer;
	}

	if (mci_dsta & S3C2410_SDIDSTA_DATATIMEOUT) {
		cmd->data->error = MMC_ERR_TIMEOUT;
		host->status = "error: data timeout";
		goto fail_transfer;
	}

	if (mci_dsta & S3C2410_SDIDSTA_XFERFINISH) {
		if (host->complete_what == COMPLETION_XFERFINISH) {
			host->status = "ok: data transfer completed";
			goto close_transfer;
		}

		if (host->complete_what == COMPLETION_XFERFINISH_RSPFIN) {
			host->complete_what = COMPLETION_RSPFIN;
		}

		mci_dclear |= S3C2410_SDIDSTA_XFERFINISH;
	}

clear_status_bits:
	writel(mci_cclear, host->base + S3C2410_SDICMDSTAT);
	writel(mci_dclear, host->base + S3C2410_SDIDSTA);

	goto irq_out;

fail_transfer:
	host->pio_active = XFER_NONE;

close_transfer:
	host->complete_what = COMPLETION_FINALIZE;

	clear_imask(host);
	tasklet_schedule(&host->pio_tasklet);

	goto irq_out;

irq_out:
	dbg(host, dbg_irq, "csta:0x%08x dsta:0x%08x "
			   "fsta:0x%08x dcnt:0x%08x status:%s.\n",
				mci_csta, mci_dsta, mci_fsta,
				mci_dcnt, host->status);

	spin_unlock_irqrestore(&host->complete_lock, iflags);
	return IRQ_HANDLED;

}

/*
 * ISR for the CardDetect Pin
*/

static irqreturn_t s3cmci_irq_cd(int irq, void *dev_id)
{
	struct s3cmci_host *host = (struct s3cmci_host *)dev_id;

	dbg(host, dbg_irq, "card detect\n");

	mmc_detect_change(host->mmc, 500);

	return IRQ_HANDLED;
}

void s3cmci_dma_done_callback(struct s3c2410_dma_chan *dma_ch, void *buf_id,
	int size, enum s3c2410_dma_buffresult result)
{
	unsigned long iflags;
	u32 mci_csta, mci_dsta, mci_fsta, mci_dcnt;
	struct s3cmci_host *host = (struct s3cmci_host *)buf_id;

	mci_csta 	= readl(host->base + S3C2410_SDICMDSTAT);
	mci_dsta 	= readl(host->base + S3C2410_SDIDSTA);
	mci_fsta 	= readl(host->base + S3C2410_SDIFSTA);
	mci_dcnt 	= readl(host->base + S3C2410_SDIDCNT);

	if ((!host->mrq) || (!host->mrq) || (!host->mrq->data))
		return;

	if (!host->dmatogo)
		return;

	spin_lock_irqsave(&host->complete_lock, iflags);

	if (result != S3C2410_RES_OK) {
		dbg(host, dbg_fail, "DMA FAILED: csta=0x%08x dsta=0x%08x "
			"fsta=0x%08x dcnt:0x%08x result:0x%08x toGo:%u\n",
			mci_csta, mci_dsta, mci_fsta,
			mci_dcnt, result, host->dmatogo);

		goto fail_request;
	}

	host->dmatogo--;
	if (host->dmatogo) {
		dbg(host, dbg_dma, "DMA DONE  Size:%i DSTA:[%08x] "
			"DCNT:[%08x] toGo:%u\n",
			size, mci_dsta, mci_dcnt, host->dmatogo);

		goto out;
	}

	dbg(host, dbg_dma, "DMA FINISHED Size:%i DSTA:%08x DCNT:%08x\n",
		size, mci_dsta, mci_dcnt);

	host->complete_what = COMPLETION_FINALIZE;

out:
	tasklet_schedule(&host->pio_tasklet);
	spin_unlock_irqrestore(&host->complete_lock, iflags);
	return;


fail_request:
	host->mrq->data->error = MMC_ERR_DMA;
	host->complete_what = COMPLETION_FINALIZE;
	writel(0, host->base + host->sdiimsk);
	goto out;

}

static void finalize_request(struct s3cmci_host *host)
{
	struct mmc_request *mrq = host->mrq;
	struct mmc_command *cmd = host->cmd_is_stop?mrq->stop:mrq->cmd;
	int debug_as_failure = 0;

	if (host->complete_what != COMPLETION_FINALIZE)
		return;

	if (!mrq)
		return;

	if (cmd->data && (cmd->error == MMC_ERR_NONE) &&
		  (cmd->data->error == MMC_ERR_NONE)) {

		if (host->dodma && (!host->dma_complete)) {
			dbg(host, dbg_dma, "DMA Missing!\n");
			return;
		}
	}

	// Read response
	cmd->resp[0] = readl(host->base + S3C2410_SDIRSP0);
	cmd->resp[1] = readl(host->base + S3C2410_SDIRSP1);
	cmd->resp[2] = readl(host->base + S3C2410_SDIRSP2);
	cmd->resp[3] = readl(host->base + S3C2410_SDIRSP3);

	// reset clock speed, as it could still be set low for
	writel(host->prescaler, host->base + S3C2410_SDIPRE);

	if (cmd->error)
		debug_as_failure = 1;

	if (cmd->data && cmd->data->error)
		debug_as_failure = 1;

	//if(cmd->flags & MMC_RSP_MAYFAIL) debug_as_failure = 0;

#ifdef CONFIG_MMC_DEBUG
	dbg_dumpcmd(host, cmd, debug_as_failure);
#endif
	//Cleanup controller
	writel(0, host->base + S3C2410_SDICMDARG);
	writel(S3C2410_SDIDCON_STOP, host->base + S3C2410_SDIDCON);
	writel(0, host->base + S3C2410_SDICMDCON);
	writel(0, host->base + host->sdiimsk);

	if (cmd->data && cmd->error)
		cmd->data->error = cmd->error;

	if (cmd->data && cmd->data->stop && (!host->cmd_is_stop)) {
		host->cmd_is_stop = 1;
		s3cmci_send_request(host->mmc);
		return;
	}

	// If we have no data transfer we are finished here
	if (!mrq->data)
		goto request_done;

	// Calulate the amout of bytes transfer, but only if there was
	// no error
	if (mrq->data->error == MMC_ERR_NONE) {
		mrq->data->bytes_xfered =
			(mrq->data->blocks * mrq->data->blksz);
	} else {
		mrq->data->bytes_xfered = 0;
	}

	// If we had an error while transfering data we flush the
	// DMA channel and the fifo to clear out any garbage
	if (mrq->data->error != MMC_ERR_NONE) {
		if (host->dodma)
			s3c2410_dma_ctrl(host->dma, S3C2410_DMAOP_FLUSH);

		if (host->is2440) {
			//Clear failure register and reset fifo
			writel(S3C2440_SDIFSTA_FIFORESET |
			       S3C2440_SDIFSTA_FIFOFAIL,
			       host->base + S3C2410_SDIFSTA);
		} else {
			u32 mci_con;

			//reset fifo
			mci_con = readl(host->base + S3C2410_SDICON);
			mci_con|= S3C2410_SDICON_FIFORESET;

			writel(mci_con, host->base + S3C2410_SDICON);
		}
	}

request_done:
	host->complete_what = COMPLETION_NONE;
	host->mrq = NULL;
	mmc_request_done(host->mmc, mrq);
}


void s3cmci_dma_setup(struct s3cmci_host *host, enum s3c2410_dmasrc source)
{
	static int setup_ok = 0;
	static enum s3c2410_dmasrc last_source = -1;

	if (last_source == source)
		return;

	last_source = source;

	s3c2410_dma_devconfig(host->dma, source, 3,
		host->mem->start + host->sdidata);

	if (!setup_ok) {
		s3c2410_dma_config(host->dma, 4,
			(S3C2410_DCON_HWTRIG | S3C2410_DCON_CH0_SDI));
		s3c2410_dma_set_buffdone_fn(host->dma, s3cmci_dma_done_callback);
		s3c2410_dma_setflags(host->dma, S3C2410_DMAF_AUTOSTART);
		setup_ok = 1;
	}
}

static void s3cmci_send_command(struct s3cmci_host *host,
					struct mmc_command *cmd)
{
	u32 ccon, imsk;

	imsk  = S3C2410_SDIIMSK_CRCSTATUS | S3C2410_SDIIMSK_CMDTIMEOUT |
		S3C2410_SDIIMSK_RESPONSEND | S3C2410_SDIIMSK_CMDSENT |
		S3C2410_SDIIMSK_RESPONSECRC;

	enable_imask(host, imsk);

	if (cmd->data) {
		host->complete_what = COMPLETION_XFERFINISH_RSPFIN;
	} else if (cmd->flags & MMC_RSP_PRESENT) {
		host->complete_what = COMPLETION_RSPFIN;
	} else {
		host->complete_what = COMPLETION_CMDSENT;
	}

	writel(cmd->arg, host->base + S3C2410_SDICMDARG);

	ccon = cmd->opcode & S3C2410_SDICMDCON_INDEX;
	ccon|= S3C2410_SDICMDCON_SENDERHOST | S3C2410_SDICMDCON_CMDSTART;

	if (cmd->flags & MMC_RSP_PRESENT)
		ccon |= S3C2410_SDICMDCON_WAITRSP;

	if (cmd->flags & MMC_RSP_136)
		ccon|= S3C2410_SDICMDCON_LONGRSP;

	writel(ccon, host->base + S3C2410_SDICMDCON);
}

static int s3cmci_setup_data(struct s3cmci_host *host, struct mmc_data *data)
{
	u32 dcon, imsk, stoptries=3;

	/* write DCON register */

	if (!data) {
		writel(0, host->base + S3C2410_SDIDCON);
		return 0;
	}

	while(readl(host->base + S3C2410_SDIDSTA) &
		(S3C2410_SDIDSTA_TXDATAON | S3C2410_SDIDSTA_RXDATAON)) {

		dbg(host, dbg_err,
			"mci_setup_data() transfer stillin progress.\n");

		writel(S3C2410_SDIDCON_STOP, host->base + S3C2410_SDIDCON);
		s3cmci_reset(host);

		if (0 == (stoptries--)) {
#ifdef CONFIG_MMC_DEBUG
			dbg_dumpregs(host, "DRF");
#endif

			return -EINVAL;
		}
	}

	dcon  = data->blocks & S3C2410_SDIDCON_BLKNUM_MASK;

	if (host->dodma) {
		dcon |= S3C2410_SDIDCON_DMAEN;
	}

	if (host->bus_width == MMC_BUS_WIDTH_4) {
		dcon |= S3C2410_SDIDCON_WIDEBUS;
	}

	if (!(data->flags & MMC_DATA_STREAM)) {
		dcon |= S3C2410_SDIDCON_BLOCKMODE;
	}

	if (data->flags & MMC_DATA_WRITE) {
		dcon |= S3C2410_SDIDCON_TXAFTERRESP;
		dcon |= S3C2410_SDIDCON_XFER_TXSTART;
	}

	if (data->flags & MMC_DATA_READ) {
		dcon |= S3C2410_SDIDCON_RXAFTERCMD;
		dcon |= S3C2410_SDIDCON_XFER_RXSTART;
	}

	if (host->is2440) {
		dcon |= S3C2440_SDIDCON_DS_WORD;
		dcon |= S3C2440_SDIDCON_DATSTART;
	}

	writel(dcon, host->base + S3C2410_SDIDCON);

	/* write BSIZE register */

	writel(data->blksz, host->base + S3C2410_SDIBSIZE);

	/* add to IMASK register */
	imsk =	S3C2410_SDIIMSK_FIFOFAIL | S3C2410_SDIIMSK_DATACRC |
		S3C2410_SDIIMSK_DATATIMEOUT | S3C2410_SDIIMSK_DATAFINISH;

	enable_imask(host, imsk);

	/* write TIMER register */

	if (host->is2440) {
		writel(0x007FFFFF, host->base + S3C2410_SDITIMER);
	} else {
		writel(0x0000FFFF, host->base + S3C2410_SDITIMER);

		//FIX: set slow clock to prevent timeouts on read
		if (data->flags & MMC_DATA_READ) {
			writel(0xFF, host->base + S3C2410_SDIPRE);
		}
	}

	//debug_dump_registers(host, "Data setup:");

	return 0;
}

static int s3cmci_prepare_pio(struct s3cmci_host *host, struct mmc_data *data)
{
	int rw = (data->flags & MMC_DATA_WRITE)?1:0;

	if (rw != ((data->flags & MMC_DATA_READ)?0:1))
		return -EINVAL;

	host->pio_sgptr = 0;
	host->pio_words = 0;
	host->pio_count = 0;
	host->pio_active = rw?XFER_WRITE:XFER_READ;

	if (rw) {
		do_pio_write(host);
		enable_imask(host, S3C2410_SDIIMSK_TXFIFOHALF);
	} else {
		enable_imask(host, S3C2410_SDIIMSK_RXFIFOHALF
			| S3C2410_SDIIMSK_RXFIFOLAST);
	}

	return 0;
}

static int s3cmci_prepare_dma(struct s3cmci_host *host, struct mmc_data *data)
{
	int dma_len, i;

	int rw = (data->flags & MMC_DATA_WRITE)?1:0;

	if (rw != ((data->flags & MMC_DATA_READ)?0:1))
		return -EINVAL;

	s3cmci_dma_setup(host, rw?S3C2410_DMASRC_MEM:S3C2410_DMASRC_HW);
	s3c2410_dma_ctrl(host->dma, S3C2410_DMAOP_FLUSH);

	dma_len = dma_map_sg(mmc_dev(host->mmc), data->sg, data->sg_len,
				(rw)?DMA_TO_DEVICE:DMA_FROM_DEVICE);


	if (dma_len == 0)
		return -ENOMEM;

	host->dma_complete = 0;
	host->dmatogo = dma_len;

	for (i = 0; i < dma_len; i++) {
		int res;

		dbg(host, dbg_dma, "enqueue %i:%u@%u\n", i,
			sg_dma_address(&data->sg[i]),
			sg_dma_len(&data->sg[i]));

		res = s3c2410_dma_enqueue(host->dma, (void *) host,
				sg_dma_address(&data->sg[i]),
				sg_dma_len(&data->sg[i]));

		if (res) {
			s3c2410_dma_ctrl(host->dma, S3C2410_DMAOP_FLUSH);
			return -EBUSY;
		}
 	}

	s3c2410_dma_ctrl(host->dma, S3C2410_DMAOP_START);

	return 0;
}

static void s3cmci_send_request(struct mmc_host *mmc)
{
	struct s3cmci_host *host = mmc_priv(mmc);
	struct mmc_request *mrq = host->mrq;
	struct mmc_command *cmd = host->cmd_is_stop?mrq->stop:mrq->cmd;

	host->ccnt++;
#ifdef CONFIG_MMC_DEBUG
	prepare_dbgmsg(host, cmd, host->cmd_is_stop);
#endif
	//Clear command, data and fifo status registers
	//Fifo clear only necessary on 2440, but doesn't hurt on 2410
	writel(0xFFFFFFFF, host->base + S3C2410_SDICMDSTAT);
	writel(0xFFFFFFFF, host->base + S3C2410_SDIDSTA);
	writel(0xFFFFFFFF, host->base + S3C2410_SDIFSTA);

	if (cmd->data) {
		int res;
		res = s3cmci_setup_data(host, cmd->data);

		host->dcnt++;

		if (res) {
			cmd->error = MMC_ERR_DMA;
			cmd->data->error = MMC_ERR_DMA;

			mmc_request_done(mmc, mrq);
			return;
		}


		if (host->dodma) {
			res = s3cmci_prepare_dma(host, cmd->data);
		} else {
			res = s3cmci_prepare_pio(host, cmd->data);
		}

		if (res) {
			cmd->error = MMC_ERR_DMA;
			cmd->data->error = MMC_ERR_DMA;

			mmc_request_done(mmc, mrq);
			return;
		}

	}

	// Send command
	s3cmci_send_command(host, cmd);

	// Enable Interrupt
	enable_irq(host->irq);
}

static void s3cmci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
 	struct s3cmci_host *host = mmc_priv(mmc);

	host->cmd_is_stop = 0;
	host->mrq = mrq;

	s3cmci_send_request(mmc);
}

static void s3cmci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct s3cmci_host *host = mmc_priv(mmc);
	u32 mci_psc, mci_con;

	//Set power
	mci_con = readl(host->base + S3C2410_SDICON);
	switch(ios->power_mode) {
		case MMC_POWER_ON:
		case MMC_POWER_UP:
			s3c2410_gpio_cfgpin(S3C2410_GPE5, S3C2410_GPE5_SDCLK);
			s3c2410_gpio_cfgpin(S3C2410_GPE6, S3C2410_GPE6_SDCMD);
			s3c2410_gpio_cfgpin(S3C2410_GPE7, S3C2410_GPE7_SDDAT0);
			s3c2410_gpio_cfgpin(S3C2410_GPE8, S3C2410_GPE8_SDDAT1);
			s3c2410_gpio_cfgpin(S3C2410_GPE9, S3C2410_GPE9_SDDAT2);
			s3c2410_gpio_cfgpin(S3C2410_GPE10, S3C2410_GPE10_SDDAT3);

			if (host->pdata->set_power)
				host->pdata->set_power(ios->power_mode, ios->vdd);

			if (!host->is2440)
				mci_con|=S3C2410_SDICON_FIFORESET;

			break;

		case MMC_POWER_OFF:
		default:
			s3c2410_gpio_setpin(S3C2410_GPE5, 0);
			s3c2410_gpio_cfgpin(S3C2410_GPE5, S3C2410_GPE5_OUTP);

			if (host->pdata->set_power)
				host->pdata->set_power(ios->power_mode, ios->vdd);

			if (host->is2440)
				mci_con|=S3C2440_SDICON_SDRESET;

			break;
	}

	//Set clock
	for (mci_psc=0; mci_psc<255; mci_psc++) {
		host->real_rate = host->clk_rate / (host->clk_div*(mci_psc+1));

		if (host->real_rate <= ios->clock)
			break;
	}

	if(mci_psc > 255) mci_psc = 255;
	host->prescaler = mci_psc;

	writel(host->prescaler, host->base + S3C2410_SDIPRE);

	//If requested clock is 0, real_rate will be 0, too
	if (ios->clock == 0)
		host->real_rate = 0;

	//Set CLOCK_ENABLE
	if (ios->clock)
		mci_con |= S3C2410_SDICON_CLOCKTYPE;
	else
		mci_con &=~S3C2410_SDICON_CLOCKTYPE;

	writel(mci_con, host->base + S3C2410_SDICON);

	if ((ios->power_mode==MMC_POWER_ON)
		|| (ios->power_mode==MMC_POWER_UP)) {

		dbg(host, dbg_conf, "running at %lukHz (requested: %ukHz).\n",
			host->real_rate/1000, ios->clock/1000);
	} else {
		dbg(host, dbg_conf, "powered down.\n");
	}

	host->bus_width = ios->bus_width;

}

static void s3cmci_reset(struct s3cmci_host *host)
{
	u32 con = readl(host->base + S3C2410_SDICON);

	con |= S3C2440_SDICON_SDRESET;

	writel(con, host->base + S3C2410_SDICON);
}

static int s3cmci_get_ro(struct mmc_host *mmc)
{
	struct s3cmci_host *host = mmc_priv(mmc);

	if (host->pdata->gpio_wprotect == 0)
		return 0;

	return s3c2410_gpio_getpin(host->pdata->gpio_wprotect);
}

static struct mmc_host_ops s3cmci_ops = {
	.request	= s3cmci_request,
	.set_ios	= s3cmci_set_ios,
	.get_ro		= s3cmci_get_ro,
};

static struct s3c24xx_mci_pdata s3cmci_def_pdata = {
	.gpio_detect	= S3C2410_GPG8, /* by www.100ask.net */
	.set_power	= NULL,
	.ocr_avail	= MMC_VDD_32_33,
};

static int s3cmci_probe(struct platform_device *pdev, int is2440)
{
	struct mmc_host 	*mmc;
	struct s3cmci_host 	*host;

	int ret;

	mmc = mmc_alloc_host(sizeof(struct s3cmci_host), &pdev->dev);
	if (!mmc) {
		ret = -ENOMEM;
		goto probe_out;
	}

	host = mmc_priv(mmc);
	host->mmc 	= mmc;
	host->pdev	= pdev;

	host->pdata = pdev->dev.platform_data;
	if (!host->pdata) {
		pdev->dev.platform_data = &s3cmci_def_pdata;
		host->pdata = &s3cmci_def_pdata;
	}

	spin_lock_init(&host->complete_lock);
	tasklet_init(&host->pio_tasklet, pio_tasklet, (unsigned long) host);
	if (is2440) {
		host->is2440	= 1;
		host->sdiimsk	= S3C2440_SDIIMSK;
		host->sdidata	= S3C2440_SDIDATA;
		host->clk_div	= 1;
	} else {
		host->is2440	= 0;
		host->sdiimsk	= S3C2410_SDIIMSK;
		host->sdidata	= S3C2410_SDIDATA;
		host->clk_div	= 2;
	}
	host->dodma		= 0;
	host->complete_what 	= COMPLETION_NONE;
	host->pio_active 	= XFER_NONE;

	host->dma		= S3CMCI_DMA;
	host->irq_cd = s3c2410_gpio_getirq(host->pdata->gpio_detect);
	s3c2410_gpio_cfgpin(host->pdata->gpio_detect, S3C2410_GPIO_IRQ);
	s3c2410_gpio_pullup(host->pdata->gpio_detect, 0); // pull-up enable, www.100ask.net

	host->mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!host->mem) {
		dev_err(&pdev->dev,
			"failed to get io memory region resouce.\n");

		ret = -ENOENT;
		goto probe_free_host;
	}

	host->mem = request_mem_region(host->mem->start,
		RESSIZE(host->mem), pdev->name);

	if (!host->mem) {
		dev_err(&pdev->dev, "failed to request io memory region.\n");
		ret = -ENOENT;
		goto probe_free_host;
	}

	host->base = ioremap(host->mem->start, RESSIZE(host->mem));
	if (host->base == 0) {
		dev_err(&pdev->dev, "failed to ioremap() io memory region.\n");
		ret = -EINVAL;
		goto probe_free_mem_region;
	}

	host->irq = platform_get_irq(pdev, 0);
	if (host->irq == 0) {
		dev_err(&pdev->dev, "failed to get interrupt resouce.\n");
		ret = -EINVAL;
		goto probe_iounmap;
	}

	if (request_irq(host->irq, s3cmci_irq, 0, DRIVER_NAME, host)) {
		dev_err(&pdev->dev, "failed to request mci interrupt.\n");
		ret = -ENOENT;
		goto probe_iounmap;
	}

	disable_irq(host->irq);

	s3c2410_gpio_cfgpin(host->pdata->gpio_detect, S3C2410_GPIO_IRQ);
	set_irq_type(host->irq_cd, IRQT_BOTHEDGE);

	if (request_irq(host->irq_cd, s3cmci_irq_cd, 0, DRIVER_NAME, host)) {
		dev_err(&pdev->dev,
			"failed to request card detect interrupt.\n");

		ret = -ENOENT;
		goto probe_free_irq;
	}

	if (host->pdata->gpio_wprotect)
		s3c2410_gpio_cfgpin(host->pdata->gpio_wprotect,
				    S3C2410_GPIO_INPUT);

	if (s3c2410_dma_request(S3CMCI_DMA, &s3cmci_dma_client, NULL)) {
		dev_err(&pdev->dev, "unable to get DMA channel.\n");
		ret = -EBUSY;
		goto probe_free_irq_cd;
	}

	host->clk = clk_get(&pdev->dev, "sdi");
	if (IS_ERR(host->clk)) {
		dev_err(&pdev->dev, "failed to find clock source.\n");
		ret = PTR_ERR(host->clk);
		host->clk = NULL;
		goto probe_free_host;
	}

	if ((ret = clk_enable(host->clk))) {
		dev_err(&pdev->dev, "failed to enable clock source.\n");
		goto clk_free;
	}

	host->clk_rate = clk_get_rate(host->clk);

	mmc->ops 	= &s3cmci_ops;
	mmc->ocr_avail	= host->pdata->ocr_avail;
	mmc->caps	= MMC_CAP_4_BIT_DATA;
	mmc->f_min 	= host->clk_rate / (host->clk_div * 256);
	mmc->f_max 	= host->clk_rate / host->clk_div;

	mmc->max_blk_count	= 4095;
	mmc->max_blk_size	= 4095;
	mmc->max_req_size	= 4095 * 512;
	mmc->max_seg_size	= mmc->max_req_size;

	mmc->max_phys_segs	= 128;
	mmc->max_hw_segs	= 128;

	dbg(host, dbg_debug, "probe: mode:%s mapped mci_base:%p irq:%u irq_cd:%u dma:%u.\n",
		(host->is2440?"2440":""),
		host->base, host->irq, host->irq_cd, host->dma);

	if ((ret = mmc_add_host(mmc))) {
		dev_err(&pdev->dev, "failed to add mmc host.\n");
		goto free_dmabuf;
	}

	platform_set_drvdata(pdev, mmc);

	dev_info(&pdev->dev,"initialisation done.\n");
	return 0;

 free_dmabuf:
	clk_disable(host->clk);

 clk_free:
	clk_put(host->clk);

 probe_free_irq_cd:
 	free_irq(host->irq_cd, host);

 probe_free_irq:
 	free_irq(host->irq, host);

 probe_iounmap:
	iounmap(host->base);

 probe_free_mem_region:
	release_mem_region(host->mem->start, RESSIZE(host->mem));

 probe_free_host:
	mmc_free_host(mmc);
 probe_out:
	return ret;
}

static int s3cmci_remove(struct platform_device *pdev)
{
	struct mmc_host 	*mmc  = platform_get_drvdata(pdev);
	struct s3cmci_host 	*host = mmc_priv(mmc);

	mmc_remove_host(mmc);
	clk_disable(host->clk);
	clk_put(host->clk);
	s3c2410_dma_free(S3CMCI_DMA, &s3cmci_dma_client);
 	free_irq(host->irq_cd, host);
 	free_irq(host->irq, host);
	iounmap(host->base);
	release_mem_region(host->mem->start, RESSIZE(host->mem));
	mmc_free_host(mmc);

	return 0;
}

static int s3cmci_probe_2410(struct platform_device *dev)
{
	return s3cmci_probe(dev, 0);
}

static int s3cmci_probe_2412(struct platform_device *dev)
{
	return s3cmci_probe(dev, 1);
}

static int s3cmci_probe_2440(struct platform_device *dev)
{
	return s3cmci_probe(dev, 1);
}

#ifdef CONFIG_PM

static int s3cmci_suspend(struct platform_device *dev, pm_message_t state)
{
	struct mmc_host *mmc = platform_get_drvdata(dev);

	return  mmc_suspend_host(mmc, state);
}

static int s3cmci_resume(struct platform_device *dev)
{
	struct mmc_host *mmc = platform_get_drvdata(dev);

	return mmc_resume_host(mmc);
}

#else /* CONFIG_PM */
#define s3cmci_suspend NULL
#define s3cmci_resume NULL
#endif /* CONFIG_PM */


static struct platform_driver s3cmci_driver_2410 =
{
	.driver.name	= "s3c2410-sdi",
	.probe		= s3cmci_probe_2410,
	.remove		= s3cmci_remove,
	.suspend	= s3cmci_suspend,
	.resume		= s3cmci_resume,
};

static struct platform_driver s3cmci_driver_2412 =
{
	.driver.name	= "s3c2412-sdi",
	.probe		= s3cmci_probe_2412,
	.remove		= s3cmci_remove,
	.suspend	= s3cmci_suspend,
	.resume		= s3cmci_resume,
};

static struct platform_driver s3cmci_driver_2440 =
{
	.driver.name	= "s3c2440-sdi",
	.probe		= s3cmci_probe_2440,
	.remove		= s3cmci_remove,
	.suspend	= s3cmci_suspend,
	.resume		= s3cmci_resume,
};


static int __init s3cmci_init(void)
{
	platform_driver_register(&s3cmci_driver_2410);
	platform_driver_register(&s3cmci_driver_2412);
	platform_driver_register(&s3cmci_driver_2440);
	return 0;
}

static void __exit s3cmci_exit(void)
{
	platform_driver_unregister(&s3cmci_driver_2410);
	platform_driver_unregister(&s3cmci_driver_2412);
	platform_driver_unregister(&s3cmci_driver_2440);
}

module_init(s3cmci_init);
module_exit(s3cmci_exit);

MODULE_DESCRIPTION("Samsung S3C MMC/SD Card Interface driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Thomas Kleffel <tk@maintech.de>");

