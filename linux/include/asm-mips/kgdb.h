#ifdef __KERNEL__
#ifndef _ASM_KGDB_H_
#define _ASM_KGDB_H_

#include <asm/sgidefs.h>
#include <asm-generic/kgdb.h>

#ifndef __ASSEMBLY__
#if (_MIPS_ISA == _MIPS_ISA_MIPS1) || (_MIPS_ISA == _MIPS_ISA_MIPS2) || \
	(_MIPS_ISA == _MIPS_ISA_MIPS32)

#define KGDB_GDB_REG_SIZE 32

#elif (_MIPS_ISA == _MIPS_ISA_MIPS3) || (_MIPS_ISA == _MIPS_ISA_MIPS4) || \
	(_MIPS_ISA == _MIPS_ISA_MIPS64)

#ifdef CONFIG_32BIT
#define KGDB_GDB_REG_SIZE 32
#else /* CONFIG_CPU_32BIT */
#define KGDB_GDB_REG_SIZE 64
#endif
#else
#error "Need to set KGDB_GDB_REG_SIZE for MIPS ISA"
#endif /* _MIPS_ISA */

#define BUFMAX			2048
#if (KGDB_GDB_REG_SIZE == 32)
#define NUMREGBYTES		(90*sizeof(u32))
#define NUMCRITREGBYTES		(12*sizeof(u32))
#else
#define NUMREGBYTES		(90*sizeof(u64))
#define NUMCRITREGBYTES		(12*sizeof(u64))
#endif
#define BREAK_INSTR_SIZE	4
#define BREAKPOINT()		__asm__ __volatile__(		\
					".globl breakinst\n\t"	\
					".set\tnoreorder\n\t"	\
					"nop\n"			\
					"breakinst:\tbreak\n\t"	\
					"nop\n\t"		\
					".set\treorder")
#define CACHE_FLUSH_IS_SAFE	0

extern int kgdb_early_setup;

#endif				/* !__ASSEMBLY__ */
#endif				/* _ASM_KGDB_H_ */
#endif				/* __KERNEL__ */
