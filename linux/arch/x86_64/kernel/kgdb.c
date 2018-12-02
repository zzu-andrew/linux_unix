/*
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

/*
 * Copyright (C) 2004 Amit S. Kale <amitkale@linsyssoft.com>
 * Copyright (C) 2000-2001 VERITAS Software Corporation.
 * Copyright (C) 2002 Andi Kleen, SuSE Labs
 * Copyright (C) 2004 LinSysSoft Technologies Pvt. Ltd.
 * Copyright (C) 2007 Jason Wessel, Wind River Systems, Inc.
 */
/****************************************************************************
 *  Contributor:     Lake Stevens Instrument Division$
 *  Written by:      Glenn Engel $
 *  Updated by:	     Amit Kale<akale@veritas.com>
 *  Modified for 386 by Jim Kingdon, Cygnus Support.
 *  Origianl kgdb, compatibility with 2.1.xx kernel by
 *  David Grothe <dave@gcom.com>
 *  Integrated into 2.2.5 kernel by Tigran Aivazian <tigran@sco.com>
 *  X86_64 changes from Andi Kleen's patch merged by Jim Houston
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <asm/system.h>
#include <linux/ptrace.h>		/* for linux pt_regs struct */
#include <linux/kgdb.h>
#include <linux/init.h>
#include <linux/kdebug.h>
#include <asm/apicdef.h>
#include <asm/mach_apic.h>
#include <asm/debugreg.h>

/* Put the error code here just in case the user cares.  */
int gdb_x86_64errcode;
/* Likewise, the vector number here (since GDB only gets the signal
   number through the usual means, and that's not very specific).  */
int gdb_x86_64vector = -1;

extern atomic_t cpu_doing_single_step;

void regs_to_gdb_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	gdb_regs[_RAX] = regs->rax;
	gdb_regs[_RBX] = regs->rbx;
	gdb_regs[_RCX] = regs->rcx;
	gdb_regs[_RDX] = regs->rdx;
	gdb_regs[_RSI] = regs->rsi;
	gdb_regs[_RDI] = regs->rdi;
	gdb_regs[_RBP] = regs->rbp;
	gdb_regs[_PS] = regs->eflags;
	gdb_regs[_PC] = regs->rip;
	gdb_regs[_R8] = regs->r8;
	gdb_regs[_R9] = regs->r9;
	gdb_regs[_R10] = regs->r10;
	gdb_regs[_R11] = regs->r11;
	gdb_regs[_R12] = regs->r12;
	gdb_regs[_R13] = regs->r13;
	gdb_regs[_R14] = regs->r14;
	gdb_regs[_R15] = regs->r15;
	gdb_regs[_RSP] = regs->rsp;
}

extern void thread_return(void);
void sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *p)
{
	gdb_regs[_RAX] = 0;
	gdb_regs[_RBX] = 0;
	gdb_regs[_RCX] = 0;
	gdb_regs[_RDX] = 0;
	gdb_regs[_RSI] = 0;
	gdb_regs[_RDI] = 0;
	gdb_regs[_RBP] = *(unsigned long *)p->thread.rsp;
	gdb_regs[_PS] = *(unsigned long *)(p->thread.rsp + 8);
	gdb_regs[_PC] = (unsigned long)&thread_return;
	gdb_regs[_R8] = 0;
	gdb_regs[_R9] = 0;
	gdb_regs[_R10] = 0;
	gdb_regs[_R11] = 0;
	gdb_regs[_R12] = 0;
	gdb_regs[_R13] = 0;
	gdb_regs[_R14] = 0;
	gdb_regs[_R15] = 0;
	gdb_regs[_RSP] = p->thread.rsp;
}

void gdb_regs_to_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	regs->rax = gdb_regs[_RAX];
	regs->rbx = gdb_regs[_RBX];
	regs->rcx = gdb_regs[_RCX];
	regs->rdx = gdb_regs[_RDX];
	regs->rsi = gdb_regs[_RSI];
	regs->rdi = gdb_regs[_RDI];
	regs->rbp = gdb_regs[_RBP];
	regs->eflags = gdb_regs[_PS];
	regs->rip = gdb_regs[_PC];
	regs->r8 = gdb_regs[_R8];
	regs->r9 = gdb_regs[_R9];
	regs->r10 = gdb_regs[_R10];
	regs->r11 = gdb_regs[_R11];
	regs->r12 = gdb_regs[_R12];
	regs->r13 = gdb_regs[_R13];
	regs->r14 = gdb_regs[_R14];
	regs->r15 = gdb_regs[_R15];
}				/* gdb_regs_to_regs */

struct hw_breakpoint {
	unsigned enabled;
	unsigned type;
	unsigned len;
	unsigned long addr;
} breakinfo[4] = { {
enabled:0}, {
enabled:0}, {
enabled:0}, {
enabled:0}};

static void kgdb_correct_hw_break(void)
{
	int breakno;
	int correctit;
	int breakbit;
	unsigned long dr7;

	get_debugreg(dr7, 7);
	correctit = 0;
	for (breakno = 0; breakno < 3; breakno++) {
		breakbit = 2 << (breakno << 1);
		if (!(dr7 & breakbit) && breakinfo[breakno].enabled) {
			correctit = 1;
			dr7 |= breakbit;
			dr7 &= ~(0xf0000 << (breakno << 2));
			dr7 |= (((breakinfo[breakno].len << 2) |
				 breakinfo[breakno].type) << 16) <<
			    (breakno << 2);
			switch (breakno) {
			case 0:
				set_debugreg(breakinfo[breakno].addr, 0);
				break;

			case 1:
				set_debugreg(breakinfo[breakno].addr, 1);
				break;

			case 2:
				set_debugreg(breakinfo[breakno].addr, 2);
				break;

			case 3:
				set_debugreg(breakinfo[breakno].addr, 3);
				break;
			}
		} else if ((dr7 & breakbit) && !breakinfo[breakno].enabled) {
			correctit = 1;
			dr7 &= ~breakbit;
			dr7 &= ~(0xf0000 << (breakno << 2));
		}
	}
	if (correctit)
		set_debugreg(dr7, 7);
}

static int kgdb_remove_hw_break(unsigned long addr, int len,
						 enum kgdb_bptype bptype)
{
	int i, idx = -1;
	for (i = 0; i < 4; i++) {
		if (breakinfo[i].addr == addr && breakinfo[i].enabled) {
			idx = i;
			break;
		}
	}
	if (idx == -1)
		return -1;

	breakinfo[idx].enabled = 0;
	return 0;
}

static void kgdb_remove_all_hw_break(void)
{
	int i;

	for (i = 0; i < 4; i++) {
		memset(&breakinfo[i], 0, sizeof(struct hw_breakpoint));
	}
}

static int kgdb_set_hw_break(unsigned long addr, int len,
					  enum kgdb_bptype bptype)
{
	int i, idx = -1;
	for (i = 0; i < 4; i++) {
		if (!breakinfo[i].enabled) {
			idx = i;
			break;
		}
	}
	if (idx == -1)
		return -1;
	if (bptype == bp_hardware_breakpoint) {
		breakinfo[idx].type = 0;
		breakinfo[idx].len = 0;
	} else if (bptype == bp_write_watchpoint) {
		breakinfo[idx].type = 1;
		if (len == 1 || len == 2 || len == 4)
			breakinfo[idx].len = len - 1;
		else
			return -1;
	} else if (bptype == bp_access_watchpoint) {
		breakinfo[idx].type = 3;
		if (len == 1 || len == 2 || len == 4)
			breakinfo[idx].len = len - 1;
		else
			return -1;
	} else
		return -1;
	breakinfo[idx].enabled = 1;
	breakinfo[idx].addr = addr;
	return 0;
}

void kgdb_disable_hw_debug(struct pt_regs *regs)
{
	/* Disable hardware debugging while we are in kgdb */
	set_debugreg(0UL, 7);
}

void kgdb_post_master_code(struct pt_regs *regs, int e_vector, int err_code)
{
	/* Master processor is completely in the debugger */
	gdb_x86_64vector = e_vector;
	gdb_x86_64errcode = err_code;
}

void kgdb_roundup_cpus(unsigned long flags)
{
	send_IPI_allbutself(APIC_DM_NMI);
}

int kgdb_arch_handle_exception(int e_vector, int signo, int err_code,
			       char *remcomInBuffer, char *remcomOutBuffer,
			       struct pt_regs *linux_regs)
{
	unsigned long addr;
	unsigned long breakno;
	char *ptr;
	int newPC;
	unsigned long dr6;

	switch (remcomInBuffer[0]) {
	case 'c':
	case 's':
		/* try to read optional parameter, pc unchanged if no parm */
		ptr = &remcomInBuffer[1];
		if (kgdb_hex2long(&ptr, &addr))
			linux_regs->rip = addr;
		newPC = linux_regs->rip;

		/* clear the trace bit */
		linux_regs->eflags &= ~TF_MASK;

		atomic_set(&cpu_doing_single_step, -1);
		/* set the trace bit if we're stepping */
		if (remcomInBuffer[0] == 's') {
			linux_regs->eflags |= TF_MASK;
			debugger_step = 1;
			if (kgdb_contthread)
				atomic_set(&cpu_doing_single_step,
					   raw_smp_processor_id());

		}

		get_debugreg(dr6, 6);
		if (!(dr6 & 0x4000)) {
			for (breakno = 0; breakno < 4; ++breakno) {
				if (dr6 & (1 << breakno)) {
					if (breakinfo[breakno].type == 0) {
						/* Set restore flag */
						linux_regs->eflags |=
						    X86_EFLAGS_RF;
						break;
					}
				}
			}
		}
		set_debugreg(0UL, 6);
		kgdb_correct_hw_break();

		return (0);
	}			/* switch */
	return -1;
}

static struct pt_regs *in_interrupt_stack(unsigned long rsp, int cpu)
{
	struct pt_regs *regs;
	unsigned long end = (unsigned long)cpu_pda(cpu)->irqstackptr;
	if (rsp <= end && rsp >= end - IRQSTACKSIZE + 8) {
		regs = *(((struct pt_regs **)end) - 1);
		return regs;
	}
	return NULL;
}

static struct pt_regs *in_exception_stack(unsigned long rsp, int cpu)
{
	int i;
	struct tss_struct *init_tss = &__get_cpu_var(init_tss);
	for (i = 0; i < N_EXCEPTION_STACKS; i++)
		if (rsp >= init_tss[cpu].ist[i] &&
		    rsp <= init_tss[cpu].ist[i] + EXCEPTION_STKSZ) {
			struct pt_regs *r =
			    (void *)init_tss[cpu].ist[i] + EXCEPTION_STKSZ;
			return r - 1;
		}
	return NULL;
}

void kgdb_shadowinfo(struct pt_regs *regs, char *buffer, unsigned threadid)
{
	static char intr_desc[] = "Stack at interrupt entrypoint";
	static char exc_desc[] = "Stack at exception entrypoint";
	struct pt_regs *stregs;
	int cpu = raw_smp_processor_id();
	stregs = in_interrupt_stack(regs->rsp, cpu);
	if (stregs)
		kgdb_mem2hex(intr_desc, buffer, strlen(intr_desc));
	else {
		stregs = in_exception_stack(regs->rsp, cpu);
		if (stregs)
			kgdb_mem2hex(exc_desc, buffer, strlen(exc_desc));
	}
}

struct task_struct *kgdb_get_shadow_thread(struct pt_regs *regs, int threadid)
{
	struct pt_regs *stregs;
	int cpu = raw_smp_processor_id();

	stregs = in_interrupt_stack(regs->rsp, cpu);
	if (stregs)
		return current;
	else {
		stregs = in_exception_stack(regs->rsp, cpu);
		if (stregs)
			return current;
	}

	return NULL;
}

struct pt_regs *kgdb_shadow_regs(struct pt_regs *regs, int threadid)
{
	struct pt_regs *stregs;
	int cpu = raw_smp_processor_id();

	stregs = in_interrupt_stack(regs->rsp, cpu);
	if (stregs)
		return stregs;
	else {
		stregs = in_exception_stack(regs->rsp, cpu);
		if (stregs)
			return stregs;
	}

	return NULL;
}

/* Register KGDB with the die_chain so that we hook into all of the right
 * spots. */
static int kgdb_notify(struct notifier_block *self, unsigned long cmd,
		       void *ptr)
{
	struct die_args *args = ptr;
	struct pt_regs *regs = args->regs;

	if (cmd == DIE_PAGE_FAULT_NO_CONTEXT && atomic_read(&debugger_active)
			&& kgdb_may_fault) {
		kgdb_fault_longjmp(kgdb_fault_jmp_regs);
		return NOTIFY_STOP;
	/* CPU roundup? */
	} else if (atomic_read(&debugger_active) && cmd == DIE_NMI_IPI) {
		kgdb_nmihook(raw_smp_processor_id(), regs);
		return NOTIFY_STOP;
		/* See if KGDB is interested. */
	} else if (cmd == DIE_DEBUG
			   && atomic_read(&cpu_doing_single_step) ==
			   raw_smp_processor_id()
			   && user_mode(regs)) {
		/* single step exception from kernel space to user space so
		 * eat the exception and continue the process
		 */
		printk(KERN_ERR "KGDB: trap/step from kernel to user space,"
			   " resuming...\n");
		kgdb_arch_handle_exception(args->trapnr, args->signr,
		args->err, "c", "", regs);
		return NOTIFY_STOP;
	} else if (cmd == DIE_PAGE_FAULT || user_mode(regs) ||
		   cmd == DIE_NMI_IPI || (cmd == DIE_DEBUG &&
					  atomic_read(&debugger_active)))
		/* Userpace events, normal watchdog event, or spurious
		 * debug exception.  Ignore. */
		return NOTIFY_DONE;

	kgdb_handle_exception(args->trapnr, args->signr, args->err, regs);

	return NOTIFY_STOP;
}

static struct notifier_block kgdb_notifier = {
	.notifier_call = kgdb_notify,
	.priority = 0x7fffffff,	/* we need to notified first */
};

int kgdb_arch_init(void)
{
	register_die_notifier(&kgdb_notifier);
	return 0;
}
/*
 * Skip an int3 exception when it occurs after a breakpoint has been
 * removed. Backtrack eip by 1 since the int3 would have caused it to
 * increment by 1.
 */

int kgdb_skipexception(int exception, struct pt_regs *regs)
{
	if (exception == 3 && kgdb_isremovedbreak(regs->rip - 1)) {
		regs->rip -= 1;
		return 1;
	}
	return 0;
}

unsigned long kgdb_arch_pc(int exception, struct pt_regs *regs)
{
	if (exception == 3) {
		return instruction_pointer(regs) - 1;
	}
	return instruction_pointer(regs);
}

struct kgdb_arch arch_kgdb_ops = {
	.gdb_bpt_instr = {0xcc},
	.flags = KGDB_HW_BREAKPOINT,
	.shadowth = 1,
	.set_hw_breakpoint = kgdb_set_hw_break,
	.remove_hw_breakpoint = kgdb_remove_hw_break,
	.remove_all_hw_break = kgdb_remove_all_hw_break,
	.correct_hw_break = kgdb_correct_hw_break,
};
