

#ifndef _RTL8181_H
#define _RTL8181_H

#define Virtual2Physical(x) (((unsigned long)x) & 0x1fffffff)
#define Physical2Virtual(x) (((unsigned long)x) | 0x80000000)
#define Virtual2NonCache(x) (((unsigned long)x) | 0x20000000)
#define Physical2NonCache(x) (((unsigned long)x) | 0xa0000000)
#define UNCACHE_MASK 0x20000000

#define TRUE 0x01
#define FALSE 0x0

#define BIT(x) (1 << (x))

#define rtl_inb(offset)                                                        \
	(*(volatile unsigned char *)(mips_io_port_base + offset))
#define rtl_inw(offset)                                                        \
	(*(volatile unsigned short *)(mips_io_port_base + offset))
#define rtl_inl(offset)                                                        \
	(*(volatile unsigned long *)(mips_io_port_base + offset))

#define rtl_outb(offset, val)                                                  \
	(*(volatile unsigned char *)(mips_io_port_base + offset) = val)
#define rtl_outw(offset, val)                                                  \
	(*(volatile unsigned short *)(mips_io_port_base + offset) = val)
#define rtl_outl(offset, val)                                                  \
	(*(volatile unsigned long *)(mips_io_port_base + offset) = val)

/*mips_io_port_base + FLASH_BASE =0xbfc0 0000   */
/*mips_io_port_base = 0xbd01 0000               */
#define FLASH_BASE 0x2bf0000
// For BUS_MASTER

// For Uart1 Controller 865x
#define UART_RBR 0x2000
#define UART_THR 0x2000
#define UART_DLL 0x2000
#define UART_IER 0x2004
#define UART_DLM 0x2004
#define UART_IIR 0x2008
#define UART_FCR 0x2008
#define UART_LCR 0x200c
#define UART_MCR 0x2010
#define UART_LSR 0x2014
#define UART_MSR 0x2018
#define UART_SCR 0x201c

// For Uart1 Flags
#define UART_RXFULL BIT(0)
#define UART_TXEMPTY (BIT(6) | BIT(5))
#define UART_RXFULL_MASK BIT(0)
#define UART_TXEMPTY_MASK BIT(1)

// For Interrupt Controller
#define GIMR0 0x3000
#define GISR 0x3004
#define IRR0 0x3008
#define IRR1 0x300c
#define IRR2 0x3010
#define IRR3 0x3014

// For General Purpose Timer/Counter
#define TC0DATA 0x3100
#define TC1DATA 0x3104
#define TC0CNT 0x3108
#define TC1CNT 0x310c
#define TCCNR 0x3110
#define TCIR 0x3114
#define BTDATA 0x3118
#define WDTCNR 0x311c

#endif
