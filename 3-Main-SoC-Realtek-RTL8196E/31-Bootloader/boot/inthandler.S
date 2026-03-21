/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * inthandler.S - Interrupt and watchpoint handler trampolines
 *
 * RTL8196E stage-2 bootloader
 *
 * IRQ_finder: saves all registers (SAVE_ALL), reads the GIMR/GISR
 * interrupt status, and calls irq_dispatch() in irq.c.
 *
 * handle_watch: saves context and calls do_watch() for Lexra
 * watchpoint exceptions.
 *
 * Copyright (c) 2009-2020 Realtek Semiconductor Corp.
 * Copyright (c) 2024-2026 J. Nilo
 */

#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/rtl8181.h>

/*===========================================================================*/
/* IRQ_finder - Interrupt dispatcher                                         */
/*===========================================================================*/
/*
 * Cyrus Tsai
 *
 * This INT dispatch routine is triggered via Exception->CAUSE:EXC-CODE:INT
 */

	.text
	.set	noreorder
	.set	noat
	.align	5

	NESTED(IRQ_finder, PT_SIZE, sp)

	/*---------------------------------------------------------------------*/
	/* Save context and disable interrupts                                */
	/*---------------------------------------------------------------------*/
	SAVE_ALL
	CLI
	.set	at

	/*---------------------------------------------------------------------*/
	/* Read and mask pending interrupts                                   */
	/*---------------------------------------------------------------------*/
	mfc0	t0, CP0_CAUSE		/* Get pending interrupts */
	nop
	nop

	mfc0	t2, CP0_STATUS
	nop
	nop

	and	t0, t2			/* Isolate allowed ones */
	andi	t2, t0, 0xfc00		/* Look at Hardware INT bits 10-15 */

	/*---------------------------------------------------------------------*/
	/* Read GIMR/GISR interrupt controller registers                      */
	/*---------------------------------------------------------------------*/
	la	t0, (GISR  + 0xb8000000)
	la	t1, (GIMR0 + 0xb8000000)
	lw	a0, 0(t0)		/* a0 = ISR: Interrupt status */
	lw	a1, 0(t1)		/* a1 = IMR: Interrupt mask */
	nop
	nop

	/*---------------------------------------------------------------------*/
	/* Isolate allowed interrupts and check for pending IRQs              */
	/*---------------------------------------------------------------------*/
	and	a0, a1			/* Isolate allowed ones */
	and	a0, 0xffffffff		/* Extend to 32 interrupts (2007/3/23) */
	bne	t2, zero, handle_it	/* Pending interrupt -> jump to handler */
	nop
	nop

	/*---------------------------------------------------------------------*/
	/* Spurious interrupt - should never reach here                       */
	/*---------------------------------------------------------------------*/
1:
	li	t0, (UART_THR + 0xb8000000)
	li	t1, 'm'			/* 'm' indicates unexpected interrupt */
	nop
	sb	t1, 0(t0)		/* Print 'm' on UART */
	nop
	nop
	b	1b			/* Hang forever */
	nop
	nop

	/*---------------------------------------------------------------------*/
	/* Handle valid interrupt                                             */
	/*---------------------------------------------------------------------*/
handle_it:
	jal	irq_dispatch		/* Call C interrupt handler */
	nop
	nop

	move	a1, sp
	RESTORE_ALL_AND_RET		/* Restore context and return */
	nop
	nop

	END(IRQ_finder)

	.set	at

/*===========================================================================*/
/* handle_watch - Lexra watchpoint exception handler                         */
/*===========================================================================*/
/*
 * jwsyu
 *
 * This is watch asm front-end for Lexra 5281/4281
 */

	.text
	.set	noreorder
	.set	noat
	.align	5

	NESTED(handle_watch, PT_SIZE, sp)

	/*---------------------------------------------------------------------*/
	/* Save context and call watchpoint handler                           */
	/*---------------------------------------------------------------------*/
	SAVE_ALL
	CLI
	move	a0, sp			/* Pass stack pointer as argument */
	jal	do_watch		/* Call C watchpoint handler */
	nop
	nop

	RESTORE_ALL_AND_RET		/* Restore context and return */
	nop
	nop

	END(handle_watch)
