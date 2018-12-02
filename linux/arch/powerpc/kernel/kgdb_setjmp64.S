/*
 * Copyright (C) 1996 Paul Mackerras
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program as licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <asm/processor.h>
#include <asm/ppc_asm.h>

	.text

/*
 * Save/restore state in case a memory access causes a fault.
 *
 * int kgdb_fault_setjmp(unsigned long *curr_context);
 * void kgdb_fault_longjmp(unsigned long *curr_context);
 */

_GLOBAL(kgdb_fault_setjmp)
	mflr	r0
	std	r0,0(r3)
	std	r1,8(r3)
	std	r2,16(r3)
	mfcr	r0
	std	r0,24(r3)
	std	r13,32(r3)
	std	r14,40(r3)
	std	r15,48(r3)
	std	r16,56(r3)
	std	r17,64(r3)
	std	r18,72(r3)
	std	r19,80(r3)
	std	r20,88(r3)
	std	r21,96(r3)
	std	r22,104(r3)
	std	r23,112(r3)
	std	r24,120(r3)
	std	r25,128(r3)
	std	r26,136(r3)
	std	r27,144(r3)
	std	r28,152(r3)
	std	r29,160(r3)
	std	r30,168(r3)
	std	r31,176(r3)
	li	r3,0
	blr

_GLOBAL(kgdb_fault_longjmp)
	ld	r13,32(r3)
	ld	r14,40(r3)
	ld	r15,48(r3)
	ld	r16,56(r3)
	ld	r17,64(r3)
	ld	r18,72(r3)
	ld	r19,80(r3)
	ld	r20,88(r3)
	ld	r21,96(r3)
	ld	r22,104(r3)
	ld	r23,112(r3)
	ld	r24,120(r3)
	ld	r25,128(r3)
	ld	r26,136(r3)
	ld	r27,144(r3)
	ld	r28,152(r3)
	ld	r29,160(r3)
	ld	r30,168(r3)
	ld	r31,176(r3)
	ld	r0,24(r3)
	mtcrf	0x38,r0
	ld	r0,0(r3)
	ld	r1,8(r3)
	ld	r2,16(r3)
	mtlr	r0
	mr	r3,r1
	blr
