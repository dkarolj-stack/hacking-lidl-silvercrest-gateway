#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>

	.text
	.set noreorder
	.globl __start

/*---------------------------------------------------------------------------*/
/* Stage-1.5 entry point                                                     */
/* This code runs from RAM after being copied by the stage-1 bootloader.     */
/*---------------------------------------------------------------------------*/
__start:
	j	1f
	nop

/*---------------------------------------------------------------------------*/
/* Register initialization data table                                        */
/* Format: <address>, <value> pairs to be written sequentially               */
/*---------------------------------------------------------------------------*/
	.align 2	/* Ensure 32-bit alignment for .word data */

regdata:
	/* UART configuration */
	.word	0xb8001008, 0xffff05c0		/* Offset 0x08 */
	.word	0xb8001004, 0x54480000		/* Offset 0x10 */
	.word	0xb8001050, 0xD2800000		/* Offset 0x18 */
	
	/* Multiple writes to same register (0xb8000010) */
	.word	0xb8000010, 0x017FFD2E		/* Offset 0x20 */
	.word	0xb8000010, 0x017FFD2E		/* Offset 0x28 */
	.word	0xb8000010, 0x017FFD2E		/* Offset 0x30 */
	.word	0xb8000010, 0x017FFD2E		/* Offset 0x38 */
	.word	0xb8000010, 0x017FFD2E		/* Offset 0x40 */
	.word	0xb8000010, 0x017FFD2E		/* Offset 0x48 */

/*---------------------------------------------------------------------------*/
/* Main code section                                                         */
/*---------------------------------------------------------------------------*/
1:
	/* Setup stack pointer */
	la	t0, __bss_end
	nop
	addi	t0, t0, 4096			/* Reserve 4KB stack */
	nop
	move	sp, t0				/* Set stack pointer */
	nop

test:
	jal	boot_entry			/* Jump to C entry point */
