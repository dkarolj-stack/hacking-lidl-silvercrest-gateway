
#include <linux/config.h>

#ifndef _RTL8196_H
#define _RTL8196_H
#define WRITE_MEM32(addr, val) (*(volatile unsigned int *)(addr)) = (val)
#define READ_MEM32(addr) (*(volatile unsigned int *)(addr))
#define WRITE_MEM16(addr, val) (*(volatile unsigned short *)(addr)) = (val)
#define READ_MEM16(addr) (*(volatile unsigned short *)(addr))
#define WRITE_MEM8(addr, val) (*(volatile unsigned char *)(addr)) = (val)
#define READ_MEM8(addr) (*(volatile unsigned char *)(addr))

#define ALL_PORT_MASK 0x3F

#define CPU_CLOCK (330 * 1000 * 1000)
#define SYS_CLK_RATE (200 * 1000 * 1000)

#define Virtual2Physical(x) (((unsigned long)x) & 0x1fffffff)
#define Physical2Virtual(x) (((unsigned long)x) | 0x80000000)
#define Virtual2NonCache(x) (((unsigned long)x) | 0x20000000)
#define Physical2NonCache(x) (((unsigned long)x) | 0xa0000000)
#define UNCACHE_MASK 0x20000000

#ifndef TRUE
#define TRUE 0x01
#endif

#ifndef FALSE
#define FALSE 0x0
#endif

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
#define mips_io_port_base (0xB8000000)

#define FLASH_BASE 0x05000000

#define clk_manage_REG 0xb8000010

/* UART0 registers */
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

#define MEM_CONTROLLER_REG 0xB8001000

#define PIN_MUX_SEL 0xb8000030
#define PIN_MUX_SEL2 (SYS_BASE + 0x44)

/*GPIO register */
#define GPIO_BASE 0xB8003500
#define PABCDCNR_REG (0x000 + GPIO_BASE)   /* Port ABCD control */
#define PABCDPTYPE_REG (0x004 + GPIO_BASE) /* Port ABCD type */
#define PABCDDIR_REG (0x008 + GPIO_BASE)   /* Port ABCD direction */
#define PABCDDAT_REG (0x00C + GPIO_BASE)   /* Port ABCD data */
#define PABCDISR_REG (0x010 + GPIO_BASE)   /* Port ABCD interrupt status */
#define PABIMR_REG (0x014 + GPIO_BASE)	   /* Port AB interrupt mask */
#define PCDIMR_REG (0x018 + GPIO_BASE)	   /* Port CD interrupt mask */
#define PEFGHCNR_REG (0x01C + GPIO_BASE)   /* Port ABCD control */
#define PEFGHPTYPE_REG (0x020 + GPIO_BASE) /* Port ABCD type */
#define PEFGHDIR_REG (0x024 + GPIO_BASE)   /* Port ABCD direction */
#define PEFGHDAT_REG (0x028 + GPIO_BASE)   /* Port ABCD data */
#define PEFGHISR_REG (0x02C + GPIO_BASE)   /* Port ABCD interrupt status */
#define PEFIMR_REG (0x030 + GPIO_BASE)	   /* Port AB interrupt mask */
#define PGHIMR_REG (0x034 + GPIO_BASE)	   /* Port CD interrupt mask */

/* Timer control registers
 */
// For General Purpose Timer/Counter
#define TC0DATA 0x3100
#define TC1DATA 0x3104
#define TC2DATA 0x68
#define TC3DATA 0x6C
#define TC0CNT 0x3108
#define TC1CNT 0x310c
#define TC2CNT 0x78
#define TC3CNT 0x7C
#define TCCNR 0x3110
#define TCIR 0x3114
#define BTDATA 0x3118
#define WDTCNR 0x311c
#define GICR 0xB8003000
#define CDBR 0xb8003118

// JSW :add for 8196 timer/counter
#define GICR_BASE 0xB8003000
#define TC0DATA_REG (0x100 + GICR_BASE) /* Timer/Counter 0 data,Normal */
#define TC1DATA_REG (0x104 + GICR_BASE) /* Timer/Counter 1 data */
#define TC2DATA_REG (0x120 + GICR_BASE) /* Timer/Counter 1 data */
#define TC3DATA_REG (0x124 + GICR_BASE) /* Timer/Counter 1 data */

#define TC0CNT_REG (0x108 + GICR_BASE) /* Timer/Counter 0 count,Normal */
#define TC1CNT_REG (0x10C + GICR_BASE) /* Timer/Counter 1 count */
#define TC2CNT_REG (0x128 + GICR_BASE) /* Timer/Counter 1 count */
#define TC3CNT_REG (0x12C + GICR_BASE) /* Timer/Counter 1 count */

#define TCCNR_REG (0x110 + GICR_BASE) /* Timer/Counter control */
#define TCIR_REG (0x114 + GICR_BASE)  /* Timer/Counter intertupt */

#define CDBR_REG (0x118 + GICR_BASE)   /* Clock division base */
#define WDTCNR_REG (0x11C + GICR_BASE) /* Watchdog timer control */

// JSW:For WatchDog

#define WDTE_OFFSET 24	    /* Watchdog enable */
#define WDSTOP_PATTERN 0xA5 /* Watchdog stop pattern */
#define WDTCLR (1 << 23)    /* Watchdog timer clear */
#define OVSEL_15 0	    /* Overflow select count 2^15 */
#define OVSEL_16 (1 << 21)  /* Overflow select count 2^16 */
#define OVSEL_17 (2 << 21)  /* Overflow select count 2^17 */
#define OVSEL_18 (3 << 21)  /* Overflow select count 2^18 */
#define WDTIND (1 << 20)    /* Indicate whether watchdog ever occurs */

/* Global interrupt control registers
 */
#define GICR_BASE 0xB8003000
#define GIMR_REG (0x000 + GICR_BASE) /* Global interrupt mask */
#define GISR_REG (0x004 + GICR_BASE) /* Global interrupt status */
#define IRR_REG (0x008 + GICR_BASE)  /* Interrupt routing */
#define IRR1_REG (0x00C + GICR_BASE) /* Interrupt routing */
#define IRR2_REG (0x010 + GICR_BASE) /* Interrupt routing */
#define IRR3_REG (0x014 + GICR_BASE) /* Interrupt routing */

#define printf dprintf

#define SYS_BASE 0xb8000000
#define SYS_INT_STATUS (SYS_BASE + 0x04)
#define SYS_HW_STRAP (SYS_BASE + 0x08)
#define SYS_BOND_OPTION (SYS_BASE + 0x0C)
#define SYS_CLKMANAGE (SYS_BASE + 0x10)
#define SYS_BIST_CTRL (SYS_BASE + 0x14)
#define SYS_BIST_DONE (SYS_BASE + 0x20)
#define SYS_BIST_FAIL (SYS_BASE + 0x24)
#define SYS_DRF_BIST_DONE (SYS_BASE + 0x28)
#define SYS_DRF_BIST_FAIL (SYS_BASE + 0x2C)
#define SYS_PLL (SYS_BASE + 0x30)

// hw strap
#define ST_CLKLX_FROM_CLKM_OFFSET 7
#define ST_SYNC_OCP_OFFSET 9
#define CK_M2X_FREQ_SEL_OFFSET 10
#define ST_CPU_FREQ_SEL_OFFSET 13
#define ST_CPU_FREQDIV_SEL_OFFSET 19

// #define ST_BOOTPINSEL (1<<0)
// #define ST_DRAMTYPE (1<<1)
#define ST_BOOTSEL (1 << 2)
// #define ST_PHYID (0x3<<3) //2'b11
#define ST_CLKLX_FROM_CLKM (1 << 7) // new, 8196C new
#define ST_EN_EXT_RST (1 << 8)
#define ST_SYNC_OCP (1 << 9) // ocp clock is come from clock lx
#define CK_M2X_FREQ_SEL (0x7 << 10)
#define ST_CPU_FREQ_SEL (0xf << 13)
#define ST_NRFRST_TYPE (1 << 17)
// #define ST_SYNC_LX (1<<18)
#define ST_CPU_FREQDIV_SEL (0x1 << 19) // 8196C, change to only one-bit
#define ST_SWAP_DBG_HALFWORD (0x1 << 22)
#define ST_EVER_REBOOT_ONCE (1 << 23)
#define ST_SYS_DBG_SEL (0x3f << 24)
#define ST_PINBUS_DBG_SEL (3 << 30)

//---------------------------------------------------------------------

/* Switch core misc control register field definitions
 */
#define DIS_P5_LOOPBACK (1 << 30) /* Disable port 5 loopback */

#define LINK_RGMII 0	   /* RGMII mode */
#define LINK_MII_MAC 1	   /* GMII/MII MAC auto mode */
#define LINK_MII_PHY 2	   /* GMII/MII PHY auto mode */

#define PLL_REG 0xb8000020
#define HW_STRAP_REG 0xb8000008

#define DDCR_REG 0xb8001050
#define MPMR_REG 0xB8001040
#define MCR_REG 0xb8001000
#define DCR_REG 0xb8001004
#define DTR_REG 0xb8001008

#define BIST_CONTROL_REG 0xb8000014

#endif
