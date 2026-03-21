/*
* ----------------------------------------------------------------
* Copyright c                  Realtek Semiconductor Corporation, 2002
* All rights reserved.
*
*
* Abstract: Board specific definitions.

*
* ---------------------------------------------------------------
*/

#ifndef _BOARD_H
#define _BOARD_H

#include <linux/config.h>

/* NIC definitions */
#define NUM_RX_PKTHDR_DESC 16
#define NUM_RX_MBUF_DESC 16
#define NUM_TX_PKTHDR_DESC 16
#define MBUF_LEN 2048

#define UNCACHED_MALLOC(x) (void *)(0xa0000000 | (uint32)malloc(x))

#endif /* _BOARD_H */
