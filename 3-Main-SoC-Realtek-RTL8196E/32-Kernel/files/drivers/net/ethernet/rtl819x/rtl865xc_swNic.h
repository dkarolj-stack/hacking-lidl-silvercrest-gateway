/*
 * RTL865xC Switch NIC Header
 * Copyright (c) 2002-2008 Realtek Semiconductor Corporation
 * Author: bo_zhao (bo_zhao@realtek.com)
 * Adapted for Linux 5.10 & RTL8196E: Jacques Nilo (2025)
 *
 * Descriptor ring management API for TX/RX.
 *
 * SPDX-License-Identifier: GPL-2.0
 */


#ifndef RTL865XC_SWNIC_H
#define	RTL865XC_SWNIC_H



#define RTL865X_SWNIC_RXRING_HW_PKTDESC	6

#define RTL865X_SWNIC_TXRING_HW_PKTDESC	4
#define RESERVERD_MBUF_RING_NUM			8

	#define ETH_REFILL_THRESHOLD			8	// must < NUM_RX_PKTHDR_DESC
	#if defined(SKIP_ALLOC_RX_BUFF)
	#define MAX_PRE_ALLOC_RX_SKB		0
	#define NUM_RX_PKTHDR_DESC			428	//128+300
	#else
	/*
	 * Buffer pool sizing for multiple TCP streams and high throughput:
	 *
	 * Evolution:
	 * - Original: 300 + 128 + 128 = 556 SKBs → exhaustion at 95 Mbps
	 * - V1:       400 + 200 + 200 = 800 SKBs → still exhausts with 8 TCP streams
	 * - V2:       500 + 300 + 300 = 1100 SKBs → handles 8 parallel TCP + UDP stress
	 *
	 * Rationale for 1100 SKBs:
	 * - RX descriptors: 500 (hardware ring)
	 * - TCP buffering: 8 streams × 50 SKBs = 400 SKBs held by Linux TCP stack
	 * - Pre-alloc queue: 300 (refill buffer)
	 * - Safety margin: 300 (for bursts)
	 * - At 80 Mbps: 1100 ÷ 6,667 pkt/s = 165ms buffer (sufficient)
	 *
	 * Memory cost: 1100 × 2KB = 2.2 MB (6.8% of 32MB RAM - acceptable)
	 * Previous issues: TCP_Parallel_8_streams caused 151,000+ refill failures
	 */
	#define MAX_PRE_ALLOC_RX_SKB		300  /* Was 200, now 300 for 8 TCP streams */
	#define NUM_RX_PKTHDR_DESC			500  /* Was 400, now 500 for more descriptors */
	#endif
	#define NUM_TX_PKTHDR_DESC			600  /* Was 400, now 600 for better TX throughput */	

#define	RTL865X_SWNIC_RXRING_MAX_PKTDESC    1
#define	RTL865X_SWNIC_TXRING_MAX_PKTDESC    1
#define	RTL_CPU_RX_RING_NUM			1
#define	NUM_RX_PKTHDR_DESC1		2
#define	NUM_RX_PKTHDR_DESC2		2
#define	NUM_RX_PKTHDR_DESC3		2
#define	NUM_RX_PKTHDR_DESC4		2
#define	NUM_RX_PKTHDR_DESC5		2
#define	NUM_TX_PKTHDR_DESC1		2

#define	ETH_REFILL_THRESHOLD1	0	// must < NUM_RX_PKTHDR_DESC
#define	ETH_REFILL_THRESHOLD2	0	// must < NUM_RX_PKTHDR_DESC
#define	ETH_REFILL_THRESHOLD3	0	// must < NUM_RX_PKTHDR_DESC
#define	ETH_REFILL_THRESHOLD4	0	// must < NUM_RX_PKTHDR_DESC
#define	ETH_REFILL_THRESHOLD5	0	// must < NUM_RX_PKTHDR_DESC

#define	QUEUEID0_RXRING_MAPPING		0
#define	QUEUEID1_RXRING_MAPPING		0
#define	QUEUEID2_RXRING_MAPPING		0
#define	QUEUEID3_RXRING_MAPPING		0
#define	QUEUEID4_RXRING_MAPPING		0
#define	QUEUEID5_RXRING_MAPPING		0

#define	NUM_TX_PKTHDR_DESC2		2
#define	NUM_TX_PKTHDR_DESC3		2

/* refer to rtl865xc_swNic.c & rtl865xc_swNic.h
 */
#define	UNCACHE_MASK   0x20000000

/* rxPreProcess */
#define	RTL8651_CPU_PORT                0x07 /* in rtl8651_tblDrv.h */
#define	_RTL865XB_EXTPORTMASKS   7

typedef struct {
	uint16			vid;
	uint16			pid;
	uint16			len;
	uint16			priority:3;
	uint16			rxPri:3;
	void* 			input;
	struct dev_priv*	priv;
	uint32			isPdev;


}	rtl_nicRx_info;

typedef struct {
	uint16		vid;
	uint16		portlist;
	uint16		srcExtPort;
	uint16		flags;
	uint32		txIdx:1;
	void 			*out_skb;
}	rtl_nicTx_info;


#define	RTL_ASSIGN_RX_PRIORITY			0
/* --------------------------------------------------------------------
 * ROUTINE NAME - swNic_init
 * --------------------------------------------------------------------
 * FUNCTION: This service initializes the switch NIC.
 * INPUT   :
        userNeedRxPkthdrRingCnt[RTL865X_SWNIC_RXRING_MAX_PKTDESC]: Number of Rx pkthdr descriptors. of each ring
        userNeedRxMbufRingCnt: Number of Rx mbuf descriptors.
        userNeedTxPkthdrRingCnt[RTL865X_SWNIC_TXRING_MAX_PKTDESC]: Number of Tx pkthdr descriptors. of each ring
        clusterSize: Size of a mbuf cluster.
 * OUTPUT  : None.
 * RETURN  : Upon successful completion, the function returns ENOERR.
        Otherwise,
		EINVAL: Invalid argument.
 * NOTE    : None.
 * -------------------------------------------------------------------*/
int32 swNic_init(uint32 userNeedRxPkthdrRingCnt[],
                 uint32 userNeedRxMbufRingCnt,
                 uint32 userNeedTxPkthdrRingCnt[],
                 uint32 clusterSize);



/* --------------------------------------------------------------------
 * ROUTINE NAME - swNic_intHandler
 * --------------------------------------------------------------------
 * FUNCTION: This function is the NIC interrupt handler.
 * INPUT   :
		intPending: Pending interrupts.
 * OUTPUT  : None.
 * RETURN  : None.
 * NOTE    : None.
 * -------------------------------------------------------------------*/
void swNic_intHandler(uint32 intPending);
int32 swNic_flushRxRingByPriority(int priority);
int32 swNic_receive(rtl_nicRx_info *info, int retryCount);
int32 swNic_send(void *skb, void * output, uint32 len, rtl_nicTx_info *nicTx);
int32 swNic_txRingFreeCount(int idx);  /* Check TX ring free space for flow control */
//__MIPS16
int32 swNic_txDone(int idx);
int32 swNic_txDone_stats(int idx, unsigned int *pkts_out, unsigned int *bytes_out);  /* TX done with BQL stats */
void swNic_freeRxBuf(void);
int swNic_refillRxRing(void);  /* Refill RX descriptors from rx_skb_queue */
int32	swNic_txRunout(void);
extern	uint32* rxMbufRing;
extern unsigned char *alloc_rx_buf(void **skb, int buflen);
extern unsigned char *alloc_rx_buf_init(void **skb, int buflen);
extern void free_rx_buf(void *skb);
extern void eth_save_and_cli(unsigned long *flags);
extern void eth_restore_flags(unsigned long flags);


#define	RTL8651_IOCTL_GETWANLINKSTATUS			2000
#define	RTL8651_IOCTL_GETLANLINKSTATUS			2001
#define	RTL8651_IOCTL_GETWANTHROUGHPUT			2002
#define	RTL8651_IOCTL_GETLANPORTLINKSTATUS		2003
#define	RTL8651_IOCTL_GETWANPORTLINKSTATUS		2004
#define 	RTL8651_IOCTL_GETWANLINKSPEED 			2100
//#define 	RTL8651_IOCTL_SETWANLINKSPEED 			2101
#define RTL8651_IOCTL_GETLANLINKSTATUSALL		2105

#define	RTL8651_IOCTL_SETWANLINKSTATUS			2200

#define	RTL8651_IOCTL_CLEARBRSHORTCUTENTRY		2210
#define	RTL8651_IOCTL_GETPORTIDBYCLIENTMAC		2013

/*
 * RTL8651 ioctl ABI structure (userspace/kernel interface)
 *
 * Security fix (2025-11-21): Replaced raw array with typed structure
 * to prevent arbitrary kernel memory write via args[3] pointer abuse.
 *
 * Usage from userspace:
 *   struct rtl8651_ioctl_args args = {
 *       .cmd = RTL8651_IOCTL_GETWANLINKSTATUS,
 *       .arg1 = 0,
 *       .arg2 = 0,
 *       .result = &my_result
 *   };
 *   ioctl(fd, SIOCDEVPRIVATE, &args);
 */
struct rtl8651_ioctl_args {
	u32 cmd;                /* Ioctl sub-command (RTL8651_IOCTL_*) */
	u32 arg1;               /* Command-specific argument 1 */
	u32 arg2;               /* Command-specific argument 2 */
	int __user *result;     /* Userspace pointer for result (validated with copy_to_user) */
};

#define	RTL_NICRX_OK	0
#define	RTL_NICRX_REPEAT	-2
#define	RTL_NICRX_NULL	-1
int32 swNic_reInit(void);

struct ring_que {
	int qlen;
	int qmax;
	int head;
	int tail;
	struct sk_buff **ring;
};

static inline void *UNCACHED_MALLOC(int size)
{
	return ((void *)(((uint32)kmalloc(size, GFP_ATOMIC)) | UNCACHE_MASK));
}

#endif /* _SWNIC_H */
