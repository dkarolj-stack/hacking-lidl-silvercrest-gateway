#ifndef _UART_H_
#define _UART_H_

#include <asm/rtl_soc.h>

#define BAUD_RATE 38400

/*
 * UART peek buffer: holds one character that was read during ESC polling
 * but belongs to the next command. -1 means empty.
 */
extern int g_uart_peek;

/* Low-level UART I/O */
void serial_outc(char c);
char serial_inc(void);

/* Console initialization */
void console_init(unsigned long cpu_clock);

/* Non-blocking polling helpers */
static inline int uart_data_ready(void)
{
	return (rtl_inl(UART_LSR) & (1 << 24));
}

static inline int uart_getc_nowait(void)
{
	return ((rtl_inl(UART_RBR) & 0xff000000) >> 24);
}

#endif /* _UART_H_ */
