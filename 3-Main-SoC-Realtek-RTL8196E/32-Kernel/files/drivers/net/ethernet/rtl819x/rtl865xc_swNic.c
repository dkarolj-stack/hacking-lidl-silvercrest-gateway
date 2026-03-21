/*
 * RTL865xC Switch NIC - Descriptor Ring Management
 * ============================================================================
 *
 * Original Code:
 *   Copyright (c) 2008-2011 Realtek Semiconductor Corporation
 *   Author: bo_zhao <bo_zhao@realtek.com>
 *
 * Rewritten for Linux 5.10:
 *   Copyright (c) 2025 Jacques Nilo
 *   Based on Realtek SDK 2.6.30 driver
 *
 * Abstract:
 *   Low-level switch core NIC driver for RTL8196E. Manages DMA descriptor
 *   rings for TX/RX packet handling with proper cache coherency for the
 *   MIPS non-coherent DMA architecture.
 *
 * Key changes from original 2.6.30 driver:
 *   - Simplified ring management (removed priority queue complexity)
 *   - Spinlock protection for descriptor ring access
 *   - Bounds checking and NULL pointer validation
 *   - DMA cache coherency fixes (TX packet corruption, RX duplication)
 *   - Error statistics via ethtool, BQL support
 *
 * SPDX-License-Identifier: GPL-2.0
 * ============================================================================
 */
#include <linux/skbuff.h>
#include <linux/cache.h>     /* for L1_CACHE_BYTES */
#include <linux/if_ether.h>  /* for ETH_ZLEN */
#include <linux/prefetch.h>
#include <linux/compiler.h>
#include <asm/io.h>          /* for dma_cache_wback_inv(), dma_cache_inv() - Kernel 5.4: moved from cacheflush.h */
#include "rtl819x.h"
#include "rtl865xc_swNic.h"
#include "AsicDriver/rtl865x_asicCom.h"
#include "AsicDriver/rtl865x_asicL2.h"

/* RTL865xC ASIC regs: local-only defines (trimmed from rtl865xc_asicregs.h) */
#define CPURPDCR1 (0x008 + CPU_IFACE_BASE)	   /* Rx pkthdr descriptor control 1 */

#define CPURPDCR2 (0x00c + CPU_IFACE_BASE)	   /* Rx pkthdr descriptor control 2 */

#define CPURPDCR3 (0x010 + CPU_IFACE_BASE)	   /* Rx pkthdr descriptor control 3 */

#define CPURPDCR4 (0x014 + CPU_IFACE_BASE)	   /* Rx pkthdr descriptor control 4 */

#define CPURPDCR5 (0x018 + CPU_IFACE_BASE)	   /* Rx pkthdr descriptor control 5 */

#define CPURMDCR0 (0x01c + CPU_IFACE_BASE)	   /* Rx mbuf descriptor control */

#define CPUTPDCR1 (0x024 + CPU_IFACE_BASE)	   /* Tx pkthdr descriptor control High */

#define CPUTPDCR2 (0x060 + CPU_IFACE_BASE)	   /* Tx Pkthdr Descriptor 2 Control Register */

#define CPUTPDCR3 (0x064 + CPU_IFACE_BASE)	   /* Tx Pkthdr Descriptor 3 Control Register */

#define TXFD (1 << 23)		  /* Notify Tx descriptor fetch */

#define DESC_OWNED_BIT (1 << 0)

#define DESC_RISC_OWNED (0 << 0)

#define DESC_SWCORE_OWNED (1 << 0)

#define DESC_WRAP (1 << 1)


/* Phase 2: Rate-limited logging */
static DEFINE_RATELIMIT_STATE(rtl_swnic_err_limit, 5*HZ, 10);

/* Optional TX path experiments (disabled by default)
 * Set to 1 to enable during A/B testing.
 */
#ifndef RTL_FIX_TX_INDEX_AFTER_OWNERSHIP
#define RTL_FIX_TX_INDEX_AFTER_OWNERSHIP 1
#endif

#ifndef RTL_FIX_TX_KICK_ONCE
#define RTL_FIX_TX_KICK_ONCE 0  /* Always pulse TXFD after OWN (more robust) */
#endif

/* Variant C: Pulse TXFD (edge) instead of keeping the bit set.
 * Some HW requires an edge on TXFD to fetch descriptors exactly once.
 * Disabled by default; enable for A/B tests.
 */
#ifndef RTL_TXFD_PULSE
#define RTL_TXFD_PULSE 1
#endif

/* Phase 2: Global error statistics (accessible via accessor function) */
static unsigned long rtl_swnic_rx_desc_null_errors = 0;
static unsigned long rtl_swnic_rx_mbuf_null_errors = 0;
static unsigned long rtl_swnic_rx_skb_null_errors = 0;
static unsigned long rtl_swnic_rx_desc_index_errors = 0;
static unsigned long rtl_swnic_rx_mbuf_index_errors = 0;
static unsigned long rtl_swnic_rx_length_errors = 0;
static unsigned long rtl_swnic_tx_desc_null_errors = 0;
static unsigned long rtl_swnic_tx_mbuf_null_errors = 0;
static unsigned long rtl_swnic_tx_desc_index_errors = 0;

/* Accessor function to avoid direct symbol access issues */
void rtl_swnic_get_error_stats(unsigned long *stats)
{
	if (!stats)
		return;

	stats[0] = rtl_swnic_rx_desc_null_errors;
	stats[1] = rtl_swnic_rx_mbuf_null_errors;
	stats[2] = rtl_swnic_rx_skb_null_errors;
	stats[3] = rtl_swnic_rx_desc_index_errors;
	stats[4] = rtl_swnic_rx_mbuf_index_errors;
	stats[5] = rtl_swnic_rx_length_errors;
	stats[6] = rtl_swnic_tx_desc_null_errors;
	stats[7] = rtl_swnic_tx_mbuf_null_errors;
	stats[8] = rtl_swnic_tx_desc_index_errors;
}
EXPORT_SYMBOL(rtl_swnic_get_error_stats);

/* Security fix (2025-11-21): Spinlocks to protect descriptor ring access
 * These locks prevent race conditions when accessing/modifying descriptor indices
 * from multiple contexts (NAPI poll, TX path, etc.)
 */
static DEFINE_SPINLOCK(rtl_rx_ring_lock);  /* Protects RX descriptor rings */
static DEFINE_SPINLOCK(rtl_tx_ring_lock);  /* Protects TX descriptor rings */

/* RX Ring */
static uint32*  rxPkthdrRing[RTL865X_SWNIC_RXRING_HW_PKTDESC];                 /* Point to the starting address of RX pkt Hdr Ring */
static uint32   rxPkthdrRingCnt[RTL865X_SWNIC_RXRING_HW_PKTDESC];              /* Total pkt count for each Rx descriptor Ring */
static uint32   rxPkthdrRefillThreshold[RTL865X_SWNIC_RXRING_HW_PKTDESC];              /* Ether refill threshold for each Rx descriptor Ring */

/* TX Ring */
static uint32*  txPkthdrRing[RTL865X_SWNIC_TXRING_HW_PKTDESC];             /* Point to the starting address of TX pkt Hdr Ring */
static uint32   txPkthdrRingCnt[RTL865X_SWNIC_TXRING_HW_PKTDESC];          /* Total pkt count for each Tx descriptor Ring */

#define txPktHdrRingFull(idx)   (((txPkthdrRingFreeIndex[idx] + 1) & (txPkthdrRingMaxIndex[idx])) == (txPkthdrRingDoneIndex[idx]))

/* Mbuf */
uint32* rxMbufRing;                                                     /* Point to the starting address of MBUF Ring */
uint32  rxMbufRingCnt;                                                  /* Total MBUF count */

static uint32  size_of_cluster;

/* descriptor ring tracing pointers */
static int32   currRxPkthdrDescIndex[RTL865X_SWNIC_RXRING_HW_PKTDESC];      /* Rx pkthdr descriptor to be handled by CPU */
static int32   currRxMbufDescIndex;        /* Rx mbuf descriptor to be handled by CPU */

static int32   currTxPkthdrDescIndex[RTL865X_SWNIC_TXRING_HW_PKTDESC];      /* Tx pkthdr descriptor to be handled by CPU */
static int32 txPktDoneDescIndex[RTL865X_SWNIC_TXRING_HW_PKTDESC];

static int32   rxDescReadyForHwIndex[RTL865X_SWNIC_RXRING_HW_PKTDESC];
static int32   rxDescCrossBoundFlag[RTL865X_SWNIC_RXRING_HW_PKTDESC];

static uint8 extPortMaskToPortNum[_RTL865XB_EXTPORTMASKS+1] =
{
	5, 6, 7, 5, 8, 5, 5, 5
};


/**
 * return_to_rxing_check - Check if RX ring has unprocessed descriptors
 * @ringIdx: RX ring index
 *
 * Security fix (2025-11-21): Added spinlock protection
 * This function compares two descriptor indices that could be modified
 * by other contexts. Without spinlock, we could read inconsistent state.
 *
 * Returns: 1 if ring has work to do, 0 otherwise
 */
inline int return_to_rxing_check(int ringIdx)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&rtl_rx_ring_lock, flags);
	ret = ((rxDescReadyForHwIndex[ringIdx]!=currRxPkthdrDescIndex[ringIdx]) && (rxPkthdrRingCnt[ringIdx]!=0))? 1:0;
	spin_unlock_irqrestore(&rtl_rx_ring_lock, flags);

	return ret;
}
static inline void set_RxPkthdrRing_OwnBit(uint32 rxRingIdx)
{
	/* Ensure all writes complete before changing ownership */
	wmb();
	rxPkthdrRing[rxRingIdx][rxDescReadyForHwIndex[rxRingIdx]] |= DESC_SWCORE_OWNED;
	/* Ensure ownership change is visible to hardware */
	wmb();

	if ( ++rxDescReadyForHwIndex[rxRingIdx] == rxPkthdrRingCnt[rxRingIdx] ) {
		rxDescReadyForHwIndex[rxRingIdx] = 0;
		/* Toggle wrap flag properly: 0->1, 1->0 */
		rxDescCrossBoundFlag[rxRingIdx] = 1 - rxDescCrossBoundFlag[rxRingIdx];
	}
}

/**
 * release_pkthdr - Return RX buffer to hardware
 * @skb: SKB to release back to RX ring
 * @idx: RX ring index
 *
 * Security fix (2025-11-21): Added spinlock protection
 * This function manipulates RX descriptor rings and must be protected
 * from concurrent access by NAPI poll and other RX processing.
 */
static void release_pkthdr(struct sk_buff  *skb, int idx)
{
	struct rtl_pktHdr *pReadyForHw;
	uint32 mbufIndex;
	unsigned long flags;

	/* Validate input parameters */
	if (!skb || !skb->head || idx >= RTL865X_SWNIC_RXRING_HW_PKTDESC) {
		rtl_swnic_rx_desc_index_errors++;
		if (__ratelimit(&rtl_swnic_err_limit)) {
			printk(KERN_WARNING "rtl819x_swnic: Invalid params in release_pkthdr: skb=%p idx=%d\n",
			       skb, idx);
		}
		return;
	}

	/* Invalidate entire RX buffer for hardware DMA (Issue #3)
	 * Note: Use skb->head (buffer start) + truesize (total allocated) here,
	 * not skb->data + skb->len, because we're preparing the buffer for the
	 * hardware to WRITE into. We need to invalidate the entire allocated
	 * buffer, not just the data portion. Our RX buffers are contiguous
	 * (allocated by alloc_rx_buf), so no scatter-gather concerns here.
	 */
	dma_cache_wback_inv((unsigned long)skb->head, skb->truesize);

	/* Security fix: Protect descriptor ring access */
	spin_lock_irqsave(&rtl_rx_ring_lock, flags);

	/* Bounds check on rxDescReadyForHwIndex */
	if (rxDescReadyForHwIndex[idx] >= rxPkthdrRingCnt[idx]) {
		rtl_swnic_rx_desc_index_errors++;
		if (__ratelimit(&rtl_swnic_err_limit)) {
			printk(KERN_WARNING "rtl819x_swnic: RX desc index OOB: ring=%d idx=%d max=%d\n",
			       idx, rxDescReadyForHwIndex[idx], rxPkthdrRingCnt[idx]);
		}
		spin_unlock_irqrestore(&rtl_rx_ring_lock, flags);
		return;
	}

	pReadyForHw = (struct rtl_pktHdr *)(rxPkthdrRing[idx][rxDescReadyForHwIndex[idx]] &
						~(DESC_OWNED_BIT | DESC_WRAP));

	/* NULL check on hardware-provided pointer */
	if (!pReadyForHw || !pReadyForHw->ph_mbuf) {
		if (!pReadyForHw)
			rtl_swnic_rx_desc_null_errors++;
		else
			rtl_swnic_rx_mbuf_null_errors++;
		if (__ratelimit(&rtl_swnic_err_limit)) {
			printk(KERN_WARNING "rtl819x_swnic: NULL pointer in RX ring %d: pPkthdr=%p mbuf=%p\n",
			       idx, pReadyForHw, pReadyForHw ? pReadyForHw->ph_mbuf : NULL);
		}
		spin_unlock_irqrestore(&rtl_rx_ring_lock, flags);
		return;
	}

	mbufIndex = ((uint32)(pReadyForHw->ph_mbuf) - (rxMbufRing[0] & ~(DESC_OWNED_BIT | DESC_WRAP))) /
					(sizeof(struct rtl_mBuf));

	/* CRITICAL: Validate mbufIndex bounds */
	if (mbufIndex >= rxMbufRingCnt) {
		rtl_swnic_rx_mbuf_index_errors++;
		if (__ratelimit(&rtl_swnic_err_limit)) {
			printk(KERN_ERR "rtl819x_swnic: CRITICAL - mbuf index OOB: %d >= %d\n",
			       mbufIndex, rxMbufRingCnt);
		}
		spin_unlock_irqrestore(&rtl_rx_ring_lock, flags);
		return;
	}

	pReadyForHw->ph_mbuf->m_data = skb->data;
	pReadyForHw->ph_mbuf->m_extbuf = skb->data;
	pReadyForHw->ph_mbuf->skb = skb;

	rxMbufRing[mbufIndex] |= DESC_SWCORE_OWNED;
	set_RxPkthdrRing_OwnBit(idx);

	spin_unlock_irqrestore(&rtl_rx_ring_lock, flags);

	dma_cache_wback_inv((unsigned long)pReadyForHw, sizeof(struct rtl_pktHdr));
	dma_cache_wback_inv((unsigned long)(pReadyForHw->ph_mbuf), sizeof(struct rtl_mBuf));
}

extern struct sk_buff *dev_alloc_8190_skb(unsigned char *data, int size);

static void increase_rx_idx_release_pkthdr(struct sk_buff  *skb, int idx)
{
	struct rtl_pktHdr *pReadyForHw;
	uint32 mbufIndex;
	unsigned long flags;

	/* Validate input parameters */
	if (!skb || !skb->head || idx >= RTL865X_SWNIC_RXRING_HW_PKTDESC) {
		return;
	}

	local_irq_save(flags);

	/* Bounds check on rxDescReadyForHwIndex */
	if (rxDescReadyForHwIndex[idx] >= rxPkthdrRingCnt[idx]) {
		local_irq_restore(flags);
		return;
	}

	/* Invalidate entire RX buffer for hardware DMA (Issue #3)
	 * Same rationale as in release_pkthdr(): preparing buffer for HW write.
	 * Our RX buffers are contiguous, no scatter-gather fragmentation.
	 */
	dma_cache_wback_inv((unsigned long)skb->head, skb->truesize);

	pReadyForHw = (struct rtl_pktHdr *)(rxPkthdrRing[idx][rxDescReadyForHwIndex[idx]] &
						~(DESC_OWNED_BIT | DESC_WRAP));

	/* NULL check on hardware-provided pointer */
	if (!pReadyForHw || !pReadyForHw->ph_mbuf) {
		local_irq_restore(flags);
		return;
	}

	if ( ++currRxPkthdrDescIndex[idx] == rxPkthdrRingCnt[idx] ) {
		currRxPkthdrDescIndex[idx] = 0;
		/* Toggle wrap flag properly: 0->1, 1->0 */
		rxDescCrossBoundFlag[idx] = 1 - rxDescCrossBoundFlag[idx];
	}

	mbufIndex = ((uint32)(pReadyForHw->ph_mbuf) - (rxMbufRing[0] & ~(DESC_OWNED_BIT | DESC_WRAP))) /
					(sizeof(struct rtl_mBuf));

	/* CRITICAL: Validate mbufIndex bounds */
	if (mbufIndex >= rxMbufRingCnt) {
		local_irq_restore(flags);
		return;
	}

	pReadyForHw->ph_mbuf->m_data = skb->data;
	pReadyForHw->ph_mbuf->m_extbuf = skb->data;
	pReadyForHw->ph_mbuf->skb = skb;

	rxMbufRing[mbufIndex] |= DESC_SWCORE_OWNED;

	set_RxPkthdrRing_OwnBit(idx);

	dma_cache_wback_inv((unsigned long)pReadyForHw, sizeof(struct rtl_pktHdr));
	dma_cache_wback_inv((unsigned long)(pReadyForHw->ph_mbuf), sizeof(struct rtl_mBuf));

	local_irq_restore(flags);
}

static int __swNic_geRxRingIdx(uint32 rxRingIdx, uint32 *currRxPktDescIdx)
{
	unsigned long flags;

	if(rxPkthdrRingCnt[rxRingIdx] == 0) {
		return FAILED;
	}

	local_irq_save(flags);

	/* Bounds check on descriptor index */
	if (currRxPkthdrDescIndex[rxRingIdx] >= rxPkthdrRingCnt[rxRingIdx]) {
		local_irq_restore(flags);
		return FAILED;
	}

	if ( (rxDescCrossBoundFlag[rxRingIdx]==0&&(currRxPkthdrDescIndex[rxRingIdx]>=rxDescReadyForHwIndex[rxRingIdx]))
		|| (rxDescCrossBoundFlag[rxRingIdx]==1&&(currRxPkthdrDescIndex[rxRingIdx]<rxDescReadyForHwIndex[rxRingIdx])) )
	{
		/* Ensure we read the latest descriptor state from hardware */
		rmb();
		if((rxPkthdrRing[rxRingIdx][currRxPkthdrDescIndex[rxRingIdx]] & DESC_OWNED_BIT) == DESC_RISC_OWNED)
		{
			local_irq_restore(flags);
			*currRxPktDescIdx = currRxPkthdrDescIndex[rxRingIdx];
			return SUCCESS;
		}
	}

	local_irq_restore(flags);
	return FAILED;
}

/**
 * swNic_receive - Receive one packet from RX descriptor ring
 * @info: Pointer to rtl_nicRx_info structure (filled on success)
 * @retryCount: Retry counter for RTL_NICRX_REPEAT handling
 *
 * Core RX function. Checks descriptor ownership, validates packet header,
 * allocates replacement buffer (synchronous), and returns packet info.
 * Uses synchronous buffer allocation for optimal performance (78+ Mbps).
 *
 * Buffer management: For each received packet, a new buffer is allocated
 * BEFORE passing the current packet to the network stack. This ensures
 * descriptors are immediately available for hardware.
 *
 * Return: RTL_NICRX_OK on success (packet in info->input),
 *         RTL_NICRX_NULL if no packet available or allocation failed,
 *         RTL_NICRX_REPEAT if caller should retry
 */
#define	RTL_NIC_RX_RETRY_MAX		(256)
#define	RTL_ETH_NIC_DROP_RX_PKT_RESTART		\
	do {\
		increase_rx_idx_release_pkthdr(pPkthdr->ph_mbuf->skb, rxRingIdx); \
		REG32(CPUIISR) = (MBUF_DESC_RUNOUT_IP_ALL|PKTHDR_DESC_RUNOUT_IP_ALL); \
	} while(0)

/**
 * validate_pkt_hdr - Validate RX packet header structure (Issue #9)
 * @pPhdr: Packet header to validate
 * @ringIdx: Ring index (for potential error reporting)
 *
 * Returns: 0 on success, -EINVAL on validation failure
 *
 * Validates hardware-provided packet header for:
 * - NULL pointer
 * - Valid mbuf pointer
 * - Valid packet length (64-1522 bytes)
 */
static inline int validate_pkt_hdr(struct rtl_pktHdr *pPhdr, int ringIdx)
{
    u32 icr;
    u32 min_len, max_len;

    if (!pPhdr)
        return -EINVAL;

    if (!pPhdr->ph_mbuf)
        return -EINVAL;

    /* Determine length rules based on EXCLUDE_CRC runtime bit */
    icr = REG32(CPUICR);
    if (icr & EXCLUDE_CRC) {
        /* Hardware excludes FCS from ph_len */
        min_len = 60;   /* 64 - 4 */
        max_len = 1518; /* 1522 - 4 (allow VLAN) */
    } else {
        /* Hardware includes FCS in ph_len */
        min_len = 64;
        max_len = 1522; /* includes potential VLAN tag */
    }

    if (pPhdr->ph_len < min_len)
        return -EINVAL;

    if (pPhdr->ph_len > max_len)
        return -EINVAL;

    return 0;
}

int32 swNic_receive(rtl_nicRx_info *info, int retryCount)
{
	struct rtl_pktHdr * pPkthdr;
	unsigned char *buf;
	void *skb;
	uint32 rxRingIdx;
	uint32 currRxPktDescIdx;

	/* Validate input */
	if (!info) {
		return RTL_NICRX_NULL;
	}

get_next:
	 /* Check OWN bit of descriptors */
	rxRingIdx = 0;
	if (__swNic_geRxRingIdx(rxRingIdx,&currRxPktDescIdx)==SUCCESS)
	{
		/* Bounds check already done in __swNic_geRxRingIdx */

		/* Fetch pkthdr */
		pPkthdr = (struct rtl_pktHdr *) (rxPkthdrRing[rxRingIdx][currRxPktDescIdx] & ~(DESC_OWNED_BIT | DESC_WRAP));

		/* Validate packet header (Issue #9) */
		if (validate_pkt_hdr(pPkthdr, rxRingIdx) != 0) {
			/* Update error counters based on failure type */
			if (!pPkthdr) {
				rtl_swnic_rx_desc_null_errors++;
			} else if (!pPkthdr->ph_mbuf) {
				rtl_swnic_rx_mbuf_null_errors++;
        } else {
            /* Classify length error with runtime EXCLUDE_CRC rules */
            u32 icr = REG32(CPUICR);
            u32 min_len = (icr & EXCLUDE_CRC) ? 60 : 64;
            u32 max_len = (icr & EXCLUDE_CRC) ? 1518 : 1522;
            if (pPkthdr->ph_len < min_len || pPkthdr->ph_len > max_len) {
                rtl_swnic_rx_length_errors++;
            }
        }

			if (__ratelimit(&rtl_swnic_err_limit)) {
				printk(KERN_WARNING "rtl819x_swnic: Invalid RX pkthdr: pPkthdr=%p mbuf=%p len=%d\n",
				       pPkthdr, pPkthdr ? pPkthdr->ph_mbuf : NULL,
				       pPkthdr ? pPkthdr->ph_len : 0);
			}
			RTL_ETH_NIC_DROP_RX_PKT_RESTART;
			goto get_next;
		}

        /* Additional check for SKB pointer after DMA cache invalidation */
        if (!pPkthdr->ph_mbuf->skb) {
			rtl_swnic_rx_skb_null_errors++;
			if (__ratelimit(&rtl_swnic_err_limit)) {
				printk(KERN_WARNING "rtl819x_swnic: NULL SKB in validated pkthdr\n");
			}
			RTL_ETH_NIC_DROP_RX_PKT_RESTART;
			goto get_next;
		}

		/* Invalidate DMA cache for packet header and mbuf */
		dma_cache_inv((unsigned long)pPkthdr, sizeof(struct rtl_pktHdr));
		dma_cache_inv((unsigned long)(pPkthdr->ph_mbuf), sizeof(struct rtl_mBuf));

		/* Drop on checksum error */
		if ((pPkthdr->ph_flags & (CSUM_TCPUDP_OK | CSUM_IP_OK)) != (CSUM_TCPUDP_OK | CSUM_IP_OK))
		{
			RTL_ETH_NIC_DROP_RX_PKT_RESTART;
			goto get_next;
		}

		/*
		 * vid is assigned in rtl8651_rxPktPreprocess()
		 * do not update it when CONFIG_RTL_HARDWARE_NAT is defined
		 */
		info->vid=pPkthdr->ph_vlanId;

		info->pid=pPkthdr->ph_portlist;

		/*
		 * CRITICAL FIX: Restore synchronous buffer allocation (original mechanism)
		 * =========================================================================
		 * Previous async mechanism using SKB destructor caused performance collapse:
		 * - Async: 24 Mbps with stalls after 15s
		 * - Sync: 78.5 Mbps stable for 30s
		 *
		 * Root cause: Buffer refill latency
		 * - Async: Descriptors refilled only when network stack frees SKB (several ms later)
		 * - Sync: New buffer allocated immediately, descriptor available instantly
		 *
		 * Original flow (restored here):
		 * 1. alloc_rx_buf() allocates NEW SKB from rx_skb_queue
		 * 2. Pass CURRENT SKB (with packet data) to network stack
		 * 3. Install NEW SKB in descriptor
		 * 4. Return descriptor to hardware immediately
		 */
        buf = alloc_rx_buf(&skb, size_of_cluster);
        if (buf)
        {
            /* Pass current packet to network stack */
            info->input = pPkthdr->ph_mbuf->skb;
            /* Runtime: if EXCLUDE_CRC bit set, ph_len excludes FCS */
            if (REG32(CPUICR) & EXCLUDE_CRC)
                info->len = pPkthdr->ph_len;
            else
                info->len = pPkthdr->ph_len - 4;

			/* Install new SKB in descriptor and return to hardware */
			increase_rx_idx_release_pkthdr(skb, rxRingIdx);

			/* Clear runout interrupt flag */
			REG32(CPUIISR) = (MBUF_DESC_RUNOUT_IP_ALL|PKTHDR_DESC_RUNOUT_IP_ALL);
		}
		else {
			/* Buffer pool exhausted - drop packet */
			return RTL_NICRX_NULL;
		}

		return RTL_NICRX_OK;
	} else {
		return RTL_NICRX_NULL;
	}
}

#undef	RTL_ETH_NIC_DROP_RX_PKT_RESTART

/**
 * _swNic_send - Internal: Send packet via TX descriptor ring
 * @skb: Socket buffer (stored in descriptor for later free)
 * @output: Pointer to packet data buffer
 * @len: Packet length in bytes
 * @nicTx: TX info structure (portlist, flags, VLAN ID, ring index)
 *
 * Core TX function. Validates parameters, checks ring space, fills
 * packet header descriptor, flushes DMA cache, sets ownership bit,
 * and triggers hardware fetch via TXFD pulse.
 *
 * Security: Protected by spinlock to prevent concurrent access from
 * multiple TX contexts and NAPI poll.
 *
 * Cache coherency: Explicit dma_cache_wback_inv() before ownership
 * transfer to ensure hardware sees correct packet data.
 *
 * Return: Descriptor index on success, -1 on error (ring full, invalid params)
 */
static __always_inline int32 _swNic_send(void *skb, void * output, uint32 len,rtl_nicTx_info *nicTx)
{
    struct rtl_pktHdr * pPkthdr;
    int next_index, ret;
    unsigned long flags;
#if RTL_FIX_TX_KICK_ONCE
    int was_empty = 0;
#endif

	/* Validate input parameters */
	if (unlikely(!skb || !output || !nicTx || len == 0)) {
		rtl_swnic_tx_desc_index_errors++;
		if (__ratelimit(&rtl_swnic_err_limit)) {
			printk(KERN_WARNING "rtl819x_swnic: Invalid TX params: skb=%p output=%p nicTx=%p len=%d\n",
			       skb, output, nicTx, len);
		}
		return -1;
	}

	/* Bounds check on TX ring index */
	if (unlikely(nicTx->txIdx >= RTL865X_SWNIC_TXRING_HW_PKTDESC)) {
		rtl_swnic_tx_desc_index_errors++;
		if (__ratelimit(&rtl_swnic_err_limit)) {
			printk(KERN_WARNING "rtl819x_swnic: TX ring index OOB: %d >= %d\n",
			       nicTx->txIdx, RTL865X_SWNIC_TXRING_HW_PKTDESC);
		}
		return -1;
	}

	/* Security fix: Lock TX ring for duration of enqueue operation
	 * Protects currTxPkthdrDescIndex and txPktDoneDescIndex from races
	 */
	spin_lock_irqsave(&rtl_tx_ring_lock, flags);

	/* Bounds check on current descriptor index */
	if (unlikely(currTxPkthdrDescIndex[nicTx->txIdx] >= txPkthdrRingCnt[nicTx->txIdx])) {
		rtl_swnic_tx_desc_index_errors++;
		if (__ratelimit(&rtl_swnic_err_limit)) {
			printk(KERN_WARNING "rtl819x_swnic: TX desc index OOB: ring=%d idx=%d max=%d\n",
			       nicTx->txIdx, currTxPkthdrDescIndex[nicTx->txIdx], txPkthdrRingCnt[nicTx->txIdx]);
		}
		spin_unlock_irqrestore(&rtl_tx_ring_lock, flags);
		return -1;
	}

	if ((currTxPkthdrDescIndex[nicTx->txIdx]+1)==txPkthdrRingCnt[nicTx->txIdx])
		next_index = 0;
	else
		next_index = currTxPkthdrDescIndex[nicTx->txIdx]+1;

	if (unlikely(next_index == txPktDoneDescIndex[nicTx->txIdx]))	{
		/*	TX ring full	*/
		/* Note: this is normal under load, don't spam logs */
		spin_unlock_irqrestore(&rtl_tx_ring_lock, flags);
		return -1;
	}

	/* Fetch packet header from Tx ring */
	pPkthdr = (struct rtl_pktHdr *) ((int32) txPkthdrRing[nicTx->txIdx][currTxPkthdrDescIndex[nicTx->txIdx]]
                                                & ~(DESC_OWNED_BIT | DESC_WRAP));

	/* NULL check on hardware-provided pointer */
	if (unlikely(!pPkthdr || !pPkthdr->ph_mbuf)) {
		if (!pPkthdr)
			rtl_swnic_tx_desc_null_errors++;
		else
			rtl_swnic_tx_mbuf_null_errors++;
		if (__ratelimit(&rtl_swnic_err_limit)) {
			printk(KERN_ERR "rtl819x_swnic: NULL in TX ring %d: pPkthdr=%p mbuf=%p\n",
			       nicTx->txIdx, pPkthdr, pPkthdr ? pPkthdr->ph_mbuf : NULL);
		}
		spin_unlock_irqrestore(&rtl_tx_ring_lock, flags);
		return -1;
	}

    /* Frame length rules (no FCS in length):
     * Hardware appends FCS (EXCLUDE_CRC set during start). Do not add +4 here.
     * Pad up to ETH_ZLEN (60 bytes) if needed.
     */
    if (unlikely(len < ETH_ZLEN)) {
        len = ETH_ZLEN;
    }

	/* Validate final packet length (exclude FCS): allow up to 1518 (with VLAN) */
	if (len > 1518) {
		if (__ratelimit(&rtl_swnic_err_limit))
			printk(KERN_WARNING "rtl819x_swnic: TX len too large: %u (>1522)\n", len);
		spin_unlock_irqrestore(&rtl_tx_ring_lock, flags);
		return -1;
	}

	pPkthdr->ph_mbuf->m_len  = len;
	pPkthdr->ph_mbuf->m_extsize = len;
	pPkthdr->ph_mbuf->skb = skb;
	pPkthdr->ph_len = len;

	pPkthdr->ph_vlanId = nicTx->vid;
	pPkthdr->ph_portlist = nicTx->portlist&0x1f;
	pPkthdr->ph_srcExtPortNum = nicTx->srcExtPort;
	pPkthdr->ph_flags = nicTx->flags;



	/* Set cluster pointer to buffer */
	pPkthdr->ph_mbuf->m_data    = (output);
	pPkthdr->ph_mbuf->m_extbuf = (output);

	pPkthdr->ph_ptpPkt = 0;

	/* CRITICAL FIX: Writeback packet data buffer before DMA read
	 *
	 * On MIPS non-coherent systems, packet data modified by CPU (e.g.,
	 * TCP/IP stack building headers/payload) remains in cache. Hardware
	 * DMA engine reads from RAM, not cache. Without this writeback,
	 * hardware transmits stale/garbage data → packet corruption.
	 *
	 * This fixes 48% TCP retransmission rate caused by corrupted TX packets.
	 * Must writeback BEFORE setting DESC_SWCORE_OWNED bit.
	 */
	dma_cache_wback_inv((unsigned long)output, len);

	/* Writeback descriptor structures */
	dma_cache_wback_inv((unsigned long)pPkthdr, sizeof(struct rtl_pktHdr));
	dma_cache_wback_inv((unsigned long)(pPkthdr->ph_mbuf), sizeof(struct rtl_mBuf));

    /* Was ring empty before enqueue? Used by optional TX kick optimization */
#if RTL_FIX_TX_KICK_ONCE
    was_empty = (txPktDoneDescIndex[nicTx->txIdx] == currTxPkthdrDescIndex[nicTx->txIdx]);
#endif

    ret = currTxPkthdrDescIndex[nicTx->txIdx];
#if RTL_FIX_TX_INDEX_AFTER_OWNERSHIP
    /* Defer index advance until after giving descriptor to hardware */
#else
    currTxPkthdrDescIndex[nicTx->txIdx] = next_index;
#endif

	/* Ensure all descriptor writes complete before giving to hardware */
	wmb();
    /* Give descriptor to switch core */
    txPkthdrRing[nicTx->txIdx][ret] |= DESC_SWCORE_OWNED;
    /* Ensure ownership change visible to hardware */
    wmb();

#if RTL_FIX_TX_INDEX_AFTER_OWNERSHIP
    /* Advance producer index only after descriptor ownership is transferred */
    currTxPkthdrDescIndex[nicTx->txIdx] = next_index;
#endif

	/* Security fix: Release TX ring lock after index update */
	spin_unlock_irqrestore(&rtl_tx_ring_lock, flags);

    /* Trigger TX fetch with a pulse while preserving configuration bits.
     * Do NOT write a raw '= TXFD' as CPUICR contains enable/config flags
     * (TXCMD/RXCMD/BURST/MBUF/EXCLUDE_CRC) which must be preserved.
     */
#if RTL_FIX_TX_KICK_ONCE
    if (was_empty) {
        unsigned long icr_snapshot = REG32(CPUICR);
        /* Set TXFD edge */
        REG32(CPUICR) = icr_snapshot | TXFD;
        wmb();
        (void)REG32(CPUICR);    /* read-back */
        /* Clear TXFD back to original config */
        REG32(CPUICR) = icr_snapshot;
        mb();
        (void)REG32(CPUICR);    /* read-back */
    }
#else
    {
        unsigned long icr_snapshot = REG32(CPUICR);
        REG32(CPUICR) = icr_snapshot | TXFD;
        wmb();
        (void)REG32(CPUICR);
        REG32(CPUICR) = icr_snapshot;
        mb();
        (void)REG32(CPUICR);
    }
#endif

	return ret;
}

/**
 * swNic_send - Send packet via TX descriptor ring (public API)
 * @skb: Socket buffer (stored in descriptor for later free)
 * @output: Pointer to packet data buffer
 * @len: Packet length in bytes
 * @nicTx: TX info structure (portlist, flags, VLAN ID, ring index)
 *
 * Public wrapper around _swNic_send() with IRQ protection.
 *
 * Return: Descriptor index on success, -1 on error
 */
int32 swNic_send(void *skb, void * output, uint32 len,rtl_nicTx_info *nicTx)
{
	int	ret;
	unsigned long flags;

	local_irq_save(flags);
	ret = _swNic_send(skb, output, len, nicTx);
	local_irq_restore(flags);
	return ret;
}

/**
 * swNic_txRingFreeCount - Get free TX descriptor count
 * @idx: TX ring index
 *
 * Returns the number of available descriptors in the TX ring.
 * Used by the driver for TX flow control decisions (stop/wake queue).
 *
 * Return: Number of free descriptors, or -1 on invalid ring index
 */
int32 swNic_txRingFreeCount(int idx)
{
	int free_count;

	/* Validate TX ring index */
	if (idx >= RTL865X_SWNIC_TXRING_HW_PKTDESC) {
		return -1;
	}

	/* Calculate free descriptors with wrap-around handling
	 * Free space = (done_idx - curr_idx - 1) mod ring_size
	 * We reserve 1 descriptor to distinguish full from empty.
	 */
	if (txPktDoneDescIndex[idx] > currTxPkthdrDescIndex[idx]) {
		free_count = txPktDoneDescIndex[idx] - currTxPkthdrDescIndex[idx] - 1;
	} else if (txPktDoneDescIndex[idx] < currTxPkthdrDescIndex[idx]) {
		free_count = txPkthdrRingCnt[idx] - currTxPkthdrDescIndex[idx] + txPktDoneDescIndex[idx] - 1;
	} else {
		/* Indexes equal: ring is empty */
		free_count = txPkthdrRingCnt[idx] - 1;
	}

	return free_count;
}

/**
 * swNic_txDone - Free completed TX descriptors
 * @idx: TX ring index
 *
 * Simple wrapper around swNic_txDone_stats() without BQL tracking.
 *
 * Return: Number of packets freed
 */
int32 swNic_txDone(int idx)
{
	return swNic_txDone_stats(idx, NULL, NULL);
}

/**
 * swNic_txDone_stats - Free completed TX descriptors with BQL stats
 * @idx: TX ring index
 * @pkts_out: Optional pointer to accumulate completed packet count
 * @bytes_out: Optional pointer to accumulate completed byte count
 *
 * Iterates through TX ring, checks ownership bits (with cache invalidation),
 * frees completed SKBs, and advances done index. Includes DMA cache
 * invalidation fix for MIPS to prevent packet duplication.
 *
 * Return: Number of packets freed
 */
int32 swNic_txDone_stats(int idx, unsigned int *pkts_out, unsigned int *bytes_out)
{
	struct rtl_pktHdr	*pPkthdr;
	struct sk_buff *skb;
	unsigned int pkts = 0, bytes = 0;
	unsigned long flags;

	/* Validate TX ring index */
	if (idx >= RTL865X_SWNIC_TXRING_HW_PKTDESC) {
		return 0;
	}

	local_irq_save(flags);
	{
		while (txPktDoneDescIndex[idx] != currTxPkthdrDescIndex[idx]) {

		/* Bounds check on descriptor index */
		if (txPktDoneDescIndex[idx] >= txPkthdrRingCnt[idx]) {
			break;
		}

		/* CRITICAL: Invalidate cache to read hardware-written ownership bit
		 *
		 * On MIPS non-coherent systems, hardware writes the ownership bit
		 * to RAM when TX completes. Without cache invalidation, CPU may read
		 * stale cached value (DESC_SWCORE_OWNED) instead of the updated RAM
		 * value (DESC_RISC_OWNED), causing premature descriptor reuse and
		 * PACKET DUPLICATION.
		 *
		 * This fixes 49% TCP "retransmissions" which are actually duplicated
		 * packets sent because driver reused descriptors before HW finished.
		 */
		dma_cache_inv((unsigned long)&txPkthdrRing[idx][txPktDoneDescIndex[idx]], sizeof(uint32));

		/* Ensure we read latest descriptor state from hardware */
		rmb();

		if ( (*(volatile uint32 *)&txPkthdrRing[idx][txPktDoneDescIndex[idx]]
			& DESC_OWNED_BIT) == DESC_RISC_OWNED )
		{

			pPkthdr = (struct rtl_pktHdr *) ((int32) txPkthdrRing[idx][txPktDoneDescIndex[idx]]
				& ~(DESC_OWNED_BIT | DESC_WRAP));

			/* NULL check on hardware-provided pointers */
			if (!pPkthdr || !pPkthdr->ph_mbuf) {
				break;
			}

			/* Invalidate DMA cache for descriptor structures (like RX path does)
			 * Must read hardware-written fields from RAM, not stale cache values.
			 */
			dma_cache_inv((unsigned long)pPkthdr, sizeof(struct rtl_pktHdr));
			dma_cache_inv((unsigned long)(pPkthdr->ph_mbuf), sizeof(struct rtl_mBuf));

			skb = (struct sk_buff *)pPkthdr->ph_mbuf->skb;
			if (skb)
			{
				/* Capture stats before freeing SKB */
				if (pkts_out && bytes_out) {
					pkts++;
					bytes += skb->len;
				}

				local_irq_restore(flags);
				dev_kfree_skb_any(skb);
				local_irq_save(flags);
				pPkthdr->ph_mbuf->skb = NULL;
			}


			if (++txPktDoneDescIndex[idx] == txPkthdrRingCnt[idx])
				txPktDoneDescIndex[idx] = 0;
		}
		else
			break;
		}
	}

	local_irq_restore(flags);

	/* Return accumulated stats if requested */
	if (pkts_out) *pkts_out = pkts;
	if (bytes_out) *bytes_out = bytes;

	return pkts;
}

void swNic_freeRxBuf(void)
{
	int i;
	//int idx;
	//struct rtl_pktHdr * pPkthdr;

	/* Initialize index of Tx pkthdr descriptor */
	for (i=0;i<RTL865X_SWNIC_TXRING_HW_PKTDESC;i++)
	{
		currTxPkthdrDescIndex[i] = 0;
		txPktDoneDescIndex[i]=0;
	}

	for(i=RTL865X_SWNIC_RXRING_HW_PKTDESC-1; i >= 0 ; i--)
	{
		/* Initialize index of current Rx pkthdr descriptor */
		currRxPkthdrDescIndex[i] = 0;
		/* Initialize index of current Rx Mbuf descriptor */
		currRxMbufDescIndex = 0;
		rxDescReadyForHwIndex[i] = 0;
		rxDescCrossBoundFlag[i] = 0;
	}
	if (rxMbufRing) {
		struct rtl_mBuf *pMbuf;

		for (i=0;i<rxMbufRingCnt;i++)
		{
			pMbuf = (struct rtl_mBuf *)(rxMbufRing[i] & ~(DESC_OWNED_BIT | DESC_WRAP));

			if (pMbuf->skb)
			{
				free_rx_buf(pMbuf->skb);
				pMbuf->skb = NULL;
			}
			if ((rxMbufRing[i] & DESC_WRAP) != 0)
				break;
		}	
	}
}

int swNic_refillRxRing(void)
{
	unsigned long flags;
	unsigned int i;
	void *skb;
	unsigned char *buf;
	int refilled_any = 0;
	int ring_refilled[RTL865X_SWNIC_RXRING_MAX_PKTDESC] = {0};

	local_irq_save(flags);
	for(i =  0; i <RTL865X_SWNIC_RXRING_MAX_PKTDESC; i++)
	{
		while (return_to_rxing_check(i)) {
			skb=NULL;
			buf = alloc_rx_buf(&skb, size_of_cluster);

			if ((buf == NULL) ||(skb==NULL) ) {
				/*
				 * CRITICAL FIX: Don't abort completely on first failure.
				 *
				 * Previous behavior (BROKEN):
				 * - If ring 0 gets last buffer, ring 1 fails → return -1
				 * - Rings 2-5 never even attempted!
				 * - Result: Massive packet loss under stress
				 *
				 * New behavior:
				 * - Continue trying all rings
				 * - Refill what we can with available buffers
				 * - Return partial success
				 */
				break;  /* Move to next ring instead of aborting */
			}

			release_pkthdr(skb, i);
			ring_refilled[i]++;
			refilled_any = 1;
		}
		REG32(CPUIISR) = (MBUF_DESC_RUNOUT_IP_ALL|PKTHDR_DESC_RUNOUT_IP_ALL);
	}

	local_irq_restore(flags);

	/* Return success if we refilled ANY ring, even if not all */
	return (refilled_any ? 0 : -1);
}

void swNic_freeTxRing(void)
{
	struct rtl_pktHdr	*pPkthdr;
	uint32 idx;
	unsigned long flags;
	local_irq_save(flags);

	/* Initialize index of Tx pkthdr descriptor */
	for (idx=0;idx<RTL865X_SWNIC_TXRING_HW_PKTDESC;idx++)
	{
			while (txPktDoneDescIndex[idx] != currTxPkthdrDescIndex[idx]) {
			pPkthdr = (struct rtl_pktHdr *) ((int32) txPkthdrRing[idx][txPktDoneDescIndex[idx]]
				& ~(DESC_OWNED_BIT | DESC_WRAP));
			if (pPkthdr->ph_mbuf->skb)
			{
				dev_kfree_skb_any((struct sk_buff *)pPkthdr->ph_mbuf->skb);
				pPkthdr->ph_mbuf->skb = NULL;
			}
			txPkthdrRing[idx][txPktDoneDescIndex[idx]] &= ~DESC_SWCORE_OWNED;

			if (++txPktDoneDescIndex[idx] == txPkthdrRingCnt[idx])
				txPktDoneDescIndex[idx] = 0;
			}
	}

	local_irq_restore(flags);
	return ; //free_num;
}

int32 swNic_reConfigRxTxRing(void)
{
	uint32 i,j,k;
	//struct rtl_pktHdr	*pPkthdr;
	unsigned long flags;

	local_irq_save(flags);

	k = 0;

	for (i = 0; i < RTL865X_SWNIC_RXRING_HW_PKTDESC; i++)
	{
		for (j = 0; j < rxPkthdrRingCnt[i]; j++)
		{
			/* Setup descriptors */
			rxPkthdrRing[i][j] = rxPkthdrRing[i][j] | DESC_SWCORE_OWNED;
			rxMbufRing[k] = rxMbufRing[k]  | DESC_SWCORE_OWNED;
			k++;
		}

		/* Initialize index of current Rx pkthdr descriptor */
		currRxPkthdrDescIndex[i] = 0;

		/* Initialize index of current Rx Mbuf descriptor */
		currRxMbufDescIndex = 0;

		/* Set wrap bit of the last descriptor */
		if(rxPkthdrRingCnt[i] > 0)
			rxPkthdrRing[i][rxPkthdrRingCnt[i] - 1] |= DESC_WRAP;

		rxDescReadyForHwIndex[i] = 0;
		rxDescCrossBoundFlag[i] = 0;
	}

	rxMbufRing[rxMbufRingCnt - 1] |= DESC_WRAP;


	for (i=0;i<RTL865X_SWNIC_TXRING_HW_PKTDESC;i++)
	{
		currTxPkthdrDescIndex[i] = 0;
		txPktDoneDescIndex[i]=0;
	}

	/* Fill Tx packet header FDP */
	REG32(CPUTPDCR0) = (uint32) txPkthdrRing[0];
	REG32(CPUTPDCR1) = (uint32) txPkthdrRing[1];
	
	REG32(CPUTPDCR2) = (uint32) txPkthdrRing[2];
	REG32(CPUTPDCR3) = (uint32) txPkthdrRing[3];


	/* Fill Rx packet header FDP */
	REG32(CPURPDCR0) = (uint32) rxPkthdrRing[0];
	REG32(CPURPDCR1) = (uint32) rxPkthdrRing[1];
	REG32(CPURPDCR2) = (uint32) rxPkthdrRing[2];
	REG32(CPURPDCR3) = (uint32) rxPkthdrRing[3];
	REG32(CPURPDCR4) = (uint32) rxPkthdrRing[4];
	REG32(CPURPDCR5) = (uint32) rxPkthdrRing[5];

	REG32(CPURMDCR0) = (uint32) rxMbufRing;

	local_irq_restore(flags);

	return 0;
}

/**
 * swNic_reInit - Reinitialize descriptor rings after reset
 *
 * Called after switch core reset to restore ring state.
 * Frees pending TX packets, refills RX buffers, and reconfigures
 * hardware descriptor registers.
 *
 * Return: SUCCESS
 */
int32 swNic_reInit(void)
{
	swNic_freeTxRing();
	swNic_refillRxRing();
	swNic_reConfigRxTxRing();
	return SUCCESS;
}

/**
 * swNic_init - Initialize TX/RX descriptor rings
 * @userNeedRxPkthdrRingCnt: Array of RX ring sizes (6 rings)
 * @userNeedRxMbufRingCnt: Total number of RX mbuf descriptors
 * @userNeedTxPkthdrRingCnt: Array of TX ring sizes (4 rings)
 * @clusterSize: Size of RX buffer cluster (typically 1536 bytes)
 *
 * Allocates and initializes all descriptor rings and data structures:
 * - RX packet header rings (6 rings for QoS prioritization)
 * - TX packet header rings (4 rings)
 * - RX mbuf ring (shared across all RX rings)
 * - Packet header and mbuf structures
 * - Initial RX buffers
 *
 * Configures hardware registers with ring base addresses.
 *
 * Return: SUCCESS on success, error code on allocation failure
 */
int32 swNic_init(uint32 userNeedRxPkthdrRingCnt[RTL865X_SWNIC_RXRING_HW_PKTDESC],
                 uint32 userNeedRxMbufRingCnt,
                 uint32 userNeedTxPkthdrRingCnt[RTL865X_SWNIC_TXRING_HW_PKTDESC],
                 uint32 clusterSize)
{
	uint32 i, j, k;
	static uint32 totalRxPkthdrRingCnt = 0, totalTxPkthdrRingCnt = 0;
	static struct rtl_pktHdr *pPkthdrList_start;
	static struct rtl_mBuf *pMbufList_start;
	struct rtl_pktHdr *pPkthdrList;
	struct rtl_mBuf *pMbufList;
	struct rtl_pktHdr * pPkthdr;
	struct rtl_mBuf * pMbuf;
	//unsigned long flags;
	int	ret;

	/* init const array for rx pre-process	*/
	extPortMaskToPortNum[0] = 5;
	extPortMaskToPortNum[1] = 6;
	extPortMaskToPortNum[2] = 7;
	extPortMaskToPortNum[3] = 5;
	extPortMaskToPortNum[4] = 8;
	extPortMaskToPortNum[5] = 5;
	extPortMaskToPortNum[6] = 5;
	extPortMaskToPortNum[7] = 5;

	rxPkthdrRefillThreshold[0] = ETH_REFILL_THRESHOLD;
	rxPkthdrRefillThreshold[1] = ETH_REFILL_THRESHOLD1;
	rxPkthdrRefillThreshold[2] = ETH_REFILL_THRESHOLD2;
	rxPkthdrRefillThreshold[3] = ETH_REFILL_THRESHOLD3;
	rxPkthdrRefillThreshold[4] = ETH_REFILL_THRESHOLD4;
	rxPkthdrRefillThreshold[5] = ETH_REFILL_THRESHOLD5;


	//rtlglue_printf("\n#######################################################\n");
	//rtlglue_printf("  NUM_RX_PKTHDR_DESC= %d, eth_skb_free_num= %d\n",
	//	NUM_RX_PKTHDR_DESC, get_buf_in_poll());
	//rtlglue_printf("#######################################################\n");

	ret = SUCCESS;
	//local_irq_save(flags);
	if (rxMbufRing == NULL)
	{
		size_of_cluster = clusterSize;

		/* Allocate Rx descriptors of rings */
		for (i = 0; i < RTL865X_SWNIC_RXRING_HW_PKTDESC; i++) {
			rxPkthdrRingCnt[i] = userNeedRxPkthdrRingCnt[i];
			if (rxPkthdrRingCnt[i] == 0)
			{
				rxPkthdrRing[i] = NULL;
				continue;
			}

			rxPkthdrRing[i] = (uint32 *) UNCACHED_MALLOC(rxPkthdrRingCnt[i] * sizeof(uint32*));

			/* Check allocation success (Issue #8) */
			if (!rxPkthdrRing[i]) {
				printk(KERN_ERR "rtl819x_swnic: Failed to allocate RX ring %d\n", i);
				ret = -ENOMEM;
				goto cleanup_partial_init;
			}

			ASSERT_CSP( (uint32) rxPkthdrRing[i] & 0x0fffffff );

			totalRxPkthdrRingCnt += rxPkthdrRingCnt[i];
		}

		if (totalRxPkthdrRingCnt == 0) {
			ret = EINVAL;
			goto out;
		}

		/* Allocate Tx descriptors of rings */
		for (i = 0; i < RTL865X_SWNIC_TXRING_HW_PKTDESC; i++) {
			txPkthdrRingCnt[i] = userNeedTxPkthdrRingCnt[i];

			if (txPkthdrRingCnt[i] == 0)
			{
				txPkthdrRing[i] = NULL;
				continue;
			}

			txPkthdrRing[i] = (uint32 *) UNCACHED_MALLOC(txPkthdrRingCnt[i] * sizeof(uint32*));

			/* Check allocation success (Issue #8) */
			if (!txPkthdrRing[i]) {
				printk(KERN_ERR "rtl819x_swnic: Failed to allocate TX ring %d\n", i);
				ret = -ENOMEM;
				goto cleanup_partial_init;
			}

			ASSERT_CSP( (uint32) txPkthdrRing[i] & 0x0fffffff );

			totalTxPkthdrRingCnt += txPkthdrRingCnt[i];
		}

		if (totalTxPkthdrRingCnt == 0) {
			ret = EINVAL;
			goto out;
		}

		/* Allocate MBuf descriptors of rings */
		rxMbufRingCnt = userNeedRxMbufRingCnt;

		if (userNeedRxMbufRingCnt == 0) {
			ret = EINVAL;
			goto out;
		}

		rxMbufRing = (uint32 *) UNCACHED_MALLOC((rxMbufRingCnt+RESERVERD_MBUF_RING_NUM) * sizeof(uint32*));

		/* Check allocation success (Issue #8) */
		if (!rxMbufRing) {
			printk(KERN_ERR "rtl819x_swnic: Failed to allocate rxMbufRing\n");
			ret = -ENOMEM;
			goto cleanup_partial_init;
		}

		ASSERT_CSP( (uint32) rxMbufRing & 0x0fffffff );

		/* Allocate pkthdr */
		pPkthdrList_start = (struct rtl_pktHdr *) kmalloc(
		(totalRxPkthdrRingCnt+totalTxPkthdrRingCnt+1) * sizeof(struct rtl_pktHdr), GFP_ATOMIC);

		/* Check allocation success (Issue #8) */
		if (!pPkthdrList_start) {
			printk(KERN_ERR "rtl819x_swnic: Failed to allocate pPkthdrList\n");
			ret = -ENOMEM;
			goto cleanup_partial_init;
		}

		ASSERT_CSP( (uint32) pPkthdrList_start & 0x0fffffff );

		pPkthdrList_start = (struct rtl_pktHdr *)(((uint32) pPkthdrList_start + (L1_CACHE_BYTES - 1))& ~(L1_CACHE_BYTES - 1));

		/* Allocate mbufs */
		pMbufList_start = (struct rtl_mBuf *) kmalloc(
		(rxMbufRingCnt+RESERVERD_MBUF_RING_NUM+totalTxPkthdrRingCnt+1) * sizeof(struct rtl_mBuf), GFP_ATOMIC);

		/* Check allocation success (Issue #8) */
		if (!pMbufList_start) {
			printk(KERN_ERR "rtl819x_swnic: Failed to allocate pMbufList\n");
			ret = -ENOMEM;
			/* Note: pPkthdrList_start already allocated, will be freed in cleanup */
			goto cleanup_partial_init;
		}

		ASSERT_CSP( (uint32) pMbufList_start & 0x0fffffff );

		pMbufList_start = (struct rtl_mBuf *)(((uint32) pMbufList_start + (L1_CACHE_BYTES - 1))& ~(L1_CACHE_BYTES - 1));
	}

	/* Initialize interrupt statistics counter */
	//rxPktCounter = txPktCounter = 0;

	/* Initialize index of Tx pkthdr descriptor */
	for (i=0;i<RTL865X_SWNIC_TXRING_HW_PKTDESC;i++)
	{
		currTxPkthdrDescIndex[i] = 0;
		txPktDoneDescIndex[i]=0;
	}

	pPkthdrList = pPkthdrList_start;
	pMbufList = pMbufList_start;

	/* Initialize Tx packet header descriptors */
	for (i = 0; i < RTL865X_SWNIC_TXRING_HW_PKTDESC; i++)
	{
		for (j = 0; j < txPkthdrRingCnt[i]; j++)
		{
			/* Dequeue pkthdr and mbuf */
			pPkthdr = pPkthdrList++;
			pMbuf = pMbufList++;

			bzero((void *) pPkthdr, sizeof(struct rtl_pktHdr));
			bzero((void *) pMbuf, sizeof(struct rtl_mBuf));

			pPkthdr->ph_mbuf = pMbuf;
			pPkthdr->ph_len = 0;
			pPkthdr->ph_flags = PKTHDR_USED | PKT_OUTGOING;
			pPkthdr->ph_type = PKTHDR_ETHERNET;
			pPkthdr->ph_portlist = 0;

			pMbuf->m_next = NULL;
			pMbuf->m_pkthdr = pPkthdr;
			pMbuf->m_flags = MBUF_USED | MBUF_EXT | MBUF_PKTHDR | MBUF_EOR;
			pMbuf->m_data = NULL;
			pMbuf->m_extbuf = NULL;
			pMbuf->m_extsize = 0;

			txPkthdrRing[i][j] = (int32) pPkthdr | DESC_RISC_OWNED;
		}


		if(txPkthdrRingCnt[i] > 0)
		{
			/* Set wrap bit of the last descriptor */
			txPkthdrRing[i][txPkthdrRingCnt[i] - 1] |= DESC_WRAP;
		}

	}

	/* Fill Tx packet header FDP */
	REG32(CPUTPDCR0) = (uint32) txPkthdrRing[0];
	REG32(CPUTPDCR1) = (uint32) txPkthdrRing[1];

	REG32(CPUTPDCR2) = (uint32) txPkthdrRing[2];
	REG32(CPUTPDCR3) = (uint32) txPkthdrRing[3];

	/* Initialize Rx packet header descriptors */
	k = 0;

	for (i = 0; i < RTL865X_SWNIC_RXRING_HW_PKTDESC; i++)
	{

		for (j = 0; j < rxPkthdrRingCnt[i]; j++)
		{
			/* Dequeue pkthdr and mbuf */
			pPkthdr = pPkthdrList++;
			pMbuf = pMbufList++;

			bzero((void *) pPkthdr, sizeof(struct rtl_pktHdr));
			bzero((void *) pMbuf, sizeof(struct rtl_mBuf));

			/* Setup pkthdr and mbuf */
			pPkthdr->ph_mbuf = pMbuf;
			pPkthdr->ph_len = 0;
			pPkthdr->ph_flags = PKTHDR_USED | PKT_INCOMING;
			pPkthdr->ph_type = PKTHDR_ETHERNET;
			pPkthdr->ph_portlist = 0;
			pMbuf->m_next = NULL;
			pMbuf->m_pkthdr = pPkthdr;
			pMbuf->m_len = 0;
			pMbuf->m_flags = MBUF_USED | MBUF_EXT | MBUF_PKTHDR | MBUF_EOR;
			pMbuf->m_extsize = size_of_cluster;

			pMbuf->m_data = pMbuf->m_extbuf = alloc_rx_buf(&pPkthdr->ph_mbuf->skb, size_of_cluster);
			
			if (pMbuf->m_data == NULL) { 
				rxPkthdrRingCnt[i] = j;
				rxMbufRingCnt = k;
				break;
			}
			else 
			{
				/* Setup descriptors */
				rxPkthdrRing[i][j] = (int32) pPkthdr | DESC_SWCORE_OWNED;
				rxMbufRing[k++] = (int32) pMbuf | DESC_SWCORE_OWNED;
			}
		}


		/* Initialize index of current Rx pkthdr descriptor */
		currRxPkthdrDescIndex[i] = 0;

		/* Initialize index of current Rx Mbuf descriptor */
		currRxMbufDescIndex = 0;

		/* Set wrap bit of the last descriptor */
		if(rxPkthdrRingCnt[i] > 0)
			rxPkthdrRing[i][rxPkthdrRingCnt[i] - 1] |= DESC_WRAP;

		rxDescReadyForHwIndex[i] = 0;
		rxDescCrossBoundFlag[i] = 0;

	}

	rxMbufRing[rxMbufRingCnt - 1] |= DESC_WRAP;

	/* Fill Rx packet header FDP */
	REG32(CPURPDCR0) = (uint32) rxPkthdrRing[0];
	REG32(CPURPDCR1) = (uint32) rxPkthdrRing[1];
	REG32(CPURPDCR2) = (uint32) rxPkthdrRing[2];
	REG32(CPURPDCR3) = (uint32) rxPkthdrRing[3];
	REG32(CPURPDCR4) = (uint32) rxPkthdrRing[4];
	REG32(CPURPDCR5) = (uint32) rxPkthdrRing[5];
	REG32(CPURMDCR0) = (uint32) rxMbufRing;

	goto out;

cleanup_partial_init:
	/* Cleanup partially allocated resources (Issue #8) */
	printk(KERN_ERR "rtl819x_swnic: Cleaning up partial initialization\n");

	/* Free RX descriptor rings */
	for (i = 0; i < RTL865X_SWNIC_RXRING_HW_PKTDESC; i++) {
		if (rxPkthdrRing[i]) {
			kfree(rxPkthdrRing[i]);
			rxPkthdrRing[i] = NULL;
		}
	}

	/* Free TX descriptor rings */
	for (i = 0; i < RTL865X_SWNIC_TXRING_HW_PKTDESC; i++) {
		if (txPkthdrRing[i]) {
			kfree(txPkthdrRing[i]);
			txPkthdrRing[i] = NULL;
		}
	}

	/* Free mbuf ring */
	if (rxMbufRing) {
		kfree(rxMbufRing);
		rxMbufRing = NULL;
	}

	/* Free pkthdr list */
	if (pPkthdrList_start) {
		kfree(pPkthdrList_start);
		pPkthdrList_start = NULL;
	}

	/* Free mbuf list */
	if (pMbufList_start) {
		kfree(pMbufList_start);
		pMbufList_start = NULL;
	}

out:

	if (ret == SUCCESS && pPkthdrList_start && pMbufList_start) {
		dma_cache_wback_inv((unsigned long)pPkthdrList_start, (totalRxPkthdrRingCnt + totalTxPkthdrRingCnt) * sizeof(struct rtl_pktHdr));
		dma_cache_wback_inv((unsigned long)pMbufList_start, (rxMbufRingCnt+RESERVERD_MBUF_RING_NUM+ totalTxPkthdrRingCnt) * sizeof(struct rtl_mBuf));
	}

	//local_irq_restore(flags);
	return ret;
}

int32 rtl_check_tx_done_desc_swCore_own(int32 *tx_done_inx)
{
	int ret = FAILED;
	int tx_ring_idx = 0;   //default use tx ring0

	if ((*(volatile uint32 *)&txPkthdrRing[tx_ring_idx][txPktDoneDescIndex[tx_ring_idx]]&DESC_OWNED_BIT) == DESC_SWCORE_OWNED) {
		*tx_done_inx = txPktDoneDescIndex[tx_ring_idx];
		ret = SUCCESS;
	}

	return ret;
}
