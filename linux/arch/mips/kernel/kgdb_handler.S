/*
 * arch/mips/kernel/kgdb_handler.S
 *
 * Copyright (C) 2007 Wind River Systems, Inc
 *
 * Copyright (C) 2004-2005 MontaVista Software Inc.
 * Author: Manish Lachwani, mlachwani@mvista.com or manish@koffee-break.com
 *
 * This file is licensed under the terms of the GNU General Public
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

/*
 * Trap Handler for the new KGDB framework. The main KGDB handler is
 * handle_exception that will be called from here
 *
 */

#include <linux/sys.h>

#include <asm/asm.h>
#include <asm/errno.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/gdb-stub.h>

#ifdef CONFIG_32BIT
#define DMFC0	mfc0
#define DMTC0	mtc0
#define LDC1	lwc1
#define SDC1	swc1
#endif
#ifdef CONFIG_64BIT
#define DMFC0	dmfc0
#define DMTC0	dmtc0
#define LDC1	ldc1
#define SDC1	sdc1
#endif

#include <asm/asmmacro.h>

/*
 * [jsun] We reserves about 2x GDB_FR_SIZE in stack.  The lower (addressed)
 * part is used to store registers and passed to exception handler.
 * The upper part is reserved for "call func" feature where gdb client
 * saves some of the regs, setups call frame and passes args.
 *
 * A trace shows about 200 bytes are used to store about half of all regs.
 * The rest should be big enough for frame setup and passing args.
 */

/*
 * The low level trap handler
 */
		.align 	5
		NESTED(trap_low, GDB_FR_SIZE, sp)
 		.set	noat
		.set 	noreorder

		mfc0	k0, CP0_STATUS
		sll	k0, 3     		/* extract cu0 bit */
		bltz	k0, 1f
		move	k1, sp

		/*
		 * Called from user mode, go somewhere else.
		 */
#if defined(CONFIG_32BIT)
		lui	k1, %hi(saved_vectors)
		mfc0	k0, CP0_CAUSE
		andi	k0, k0, 0x7c
		add	k1, k1, k0
		lw	k0, %lo(saved_vectors)(k1)
#elif defined(CONFIG_64BIT) && defined(CONFIG_BUILD_ELF64)
		DMFC0	k0, CP0_CAUSE
		lui	k1, %highest(saved_vectors)
                andi    k0, k0, 0x7c           /* mask exception type */
                dsll    k0, 1                  /* turn into byte offset */
		daddiu	k1, %higher(saved_vectors)
		dsll	k1, k1, 16
		daddiu	k1, %hi(saved_vectors)
		dsll	k1, k1, 16
		daddu	k1, k1, k0
		LONG_L	k0, %lo(saved_vectors)(k1)
#else
#error "MIPS configuration is unsupported for kgdb!!"
#endif
		jr	k0
		nop
1:
		move	k0, sp
		PTR_SUBU sp, k1, GDB_FR_SIZE*2	# see comment above
		LONG_S	k0, GDB_FR_REG29(sp)
		LONG_S	$2, GDB_FR_REG2(sp)

/*
 * First save the CP0 and special registers
 */

		mfc0	v0, CP0_STATUS
		LONG_S	v0, GDB_FR_STATUS(sp)
		mfc0	v0, CP0_CAUSE
		LONG_S	v0, GDB_FR_CAUSE(sp)
		DMFC0	v0, CP0_EPC
		LONG_S	v0, GDB_FR_EPC(sp)
		DMFC0	v0, CP0_BADVADDR
		LONG_S	v0, GDB_FR_BADVADDR(sp)
		mfhi	v0
		LONG_S	v0, GDB_FR_HI(sp)
		mflo	v0
		LONG_S	v0, GDB_FR_LO(sp)

/*
 * Now the integer registers
 */

		LONG_S	zero, GDB_FR_REG0(sp)		/* I know... */
		LONG_S	$1, GDB_FR_REG1(sp)
		/* v0 already saved */
		LONG_S	$3, GDB_FR_REG3(sp)
		LONG_S	$4, GDB_FR_REG4(sp)
		LONG_S	$5, GDB_FR_REG5(sp)
		LONG_S	$6, GDB_FR_REG6(sp)
		LONG_S	$7, GDB_FR_REG7(sp)
		LONG_S	$8, GDB_FR_REG8(sp)
		LONG_S	$9, GDB_FR_REG9(sp)
		LONG_S	$10, GDB_FR_REG10(sp)
		LONG_S	$11, GDB_FR_REG11(sp)
		LONG_S	$12, GDB_FR_REG12(sp)
		LONG_S	$13, GDB_FR_REG13(sp)
		LONG_S	$14, GDB_FR_REG14(sp)
		LONG_S	$15, GDB_FR_REG15(sp)
		LONG_S	$16, GDB_FR_REG16(sp)
		LONG_S	$17, GDB_FR_REG17(sp)
		LONG_S	$18, GDB_FR_REG18(sp)
		LONG_S	$19, GDB_FR_REG19(sp)
		LONG_S	$20, GDB_FR_REG20(sp)
		LONG_S	$21, GDB_FR_REG21(sp)
		LONG_S	$22, GDB_FR_REG22(sp)
		LONG_S	$23, GDB_FR_REG23(sp)
		LONG_S	$24, GDB_FR_REG24(sp)
		LONG_S	$25, GDB_FR_REG25(sp)
		LONG_S	$26, GDB_FR_REG26(sp)
		LONG_S	$27, GDB_FR_REG27(sp)
		LONG_S	$28, GDB_FR_REG28(sp)
		/* sp already saved */
		LONG_S	$30, GDB_FR_REG30(sp)
		LONG_S	$31, GDB_FR_REG31(sp)

		CLI				/* disable interrupts */

/*
 * Followed by the floating point registers
 */
		mfc0	v0, CP0_STATUS		/* FPU enabled? */
		srl	v0, v0, 16
		andi	v0, v0, (ST0_CU1 >> 16)

		beqz	v0,3f			/* disabled, skip */
		 nop

		li	t0, 0
#ifdef CONFIG_64BIT
		mfc0	t0, CP0_STATUS
#endif
		fpu_save_double_kgdb sp t0 t1	# clobbers t1


/*
 * Current stack frame ptr
 */

3:
		LONG_S	sp, GDB_FR_FRP(sp)

/*
 * CP0 registers (R4000/R4400 unused registers skipped)
 */

		mfc0	v0, CP0_INDEX
		LONG_S	v0, GDB_FR_CP0_INDEX(sp)
		mfc0	v0, CP0_RANDOM
		LONG_S	v0, GDB_FR_CP0_RANDOM(sp)
		DMFC0	v0, CP0_ENTRYLO0
		LONG_S	v0, GDB_FR_CP0_ENTRYLO0(sp)
		DMFC0	v0, CP0_ENTRYLO1
		LONG_S	v0, GDB_FR_CP0_ENTRYLO1(sp)
		DMFC0	v0, CP0_CONTEXT
		LONG_S	v0, GDB_FR_CP0_CONTEXT(sp)
		mfc0	v0, CP0_PAGEMASK
		LONG_S	v0, GDB_FR_CP0_PAGEMASK(sp)
		mfc0	v0, CP0_WIRED
		LONG_S	v0, GDB_FR_CP0_WIRED(sp)
		DMFC0	v0, CP0_ENTRYHI
		LONG_S	v0, GDB_FR_CP0_ENTRYHI(sp)
		mfc0	v0, CP0_PRID
		LONG_S	v0, GDB_FR_CP0_PRID(sp)

		.set	at

/*
 * Continue with the higher level handler
 */

		move	a0,sp

		jal	handle_exception
		 nop

/*
 * Restore all writable registers, in reverse order
 */

		.set	noat

		LONG_L	v0, GDB_FR_CP0_ENTRYHI(sp)
		LONG_L	v1, GDB_FR_CP0_WIRED(sp)
		DMTC0	v0, CP0_ENTRYHI
		mtc0	v1, CP0_WIRED
		LONG_L	v0, GDB_FR_CP0_PAGEMASK(sp)
		LONG_L	v1, GDB_FR_CP0_ENTRYLO1(sp)
		mtc0	v0, CP0_PAGEMASK
		DMTC0	v1, CP0_ENTRYLO1
		LONG_L	v0, GDB_FR_CP0_ENTRYLO0(sp)
		LONG_L	v1, GDB_FR_CP0_INDEX(sp)
		DMTC0	v0, CP0_ENTRYLO0
		LONG_L	v0, GDB_FR_CP0_CONTEXT(sp)
		mtc0	v1, CP0_INDEX
		DMTC0	v0, CP0_CONTEXT


/*
 * Next, the floating point registers
 */
		mfc0	v0, CP0_STATUS		/* check if FPU is enabled */
		srl	v0, v0, 16
		andi	v0, v0, (ST0_CU1 >> 16)

		beqz	v0, 3f			/* disabled, skip */
		 nop

		li	t0, 0
#ifdef CONFIG_64BIT
		mfc0	t0, CP0_STATUS
#endif
		fpu_restore_double_kgdb sp t0 t1 # clobbers t1


/*
 * Now the CP0 and integer registers
 */

3:
		mfc0	t0, CP0_STATUS
		ori	t0, 0x1f
		xori	t0, 0x1f
		mtc0	t0, CP0_STATUS

		LONG_L	v0, GDB_FR_STATUS(sp)
		LONG_L	v1, GDB_FR_EPC(sp)
		mtc0	v0, CP0_STATUS
		DMTC0	v1, CP0_EPC
		LONG_L	v0, GDB_FR_HI(sp)
		LONG_L	v1, GDB_FR_LO(sp)
		mthi	v0
		mtlo	v1
		LONG_L	$31, GDB_FR_REG31(sp)
		LONG_L	$30, GDB_FR_REG30(sp)
		LONG_L	$28, GDB_FR_REG28(sp)
		LONG_L	$27, GDB_FR_REG27(sp)
		LONG_L	$26, GDB_FR_REG26(sp)
		LONG_L	$25, GDB_FR_REG25(sp)
		LONG_L	$24, GDB_FR_REG24(sp)
		LONG_L	$23, GDB_FR_REG23(sp)
		LONG_L	$22, GDB_FR_REG22(sp)
		LONG_L	$21, GDB_FR_REG21(sp)
		LONG_L	$20, GDB_FR_REG20(sp)
		LONG_L	$19, GDB_FR_REG19(sp)
		LONG_L	$18, GDB_FR_REG18(sp)
		LONG_L	$17, GDB_FR_REG17(sp)
		LONG_L	$16, GDB_FR_REG16(sp)
		LONG_L	$15, GDB_FR_REG15(sp)
		LONG_L	$14, GDB_FR_REG14(sp)
		LONG_L	$13, GDB_FR_REG13(sp)
		LONG_L	$12, GDB_FR_REG12(sp)
		LONG_L	$11, GDB_FR_REG11(sp)
		LONG_L	$10, GDB_FR_REG10(sp)
		LONG_L	$9, GDB_FR_REG9(sp)
		LONG_L	$8, GDB_FR_REG8(sp)
		LONG_L	$7, GDB_FR_REG7(sp)
		LONG_L	$6, GDB_FR_REG6(sp)
		LONG_L	$5, GDB_FR_REG5(sp)
		LONG_L	$4, GDB_FR_REG4(sp)
		LONG_L	$3, GDB_FR_REG3(sp)
		LONG_L	$2, GDB_FR_REG2(sp)
		LONG_L	$1, GDB_FR_REG1(sp)
#if defined(CONFIG_CPU_R3000) || defined(CONFIG_CPU_TX39XX)
		LONG_L	k0, GDB_FR_EPC(sp)
		LONG_L	$29, GDB_FR_REG29(sp)		/* Deallocate stack */
		jr	k0
		rfe
#else
		LONG_L	sp, GDB_FR_REG29(sp)		/* Deallocate stack */

		.set	mips3
		eret
		.set	mips0
#endif
		.set	at
		.set	reorder
		END(trap_low)

LEAF(kgdb_read_byte)
4:		lb	t0, (a0)
		sb	t0, (a1)
		li	v0, 0
		jr	ra
		.section __ex_table,"a"
		PTR	4b, kgdbfault
		.previous
		END(kgdb_read_byte)

LEAF(kgdb_write_byte)
5:		sb	a0, (a1)
		li	v0, 0
		jr	ra
		.section __ex_table,"a"
		PTR	5b, kgdbfault
		.previous
		END(kgdb_write_byte)

		.type	kgdbfault@function
		.ent	kgdbfault

kgdbfault:	li	v0, -EFAULT
		jr	ra
		.end	kgdbfault
