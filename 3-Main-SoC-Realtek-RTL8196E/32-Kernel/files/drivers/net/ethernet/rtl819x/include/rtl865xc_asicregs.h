/*
 * RTL865xC ASIC Register Definitions
 * Copyright (c) 2002 Realtek Semiconductor Corporation
 * Author: alva_zhang
 * Adapted for Linux 5.10 & RTL8196E: Jacques Nilo (2025)
 *
 * ASIC specific register definitions for RTL8196E driver usage.
 * Trimmed to only the macros referenced by the RTL8196E driver.
 *
 * SPDX-License-Identifier: GPL-2.0
 */
#ifndef _ASICREGS_H
#define _ASICREGS_H

/* Auto-trimmed: shared macros only. */

#define REG32(reg) (*((volatile uint32 *)(reg)))

#define big_endian32(x) (x)

#define big_endian(x) big_endian32(x) /* backward-compatible */

#define WRITE_MEM32(reg, val) REG32(reg) = big_endian(val)

#define READ_MEM32(reg) big_endian(REG32(reg))

#define TOGGLE_BIT_IN_REG_TWICE(reg, bit_shift)

#define REAL_SWCORE_BASE 0xBB800000

#define REAL_SYSTEM_BASE 0xB8000000

#define SWCORE_BASE REAL_SWCORE_BASE

#define SYSTEM_BASE REAL_SYSTEM_BASE

#define CPU_IFACE_BASE (SYSTEM_BASE + 0x10000) /* 0xB8010000 */

#define CPUICR (0x000 + CPU_IFACE_BASE)		   /* Interface control */

#define CPURPDCR0 (0x004 + CPU_IFACE_BASE)	   /* Rx pkthdr descriptor control 0 */

#define CPUTPDCR0 (0x020 + CPU_IFACE_BASE)	   /* Tx pkthdr descriptor control Low */

#define CPUIIMR (0x028 + CPU_IFACE_BASE)	   /* Interrupt mask control */

#define CPUIISR (0x02c + CPU_IFACE_BASE)	   /* Interrupt status control */

#define TXCMD (1 << 31) /* Enable Tx */

#define RXCMD (1 << 30) /* Enable Rx */

#define BUSBURST_32WORDS 0

#define MBUF_2048BYTES (4 << 24)

#define EXCLUDE_CRC (1 << 16) /* Exclude CRC from length */

#define LINK_CHANGE_IE (1 << 31) /* Link change interrupt enable */

#define PKTHDR_DESC_RUNOUT_IE_ALL (0x3f << 17) /* Run out anyone pkthdr descriptor interrupt pending */

#define RX_DONE_IE_ALL (0x3f << 3) /* Rx Descript Ring any one packet done interrupt enable */

#define TX_ALL_DONE_IE_ALL (0x3 << 1) /* Any Tx Descript Ring all packets done interrupt enable */

#define PKTHDR_DESC_RUNOUT_IP_ALL (0x3f << 17)		   /* Run out anyone pkthdr descriptor interrupt pending */

#define MBUF_DESC_RUNOUT_IP_ALL (1 << 16) /* Run out anyone mbuf interrupt pending */

#define SWMACCR_BASE (SWCORE_BASE + 0x4000)

#define MACCR (0x000 + SWMACCR_BASE)   /* MAC Configuration Register */

#define PCRAM_BASE (SWCORE_BASE + 0x4100)

#define PCRP0 (0x004 + PCRAM_BASE)	  /* Port Configuration Register of Port 0 */

#define PCRP1 (0x008 + PCRAM_BASE)	  /* Port Configuration Register of Port 1 */

#define PCRP2 (0x00C + PCRAM_BASE)	  /* Port Configuration Register of Port 2 */

#define PCRP3 (0x010 + PCRAM_BASE)	  /* Port Configuration Register of Port 3 */

#define PCRP4 (0x014 + PCRAM_BASE)	  /* Port Configuration Register of Port 4 */

#define PCRP5 (0x018 + PCRAM_BASE)	  /* Port Configuration Register of Port 5 */

#define PCRP6 (0x01C + PCRAM_BASE)	  /* Port Configuration Register of Ext Port 0 */

#define PSRP0 (0x028 + PCRAM_BASE)	  /* Port Status Register Port 0 */

#define EnForceMode (1 << 25)	   /* Enable Force Mode to set link/speed/duplix/flow status */

#define ForceLink (1 << 23) /* 0-link down, 1-link up */

#define EnablePHYIf (1 << 0)		   /* Enable PHY interface. */

#define PortStatusLinkUp (1 << 4)		  /* Link Up */

#define SWMISC_BASE (0x4200 + SWCORE_BASE)

#define SSIR (0x04 + SWMISC_BASE)  /* System Initial and Reset Registe*/

#define SIRR (SSIR)						/* Alias Name */

#define TRXRDY (1 << 0)		   /* Start normal TX and RX */

#define ALE_BASE (0x4400 + SWCORE_BASE)

#define MSCR (0x10 + ALE_BASE)		 /* Module Switch Control Register */

#define SWTCR0 (0x18 + ALE_BASE)	 /* swtich table control register 0 */

#define SWTCR1 (0x1C + ALE_BASE)	 /* swtich table control register 1 */

#define LIMDBC_MASK (3 << 16)

#define LIMDBC_VLAN (0 << 16)							 /* By VLAN base */

#define OQNCR_BASE (SWCORE_BASE + 0x4700) /* Output Queue Number Control Registers */

#define IBCR0 (0x04 + OQNCR_BASE)		  /* Ingress Bandwidth Control Register 0 */

#define QNUMCR (0x54 + OQNCR_BASE)		  /*Queue Number Control Register*/

#define RTL865X_PORTMASK_UNASIGNED 0x5A5A5A5A

#define RTL865X_PREALLOC_SKB_UNASIGNED 0xA5A5A5A5

#define RTL865XC_PORT_NUMBER 9

#define RTL8651_L2TBL_ROW 256

#define RTL8651_L2TBL_COLUMN 4

#define RTL865XC_LAGHASHIDX_NUMBER 8 /* There are 8 hash values in RTL865xC Link Aggregation. */

#define RTL865XC_VLAN_NUMBER 4096

#define RTL865XC_NETINTERFACE_NUMBER 8

#define RTL8651_L2_NUMBER 1024

#define GICR_BASE (SYSTEM_BASE + 0x3000) /* 0xB8003000 */

#define GIMR (0x000 + GICR_BASE)		 /* Global interrupt mask */

#define SYS_CLK_MAG (SYSTEM_BASE + 0x0010)

#define BOND_OPTION (SYSTEM_BASE + 0x000C)

#define BOND_ID_MASK (0xF)

#define BOND_8196ES1 (0x1)

#define BOND_8196ES3 (0x5)

#define BOND_8196ES2 (0x9)

#define BOND_8196ES (0xD)

#endif /* _ASICREGS_H */
