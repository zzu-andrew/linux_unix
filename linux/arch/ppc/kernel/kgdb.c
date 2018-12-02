/*
 * arch/ppc/kernel/kgdb.c
 *
 * PowerPC backend to the KGDB stub.
 *
 * Maintainer: Tom Rini <trini@kernel.crashing.org>
 *
 * 1998 (c) Michael AK Tesch (tesch@cs.wisc.edu)
 * Copyright (C) 2003 Timesys Corporation.
 * Copyright (C) 2004, 2006 MontaVista Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program as licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kgdb.h>
#include <linux/smp.h>
#include <linux/signal.h>
#include <linux/ptrace.h>
p#include <asm/current.h>
#include <asm/processor.h>
#include <asm/machdep.h>

/*
 * This table contains the mapping between PowerPC hardware trap types, and
 * signals, which are primarily what GDB understands.  GDB and the kernel
 * don't always agree on values, so we use constants taken from gdb-6.2.
 */
static struct hard_trap_info
{
	unsigned int tt;		/* Trap type code for powerpc */
	unsigned char signo;		/* Signal that we map this trap into */
} hard_trap_info[] = {
	{ 0x0100, 0x02 /* SIGINT */  },		/* system reset */
	{ 0x0200, 0x0b /* SIGSEGV */ },		/* machine check */
	{ 0x0300, 0x0b /* SIGSEGV */ },		/* data access */
	{ 0x0400, 0x0b /* SIGSEGV */ },		/* instruction access */
	{ 0x0500, 0x02 /* SIGINT */  },		/* external interrupt */
	{ 0x0600, 0x0a /* SIGBUS */  },		/* alignment */
	{ 0x0700, 0x05 /* SIGTRAP */ },		/* program check */
	{ 0x0800, 0x08 /* SIGFPE */  },		/* fp unavailable */
	{ 0x0900, 0x0e /* SIGALRM */ },		/* decrementer */
	{ 0x0c00, 0x14 /* SIGCHLD */ },		/* system call */
#if defined(CONFIG_40x) || defined(CONFIG_BOOKE)
	{ 0x2002, 0x05 /* SIGTRAP */ },		/* debug */
#if defined(CONFIG_FSL_BOOKE)
	{ 0x2010, 0x08 /* SIGFPE */  },		/* spe unavailable */
	{ 0x2020, 0x08 /* SIGFPE */  },		/* spe unavailable */
	{ 0x2030, 0x08 /* SIGFPE */  },		/* spe fp data */
	{ 0x2040, 0x08 /* SIGFPE */  },		/* spe fp data */
	{ 0x2050, 0x08 /* SIGFPE */  },		/* spe fp round */
	{ 0x2060, 0x0e /* SIGILL */  },		/* performace monitor */
	{ 0x2900, 0x08 /* SIGFPE */  },		/* apu unavailable */
	{ 0x3100, 0x0e /* SIGALRM */ },		/* fixed interval timer */
	{ 0x3200, 0x02 /* SIGINT */  }, 	/* watchdog */
#else /* ! CONFIG_FSL_BOOKE */
	{ 0x1000, 0x0e /* SIGALRM */ },		/* prog interval timer */
	{ 0x1010, 0x0e /* SIGALRM */ },		/* fixed interval timer */
	{ 0x1020, 0x02 /* SIGINT */  }, 	/* watchdog */
	{ 0x2010, 0x08 /* SIGFPE */  },		/* fp unavailable */
	{ 0x2020, 0x08 /* SIGFPE */  },		/* ap unavailable */
#endif
#else /* ! (defined(CONFIG_40x) || defined(CONFIG_BOOKE)) */
	{ 0x0d00, 0x05 /* SIGTRAP */ },		/* single-step */
#if defined(CONFIG_8xx)
	{ 0x1000, 0x04 /* SIGILL */  },		/* software emulation */
#else /* ! CONFIG_8xx */
	{ 0x0f00, 0x04 /* SIGILL */  },		/* performance monitor */
	{ 0x0f20, 0x08 /* SIGFPE */  },		/* altivec unavailable */
	{ 0x1300, 0x05 /* SIGTRAP */ }, 	/* instruction address break */
	{ 0x1400, 0x02 /* SIGINT */  },		/* SMI */
	{ 0x1600, 0x08 /* SIGFPE */  },		/* altivec assist */
	{ 0x1700, 0x04 /* SIGILL */  },		/* TAU */
	{ 0x2000, 0x05 /* SIGTRAP */ },		/* run mode */
#endif
#endif
	{ 0x0000, 0x00 }			/* Must be last */
};

extern atomic_t cpu_doing_single_step;

static int computeSignal(unsigned int tt)
{
	struct hard_trap_info *ht;

	for (ht = hard_trap_info; ht->tt && ht->signo; ht++)
		if (ht->tt == tt)
			return ht->signo;

	return SIGHUP;		/* default for things we don't know about */
}

/* KGDB functions to use existing PowerPC hooks. */
static void kgdb_debugger(struct pt_regs *regs)
{
	kgdb_handle_exception(0, computeSignal(TRAP(regs)), 0, regs);
}

static int kgdb_breakpoint(struct pt_regs *regs)
{
	if (user_mode(regs))
		return 0;

	kgdb_handle_exception(0, SIGTRAP, 0, regs);

	if (*(u32 *) (regs->nip) == *(u32 *) (&arch_kgdb_ops.gdb_bpt_instr))
		regs->nip += 4;

	return 1;
}

static int kgdb_singlestep(struct pt_regs *regs)
{
	struct thread_info *thread_info, *exception_thread_info;

	if (user_mode(regs))
		return 0;
	/*
	* On Book E and perhaps other processsors, singlestep is handled on
	* the critical exception stack.  This causes current_thread_info()
	* to fail, since it it locates the thread_info by masking off
	* the low bits of the current stack pointer.  We work around
	* this issue by copying the thread_info from the kernel stack
	* before calling kgdb_handle_exception, and copying it back
	* afterwards.  On most processors the copy is avoided since
	* exception_thread_info == thread_info.
	*/
	thread_info = (struct thread_info *)(regs->gpr[1] & ~(THREAD_SIZE-1));
	exception_thread_info = current_thread_info();

	if (thread_info != exception_thread_info)
		memcpy(exception_thread_info, thread_info, sizeof *thread_info);

	kgdb_handle_exception(0, SIGTRAP, 0, regs);

	if (thread_info != exception_thread_info)
		memcpy(thread_info, exception_thread_info, sizeof *thread_info);

	return 1;
}

int kgdb_iabr_match(struct pt_regs *regs)
{
	if (user_mode(regs))
		return 0;

	kgdb_handle_exception(0, computeSignal(TRAP(regs)), 0, regs);
	return 1;
}

int kgdb_dabr_match(struct pt_regs *regs)
{
	if (user_mode(regs))
		return 0;

	kgdb_handle_exception(0, computeSignal(TRAP(regs)), 0, regs);
	return 1;
}

void regs_to_gdb_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	unsigned long *ptr = gdb_regs;
	int reg;

	memset(gdb_regs, 0, MAXREG * 4);

	for (reg = 0; reg < 32; reg++)
		*(ptr++) = regs->gpr[reg];

#ifdef CONFIG_FSL_BOOKE
#ifdef CONFIG_SPE
	for (reg = 0; reg < 32; reg++)
		*(ptr++) = current->thread.evr[reg];
#else
	ptr += 32;
#endif
#else
	ptr += 64;
#endif

	*(ptr++) = regs->nip;
	*(ptr++) = regs->msr;
	*(ptr++) = regs->ccr;
	*(ptr++) = regs->link;
	*(ptr++) = regs->ctr;
	*(ptr++) = regs->xer;

#ifdef CONFIG_SPE
	/* u64 acc */
	*(ptr++) = current->thread.acc >> 32;
	*(ptr++) = current->thread.acc & 0xffffffff;
	*(ptr++) = current->thread.spefscr;
#endif
}

void sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *p)
{
	struct pt_regs *regs = (struct pt_regs *)(p->thread.ksp +
						  STACK_FRAME_OVERHEAD);
	unsigned long *ptr = gdb_regs;
	int reg;

	memset(gdb_regs, 0, MAXREG * 4);

	/* Regs GPR0-2 */
	for (reg = 0; reg < 3; reg++)
		*(ptr++) = regs->gpr[reg];

	/* Regs GPR3-13 are not saved */
	ptr += 11;

	/* Regs GPR14-31 */
	for (reg = 14; reg < 32; reg++)
		*(ptr++) = regs->gpr[reg];

#ifdef CONFIG_FSL_BOOKE
#ifdef CONFIG_SPE
	for (reg = 0; reg < 32; reg++)
		*(ptr++) = p->thread.evr[reg];
#else
	ptr += 32;
#endif
#else
	ptr += 64;
#endif

	*(ptr++) = regs->nip;
	*(ptr++) = regs->msr;
	*(ptr++) = regs->ccr;
	*(ptr++) = regs->link;
	*(ptr++) = regs->ctr;
	*(ptr++) = regs->xer;

#ifdef CONFIG_SPE
	/* u64 acc */
	*(ptr++) = p->thread.acc >> 32;
	*(ptr++) = p->thread.acc & 0xffffffff;
	*(ptr++) = p->thread.spefscr;
#endif
}

void gdb_regs_to_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	unsigned long *ptr = gdb_regs;
	int reg;
#ifdef CONFIG_SPE
	union {
		u32 v32[2];
		u64 v64;
	} acc;
#endif

	for (reg = 0; reg < 32; reg++)
		regs->gpr[reg] = *(ptr++);

#ifdef CONFIG_FSL_BOOKE
#ifdef CONFIG_SPE
	for (reg = 0; reg < 32; reg++)
		current->thread.evr[reg] = *(ptr++);
#else
	ptr += 32;
#endif
#else
	ptr += 64;
#endif

	regs->nip = *(ptr++);
	regs->msr = *(ptr++);
	regs->ccr = *(ptr++);
	regs->link = *(ptr++);
	regs->ctr = *(ptr++);
	regs->xer = *(ptr++);

#ifdef CONFIG_SPE
	/* u64 acc */
	acc.v32[0] = *(ptr++);
	acc.v32[1] = *(ptr++);
	current->thread.acc = acc.v64;
	current->thread.spefscr = *(ptr++);
#endif
}

/*
 * This function does PowerPC specific processing for interfacing to gdb.
 */
int kgdb_arch_handle_exception(int vector, int signo, int err_code,
			       char *remcom_in_buffer, char *remcom_out_buffer,
			       struct pt_regs *linux_regs)
{
	char *ptr = &remcom_in_buffer[1];
	unsigned long addr;

	switch (remcom_in_buffer[0]) {
		/*
		 * sAA..AA   Step one instruction from AA..AA
		 * This will return an error to gdb ..
		 */
	case 's':
	case 'c':
		/* handle the optional parameter */
		if (kgdb_hex2long (&ptr, &addr))
			linux_regs->nip = addr;

		atomic_set(&cpu_doing_single_step, -1);
		/* set the trace bit if we're stepping */
		if (remcom_in_buffer[0] == 's') {
#if defined(CONFIG_40x) || defined(CONFIG_BOOKE)
			mtspr(SPRN_DBCR0,
				  mfspr(SPRN_DBCR0) | DBCR0_IC | DBCR0_IDM);
			linux_regs->msr |= MSR_DE;
#else
			linux_regs->msr |= MSR_SE;
#endif
			debugger_step = 1;
			if (kgdb_contthread)
				atomic_set(&cpu_doing_single_step,
						   smp_processor_id());
		}
		return 0;
	}

	return -1;
}

/*
 * Global data
 */
struct kgdb_arch arch_kgdb_ops = {
	.gdb_bpt_instr = {0x7d, 0x82, 0x10, 0x08},
};

int kgdb_arch_init(void)
{
	debugger = kgdb_debugger;
	debugger_bpt = kgdb_breakpoint;
	debugger_sstep = kgdb_singlestep;
	debugger_iabr_match = kgdb_iabr_match;
	debugger_dabr_match = kgdb_dabr_match;

	return 0;
}

arch_initcall(kgdb_arch_init);
