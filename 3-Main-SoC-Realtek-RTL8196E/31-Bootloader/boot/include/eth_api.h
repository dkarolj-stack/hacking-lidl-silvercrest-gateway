/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * eth_api.h - Minimal Ethernet API used by the boot monitor and TFTP
 */

#ifndef _BOOT_ETH_API_H_
#define _BOOT_ETH_API_H_

extern char eth0_mac[6];
extern void eth_startup(int etherport);
extern void prepare_txpkt(int etherport, unsigned short type,
			  unsigned char *destaddr, unsigned char *data,
			  unsigned short len);

#endif
