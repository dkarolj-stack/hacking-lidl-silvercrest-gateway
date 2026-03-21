// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * irq.c - Exception and interrupt handling
 *
 * RTL8196E stage-2 bootloader
 *
 * Copyright (c) 2009-2020 Realtek Semiconductor Corp.
 * Copyright (c) 2024-2026 J. Nilo
 */

#include "boot_common.h"
#include "boot_soc.h"
#include <linux/interrupt.h>
#include <asm/errno.h>
#include <asm/io.h>
#include "cache.h"

/*Cyrus Tsai*/
unsigned long exception_handlers[32];
void set_except_vector(int n, void *addr);
asmlinkage void do_reserved(struct pt_regs *regs);
void __init exception_init(void);
/*Cyrus Tsai*/

static struct irqaction *irq_action[NR_IRQS] = {
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

#define ALLINTS                                                                \
	(IE_IRQ0 | IE_IRQ1 | IE_IRQ2 | IE_IRQ3 | IE_IRQ4 |                     \
	 IE_IRQ5) 

static void unmask_irq(unsigned int irq)
{
	outl((inl(GIMR0) | (1 << irq)), GIMR0);
	inl(GIMR0);
}
static void mask_irq(unsigned int irq)
{
	outl(inl(GIMR0) & (~(1 << irq)), GIMR0);
	inl(GIMR0);
}

extern asmlinkage void do_IRQ(int irq, struct pt_regs *regs);

/**
 * irq_dispatch - Walk pending IRQ bits and dispatch handlers
 * @irq_nr: bitmask of pending interrupt lines (from GIMR & GISR)
 * @regs: saved CPU register state
 *
 * Scans all 32 bits of @irq_nr; for each set bit, calls do_IRQ()
 * with the corresponding IRQ number.
 */
void irq_dispatch(int irq_nr, struct pt_regs *regs)
{
	int i, irq = 0;
	for (i = 0; i <= 31; i++) {
		if (irq_nr & 0x01) {
			do_IRQ(irq, regs);
		}
		irq++;
		irq_nr = irq_nr >> 1;
	}
}

static inline unsigned int clear_cp0_status(unsigned int clear)
{
	unsigned int res;

	res = read_32bit_cp0_register(CP0_STATUS);
	res &= ~clear;
	write_32bit_cp0_register(CP0_STATUS, res);

	return res;
}

static inline unsigned int change_cp0_status(unsigned int change,
					     unsigned int newvalue)
{
	unsigned int res;

	res = read_32bit_cp0_register(CP0_STATUS);
	res &= ~change;
	res |= (newvalue & change);
	write_32bit_cp0_register(CP0_STATUS, res);

	return res;
}

void __init ExceptionToIrq_setup(void)
{
	extern asmlinkage void IRQ_finder(void);

	/* Disable all hardware interrupts */
	change_cp0_status(ST0_IM, 0x00);

	/* Set up the external interrupt exception vector */
	/* First exception is Interrupt*/
	set_except_vector(0, IRQ_finder);

	/* Enable all interrupts */
	change_cp0_status(ST0_IM, ALLINTS);
}

void __init init_IRQ(void) { ExceptionToIrq_setup(); }

// below is adopted from kernel/irq.c

/**
 * setup_IRQ - Install an IRQ action handler
 * @irq: IRQ number
 * @new: irqaction structure to install
 *
 * Return: 0 on success
 */
int setup_IRQ(int irq, struct irqaction *new)
{
	struct irqaction **p;
	unsigned long flags;

	p = irq_action + irq;
	save_and_cli(flags);
	*p = new;

	restore_flags(flags);

	return 0;
}

/**
 * request_IRQ - Register and enable an interrupt handler
 * @irq: IRQ number (0 to NR_IRQS-1)
 * @action: irqaction describing the handler
 * @dev_id: device identifier passed to the handler
 *
 * Return: 0 on success, -EINVAL if @irq is out of range
 */
int request_IRQ(unsigned long irq, struct irqaction *action, void *dev_id)
{

	int retval;

	if (irq >= NR_IRQS)
		return -EINVAL;

	action->dev_id = dev_id;

	retval = setup_IRQ(irq, action);
	unmask_irq(irq);

	if (retval)

		return retval;
}

/**
 * free_IRQ - Mask and deregister an interrupt
 * @irq: IRQ number to disable
 */
int free_IRQ(unsigned long irq) { mask_irq(irq); }

/**
 * do_IRQ - Dispatch a single hardware interrupt
 * @irqnr: IRQ number (0-31)
 * @regs: saved CPU register state
 *
 * Looks up the registered irqaction for @irqnr and calls its handler.
 * If no handler is registered, prints diagnostic info and halts.
 */
asmlinkage void do_IRQ(int irqnr, struct pt_regs *regs)
{
	struct irqaction *action;
	unsigned long i;

	action = *(irqnr + irq_action);

	if (action) {
		action->handler(irqnr, action->dev_id, regs);
	} else {
		prom_printf("cp0_cause=%X, cp0_epc=%X",
			    read_32bit_cp0_register(CP0_CAUSE),
			    read_32bit_cp0_register(CP0_EPC));
		prom_printf("you got irq=%X\n", irqnr);
		for (;;)
			;
	}
}

/**
 * set_except_vector - Register a CPU exception handler
 * @n: exception number (0-31, from CP0 Cause ExcCode)
 * @addr: pointer to the handler function
 */
void set_except_vector(int n, void *addr)
{
	unsigned handler = (unsigned long)addr;
	exception_handlers[n] = handler;
}

asmlinkage void do_reserved(struct pt_regs *regs)
{
	prom_printf("cp0_cause=%X, cp0_epc=%X, ra=%X",
		    read_32bit_cp0_register(CP0_CAUSE),
		    read_32bit_cp0_register(CP0_EPC), regs->regs[31]);
	prom_printf("Undefined Exception happen.");
	for (;;)
		;
	/*Just hang here.*/
}

#include <asm/inst.h>
#include <asm/branch.h>
#include <asm/lexraregs.h>
char *watch_string[] = {"write", "read", "instruction"};
static int do_watch_cnt = 0;
asmlinkage void do_watch(struct pt_regs *regs)
{
	int addr, status, tmp;
	char *print_string;
	union mips_instruction insn;
	unsigned long value;
	unsigned int *pc;

	addr = __read_32bit_lxc0_register(LX0_WMPVADDR, 0);
	status = __read_32bit_lxc0_register(LX0_WMPSTATUS, 0);

	do_watch_cnt++;
	if (do_watch_cnt == 2) {
		do_watch_cnt = 0;
		tmp = __read_32bit_lxc0_register(LX0_WMPCTL, 0);
		tmp = tmp & (~(status & 0xff0000));
		__write_32bit_lxc0_register(LX0_WMPCTL, 0, tmp);
	}

	if (addr & 4)
		value = 0xfee1bad;
	else
		value = 0xc0ffee;

	prom_printf("%s", __FUNCTION__);
	prom_printf("ADDR:%x, ENTRY:%x", addr, (status & 0xff0000) >> 16,
		    status & 0x7);

	if (status & 1)
		print_string = watch_string[0];
	if (status & 2)
		print_string = watch_string[1];
	if (status & 4)
		print_string = watch_string[2];
	prom_printf("cause by: %s \n", print_string);

	pc = (unsigned int *)(regs->cp0_epc + ((regs->cp0_cause & CAUSEF_BD)
						   ? 4
						   : 0)); 
	insn.word = *pc;
	regs->regs[insn.i_format.rt] = value;
	if (!delay_slot(regs))
		regs->cp0_epc += 4;
	else {
		prom_printf("\nNOT HANDLE TRAP IN JUMP DELAY SLOT\n");
		regs->cp0_epc += 8;
	}
}

/**
 * exception_init - Install exception handlers at KSEG0+0x80
 *
 * Clears BEV in CP0 Status, fills all 32 exception slots with
 * do_reserved(), copies the exception_matrix dispatcher to the
 * hardware vector address (KSEG0 + 0x80), and registers the
 * watchpoint handler on exception 23.
 */
/*In Head.S, as sw boot up, set vector program path.*/
extern char exception_matrix[];
void __init exception_init(void)
{
	unsigned long i;
	clear_cp0_status(ST0_BEV);
	/*this is for default exception handlers: NULL*/
	for (i = 0; i <= 31; i++)
		set_except_vector(i, do_reserved);
	// KSEG0 8000 0000 and remember it is cacheable,
	// remember here we set BEV=0, and vector base is 80000000, offset 0x80
	memcpy((void *)(KSEG0 + 0x80), &exception_matrix, 0x80);
	flush_cache();

	extern asmlinkage void handle_watch(void);

	set_except_vector(23, handle_watch);
}
