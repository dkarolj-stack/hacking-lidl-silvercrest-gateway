/*
 * RTL8196E Ethernet NIC Driver
 * ============================================================================
 *
 * Original Code:
 *   Copyright (c) 2003-2011 Realtek Semiconductor Corporation
 *   All rights reserved.
 *   Author: bo_zhao <bo_zhao@realtek.com>
 *
 * Rewritten for Linux 5.10:
 *   Copyright (c) 2025 Jacques Nilo
 *   Based on Realtek SDK 2.6.30 driver
 *
 * Abstract:
 *   Pure L2 NIC driver for RTL8196E SoC. Simplified rewrite of the original
 *   Realtek multi-platform driver, supporting only RTL8196E with basic L2
 *   switching (no L3/L4 hardware offload). Features NAPI polling and
 *   Device Tree integration for Linux 5.10 compatibility.
 *
 * Key changes from original 2.6.30 driver:
 *   - Single-chip support (RTL8196E only)
 *   - L2 switching only (removed NAT/routing HW acceleration)
 *   - NAPI polling (replaced tasklet RX/TX)
 *   - Device Tree integration (platform_driver)
 *   - 64-bit statistics, ethtool support, BQL
 *   - Security fixes (spinlocks, atomic ops, input validation)
 *
 * SPDX-License-Identifier: GPL-2.0
 * ============================================================================
 */

#define DRV_VERSION     "2.1.0"
#define DRV_RELDATE     "Feb 05, 2026"
#define DRV_NAME        "rtl819x"
#define DRV_DESCRIPTION "RTL8196E Ethernet Driver (L2)"
#define DRV_AUTHOR      "Jacques Nilo"

/* TX flow control thresholds (ring 0 has 600 descriptors) */
#define RTL_NIC_TX_STOP_THRESHOLD  16  /* Stop queue when < 16 free */
#define RTL_NIC_TX_WAKE_THRESHOLD  64  /* Wake queue when > 64 free */

#include <linux/if_vlan.h>
#include <linux/proc_fs.h>
#include <linux/ethtool.h>
#include <linux/kernel.h>  /* for vprintk() and va_list */
#include <asm/io.h>  /* for dma_cache_wback_inv() - Kernel 5.4: moved from cacheflush.h to io.h */
#include <linux/platform_device.h>  /* DT integration: platform_driver support */
#include <linux/of.h>               /* DT integration: device tree parsing */
#include <linux/of_platform.h>      /* DT integration: of_match_table */
#include "rtl819x.h"
#include "AsicDriver/rtl865x_asicCom.h"
#include "AsicDriver/rtl865x_asicL2.h"
#include "rtl865xc_swNic.h"
#include "common/rtl865x_vlan.h"  /* For VLAN functions */
#include "common/rtl865x_eventMgr.h"  /* For event manager */

/* RTL865xC ASIC regs: local-only defines (trimmed from rtl865xc_asicregs.h) */
#define MAX_PORT_NUMBER 6

#define RTL_R32(addr) (*(volatile unsigned long *)(addr))

#define MIB_ADDROFFSETBYPORT 0x80 /* Address offset of the same counters of each port. Ex: P0's ifInOctets counter and P1's ifInOctets counter. */

#define OFFSET_IFINOCTETS_P0 0x100

#define OFFSET_IFINUCASTPKTS_P0 0x108

#define OFFSET_ETHERSTATSJABBERS_P0 0x138

#define OFFSET_ETHERSTATSMULTICASTPKTS_P0 0x13C

#define OFFSET_ETHERSTATSBROADCASTPKTS_P0 0x140

#define OFFSET_DOT1DTPPORTINDISCARDS_P0 0x144

#define OFFSET_DOT3STATSFCSERRORS_P0 0x14C

#define OFFSET_IFOUTOCTETS_P0 0x800

#define OFFSET_IFOUTUCASTPKTS_P0 0x808

#define OFFSET_IFOUTMULTICASTPKTS_P0 0x80C

#define OFFSET_IFOUTBROADCASTPKTS_P0 0x810

#define OFFSET_IFOUTDISCARDS 0x814

#define OFFSET_DOT3STATSDEFERREDTRANSMISSIONS_P0 0x820

#define OFFSET_ETHERSTATSCOLLISIONS_P0 0x834

#define CPUQDM0 (0x030 + CPU_IFACE_BASE)	   /* Queue ID 0 and Descriptor Ring Mapping Register */

#define CPUQDM2 (0x034 + CPU_IFACE_BASE)	   /* Queue ID 2 and Descriptor Ring Mapping Register */

#define CPUQDM4 (0x038 + CPU_IFACE_BASE)	   /* Queue ID 4 and Descriptor Ring Mapping Register */

#define LINK_CHANGE_IP (1 << 31) /* Link change interrupt pending */

#define RX_DONE_IP_ALL (0x3f << 3)		   /* Rx one packet done anyone interrupt pending */

#define TX_ALL_DONE_IP_ALL (0x03 << 1)		   /* Tx all packets done any interrupt pending */

#define SWCORECNR (SWCORE_BASE + 0x00006000)

#define VLANTCR (0x008 + SWCORECNR) /* Vlan tag control */

#define NwayAbility1000MF (1 << 22) /* Auto-Negotiation advertise ability: 1000Mbps Full-duplex*/

#define NwayAbility100MF (1 << 21)	/* Auto-Negotiation advertise ability: 100Mbps Full-duplex */

#define NwayAbility100MH (1 << 20)	/* Auto-Negotiation advertise ability: 100Mbps Half-duplex */

#define PortStatusNWayEnable (1 << 7)	  /* N-Way Enable */

#define PortStatusDuplex (1 << 3)		  /* Duplex */

#define PortStatusLinkSpeed_MASK (3 << 0) /* Link Speed */

#define PortStatusLinkSpeed_OFFSET 0

#define PortStatusLinkSpeed10M (0 << 0)		 /* 10M */

#define PortStatusLinkSpeed100M (1 << 0)	 /* 100M */

#define PortStatusLinkSpeed1000M (2 << 0)	 /* 1000M */

#define SYS_SW_CLK_ENABLE 0x200

#define BOND_8196EU1 (0x0)

#define BOND_8196EU3 (0x4)

#define BOND_8196EU2 (0x8)

#define BOND_8196EU (0xC)


/* Local driver definitions (formerly in include/rtl_nic.h) */
#define MAX_ETH_SKB_NUM ( \
	NUM_RX_PKTHDR_DESC + NUM_RX_PKTHDR_DESC1 + NUM_RX_PKTHDR_DESC2 + NUM_RX_PKTHDR_DESC3 + NUM_RX_PKTHDR_DESC4 + NUM_RX_PKTHDR_DESC5 + MAX_PRE_ALLOC_RX_SKB + 690)
/* Kernel 5.4: NET_SKB_PAD uses max() which is braced-group, can't use in array declaration
 * On MIPS, L1_CACHE_BYTES=32, so NET_SKB_PAD=32. Use constant instead. */
#define ETH_SKB_BUF_SIZE 2048
#define ETH_MAGIC_CODE "819X"
#define ETH_MAGIC_LEN 4

struct re865x_priv
{
	u16 ready;
	u16 addIF;
	u16 devnum;
	u32 sec_count;
	u32 sec;
	struct net_device *dev[ETH_INTF_NUM];
	void *regs;
	struct tasklet_struct rx_tasklet;
	struct timer_list timer; /* Media monitoring timer. */
	unsigned long linkchg;
};

struct dev_priv
{
	u32 id;		  /* VLAN id, not vlan index */
	u32 portmask; /* member port mask */
	u32 portnum;  /* number of member ports */
	u32 netinit;
	struct net_device *dev;
	struct net_device *dev_prev;
	struct net_device *dev_next;

	/* Phase 2: NAPI migration (v3.5.0) */
	struct napi_struct napi;		/* Combined RX+TX NAPI polling */
	struct tasklet_struct link_dsr_tasklet;	/* Link changes (not NAPI) */

	spinlock_t lock;
	u32 msg_enable;
	u32 opened;
	u32 irq_owner; // record which dev request IRQ
	struct net_device_stats net_stats;
	struct timer_list expire_timer;

	/* Phase 2: Descriptor error statistics */
	unsigned long rx_desc_null_errors;      /* NULL descriptor pointers in RX */
	unsigned long rx_mbuf_null_errors;      /* NULL mbuf pointers in RX */
	unsigned long rx_skb_null_errors;       /* NULL skb pointers in RX */
	unsigned long rx_desc_index_errors;     /* RX descriptor index out of bounds */
	unsigned long rx_mbuf_index_errors;     /* RX mbuf index out of bounds */
	unsigned long rx_length_errors;         /* Invalid packet length */
	unsigned long tx_desc_null_errors;      /* NULL descriptor pointers in TX */
	unsigned long tx_mbuf_null_errors;      /* NULL mbuf pointers in TX */
	unsigned long tx_desc_index_errors;     /* TX descriptor index out of bounds */
	unsigned long tx_ring_full_errors;      /* TX ring full events */
	unsigned long ring_recovery_count;      /* Number of ring recoveries */
	unsigned long last_recovery_jiffies;    /* Last recovery timestamp */

	/* Phase 6: Buffer pool monitoring (freeze debugging) */
	unsigned long rx_refill_failures;       /* refill_rx_skb() allocation failures */
	unsigned long rx_pool_empty_events;     /* Times eth_skb_free_num == 0 */
	int last_eth_skb_free_num;             /* Snapshot of buffer pool size */
};

typedef struct __rtlInterruptRxData
{
} rtlInterruptRxData;

#define RTL_RX_PROCESS_RETURN_SUCCESS 0
#define RTL_RX_PROCESS_RETURN_CONTINUE -1
#define RTL_RX_PROCESS_RETURN_BREAK -2

#define RTL819X_IOCTL_READ_PORT_STATUS (SIOCDEVPRIVATE + 0x01)
#define RTL819X_IOCTL_READ_PORT_STATS (SIOCDEVPRIVATE + 0x02)

struct lan_port_status
{
	unsigned char link;
	unsigned char speed;
	unsigned char duplex;
	unsigned char nway;
};

struct port_statistics
{
	unsigned int rx_bytes;
	unsigned int rx_unipkts;
	unsigned int rx_mulpkts;
	unsigned int rx_bropkts;
	unsigned int rx_discard;
	unsigned int rx_error;
	unsigned int tx_bytes;
	unsigned int tx_unipkts;
	unsigned int tx_mulpkts;
	unsigned int tx_bropkts;
	unsigned int tx_discard;
	unsigned int tx_error;
};

#define RTL_PS_BR0_DEV_NAME RTL_BR_NAME
#define RTL_PS_ETH_NAME "eth"
#define RTL_PS_WLAN_NAME RTL_WLAN_NAME
#define RTL_PS_PPP_NAME "ppp"
#define RTL_PS_LAN_P0_DEV_NAME RTL_DEV_NAME_NUM(RTL_PS_ETH_NAME, 0)
#define RTL_PS_WAN0_DEV_NAME RTL_DEV_NAME_NUM(RTL_PS_ETH_NAME, 1)
#define RTL_PS_PPP0_DEV_NAME RTL_DEV_NAME_NUM(RTL_PS_PPP_NAME, 0)
#define RTL_PS_PPP1_DEV_NAME RTL_DEV_NAME_NUM(RTL_PS_PPP_NAME, 1)
#define RTL_PS_WLAN0_DEV_NAME RTL_DEV_NAME_NUM(RTL_PS_WLAN_NAME, 0)
#define RTL_PS_WLAN1_DEV_NAME RTL_DEV_NAME_NUM(RTL_PS_WLAN_NAME, 1)
#define QOS_LAN_DEV_NAME RTL_PS_BR0_DEV_NAME

struct rtl865x_vlanConfig
{
	uint8 ifname[IFNAMSIZ];
	uint8 isWan;
	uint16 if_type;
	uint16 vid;
	uint16 fid;
	uint32 memPort;
	uint32 untagSet;
	uint32 mtu;
	ether_addr_t mac;
	uint8 is_slave;
};
#define RTL865X_CONFIG_END {"", 0, 0, 0, 0, 0, 0, 0, {{0}}, 0}

#define CONFIG_CHECK(expr)                                                        \
	do                                                                            \
	{                                                                             \
		if (((int32)expr) != SUCCESS)                                             \
		{                                                                         \
			rtlglue_printf("Error >>> %s:%d failed !\n", __FUNCTION__, __LINE__); \
			return FAILED;                                                        \
		}                                                                         \
	} while (0)

#define INIT_CHECK(expr)                                                          \
	do                                                                            \
	{                                                                             \
		if (((int32)expr) != SUCCESS)                                             \
		{                                                                         \
			rtlglue_printf("Error >>> %s:%d failed !\n", __FUNCTION__, __LINE__); \
			return FAILED;                                                        \
		}                                                                         \
	} while (0)

typedef struct _ps_drv_netif_mapping_s
{
	uint32 valid : 1,			  // entry enable?
		flags;					  // reserverd
	struct net_device *ps_netif;  // linux ps network interface
	char drvName[MAX_IFNAMESIZE]; // netif name in driver

} ps_drv_netif_mapping_t;

/* Phase 2: External error statistics accessor from swNic.c */
extern void rtl_swnic_get_error_stats(unsigned long *stats);

/* Optional TX experiments (disabled by default) */
#ifndef RTL_FORCE_DIRECT_TX
#define RTL_FORCE_DIRECT_TX 0   /* 1 = always use direct portlist (no HW lookup) */
#endif

/* Kernel 5.4: panic_printk was removed, provide wrapper for pre-compiled blob (rtl865x_asicBasic.o) */
int panic_printk(const char *fmt, ...)
{
	va_list args;
	int r;

	va_start(args, fmt);
	r = vprintk(fmt, args);
	va_end(args);

	return r;
}
EXPORT_SYMBOL(panic_printk);

/* Kernel 5.4 migration: Stub for optional features */
static inline void rtl865x_LinkChange_Process(void) { /* Stub: link change processing disabled */ }
static inline void rtl865x_config_callback_for_get_drv_netifName(void *cb) { /* Stub: callback disabled */ }

/**
 * rtl_skb_dma_cache_wback_inv - Flush DMA cache for TX SKB
 * @skb: Socket buffer to flush
 *
 * Flushes the SKB data cache before DMA transmission to ensure hardware
 * sees the correct data.
 *
 * NOTE: Scatter-gather NOT supported by this driver
 * ====================================================
 * This driver does NOT set NETIF_F_SG in dev->features, so the Linux
 * network stack automatically linearizes all SKBs before passing them
 * to us. This means nr_frags is ALWAYS 0, and we only need to flush
 * the linear data portion.
 *
 * If scatter-gather support is added in future:
 * 1. Set dev->features |= NETIF_F_SG during device initialization
 * 2. Add fragment flushing loop here (see git history for reference)
 * 3. Test with large packets: iperf -c <host> -l 9000
 *
 * See SCATTER_GATHER_ANALYSIS.md for full details.
 *
 * Kernel 5.4 migration: Use dma_cache_wback_inv from asm/io.h
 */
void rtl_skb_dma_cache_wback_inv(struct sk_buff *skb)
{
	if (unlikely(!skb))
		return;

	/* Flush linear data portion (this is all we ever receive) */
	if (skb->len > 0 && skb->data) {
		dma_cache_wback_inv((unsigned long)skb->data, skb_headlen(skb));
	}
}

static unsigned int curLinkPortMask = 0;
static unsigned int newLinkPortMask = 0;

#define SET_MODULE_OWNER(dev) \
	do                        \
	{                         \
	} while (0)

#define DEBUG_ERR(format, args...)

static int32 __865X_Config;

#define STATE_NO_ERROR 0
#define STATE_SW_CLK_ENABLE_WAITING 1
#define STATE_TO_REINIT_SWITCH_CORE 2
int rtl865x_duringReInitSwtichCore = 0;
int rtl865x_reInitState = STATE_NO_ERROR;
int rtl865x_reInitWaitCnt = 0;
static int32 rtl_last_tx_done_idx = 0;
static int32 rtl_swCore_tx_hang_cnt = 0;
static uint32 rtl_check_swCore_timer = 0;
static int32 rtl_check_swCore_tx_hang_interval = 5;
static int32 rtl_reinit_swCore_threshold = 3;
static int32 rtl_reinit_swCore_counter = 0;
void rtl_check_swCore_tx_hang(void);
extern int32 rtl_check_tx_done_desc_swCore_own(int32 *tx_done_inx);

static void one_sec_timer(struct timer_list *t);

static struct sk_buff *dev_alloc_skb_priv_eth(unsigned int size);
static void init_priv_eth_skb_buf(void);

/* Switch core management functions */
int rtl865x_reinitSwitchCore(void);

static struct ring_que rx_skb_queue;
int skb_num = 0;

int32 rtl865x_init(void);
int32 rtl865x_config(struct rtl865x_vlanConfig vlanconfig[]);

/* These identify the driver base version and may not be removed. */
MODULE_DESCRIPTION("RealTek RTL-8650 series 10/100 Ethernet driver");
MODULE_LICENSE("GPL");

/* Use module_param for kernel 2.6 (MODULE_PARM is obsolete) */
static char *multicast_filter_limit = "maximum number of filtered multicast addresses";
module_param(multicast_filter_limit, charp, S_IRUGO);

#define PFX DRV_NAME ": "
#define TX_TIMEOUT (10 * HZ)
#define BDINFO_ADDR 0xbe3fc000

/* Shutdown flag to break NAPI poll loops during device close
 * Security fix (2025-11-21): Changed from volatile int to atomic_t for proper
 * multi-core synchronization (volatile doesn't guarantee memory ordering)
 */
static atomic_t rtl_driver_shutting_down = ATOMIC_INIT(0);

/* Phase 2: Interrupt statistics (used by NAPI ISR)
 * Note: These are statistics only, not critical for correctness.
 * Races are acceptable here (overhead of atomic operations not justified).
 */
int cnt_swcore = 0;
int cnt_swcore_tx = 0;
int cnt_swcore_rx = 0;
int cnt_swcore_link = 0;
int cnt_swcore_err = 0;

/* Link change interrupt handling (still uses tasklet, not NAPI) */
static inline void rtl_link_change_interrupt_process(unsigned int status, struct dev_priv *cp);

/* Security fix (2025-11-21): Spinlock to protect CPUIIMR register access
 * This register is accessed from ISR and user context - needs protection
 */
static DEFINE_SPINLOCK(rtl_iimr_lock);
static int rtl_rxTxDoneCnt = 0;
static atomic_t rtl_devOpened;

/**
 * rtl_rxSetTxDone - Enable/disable TX done interrupt
 * @enable: TRUE to enable, FALSE to disable
 *
 * Security fix (2025-11-21): Added spinlock protection for race condition
 * CRITICAL FIX: This function is called from ISR (interrupt_isr_napi) and
 * potentially other contexts. Without spinlock protection:
 * - rtl_rxTxDoneCnt can be corrupted (non-atomic increment/decrement)
 * - REG32(CPUIIMR) read-modify-write can race, losing interrupt mask updates
 *
 * Also fixed: Use atomic_read() instead of direct .counter access (unsafe)
 */
void rtl_rxSetTxDone(int enable)
{
	unsigned long flags;
	u32 iimr;

	/* Security fix: Use atomic_read() instead of .counter (unsafe) */
	if (unlikely(atomic_read(&rtl_devOpened) == 0))
		return;

	/* Security fix: Spinlock protects counter + CPUIIMR register R-M-W */
	spin_lock_irqsave(&rtl_iimr_lock, flags);

	if (FALSE == enable) {
		rtl_rxTxDoneCnt--;
		if (rtl_rxTxDoneCnt == -1) {
			iimr = REG32(CPUIIMR);
			iimr &= ~(TX_ALL_DONE_IE_ALL);
			REG32(CPUIIMR) = iimr;
		}
	} else {
		rtl_rxTxDoneCnt++;
		if (rtl_rxTxDoneCnt == 0) {
			iimr = REG32(CPUIIMR);
			iimr |= (TX_ALL_DONE_IE_ALL);
			REG32(CPUIIMR) = iimr;
		}
	}

	spin_unlock_irqrestore(&rtl_iimr_lock, flags);
}

#define NEXT_DEV(cp) (cp->dev_next ? cp->dev_next : cp->dev_prev)
#define NEXT_CP(cp) ((struct dev_priv *)((NEXT_DEV(cp))->priv))
#define IS_FIRST_DEV(cp) (NEXT_CP(cp)->opened ? 0 : 1)
#define GET_IRQ_OWNER(cp) (cp->irq_owner ? cp->dev : NEXT_DEV(cp))

#define MAX_PORT_NUM 9

static unsigned int rxRingSize[RTL865X_SWNIC_RXRING_HW_PKTDESC] =
	{NUM_RX_PKTHDR_DESC,
	 NUM_RX_PKTHDR_DESC1,
	 NUM_RX_PKTHDR_DESC2,
	 NUM_RX_PKTHDR_DESC3,
	 NUM_RX_PKTHDR_DESC4,
	 NUM_RX_PKTHDR_DESC5};

static unsigned int txRingSize[RTL865X_SWNIC_TXRING_HW_PKTDESC] =
	{NUM_TX_PKTHDR_DESC,
	 NUM_TX_PKTHDR_DESC1,
	 NUM_TX_PKTHDR_DESC2,
	 NUM_TX_PKTHDR_DESC3};

/*
linux protocol stack netif VS rtl819x driver network interface
the name of ps netif maybe different with driver.
*/
static ps_drv_netif_mapping_t ps_drv_netif_mapping[NETIF_NUMBER];

static struct rtl865x_vlanConfig vlanconfig[] = {
	/*      	ifName  W/L      If type		VID	 FID	   Member Port	UntagSet		mtu		MAC Addr	is_slave								*/
	/*		=====  ===   =======	===	 ===   =========   =======	====	====================================	*/

	/* Single physical port (port 4) = single interface eth0 */
	{"eth0", 0, IF_ETHER, 1, 0, 0x10, 0x10, 1500, {{0x02, 0x14, 0xB8, 0xEE, 0xB7, 0x54}}, 0},
	RTL865X_CONFIG_END,
};

/*	The following structure's field orders was arranged for special purpose,
	it should NOT be modify	*/
struct priv_skb_buf2
{
	unsigned char magic[ETH_MAGIC_LEN];
	void *buf_pointer;
	/* the below 2 filed MUST together */
	struct list_head list;
	unsigned char buf[ETH_SKB_BUF_SIZE];
};

static struct priv_skb_buf2 eth_skb_buf[MAX_ETH_SKB_NUM + 1];
static struct list_head eth_skbbuf_list;
int eth_skb_free_num;
EXPORT_SYMBOL(eth_skb_free_num);

/* Forward declarations */
int is_rtl865x_eth_priv_buf(unsigned char *head);
void free_rtl865x_eth_priv_buf(unsigned char *head);

/* External cache for sk_buff struct allocation - defined in net/core/skbuff.c */
extern struct kmem_cache *skbuff_head_cache;

/**
 * dev_alloc_8190_skb - Create SKB using pre-allocated buffer from private pool
 * @data: Pre-allocated buffer from eth_skb_buf pool
 * @size: Size of buffer
 *
 * Kernel 5.4 migration: Manual SKB allocation like 2.6.30, NOT build_skb().
 * build_skb() calls ksize() on the buffer which doesn't work for private pools,
 * causing "Bad page state" errors. We must allocate sk_buff struct separately
 * and manually initialize all fields, just like 2.6.30 did.
 *
 * Returns: SKB on success, NULL on failure
 */
static inline struct sk_buff *dev_alloc_8190_skb(unsigned char *data, int size)
{
	struct sk_buff *skb;
	struct skb_shared_info *shinfo;

	if (!data)
		return NULL;

	/* Get the HEAD - EXACT copy from 2.6.30 */
	skb = kmem_cache_alloc(skbuff_head_cache, GFP_ATOMIC & ~__GFP_DMA);
	if (!skb)
		return NULL;

	/* EXACT copy from 2.6.30 line 293 - zero up to truesize
	 * In 5.4, truesize is AFTER tail/end/head/data, so this zeros them too */
	memset(skb, 0, offsetof(struct sk_buff, truesize));

	/* EXACT copy from 2.6.30 lines 294-301 */
	refcount_set(&skb->users, 1);  /* was atomic_set in 2.6.30 */
	skb->head = data;
	skb->data = data;
	skb->tail = data;  /* Direct assignment, NOT skb_reset_tail_pointer! */

	size = SKB_DATA_ALIGN(size + 128 + NET_SKB_PAD);  /* RTL_PRIV_DATA_SIZE = 128 */

	skb->end = data + size;  /* Direct assignment, NOT skb->tail + size! */
	skb->truesize = size + sizeof(struct sk_buff);

	/* EXACT copy from 2.6.30 lines 305-312 - initialize shinfo */
	shinfo = skb_shinfo(skb);
	atomic_set(&shinfo->dataref, 1);
	shinfo->nr_frags = 0;
	shinfo->gso_size = 0;
	shinfo->gso_segs = 0;
	shinfo->gso_type = 0;
	shinfo->frag_list = NULL;

	/* Kernel 5.4 addition: mark buffer NOT from page_frag */
	skb->head_frag = 0;

	/* NO destructor needed - kernel patch in skb_free_head() handles pool return */

	/* EXACT copy from 2.6.30 line 354 */
	skb_reserve(skb, 128);  /* RTL_PRIV_DATA_SIZE */

	return skb;
}

struct sk_buff *priv_skb_copy(struct sk_buff *skb);

/* Operation mode fixed to GATEWAY_MODE (LAN/WAN routing) - no dynamic mode switching */

static struct re865x_priv _rtl86xx_dev;

static int re865x_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);

static inline int rtl_isWanDev(struct dev_priv *cp);


struct rtl865x_vlanConfig *rtl_get_vlanconfig_by_netif_name(const char *name)
{
	int i;
	for (i = 0; vlanconfig[i].vid != 0; i++)
	{
		if (memcmp(vlanconfig[i].ifname, name, strlen(name)) == 0)
			return &vlanconfig[i];
	}

	return NULL;
}

static int rtl_ps_drv_netif_mapping_init(void)
{
	memset(ps_drv_netif_mapping, 0, NETIF_NUMBER * sizeof(ps_drv_netif_mapping_t));
	return SUCCESS;
}

int rtl_get_ps_drv_netif_mapping_by_psdev_name(const char *psname, char *netifName)
{
	int i;
	if (netifName == NULL || strlen(psname) >= MAX_IFNAMESIZE)
		return FAILED;

	for (i = 0; i < NETIF_NUMBER; i++)
	{
		if (ps_drv_netif_mapping[i].valid == 1 && memcmp(ps_drv_netif_mapping[i].ps_netif->name, psname, strlen(psname)) == 0)
		{
			memcpy(netifName, ps_drv_netif_mapping[i].drvName, MAX_IFNAMESIZE);
			return SUCCESS;
		}
	}

	// back compatible,user use br0 to get lan netif
	if (memcmp(psname, RTL_PS_BR0_DEV_NAME, strlen(RTL_PS_BR0_DEV_NAME)) == 0)
	{
		for (i = 0; i < NETIF_NUMBER; i++)
		{
			if (ps_drv_netif_mapping[i].valid == 1 &&
				memcmp(ps_drv_netif_mapping[i].drvName, RTL_DRV_LAN_NETIF_NAME, strlen(RTL_DRV_LAN_NETIF_NAME)) == 0)
			{
				memcpy(netifName, ps_drv_netif_mapping[i].drvName, MAX_IFNAMESIZE);
				return SUCCESS;
			}
		}
	}

	return FAILED;
}

ps_drv_netif_mapping_t *rtl_get_ps_drv_netif_mapping_by_psdev(struct net_device *dev)
{
	int i;
	for (i = 0; i < NETIF_NUMBER; i++)
	{
		if (ps_drv_netif_mapping[i].valid == 1 && ps_drv_netif_mapping[i].ps_netif == dev)
			return &ps_drv_netif_mapping[i];
	}

	// back compatible,user use br0 to get lan netif
	if (memcmp(dev->name, RTL_PS_BR0_DEV_NAME, strlen(RTL_PS_BR0_DEV_NAME)) == 0)
	{
		for (i = 0; i < NETIF_NUMBER; i++)
		{
			if (ps_drv_netif_mapping[i].valid == 1 &&
				memcmp(ps_drv_netif_mapping[i].drvName, RTL_DRV_LAN_NETIF_NAME, strlen(RTL_DRV_LAN_NETIF_NAME)) == 0)
				return &ps_drv_netif_mapping[i];
		}
	}

	return NULL;
}

int rtl_add_ps_drv_netif_mapping(struct net_device *dev, const char *name)
{
	int i;
	size_t name_len;

	// duplicate check
	if (rtl_get_ps_drv_netif_mapping_by_psdev(dev) != NULL)
		return FAILED;

	for (i = 0; i < NETIF_NUMBER; i++)
	{
		if (ps_drv_netif_mapping[i].valid == 0)
			break;
	}

	if (i == NETIF_NUMBER)
		return FAILED;

	/*
	 * Security fix (2025-11-21): Validate string length before memcpy
	 * to prevent buffer overflow in drvName[MAX_IFNAMESIZE].
	 */
	name_len = strnlen(name, MAX_IFNAMESIZE);
	if (unlikely(name_len >= MAX_IFNAMESIZE))
		return FAILED;  /* Name too long */

	ps_drv_netif_mapping[i].ps_netif = dev;
	memcpy(ps_drv_netif_mapping[i].drvName, name, name_len);
	ps_drv_netif_mapping[i].drvName[name_len] = '\0';  /* Ensure null termination */
	ps_drv_netif_mapping[i].valid = 1;
	return SUCCESS;
}


/*
 * Disable TX/RX through IO_CMD register
 */
static void rtl8186_stop_hw(struct net_device *dev, struct dev_priv *cp)
{

}

/* Set or clear the multicast filter for this adaptor.
 * This routine is not state sensitive and need not be SMP locked.
 */
static void re865x_set_rx_mode(struct net_device *dev)
{
	/*	Not yet implemented.
		unsigned long flags;
		spin_lock_irqsave (&_rtl86xx_dev.lock, flags);
		spin_unlock_irqrestore (&_rtl86xx_dev.lock, flags);
	*/
}

/**
 * re865x_get_stats64 - Get 64-bit network statistics (Linux 5.4)
 * @dev: Network device
 * @stats: Pointer to 64-bit statistics structure
 *
 * Provides 64-bit statistics to prevent counter overflow on long-running systems.
 * Replaces the legacy 32-bit ndo_get_stats interface.
 */
static void re865x_get_stats64(struct net_device *dev,
			       struct rtnl_link_stats64 *stats)
{
	struct dev_priv *dp = netdev_priv(dev);
	struct net_device_stats *ns = &dp->net_stats;

	/* Copy standard statistics to 64-bit structure */
	stats->rx_packets = ns->rx_packets;
	stats->tx_packets = ns->tx_packets;
	stats->rx_bytes = ns->rx_bytes;
	stats->tx_bytes = ns->tx_bytes;
	stats->rx_errors = ns->rx_errors;
	stats->tx_errors = ns->tx_errors;
	stats->rx_dropped = ns->rx_dropped;
	stats->tx_dropped = ns->tx_dropped;
	stats->multicast = ns->multicast;
	stats->collisions = ns->collisions;

	/* Detailed RX error statistics */
	stats->rx_length_errors = ns->rx_length_errors;
	stats->rx_over_errors = ns->rx_over_errors;
	stats->rx_crc_errors = ns->rx_crc_errors;
	stats->rx_frame_errors = ns->rx_frame_errors;
	stats->rx_fifo_errors = ns->rx_fifo_errors;
	stats->rx_missed_errors = ns->rx_missed_errors;

	/* Detailed TX error statistics */
	stats->tx_aborted_errors = ns->tx_aborted_errors;
	stats->tx_carrier_errors = ns->tx_carrier_errors;
	stats->tx_fifo_errors = ns->tx_fifo_errors;
	stats->tx_heartbeat_errors = ns->tx_heartbeat_errors;
	stats->tx_window_errors = ns->tx_window_errors;
}

static void rtl865x_disableDevPortForward(struct net_device *dev, struct dev_priv *cp)
{
	int port;
	for (port = 0; port < RTL8651_AGGREGATOR_NUMBER; port++)
	{
		if ((1 << port) & cp->portmask)
		{
			REG32(PCRP0 + (port << 2)) = ((REG32(PCRP0 + (port << 2))) & (~ForceLink));
			REG32(PCRP0 + (port << 2)) = ((REG32(PCRP0 + (port << 2))) & (~EnablePHYIf));
			TOGGLE_BIT_IN_REG_TWICE(PCRP0 + (port << 2), EnablePHYIf);
			TOGGLE_BIT_IN_REG_TWICE(PCRP0 + (port << 2), ForceLink);
			TOGGLE_BIT_IN_REG_TWICE(PCRP0 + (port << 2), EnForceMode);
		}
	}
}

static void rtl865x_restartDevPHYNway(struct net_device *dev, struct dev_priv *cp)
{
	int port;
	for (port = 0; port < RTL8651_AGGREGATOR_NUMBER; port++)
	{
		if ((1 << port) & cp->portmask)
		{
			rtl8651_restartAsicEthernetPHYNway(port);
		}
	}
	return;
}

static void rtl865x_enableDevPortForward(struct net_device *dev, struct dev_priv *cp)
{
	int port;
	for (port = 0; port < RTL8651_AGGREGATOR_NUMBER; port++)
	{
		if ((1 << port) & cp->portmask)
		{
			REG32(PCRP0 + (port << 2)) = ((REG32(PCRP0 + (port << 2))) | (ForceLink));
			REG32(PCRP0 + (port << 2)) = ((REG32(PCRP0 + (port << 2))) | (EnablePHYIf));
			TOGGLE_BIT_IN_REG_TWICE(PCRP0 + (port << 2), EnablePHYIf);
			TOGGLE_BIT_IN_REG_TWICE(PCRP0 + (port << 2), ForceLink);
		}
	}
}

static void rtl865x_disableInterrupt(void)
{
	REG32(CPUICR) = 0;
	REG32(CPUIIMR) = 0;
	REG32(CPUIISR) = REG32(CPUIISR);
}

static void rtk_queue_init(struct ring_que *que)
{
	memset(que, 0, sizeof(struct ring_que));
	que->ring = (struct sk_buff **)kmalloc(
		(sizeof(struct skb_buff *) * (rtl865x_maxPreAllocRxSkb + 1)), GFP_ATOMIC);
	memset(que->ring, 0, (sizeof(struct sk_buff *)) * (rtl865x_maxPreAllocRxSkb + 1));
	que->qmax = rtl865x_maxPreAllocRxSkb;
}

static void rtk_queue_exit(struct ring_que *que)
{

	if (que->ring != NULL)
	{
		kfree(que->ring);
		que->ring = NULL;
	}
}

static int rtk_queue_tail(struct ring_que *que, struct sk_buff *skb)
{
	int next;

	if (que->head == que->qmax)
		next = 0;
	else
		next = que->head + 1;

	if (que->qlen >= que->qmax || next == que->tail)
	{
		return 0;
	}

	que->ring[que->head] = skb;
	que->head = next;
	que->qlen++;

	return 1;
}

static struct sk_buff *rtk_dequeue(struct ring_que *que)
{
	struct sk_buff *skb;

	if (que->qlen <= 0 || que->tail == que->head)
	{
		return NULL;
	}

	skb = que->ring[que->tail];

	if (que->tail == que->qmax)
		que->tail = 0;
	else
		que->tail++;

	que->qlen--;

	return (struct sk_buff *)skb;
}

static void refill_rx_skb(void)
{
	struct sk_buff *skb;
	unsigned long flags;
	int idx;
	struct dev_priv *cp;
	static int refill_fail_count = 0;
	int consecutive_failures = 0;

	local_irq_save(flags);
	idx = RTL865X_SWNIC_RXRING_MAX_PKTDESC - 1;

	while (rx_skb_queue.qlen < rtl865x_maxPreAllocRxSkb)
	{
		skb = dev_alloc_skb_priv_eth(CROSS_LAN_MBUF_LEN);

		if (skb == NULL)
		{
			/* Phase 6: Track buffer pool exhaustion */
			refill_fail_count++;
			consecutive_failures++;

			/*
			 * Update stats on ALL opened devices since buffer pool is shared.
			 *
			 * Previously only updated dev[0] (eth0), causing ethtool -S eth1
			 * to show 0 even when console messages appeared.
			 */
			{
				int i;
				for (i = 0; i < ETH_INTF_NUM; i++) {
					if (_rtl86xx_dev.dev[i] && netdev_priv(_rtl86xx_dev.dev[i])) {
						cp = (struct dev_priv *)netdev_priv(_rtl86xx_dev.dev[i]);
						if (cp->opened) {
							cp->rx_refill_failures++;
							cp->last_eth_skb_free_num = eth_skb_free_num;

							if (eth_skb_free_num == 0) {
								cp->rx_pool_empty_events++;
							}
						}
					}
				}
			}

			/*
			 * Intelligent logging to avoid spam during high load:
			 *
			 * Under sustained 95+ Mbps UDP traffic, buffer allocation failures
			 * are expected due to MIPS 400MHz CPU limitations, not driver bugs.
			 *
			 * Log strategy:
			 * - First 10 failures: Always log (helps debug startup issues)
			 * - 11-1000 failures: Log every 100th
			 * - 1000+ failures: Log every 1000th
			 * - Only log if pool critically low (< 10 buffers free)
			 *
			 * Users can monitor via: ethtool -S eth0 | grep rx_refill_failures
			 */
			if (eth_skb_free_num < 10) {
				if (refill_fail_count <= 10 ||
				    (refill_fail_count <= 1000 && refill_fail_count % 100 == 0) ||
				    (refill_fail_count % 1000 == 0)) {
					printk(KERN_WARNING "rtl819x: RX refill failed! Pool: %d free, Queue: %d/%d (failure #%d)\n",
					       eth_skb_free_num, rx_skb_queue.qlen, rtl865x_maxPreAllocRxSkb, refill_fail_count);
				}
			}

			/*
			 * CRITICAL FIX: Don't give up after first failure.
			 *
			 * Previous behavior (BROKEN):
			 * - First alloc fails → return immediately
			 * - Never retry, even if pool recovers
			 * - Under stress: pool constantly low, refill never happens
			 *
			 * New behavior:
			 * - Only give up after 3 consecutive failures
			 * - Allows recovery if pool replenished between calls
			 * - Better utilization of available buffers
			 */
			if (consecutive_failures >= 3) {
				local_irq_restore(flags);
				return;
			}
			/* Continue loop, try again */
			continue;
		}

		/* Success - reset failure counter */
		consecutive_failures = 0;

		skb_reserve(skb, RX_OFFSET);


		// local_irq_save(flags);
		rtk_queue_tail(&rx_skb_queue, skb);
	}
	local_irq_restore(flags);
	return;
}

//---------------------------------------------------------------------------
static void free_rx_skb(void)
{
	struct sk_buff *skb;

	swNic_freeRxBuf();

	while (rx_skb_queue.qlen > 0)
	{
		skb = rtk_dequeue(&rx_skb_queue);
		dev_kfree_skb_any(skb);
	}
}

//---------------------------------------------------------------------------
unsigned char *alloc_rx_buf(void **skb, int buflen)
{
	struct sk_buff *new_skb;
	unsigned long flags;

	if (rx_skb_queue.qlen == 0)
	{
		new_skb = dev_alloc_skb_priv_eth(CROSS_LAN_MBUF_LEN);
		if (new_skb == NULL)
		{
			DEBUG_ERR("EthDrv: alloc skb failed!\n");
		}
		else
			skb_reserve(new_skb, RX_OFFSET);
	}
	else
	{
		local_irq_save(flags);
		new_skb = rtk_dequeue(&rx_skb_queue);
		local_irq_restore(flags);
	}

	if (new_skb == NULL)
		return NULL;

	*skb = new_skb;

	return new_skb->data;
}


//---------------------------------------------------------------------------
void free_rx_buf(void *skb)
{
	dev_kfree_skb_any((struct sk_buff *)skb);
}

//---------------------------------------------------------------------------

static inline int32 rtl_isWanDev(struct dev_priv *cp)
{
	return (cp->id == RTL_WANVLANID);
}


static inline int32 rtl_processReceivedInfo(rtl_nicRx_info *info, int nicRxRet)
{
	int ret;

	ret = RTL_RX_PROCESS_RETURN_BREAK;
	switch (nicRxRet)
	{
	case RTL_NICRX_OK:
	{
		ret = RTL_RX_PROCESS_RETURN_SUCCESS;
		break;
	}
	case RTL_NICRX_NULL:
	case RTL_NICRX_REPEAT:
		break;
	}
	return ret;
}

static inline int32 rtl_decideRxDevice(rtl_nicRx_info *info)
{
	struct dev_priv *cp;
	struct sk_buff *skb;

	/* Validate input SKB pointer */
	if (!info || !info->input) {
		if (info && info->input) {
			dev_kfree_skb_any(info->input);
		}
		return FAILED;
	}

	skb = info->input;
	info->isPdev = FALSE;
	info->priv = NULL;

	/* Single interface configuration - trivial decision (no loop needed) */
	cp = ((struct dev_priv *)netdev_priv(_rtl86xx_dev.dev[0]));

	if (cp && cp->opened)
	{
		info->priv = cp;
		return SUCCESS;
	}
	else
	{
		/* Interface not opened or invalid */
		info->priv = NULL;
		dev_kfree_skb_any(skb);
		return FAILED;
	}
}

static inline void rtl_processRxToProtcolStack(struct sk_buff *skb, struct dev_priv *cp_this)
{
	skb->protocol = eth_type_trans(skb, skb->dev);

	// skb->ip_summed = CHECKSUM_NONE;
	/* It must be a TCP or UDP packet with a valid checksum */
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	// printk("[%s][%d]-skb->dev[%s],proto(0x%x)\n", __FUNCTION__, __LINE__, skb->dev->name,skb->protocol);

	netif_receive_skb(skb);
}

static inline void rtl_processRxFrame(rtl_nicRx_info *info)
{
	struct dev_priv *cp_this;
	struct sk_buff *skb;
	uint32 vid, pid, len;
	uint8 *data;

	cp_this = info->priv;
	skb = info->input;
	vid = info->vid;
	data = skb->tail = skb->data;

	if (skb->head == NULL || skb->end == NULL)
	{
		dev_kfree_skb_any(skb);
		return;
	}
	/*	sanity check end 	*/

	pid = info->pid;
	len = info->len;
	skb->len = 0;
	skb_put(skb, len);
	skb->dev = info->priv->dev;

	/*	vlan process (including strip vlan tag)	*/

	/* Validate SKB length before accessing VLAN tag (Issue #11) */
	/* Need at least DA(6) + SA(6) + Type(2) + TCI(2) = 16 bytes */
	if (skb->len >= 16 &&
	    *((uint16 *)(skb->data + (ETH_ALEN << 1))) == __constant_htons(ETH_P_8021Q))
	{
		vid = *((unsigned short *)(data + (ETH_ALEN << 1) + 2));
		vid &= 0x0fff;

		/* VLAN_ETH_ALEN removed in 5.4, use ETH_ALEN (6 bytes MAC) */
	memmove(data + VLAN_HLEN, data, ETH_ALEN << 1);  /* Move src+dst MACs */
		skb_pull(skb, VLAN_HLEN);
	}
	/*	vlan process end (vlan tag has already stripped)	*/

	/*	update statistics	*/
	cp_this->net_stats.rx_packets++;
	cp_this->net_stats.rx_bytes += skb->len;
	/* cp_this->dev->last_rx = jiffies; */  /* Removed in 5.4 - not needed */
	/*	update statistics end	*/


	/*	finally successfuly rx to protocol stack	*/
	rtl_processRxToProtcolStack(skb, cp_this);
}

rtlInterruptRxData RxIntData;

/* ============================================================================
 * Phase 2: NAPI Polling Functions (v3.5.0)
 * ============================================================================ */

/**
 * rtl819x_poll_tx - Process TX completions in NAPI context
 * @cp: Device private structure
 *
 * Lightweight TX completion processing integrated into NAPI poll.
 * Frees completed TX descriptors and wakes queue if needed.
 */
static void rtl819x_poll_tx(struct dev_priv *cp)
{
	struct netdev_queue *txq;
	int32 idx, free_count;
	unsigned int pkts = 0, bytes = 0;

	/* Free completed TX descriptors - ring 0 with BQL stats */
	swNic_txDone_stats(0, &pkts, &bytes);

	/* Free other rings without stats (rarely used) */
	for (idx = RTL865X_SWNIC_TXRING_MAX_PKTDESC - 1; idx >= 1; idx--) {
		swNic_txDone(idx);
	}

	/* BQL: Notify completion to reduce queue limit dynamically */
	if (cp && cp->dev && (pkts > 0)) {
		txq = netdev_get_tx_queue(cp->dev, 0);
		netdev_tx_completed_queue(txq, pkts, bytes);
	}

	/* Check if we should wake the queue (main TX ring is ring 0) */
	free_count = swNic_txRingFreeCount(0);
	if (free_count >= RTL_NIC_TX_WAKE_THRESHOLD) {
		if (cp && cp->dev && netif_queue_stopped(cp->dev)) {
			smp_mb();  /* Ensure descriptor updates visible before waking */
			netif_wake_queue(cp->dev);
		}
	}
}

/**
 * rtl819x_poll - NAPI poll function
 * @napi: NAPI structure
 * @budget: Maximum number of packets to process
 *
 * Main NAPI polling function. Processes RX packets up to budget,
 * handles TX completion, and re-enables interrupts when done.
 *
 * Returns: Number of packets actually processed
 */
static int rtl819x_poll(struct napi_struct *napi, int budget)
{
	struct dev_priv *cp = container_of(napi, struct dev_priv, napi);
	static rtl_nicRx_info info;
	int work_done = 0;
	int ret, count;

	/* Early exit if driver is shutting down
	 * Security fix: Use atomic_read() for proper synchronization
	 */
	if (unlikely(atomic_read(&rtl_driver_shutting_down)))
		return 0;

	/* Process RX packets up to budget */
	while (work_done < budget) {
		/* Break if shutting down */
		if (unlikely(atomic_read(&rtl_driver_shutting_down)))
			break;

		count = 0;
		do {
			ret = swNic_receive(&info, count++);
		} while (ret == RTL_NICRX_REPEAT && !atomic_read(&rtl_driver_shutting_down));

		switch (rtl_processReceivedInfo(&info, ret)) {
		case RTL_RX_PROCESS_RETURN_SUCCESS:
		{
			int decide_ret = rtl_decideRxDevice(&info);

			if (SUCCESS == decide_ret) {
				struct dev_priv *cp_this = info.priv;
				struct sk_buff *skb = info.input;
				uint32 vid, len;
				uint8 *data;

				/* Sanity check */
				if (skb->head == NULL || skb->end == NULL) {
					dev_kfree_skb_any(skb);
					break;
				}

				/* Setup SKB */
				data = skb->tail = skb->data;
				len = info.len;
				skb->len = 0;
				skb_put(skb, len);
				skb->dev = cp_this->dev;

				/* VLAN processing (strip VLAN tag if present) */
				if (skb->len >= 16 &&
				    *((uint16 *)(skb->data + (ETH_ALEN << 1))) == __constant_htons(ETH_P_8021Q)) {
					vid = *((unsigned short *)(data + (ETH_ALEN << 1) + 2));
					vid &= 0x0fff;
					memmove(data + VLAN_HLEN, data, ETH_ALEN << 1);
					skb_pull(skb, VLAN_HLEN);
				}

				/* Update statistics */
				cp_this->net_stats.rx_packets++;
				cp_this->net_stats.rx_bytes += skb->len;

				/* Submit to stack with GRO */
				skb->protocol = eth_type_trans(skb, skb->dev);
				skb->ip_summed = CHECKSUM_UNNECESSARY;
				napi_gro_receive(napi, skb);

				work_done++;
			} else {
				/* CRITICAL: Free skb if no device found! */
				if (info.input) {
					dev_kfree_skb_any(info.input);
				}
			}
			break;
		}
		case RTL_RX_PROCESS_RETURN_BREAK:
			goto poll_done;
		default:
			break;
		}
	}

poll_done:
	/* Process TX completion (lightweight)
	 * Security fix: Use atomic_read() for proper synchronization
	 */
	if (likely(!atomic_read(&rtl_driver_shutting_down)))
		rtl819x_poll_tx(cp);

	/* If we processed less than budget, we're done - re-enable interrupts */
	if (work_done < budget) {
		if (napi_complete_done(napi, work_done)) {
			unsigned long flags;
			local_irq_save(flags);

			/*
			 * CRITICAL: Clear pending runout interrupts before re-enabling.
			 *
			 * Without clearing the interrupt flag here, if descriptors are still low
			 * when we re-enable interrupts, we immediately get another runout interrupt,
			 * creating an interrupt storm (3+ million ERR interrupts, 99%+ packet loss).
			 */
			REG32(CPUIISR) = (PKTHDR_DESC_RUNOUT_IP_ALL | MBUF_DESC_RUNOUT_IP_ALL);

			/* Re-enable RX and TX interrupts */
			rtl_rxSetTxDone(TRUE);
			REG32(CPUIIMR) |= (RX_DONE_IE_ALL | PKTHDR_DESC_RUNOUT_IE_ALL | TX_ALL_DONE_IE_ALL);

			local_irq_restore(flags);
		}
	}

	return work_done;
}

/**
 * interrupt_isr_napi - NAPI interrupt service routine
 * @irq: IRQ number
 * @dev_instance: Network device
 *
 * ISR for NAPI mode. Schedules NAPI poll for RX/TX,
 * keeps using tasklet for link changes (rare event).
 *
 * Returns: IRQ_HANDLED
 */
static irqreturn_t interrupt_isr_napi(int irq, void *dev_instance)
{
	struct net_device *dev = dev_instance;
	struct dev_priv *cp = netdev_priv(dev);
	unsigned int status;

	/* Read and clear interrupt status */
	status = REG32(CPUIISR);
	REG32(CPUIISR) = status;
	status &= REG32(CPUIIMR);

	/* Update global statistics
	 * Optimization: Branch hints for common (RX/TX) vs rare (link/error) events
	 */
	cnt_swcore++;
	if (likely(status & RX_DONE_IP_ALL))
		cnt_swcore_rx++;
	if (likely(status & TX_ALL_DONE_IP_ALL))
		cnt_swcore_tx++;
	if (unlikely(status & LINK_CHANGE_IP))
		cnt_swcore_link++;
	if (unlikely(status & (PKTHDR_DESC_RUNOUT_IP_ALL | MBUF_DESC_RUNOUT_IP_ALL)))
		cnt_swcore_err++;

	/* RX/TX: Schedule NAPI (combined handling)
	 * Optimization: This is the hot path (most interrupts are RX/TX)
	 */
	if (likely(status & (RX_DONE_IP_ALL | TX_ALL_DONE_IP_ALL | PKTHDR_DESC_RUNOUT_IP_ALL))) {
		if (likely(napi_schedule_prep(&cp->napi))) {
			/* Mask RX and TX interrupts */
			REG32(CPUIIMR) &= ~(RX_DONE_IE_ALL | PKTHDR_DESC_RUNOUT_IE_ALL | TX_ALL_DONE_IE_ALL);
			rtl_rxSetTxDone(FALSE);
			__napi_schedule(&cp->napi);
		}
	}

	/* Link changes: Keep using tasklet (rare event, not worth NAPI)
	 * Optimization: Link changes are rare (cable plug/unplug)
	 */
	if (unlikely(status & LINK_CHANGE_IP)) {
		REG32(CPUIIMR) &= ~LINK_CHANGE_IP;
		tasklet_schedule(&cp->link_dsr_tasklet);
	}

	return IRQ_HANDLED;
}

unsigned int rtl865x_getPhysicalPortLinkStatus(void)
{
	unsigned int port_num = 0;
	unsigned int linkPortMask = 0;
	for (port_num = 0; port_num <= RTL8651_PHY_NUMBER; port_num++)
	{
		if ((READ_MEM32(PSRP0 + (port_num << 2)) & PortStatusLinkUp) != 0)
		{
			linkPortMask |= 1 << port_num;
		}
	}
	return linkPortMask;
}

/* Phase 2: cnt_swcore variables moved to top of file (line ~164) */

static unsigned int AutoDownSpeed_10M[MAX_PORT_NUMBER];
static unsigned int DownSpeed_counter[MAX_PORT_NUMBER];
static unsigned int ReverSpeed_flag[MAX_PORT_NUMBER];
static int rtl819xD_checkPhyCbSnr(void)
{

	unsigned int port = 0;
	int curr_sts = 0;
	int link_speed_10M = 0;
	unsigned int val = 0, cb = 0, snr = 0;
	for (port = 0; port < MAX_PORT_NUMBER; port++)
	{
		curr_sts = (REG32(PSRP0 + (port * 4)) & PortStatusLinkUp) >> 4;
		// PortStatusLinkSpeed10M
		link_speed_10M = (REG32(PSRP0 + (port * 4)) & PortStatusLinkSpeed10M);
		if (AutoDownSpeed_10M[port] == 0x12345678)
		{

			DownSpeed_counter[port] = DownSpeed_counter[port] + 1;
			if ((!curr_sts) && (ReverSpeed_flag[port] == 1))
			{
				REG32(PCRP0 + (port << 2)) = ((REG32(PCRP0 + (port << 2))) | ((NwayAbility1000MF | NwayAbility100MF | NwayAbility100MH)));
				DownSpeed_counter[port] = 0;
				AutoDownSpeed_10M[port] = 0;
				ReverSpeed_flag[port] = 0;
				rtl8651_restartAsicEthernetPHYNway(port);
				// printk("ph1\r\n" );
			}
			if ((!curr_sts) && (DownSpeed_counter[port] > 5))
			{
				REG32(PCRP0 + (port << 2)) = ((REG32(PCRP0 + (port << 2))) | ((NwayAbility1000MF | NwayAbility100MF | NwayAbility100MH)));
				DownSpeed_counter[port] = 0;
				AutoDownSpeed_10M[port] = 0;
				ReverSpeed_flag[port] = 0;
				rtl8651_restartAsicEthernetPHYNway(port);
				// printk("ph2\r\n" );
			}
			else if (curr_sts && (DownSpeed_counter[port] < 5))
			{
				// Connect to 10M Successfully
				ReverSpeed_flag[port] = 1;
				// printk("ph3\r\n" );
			}
		}
		else
		{
			AutoDownSpeed_10M[port] = 0;
			DownSpeed_counter[port] = 0;
			ReverSpeed_flag[port] = 0;
		}
		if ((curr_sts == 1) && (!link_speed_10M))
		{
			// Read CB (bit[15]&[7])==1
			rtl8651_setAsicEthernetPHYReg(port, 25, (0x6964));
			rtl8651_getAsicEthernetPHYReg(port, 26, &val);
			rtl8651_setAsicEthernetPHYReg(port, 26, ((val & 0xBF00) | 0x9E)); // Close new_SD.
			rtl8651_getAsicEthernetPHYReg(port, 17, &val);
			rtl8651_setAsicEthernetPHYReg(port, 17, ((val & 0xFFF0) | 0x8)); // CB bit
			rtl8651_getAsicEthernetPHYReg(port, 29, &cb);

			if (((cb & (1 << 15)) >> 15) && ((cb & (1 << 7)) >> 7))
			{

				REG32(PCRP0 + (port << 2)) = ((REG32(PCRP0 + (port << 2))) & (~(NwayAbility1000MF | NwayAbility100MF | NwayAbility100MH)));
				rtl8651_restartAsicEthernetPHYNway(port);
				AutoDownSpeed_10M[port] = 0x12345678;
				DownSpeed_counter[port] = 0;
				ReverSpeed_flag[port] = 0;
				// printk("AN1-->cb\r\n" );
			}
			rtl8651_setAsicEthernetPHYReg(port, 25, (0x6964));
			rtl8651_getAsicEthernetPHYReg(port, 26, &val);
			rtl8651_setAsicEthernetPHYReg(port, 26, ((val & 0xBF00) | 0x9E)); // Close new_SD.
			rtl8651_getAsicEthernetPHYReg(port, 17, &val);
			rtl8651_setAsicEthernetPHYReg(port, 17, ((val & 0xFFF0))); // SNR bit
			rtl8651_getAsicEthernetPHYReg(port, 29, &snr);
			if (snr > 0x4000)
			{
				REG32(PCRP0 + (port << 2)) = ((REG32(PCRP0 + (port << 2))) & (~(NwayAbility1000MF | NwayAbility100MF | NwayAbility100MH)));
				rtl8651_restartAsicEthernetPHYNway(port);
				AutoDownSpeed_10M[port] = 0x12345678;
				DownSpeed_counter[port] = 0;
				ReverSpeed_flag[port] = 0;
				printk("AN2-->snr\r\n");
			}
			// printk("cb:%x snr:%x\r\n",cb,snr );
		}
	}
	return 0;
}


static void interrupt_dsr_link(unsigned long task_priv)
{

	newLinkPortMask = rtl865x_getPhysicalPortLinkStatus();

	rtl865x_LinkChange_Process();

	curLinkPortMask = newLinkPortMask;

	REG32(CPUIIMR) |= (LINK_CHANGE_IP);

	return;
}

static inline void rtl_link_change_interrupt_process(unsigned int status, struct dev_priv *cp)
{
	if (status & LINK_CHANGE_IP)
	{
		REG32(CPUIIMR) &= ~LINK_CHANGE_IP;
		tasklet_schedule(&cp->link_dsr_tasklet);
	}
}

static int rtl865x_init_hw(void)
{
	unsigned int mbufRingSize;
	int i;

	mbufRingSize = rtl865x_rxSkbPktHdrDescNum; /* rx ring 0	*/
	for (i = 1; i < RTL865X_SWNIC_RXRING_HW_PKTDESC; i++)
	{
		mbufRingSize += rxRingSize[i];
	}

	/* Initialize NIC module */
	if (swNic_init(rxRingSize, mbufRingSize, txRingSize, MBUF_LEN))
	{
		printk("865x-nic: swNic_init failed!\n");
		return FAILED;
	}

	return SUCCESS;
}

void refine_phy_setting(void)
{
	int i, start_port = 0;
	uint32 val;

	val = (REG32(BOND_OPTION) & BOND_ID_MASK);
	if ((val == BOND_8196ES) || (val == BOND_8196ES1) || (val == BOND_8196ES2) || (val == BOND_8196ES3))
		return;

	if ((val == BOND_8196EU) || (val == BOND_8196EU1) || (val == BOND_8196EU2) || (val == BOND_8196EU3))
		start_port = 4;

	for (i = start_port; i < 5; i++)
	{
		rtl8651_setAsicEthernetPHYReg(i, 25, 0x6964);
		rtl8651_getAsicEthernetPHYReg(i, 26, &val);
		rtl8651_setAsicEthernetPHYReg(i, 26, ((val & (0xff00)) | 0x9E));

		rtl8651_getAsicEthernetPHYReg(i, 17, &val);
		rtl8651_setAsicEthernetPHYReg(i, 17, ((val & (0xfff0)) | 0x8));

		rtl8651_getAsicEthernetPHYReg(i, 29, &val);
		if ((val & 0x8080) == 0x8080)
		{
			rtl8651_getAsicEthernetPHYReg(i, 21, &val);
			rtl8651_setAsicEthernetPHYReg(i, 21, (val | 0x8000));
			rtl8651_setAsicEthernetPHYReg(i, 21, (val & ~0x8000));
		}
	}
	return;
}

static void one_sec_timer(struct timer_list *t)
{
	unsigned long flags;
	struct dev_priv *cp;
	int i;

	cp = from_timer(cp, t, expire_timer);  /* Kernel 5.4: timer callback signature changed */

	// spin_lock_irqsave (&cp->lock, flags);
	local_irq_save(flags);

	for (i = 0; i < ETH_INTF_NUM; i++)
	{
		struct dev_priv *tmp_cp;

		int portnum, startport = 0;
		if (rtl865x_duringReInitSwtichCore == 1)
			continue;
		tmp_cp = ((struct dev_priv *)netdev_priv(_rtl86xx_dev.dev[i]));
		if (tmp_cp && tmp_cp->portmask && tmp_cp->opened)
		{

			for (portnum = startport; portnum < 5; portnum++)
			{
				if (tmp_cp->portmask & (1 << portnum))
					break;
			}
			if (5 == portnum)
				continue;
			if ((RTL_R32(PCRP0 + portnum * 4) & EnablePHYIf) == 0)
			{
				switch (rtl865x_reInitState)
				{
				case STATE_NO_ERROR:
					if ((REG32(SYS_CLK_MAG) & SYS_SW_CLK_ENABLE) == 0)
					{
						rtl865x_reInitState = STATE_SW_CLK_ENABLE_WAITING;
						rtl865x_reInitWaitCnt = 2;
						REG32(SYS_CLK_MAG) = REG32(SYS_CLK_MAG) | SYS_SW_CLK_ENABLE;
					}
					else
					{
						rtl865x_reinitSwitchCore();
						rtl865x_reInitState = STATE_NO_ERROR;
					}
					break;

				case STATE_SW_CLK_ENABLE_WAITING:
					rtl865x_reInitWaitCnt--;
					if (rtl865x_reInitWaitCnt <= 0)
					{
						rtl865x_reInitWaitCnt = 2;
						rtl865x_reInitState = STATE_TO_REINIT_SWITCH_CORE;
					}
					break;

				case STATE_TO_REINIT_SWITCH_CORE:
					rtl865x_reInitWaitCnt--;
					if (rtl865x_reInitWaitCnt <= 0)
					{
						rtl865x_reinitSwitchCore();
						rtl865x_reInitState = STATE_NO_ERROR;
					}
					break;

				default:
					rtl865x_reinitSwitchCore();
					rtl865x_reInitState = STATE_NO_ERROR;
					break;
				}
				break;
			}
		}
	}
	rtl_check_swCore_tx_hang();

	rtl819xD_checkPhyCbSnr();

	refine_phy_setting();

	mod_timer(&cp->expire_timer, jiffies + HZ);

	// spin_unlock_irqrestore(&cp->lock, flags);
	local_irq_restore(flags);
}

static struct net_device *irqDev = NULL;

/**
 * re865x_open - Open network interface
 * @dev: Network device structure
 *
 * Called when the interface is brought up (ifconfig ethX up).
 * Initializes hardware descriptor rings, allocates RX buffers,
 * sets up NAPI polling and registers IRQ handler.
 *
 * Return: SUCCESS on success, error code on failure
 */
static int re865x_open(struct net_device *dev)
{
	struct dev_priv *cp;
	unsigned long flags;
	int rc;

	cp = netdev_priv(dev);
	if (cp->opened)
		return SUCCESS;

	/* Security fix: Reset shutdown flag on device open
	 * atomic_set() provides memory barriers, explicit wmb() kept for clarity
	 */
	atomic_set(&rtl_driver_shutting_down, 0);
	wmb();  /* Ensure visibility to all CPUs */

	local_irq_save(flags);

	/*	The first device be opened	*/
	if (atomic_read(&rtl_devOpened) == 0)
	{
		/* this is the first open dev */
		/* should not call rtl865x_down() here */
		/* rtl865x_down();*/
		// spin_lock_irqsave(&cp->lock, flags);
		// local_irq_save(flags);
		rtk_queue_init(&rx_skb_queue);
		// local_irq_restore(flags);  //modify to improve interrupt lantency
		rc = rtl865x_init_hw();

		atomic_inc(&rtl_devOpened);
		refill_rx_skb();
		// spin_unlock_irqrestore(&cp->lock, flags);
		if (rc)
		{
			// printk("rtl865x_init_hw() failed!\n");
			atomic_dec(&rtl_devOpened);
			local_irq_restore(flags);
			return FAILED;
		}

		/* Phase 2: Initialize NAPI (v3.5.0) */
		netif_napi_add(dev, &cp->napi, rtl819x_poll, 64);  /* budget = 64 packets */
		napi_enable(&cp->napi);

		/* Link tasklet still needed (link changes are rare, not worth NAPI) */
		tasklet_init(&cp->link_dsr_tasklet, interrupt_dsr_link, (unsigned long)cp);

		/* Phase 2: Use NAPI ISR instead of tasklet ISR */
		rc = request_irq(dev->irq, interrupt_isr_napi, IRQF_SHARED, dev->name, dev);
		if (rc)
		{
			printk("request_irq() error!\n");
			atomic_dec(&rtl_devOpened);
			goto err_out_hw;
		}
		irqDev = dev;
		// cp->irq_owner =1;
		rtl865x_start();
	}
	else
	{
		atomic_inc(&rtl_devOpened);
	}
	cp->opened = 1;

	netif_start_queue(dev);

	if (dev->name[3] == '0')
	{
		timer_setup(&cp->expire_timer, one_sec_timer, 0);  /* Kernel 5.4: init_timer → timer_setup */
		cp->expire_timer.expires = jiffies + HZ;
		mod_timer(&cp->expire_timer, jiffies + HZ);
	}

	rtl865x_enableDevPortForward(dev, cp);
	local_irq_restore(flags);

	return SUCCESS;

err_out_hw:
	rtl8186_stop_hw(dev, cp);
	rtl865x_down();
	local_irq_restore(flags);
	return rc;
}

/**
 * re865x_close - Close network interface
 * @dev: Network device structure
 *
 * Called when the interface is brought down (ifconfig ethX down).
 * Disables interrupts, stops NAPI, frees IRQ, releases RX buffers,
 * and resets interface statistics.
 *
 * Return: SUCCESS on success
 */
static int re865x_close(struct net_device *dev)
{
	struct dev_priv *cp;
	unsigned long flags;

	cp = netdev_priv(dev);
	//	cp = netdev_priv(dev);
	if (!cp->opened)
		return SUCCESS;

	local_irq_save(flags);
	netif_stop_queue(dev);
	/* The last opened device	*/
	if (atomic_read(&rtl_devOpened) == 1)
	{
		/*	warning:
			1.if we don't reboot,we shouldn't hold switch core from rx/tx, otherwise there will be some problem during change operation mode
			2.only when two devices go down,can we shut down nic interrupt
			3.the interrupt will be re_enable by rtl865x_start()
		*/
		rtl865x_disableInterrupt();

		// free_irq(dev->irq, GET_IRQ_OWNER(cp));
		//((struct dev_priv *)((GET_IRQ_OWNER(cp))->priv))->irq_owner = 0;
		free_irq(dev->irq, irqDev);
		//((struct dev_priv *)(irqDev->priv))->irq_owner = 0;

		/* Security fix: Signal NAPI/tasklets to exit their loops before stopping them
		 * atomic_set() provides memory barriers, explicit wmb() kept for clarity
		 */
		atomic_set(&rtl_driver_shutting_down, 1);
		/* Memory barrier to ensure flag is visible to all CPUs */
		wmb();

		/* Phase 2: Disable NAPI (waits for poll to finish) */
		napi_disable(&cp->napi);
		netif_napi_del(&cp->napi);

		/* Link tasklet still exists */
		tasklet_kill(&cp->link_dsr_tasklet);

		atomic_dec(&rtl_devOpened);
		free_rx_skb();

		rtk_queue_exit(&rx_skb_queue);
	}

	memset(&cp->net_stats, '\0', sizeof(struct net_device_stats));
	if (atomic_read(&rtl_devOpened) > 0)
		atomic_dec(&rtl_devOpened);
	cp->opened = 0;

	{
		rtl865x_disableDevPortForward(dev, cp);
		/*for lan dhcp client to renew ip address*/
		rtl865x_restartDevPHYNway(dev, cp);
		rtl8186_stop_hw(dev, cp);
	}

	if (timer_pending(&cp->expire_timer))
		del_timer_sync(&cp->expire_timer);



	local_irq_restore(flags);

	return SUCCESS;
}

static inline int rtl_pstProcess_xmit(struct dev_priv *cp, int len)
{
	cp->net_stats.tx_packets++;
	cp->net_stats.tx_bytes += len;
	/* cp->dev->trans_start = jiffies; */  /* Removed in 5.4 */
	return SUCCESS;
}

static inline int rtl_preProcess_xmit(rtl_nicTx_info *txInfo)
{
	if (rtl865x_duringReInitSwtichCore == 1)
	{
		dev_kfree_skb_any(txInfo->out_skb);
		return FAILED;
	}

	return SUCCESS;
}

static inline void rtl_direct_txInfo(uint32 port_mask, rtl_nicTx_info *txInfo)
{
	/* Validate and mask port_mask to valid physical ports (Issue #12) */
	/* RTL8196E has 6 ports: 5 physical (0-4) + 1 CPU port (5) = mask 0x3f */
	uint32 valid_mask = port_mask & 0x3f;

	/* Ensure non-zero port mask (at least one port must be set) */
	if (unlikely(valid_mask == 0)) {
		valid_mask = 0x3f;  /* Fallback to all ports */
	}

	txInfo->portlist = valid_mask;
	txInfo->srcExtPort = 0; // PKTHDR_EXTPORT_LIST_CPU;
	txInfo->flags = (PKTHDR_USED | PKT_OUTGOING);
}

static inline void rtl_hwLookup_txInfo(rtl_nicTx_info *txInfo)
{
	txInfo->portlist = RTL8651_CPU_PORT; /* must be set 0x7 */
	txInfo->srcExtPort = PKTHDR_EXTPORT_LIST_CPU;
	txInfo->flags = (PKTHDR_USED | PKTHDR_HWLOOKUP | PKTHDR_BRIDGING | PKT_OUTGOING);
}

static inline int rtl_ip_option_check(struct sk_buff *skb)
{
	int flag = FALSE;

	/* Validate SKB length before accessing headers (Issue #11) */
	if (skb->len < ETH_HLEN)
		return FALSE;

	if (((*((unsigned short *)(skb->data + ETH_ALEN * 2)) == __constant_htons(ETH_P_IP)) && ((*((unsigned char *)(skb->data + ETH_ALEN * 2 + 2)) != __constant_htons(0x45)))) ||
		((skb->len >= 16) &&
		 (*((unsigned short *)(skb->data + ETH_ALEN * 2)) == __constant_htons(ETH_P_8021Q)) &&
		 (*((unsigned short *)(skb->data + ETH_ALEN * 2 + 2)) == __constant_htons(ETH_P_IP)) &&
		 ((*((unsigned char *)(skb->data + ETH_ALEN * 2 + 4)) != __constant_htons(0x45)))))
		flag = TRUE;
	return flag;
}
static inline int rtl_isHwlookup(struct sk_buff *skb, struct dev_priv *cp, uint32 *portlist)

{
	int flag = FALSE;

	if ((rtl_isWanDev(cp) != TRUE) && (rtl_ip_option_check(skb) != TRUE))
	{
		flag = TRUE;
	}
	else
	{
		flag = FALSE;
	}

	if (flag == FALSE)
	{
		*portlist = cp->portmask;
	}

	return flag;
}
static inline int rtl_fill_txInfo(rtl_nicTx_info *txInfo)
{
    /* Conditionally declare variables used only when HW lookup is enabled */
#if !RTL_FORCE_DIRECT_TX
    uint32 portlist;
    int hwlookup;
#endif
    struct sk_buff *skb = txInfo->out_skb;
    struct dev_priv *cp;

	cp = netdev_priv(skb->dev);
	txInfo->vid = cp->id;
	// printk("%s %d txInfo->vid=%d  txInfo->portlist=0x%x \n", __FUNCTION__, __LINE__, txInfo->vid,  txInfo->portlist);

	// default output queue is 0
	txInfo->txIdx = 0;

    if ((skb->data[0] & 0x01) == 0)
    {
#if RTL_FORCE_DIRECT_TX
        /* Force direct TX to VLAN member ports (no HW L2 lookup) */
        rtl_direct_txInfo(cp->portmask, txInfo);
#else
        hwlookup = rtl_isHwlookup(skb, cp, &portlist);
        if (hwlookup == TRUE)
            rtl_hwLookup_txInfo(txInfo);
        else
            rtl_direct_txInfo(portlist, txInfo);
#endif
    }
    else
    {
        /*multicast process*/
        rtl_direct_txInfo(cp->portmask, txInfo);
    }

	// for WAN Tag

	if (txInfo->portlist == 0)
	{
		dev_kfree_skb_any(skb);
		return FAILED;
	}
	return SUCCESS;
}

/**
 * re865x_start_xmit - Transmit a packet
 * @skb: Socket buffer containing the packet to transmit
 * @dev: Network device structure
 *
 * Main TX path called by the network stack. Validates the packet,
 * fills TX descriptor info, flushes DMA cache, and submits to
 * hardware ring. Implements TX flow control via BQL and queue
 * stop/wake mechanisms.
 *
 * Return: NETDEV_TX_OK on success (packet consumed or freed),
 *         NETDEV_TX_BUSY if ring is full (stack will retry)
 */
static int re865x_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	int retval, free_count;
	struct dev_priv *cp;
	struct sk_buff *tx_skb;
	rtl_nicTx_info nicTx;
	struct netdev_queue *txq;

	nicTx.out_skb = skb;
	retval = rtl_preProcess_xmit(&nicTx);

	if (FAILED == retval)
		return NETDEV_TX_OK;

	tx_skb = nicTx.out_skb;
	cp = netdev_priv(tx_skb->dev);

	if ((cp->id == 0) || (cp->portmask == 0))
	{
		dev_kfree_skb_any(tx_skb);
		return NETDEV_TX_OK;
	}

	retval = rtl_fill_txInfo(&nicTx);
	if (FAILED == retval)
		return NETDEV_TX_OK;

	/* Flush DMA cache for SKB (handles scatter-gather fragments) - Issue #3 */
	rtl_skb_dma_cache_wback_inv(tx_skb);

	/* Try to send packet to TX ring */
	retval = swNic_send((void *)tx_skb, tx_skb->data, tx_skb->len, &nicTx);

    if (retval < 0)
    {
        /* TX ring full - try to reclaim completed descriptors once */
        swNic_txDone(nicTx.txIdx);
        retval = swNic_send((void *)tx_skb, tx_skb->data, tx_skb->len, &nicTx);

        if (retval < 0)
        {
            /* Still no space - record and stop queue, ask stack to retry */
            cp->tx_ring_full_errors++;
            smp_mb();  /* Ensure all prior writes visible before stopping queue */
            netif_stop_queue(dev);
            return NETDEV_TX_BUSY;
        }
    }

	/* Packet sent successfully to hardware ring */
	txq = netdev_get_tx_queue(dev, 0);
	netdev_tx_sent_queue(txq, tx_skb->len);  /* BQL: notify bytes sent */

	/* Check if we should stop queue for backpressure */
	free_count = swNic_txRingFreeCount(nicTx.txIdx);
	if (free_count >= 0 && free_count < RTL_NIC_TX_STOP_THRESHOLD) {
		smp_mb();  /* Ensure descriptor update visible before stopping */
		netif_stop_queue(dev);
	}

	rtl_pstProcess_xmit(cp, tx_skb->len);
	// cp->net_stats.tx_packets++;
	// cp->net_stats.tx_bytes += tx_skb->len;

	return NETDEV_TX_OK;
}

static void re865x_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
    struct dev_priv *cp = netdev_priv(dev);
    /* Count recovery events for ethtool stats */
    if (cp)
        cp->ring_recovery_count++;
    rtlglue_printf("Tx Timeout!!! Can't send packet\n");
}

int rtl819x_get_port_status(int portnum, struct lan_port_status *port_status)
{
	uint32 regData;
	uint32 data0;

	if (portnum < 0 || portnum > CPU)
		return -1;

	regData = READ_MEM32(PSRP0 + ((portnum) << 2));

	// printk("rtl819x_get_port_status port = %d data=%x\n", portnum,regData); //mark_debug
	data0 = regData & PortStatusLinkUp;
	if (data0)
		port_status->link = 1;
	else
		port_status->link = 0;

	data0 = regData & PortStatusNWayEnable;
	if (data0)
		port_status->nway = 1;
	else
		port_status->nway = 0;

	data0 = regData & PortStatusDuplex;
	if (data0)
		port_status->duplex = 1;
	else
		port_status->duplex = 0;

	data0 = (regData & PortStatusLinkSpeed_MASK) >> PortStatusLinkSpeed_OFFSET;
	port_status->speed = data0; // 0 = 10M , 1= 100M , 2=1G ,

	return 0;
}

int rtl819x_get_port_stats(int portnum, struct port_statistics *port_stats)
{
	uint32 addrOffset_fromP0 = 0;

	/* Validate port number to prevent integer overflow */
	if (unlikely(portnum < 0 || portnum > CPU))
		return -1;

	/*
	 * Security fix (2025-11-21): Validate offset after multiplication
	 * to prevent integer overflow leading to out-of-bounds register access.
	 * Max safe offset: CPU * MIB_ADDROFFSETBYPORT = 6 * 0x80 = 0x300
	 */
	addrOffset_fromP0 = portnum * MIB_ADDROFFSETBYPORT;
	if (unlikely(addrOffset_fromP0 > (CPU * MIB_ADDROFFSETBYPORT)))
		return -EINVAL;

	// port_stats->rx_bytes =(uint32) (rtl865xC_returnAsicCounter64( OFFSET_IFINOCTETS_P0 + addrOffset_fromP0 )) ;
	port_stats->rx_bytes = rtl8651_returnAsicCounter(OFFSET_IFINOCTETS_P0 + addrOffset_fromP0);
	port_stats->rx_unipkts = rtl8651_returnAsicCounter(OFFSET_IFINUCASTPKTS_P0 + addrOffset_fromP0);
	port_stats->rx_mulpkts = rtl8651_returnAsicCounter(OFFSET_ETHERSTATSMULTICASTPKTS_P0 + addrOffset_fromP0);
	port_stats->rx_bropkts = rtl8651_returnAsicCounter(OFFSET_ETHERSTATSBROADCASTPKTS_P0 + addrOffset_fromP0);
	port_stats->rx_discard = rtl8651_returnAsicCounter(OFFSET_DOT1DTPPORTINDISCARDS_P0 + addrOffset_fromP0);
	port_stats->rx_error = (rtl8651_returnAsicCounter(OFFSET_DOT3STATSFCSERRORS_P0 + addrOffset_fromP0) +
							rtl8651_returnAsicCounter(OFFSET_ETHERSTATSJABBERS_P0 + addrOffset_fromP0));

	// port_stats->tx_bytes =(uint32) (rtl865xC_returnAsicCounter64( OFFSET_IFOUTOCTETS_P0 + addrOffset_fromP0 )) ;
	port_stats->tx_bytes = rtl8651_returnAsicCounter(OFFSET_IFOUTOCTETS_P0 + addrOffset_fromP0);
	port_stats->tx_unipkts = rtl8651_returnAsicCounter(OFFSET_IFOUTUCASTPKTS_P0 + addrOffset_fromP0);
	port_stats->tx_mulpkts = rtl8651_returnAsicCounter(OFFSET_IFOUTMULTICASTPKTS_P0 + addrOffset_fromP0);
	port_stats->tx_bropkts = rtl8651_returnAsicCounter(OFFSET_IFOUTBROADCASTPKTS_P0 + addrOffset_fromP0);
	port_stats->tx_discard = rtl8651_returnAsicCounter(OFFSET_IFOUTDISCARDS + addrOffset_fromP0);
	port_stats->tx_error = (rtl8651_returnAsicCounter(OFFSET_ETHERSTATSCOLLISIONS_P0 + addrOffset_fromP0) +
							rtl8651_returnAsicCounter(OFFSET_DOT3STATSDEFERREDTRANSMISSIONS_P0 + addrOffset_fromP0));

	return 0;
}

int re865x_priv_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	int32 rc = 0;
	unsigned long *data_32;
	unsigned long portnum_ulong;
	int portnum = 0;
	struct lan_port_status port_status;
	struct port_statistics port_stats;

	data_32 = (unsigned long *)rq->ifr_data;

	/*
	 * Security fix (2025-11-21): Validate user input IMMEDIATELY after copy
	 * to prevent TOCTOU race and integer overflow on 64-bit systems.
	 */
	if (copy_from_user(&portnum_ulong, data_32, sizeof(unsigned long)))
		return -EFAULT;

	/* Validate range before casting to prevent sign extension issues */
	if (unlikely(portnum_ulong > CPU))
		return -EINVAL;

	portnum = (int)portnum_ulong;

	switch (cmd)
	{
	case RTL819X_IOCTL_READ_PORT_STATUS:
		rc = rtl819x_get_port_status(portnum, &port_status); // portnumber
		if (rc != 0)
		{
			return -EFAULT;
		}
		if (copy_to_user((void *)rq->ifr_data, (void *)&port_status, sizeof(struct lan_port_status)))
		{
			return -EFAULT;
		}
		break;
	case RTL819X_IOCTL_READ_PORT_STATS:
		rc = rtl819x_get_port_stats(portnum, &port_stats); // portnumber
		if (rc != 0)
			return -EFAULT;
		if (copy_to_user((void *)rq->ifr_data, (void *)&port_stats, sizeof(struct port_statistics)))
			return -EFAULT;
		break;
		break;
	default:
		rc = -EOPNOTSUPP;
		break;
	}
	return SUCCESS;
}
int re865x_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	int32 rc = 0;
	unsigned long *data;
	int32 args[4];
	int32 *pRet;


	if (cmd == RTL8651_IOCTL_CLEARBRSHORTCUTENTRY)
	{


		return 0;
	}

	if (cmd != SIOCDEVPRIVATE)
	{
		rc = re865x_priv_ioctl(dev, rq, cmd);
		return rc;
	}

	data = (unsigned long *)rq->ifr_data;

	if (copy_from_user(args, data, 4 * sizeof(unsigned long)))
	{
		return -EFAULT;
	}

	switch (args[0])
	{

	case RTL8651_IOCTL_GETWANLINKSTATUS:
	{
		int i;
		int wanPortMask;
		int32 totalVlans;

		pRet = (int32 *)args[3];
		*pRet = FAILED;
		rc = SUCCESS;

		wanPortMask = 0;
		totalVlans = ((sizeof(vlanconfig)) / (sizeof(struct rtl865x_vlanConfig))) - 1;
		for (i = 0; i < totalVlans; i++)
		{
			if (vlanconfig[i].isWan == TRUE)
				wanPortMask = vlanconfig[i].memPort;
		}
		if (wanPortMask == 0)
		{
			/* no wan port exist */
			break;
		}

		for (i = 0; i < RTL8651_AGGREGATOR_NUMBER; i++)
		{
			if ((1 << i) & wanPortMask)
			{
				if ((READ_MEM32(PSRP0 + (i << 2)) & PortStatusLinkUp) != 0)
				{
					*pRet = SUCCESS;
				}
				break;
			}
		}
		break;
	}

	case RTL8651_IOCTL_GETWANLINKSPEED:
	{
		int i;
		int wanPortMask;
		int32 totalVlans;

		pRet = (int32 *)args[3];
		*pRet = FAILED;
		rc = FAILED;

		wanPortMask = 0;
		totalVlans = ((sizeof(vlanconfig)) / (sizeof(struct rtl865x_vlanConfig))) - 1;
		for (i = 0; i < totalVlans; i++)
		{
			if (vlanconfig[i].isWan == TRUE)
				wanPortMask = vlanconfig[i].memPort;
		}

		if (wanPortMask == 0)
		{
			/* no wan port exist */
			break;
		}

		for (i = 0; i < RTL8651_AGGREGATOR_NUMBER; i++)
		{
			if ((1 << i) & wanPortMask)
			{
				break;
			}
		}

		switch (READ_MEM32(PSRP0 + (i << 2)) & PortStatusLinkSpeed_MASK)
		{
		case PortStatusLinkSpeed10M:
			*pRet = PortStatusLinkSpeed10M;
			rc = SUCCESS;
			break;
		case PortStatusLinkSpeed100M:
			*pRet = PortStatusLinkSpeed100M;
			rc = SUCCESS;
			break;
		case PortStatusLinkSpeed1000M:
			*pRet = PortStatusLinkSpeed1000M;
			rc = SUCCESS;
			break;
		default:
			break;
		}
		break;
	}

	default:
		rc = SUCCESS;
		break;
	}

	return rc;
	if (!netif_running(dev))
		return -EINVAL;
	switch (cmd)
	{
	default:
		rc = -EOPNOTSUPP;
		break;
	}
	return rc;
}

static int rtl865x_set_hwaddr(struct net_device *dev, void *addr)
{
	unsigned long flags;
	int i;
	unsigned char *p;
	ps_drv_netif_mapping_t *mapp_entry;
	struct rtl865x_vlanConfig *vlancfg_entry;

	p = ((struct sockaddr *)addr)->sa_data;
	local_irq_save(flags);

	for (i = 0; i < ETHER_ADDR_LEN; ++i)
	{
		dev->dev_addr[i] = p[i];
	}

	mapp_entry = rtl_get_ps_drv_netif_mapping_by_psdev(dev);
	if (mapp_entry == NULL)
		goto out;

	vlancfg_entry = rtl_get_vlanconfig_by_netif_name(mapp_entry->drvName);
	if (vlancfg_entry == NULL)
		goto out;

	if (vlancfg_entry->vid != 0)
	{
		rtl865x_netif_t netif;
		memcpy(vlancfg_entry->mac.octet, dev->dev_addr, ETHER_ADDR_LEN);
		memcpy(netif.macAddr.octet, vlancfg_entry->mac.octet, ETHER_ADDR_LEN);
		memcpy(netif.name, vlancfg_entry->ifname, MAX_IFNAMESIZE);
		rtl865x_setNetifMac(&netif);
	}

out:
	local_irq_restore(flags);
	return SUCCESS;
}

static int rtl865x_set_mtu(struct net_device *dev, int new_mtu)
{
	unsigned long flags;
	ps_drv_netif_mapping_t *mapp_entry;
	struct rtl865x_vlanConfig *vlancfg_entry;

	local_irq_save(flags);
	dev->mtu = new_mtu;

	mapp_entry = rtl_get_ps_drv_netif_mapping_by_psdev(dev);
	if (mapp_entry == NULL)
		goto out;

	vlancfg_entry = rtl_get_vlanconfig_by_netif_name(mapp_entry->drvName);
	if (vlancfg_entry == NULL)
		goto out;

	if (vlancfg_entry->vid != 0)
	{
		rtl865x_netif_t netif;
		vlancfg_entry->mtu = new_mtu;
		netif.mtu = new_mtu;
		memcpy(netif.name, vlancfg_entry->ifname, MAX_IFNAMESIZE);

		rtl865x_setNetifMtu(&netif);
	}

out:
	local_irq_restore(flags);

	return SUCCESS;
}

/* Phase 2: ethtool support for statistics visibility */
static const char rtl819x_gstrings_stats[][ETH_GSTRING_LEN] = {
	"rx_desc_null_errors",
	"rx_mbuf_null_errors",
	"rx_skb_null_errors",
	"rx_desc_index_errors",
	"rx_mbuf_index_errors",
	"rx_length_errors",
	"tx_desc_null_errors",
	"tx_mbuf_null_errors",
	"tx_desc_index_errors",
	"rx_refill_failures",
	"rx_pool_empty_events",
	"last_eth_skb_free",
	"pool_free_current",  /* Real-time pool free count (not just failure snapshot) */
	"tx_ring_full_errors",
	"ring_recovery_count",
};

#define RTL819X_STATS_LEN ARRAY_SIZE(rtl819x_gstrings_stats)

static void rtl819x_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	/*
	 * Security fix (2025-11-21): Use strlcpy instead of strcpy
	 * to prevent buffer overflow if constants are changed in future.
	 */
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION " (" DRV_RELDATE ")", sizeof(info->version));
	strlcpy(info->bus_info, "internal", sizeof(info->bus_info));
	strlcpy(info->fw_version, DRV_DESCRIPTION, sizeof(info->fw_version));
}

static int rtl819x_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return RTL819X_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void rtl819x_get_ethtool_stats(struct net_device *dev,
				      struct ethtool_stats *stats, u64 *data)
{
	unsigned long driver_stats[9];
	struct dev_priv *cp = netdev_priv(dev);
	int i;

	/* Use accessor function to avoid MIPS alignment and symbol issues */
	rtl_swnic_get_error_stats(driver_stats);

	/* Copy swNic stats (9 counters) */
	for (i = 0; i < 9; i++)
		data[i] = (u64)driver_stats[i];

	/* Phase 6: Add buffer pool monitoring stats (4 counters) */
	data[9]  = (u64)cp->rx_refill_failures;
	data[10] = (u64)cp->rx_pool_empty_events;
	data[11] = (u64)cp->last_eth_skb_free_num;  /* Snapshot at last failure */
	data[12] = (u64)eth_skb_free_num;           /* Real-time current value */

	/* Phase 7: TX path instrumentation (2 counters) */
	data[13] = (u64)cp->tx_ring_full_errors;
	data[14] = (u64)cp->ring_recovery_count;
}

static void rtl819x_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(data, rtl819x_gstrings_stats, sizeof(rtl819x_gstrings_stats));
		break;
	}
}

/**
 * rtl819x_get_regs_len - Get register dump length
 * @dev: Network device
 *
 * Returns the size in bytes needed for register dump.
 * Phase 1: Extended ethtool support for debugging.
 */
static int rtl819x_get_regs_len(struct net_device *dev)
{
	/* Dump key registers:
	 * - CPU Interface: CPUIIMR, CPUIISR (2 regs)
	 * - Port Control: PCRP0-PCRP5 (6 regs)
	 * - Switch Core: MACCR, MSCR, QNUMCR, etc (8 regs)
	 * Total: 16 registers × 4 bytes = 64 bytes
	 */
	return 64;
}

/**
 * rtl819x_get_regs - Dump important hardware registers
 * @dev: Network device
 * @regs: Register dump metadata
 * @p: Buffer to fill with register values
 *
 * Dumps critical hardware registers for debugging.
 * Phase 1: Extended ethtool support.
 */
static void rtl819x_get_regs(struct net_device *dev, struct ethtool_regs *regs, void *p)
{
	u32 *data = p;
	int i;

	regs->version = 1;  /* Register layout version */

	/* CPU Interface registers (offsets 0x028, 0x02c from CPU_IFACE_BASE) */
	data[0] = REG32(CPUIIMR);  /* Interrupt mask */
	data[1] = REG32(CPUIISR);  /* Interrupt status */

	/* Port Control registers PCRP0-PCRP5 (offsets 0x004-0x018 from SWCORE_BASE) */
	for (i = 0; i < 6; i++) {
		data[2 + i] = REG32(PCRP0 + (i * 4));
	}

	/* Switch Core registers */
	data[8] = REG32(MACCR);    /* MAC control */
	data[9] = REG32(MSCR);     /* Mode switch control (L2/L3/L4) */
	data[10] = REG32(QNUMCR);  /* Queue number control */
	data[11] = REG32(VLANTCR); /* VLAN tag control */
	data[12] = REG32(SWTCR0);  /* Switch table control 0 */
	data[13] = REG32(SWTCR1);  /* Switch table control 1 */

	/* RX/TX descriptor registers */
	data[14] = REG32(CPURPDCR0);  /* RX descriptor base ring 0 */
	data[15] = REG32(CPUTPDCR0);  /* TX descriptor base ring 0 */
}

static const struct ethtool_ops rtl819x_ethtool_ops = {
	.get_drvinfo		= rtl819x_get_drvinfo,
	.get_sset_count		= rtl819x_get_sset_count,
	.get_strings		= rtl819x_get_strings,
	.get_ethtool_stats	= rtl819x_get_ethtool_stats,
	/* Phase 1: Extended ethtool support */
	.get_link		= ethtool_op_get_link,     /* Standard link status */
	.get_regs_len		= rtl819x_get_regs_len,    /* Register dump size */
	.get_regs		= rtl819x_get_regs,        /* Register dump for debugging */
};

static const struct net_device_ops rtl819x_netdev_ops = {
	.ndo_open = re865x_open,
	.ndo_stop = re865x_close,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_mac_address = rtl865x_set_hwaddr,
	.ndo_set_rx_mode = re865x_set_rx_mode,  /* renamed from ndo_set_multicast_list in 5.4 */
	.ndo_get_stats64 = re865x_get_stats64,  /* Phase 1: 64-bit stats (prevent overflow) */
	.ndo_do_ioctl = re865x_ioctl,
	.ndo_start_xmit = re865x_start_xmit,
	.ndo_tx_timeout = re865x_tx_timeout,
	.ndo_change_mtu = rtl865x_set_mtu,

};


/**
 * re865x_legacy_init - Legacy initialization function (renamed from re865x_probe)
 *
 * This function contains the original initialization logic. It's now called
 * from the platform_driver probe function below.
 */
static int __init re865x_legacy_init(void)
{
	/*2007-12-19*/
	int32 i, j;
	int32 totalVlans = ((sizeof(vlanconfig)) / (sizeof(struct rtl865x_vlanConfig))) - 1;
	// WRITE_MEM32(PIN_MUX_SEL_2, 0x7<<21);
	//  initial the global variables in dmem.

	printk(KERN_INFO "rtl819x: %s v%s (%s) - %s\n", DRV_DESCRIPTION, DRV_VERSION, DRV_RELDATE, DRV_AUTHOR);
	REG32(CPUIIMR) = 0x00;
	REG32(CPUICR) &= ~(TXCMD | RXCMD);
	rxMbufRing = NULL;

	/*Initial ASIC table*/
	FullAndSemiReset();
	{
		rtl8651_tblAsic_InitPara_t para;

		memset(&para, 0, sizeof(rtl8651_tblAsic_InitPara_t));

		/*
			For DEMO board layout, RTL865x platform define corresponding PHY setting and PHYID.
		*/

		rtl865x_wanPortMask = RTL865X_PORTMASK_UNASIGNED;

		INIT_CHECK(rtl865x_initAsicL2(&para));

		/*
			Re-define the wan port according the wan port detection result.
			NOTE:
				There are a very strong assumption that if port5 was giga port,
				then wan port was port 5.
		*/
		if (RTL865X_PORTMASK_UNASIGNED == rtl865x_wanPortMask)
		{
			/* keep the original mask */
			assert(RTL865X_PORTMASK_UNASIGNED == rtl865x_lanPortMask);
			rtl865x_wanPortMask = RTL_WANPORT_MASK;
			rtl865x_lanPortMask = RTL_LANPORT_MASK;
		}
		else
		{
			/* redefine wan port mask */
			assert(RTL865X_PORTMASK_UNASIGNED != rtl865x_lanPortMask);
			for (i = 0; i < totalVlans; i++)
			{
				if (TRUE == vlanconfig[i].isWan)
				{
					vlanconfig[i].memPort = vlanconfig[i].untagSet = rtl865x_wanPortMask;
				}
				else
				{
					vlanconfig[i].memPort = vlanconfig[i].untagSet = rtl865x_lanPortMask;
				}
			}
		}
		/*
			Re-define the pre-allocated skb number according the wan
			port detection result.
			NOTE:
				There are a very strong assumption that if port1~port4 were
				all giga port, then the sdram was 32M.
		*/
		{
			if (RTL865X_PREALLOC_SKB_UNASIGNED == rtl865x_maxPreAllocRxSkb)
			{
				assert(rtl865x_rxSkbPktHdrDescNum ==
					   rtl865x_txSkbPktHdrDescNum ==
					   RTL865X_PREALLOC_SKB_UNASIGNED);

				rtl865x_maxPreAllocRxSkb = MAX_PRE_ALLOC_RX_SKB;
				rtl865x_rxSkbPktHdrDescNum = NUM_RX_PKTHDR_DESC;
				rtl865x_txSkbPktHdrDescNum = NUM_TX_PKTHDR_DESC;
			}
			else
			{
				assert(rtl865x_rxSkbPktHdrDescNum != RTL865X_PREALLOC_SKB_UNASIGNED);
				assert(rtl865x_txSkbPktHdrDescNum != RTL865X_PREALLOC_SKB_UNASIGNED);
				/* Assigned value in function of rtl8651_initAsic() */
				rxRingSize[0] = rtl865x_rxSkbPktHdrDescNum;
				txRingSize[0] = rtl865x_txSkbPktHdrDescNum;
			}

			for (i = 1; i < RTL865X_SWNIC_RXRING_HW_PKTDESC; i++)
			{
				rtl865x_maxPreAllocRxSkb += rxRingSize[i];
			}
		}
	}

	/*init PHY LED style*/

	/*2007-12-19*/

	INIT_CHECK(rtl865x_init());

	INIT_CHECK(rtl865x_config(vlanconfig));

	/* create all default VLANs */
	//	rtlglue_printf("	creating eth0~eth%d...\n",totalVlans-1 );

	for (i = 0; i < totalVlans; i++)
	{
		struct net_device *dev;
		struct dev_priv *dp;
		int rc;

		if (IF_ETHER != vlanconfig[i].if_type)
		{
			continue;
		}
		dev = alloc_etherdev(sizeof(struct dev_priv));
		if (!dev)
		{
			printk("failed to allocate dev %d", i);
			return -1;
		}
		SET_MODULE_OWNER(dev);
		dp = netdev_priv(dev);
		memset(dp, 0, sizeof(*dp));
		dp->dev = dev;
		dp->id = vlanconfig[i].vid;
		dp->portmask = vlanconfig[i].memPort;
		dp->portnum = 0;
		for (j = 0; j < RTL8651_AGGREGATOR_NUMBER; j++)
		{
			if (dp->portmask & (1 << j))
				dp->portnum++;
		}
		memcpy((char *)dev->dev_addr, (char *)(&(vlanconfig[i].mac)), ETHER_ADDR_LEN);
		dev->netdev_ops = &rtl819x_netdev_ops;
		dev->ethtool_ops = &rtl819x_ethtool_ops;  /* Kernel 5.4: SET_ETHTOOL_OPS removed */
		dev->watchdog_timeo = TX_TIMEOUT;
		/* Kernel 5.4: Use Linux virtual IRQ mapped by INTC driver
		 * Switch hardware IRQ 15 (rtl819x.dtsi) → Linux virtual IRQ = INTC_BASE + 15 = 16 + 15 = 31
		 * See arch/mips/realtek/irq.c: REALTEK_INTC_IRQ_BASE=16, REALTEK_HW_SW_CORE_BIT=15 */
		dev->irq = 31;  /* 16 + 15 */
		rc = register_netdev(dev);

		if (!rc)
		{
			_rtl86xx_dev.dev[i] = dev;
			rtl_add_ps_drv_netif_mapping(dev, vlanconfig[i].ifname);
			/*2007-12-19*/
			/* Use isWan flag (from DT or hardcoded) for consistent LAN/WAN detection */
			printk(KERN_INFO "rtl819x: %s registered (VLAN %d, %s)\n",
				vlanconfig[i].ifname, vlanconfig[i].vid,
				vlanconfig[i].isWan ? "WAN" : "LAN");
		}
		else
			rtlglue_printf("Failed to allocate eth%d\n", i);
	}

	/* Single interface - no linked list needed (dev_next/dev_prev remain NULL) */

	init_priv_eth_skb_buf();
	rtl_rxTxDoneCnt = 0;
	atomic_set(&rtl_devOpened, 0);

	memset(&rx_skb_queue, 0, sizeof(struct ring_que));

	printk(KERN_INFO "rtl819x: Driver initialization complete\n");
	return 0;
}


//---------------------------------------------------------------------------
static void init_priv_eth_skb_buf(void)
{
	int i;

	DEBUG_ERR("Init priv skb.\n");
	memset(eth_skb_buf, '\0', sizeof(struct priv_skb_buf2) * (MAX_ETH_SKB_NUM));
	INIT_LIST_HEAD(&eth_skbbuf_list);
	eth_skb_free_num = MAX_ETH_SKB_NUM;

	for (i = 0; i < MAX_ETH_SKB_NUM; i++)
	{
		memcpy(eth_skb_buf[i].magic, ETH_MAGIC_CODE, ETH_MAGIC_LEN);
		eth_skb_buf[i].buf_pointer = (void *)(&eth_skb_buf[i]);
		INIT_LIST_HEAD(&eth_skb_buf[i].list);
		list_add_tail(&eth_skb_buf[i].list, &eth_skbbuf_list);
	}
}

static __inline__ unsigned char *get_buf_from_poll(struct list_head *phead, unsigned int *count)
{
	unsigned long flags;
	unsigned char *buf;
	struct list_head *plist;

	local_irq_save(flags);

	if (list_empty(phead))
	{
		local_irq_restore(flags);
		DEBUG_ERR("eth_drv: phead=%X buf is empty now!\n", (unsigned int)phead);
		DEBUG_ERR("free count %d\n", *count);
		return NULL;
	}

	if (*count == 1)
	{
		local_irq_restore(flags);
		DEBUG_ERR("eth_drv: phead=%X under-run!\n", (unsigned int)phead);
		return NULL;
	}

	*count = *count - 1;
	plist = phead->next;
	list_del_init(plist);
	buf = (unsigned char *)((unsigned int)plist + sizeof(struct list_head));
	local_irq_restore(flags);
	return buf;
}

__inline__ void release_buf_to_poll(unsigned char *pbuf, struct list_head *phead, unsigned int *count)
{
	unsigned long flags;
	struct list_head *plist;

	local_irq_save(flags);

	*count = *count + 1;
	plist = (struct list_head *)((unsigned int)pbuf - sizeof(struct list_head));
	list_add_tail(plist, phead);
	local_irq_restore(flags);
}

void free_rtl865x_eth_priv_buf(unsigned char *head)
{
	release_buf_to_poll(head, &eth_skbbuf_list, (unsigned int *)&eth_skb_free_num);
}
EXPORT_SYMBOL(free_rtl865x_eth_priv_buf);

/*
 * NOTE: SKB destructor removed - no longer needed with synchronous allocation.
 *
 * Previous async mechanism (commit 70575b63) used rtl865x_eth_skb_destructor()
 * to refill RX descriptors when network stack freed SKBs. This caused:
 * - Performance: 24 Mbps (vs 78.5 Mbps with sync)
 * - Latency: Several ms delay between packet RX and descriptor refill
 * - Stalls: Complete TCP freeze after 15 seconds
 *
 * Current synchronous mechanism (restored from original):
 * - swNic_receive() calls alloc_rx_buf() immediately for each packet
 * - New SKB allocated before passing current packet to stack
 * - Descriptor refilled instantly via increase_rx_idx_release_pkthdr()
 * - Performance: 78.5 Mbps stable
 */

static struct sk_buff *dev_alloc_skb_priv_eth(unsigned int size)
{
	struct sk_buff *skb;
	unsigned char *data;

	/* first argument is not used */
	if (eth_skb_free_num > 0)
	{
		data = get_buf_from_poll(&eth_skbbuf_list, (unsigned int *)&eth_skb_free_num);
		if (data == NULL)
		{
			DEBUG_ERR("eth_drv: priv_skb buffer empty!\n");
			return NULL;
		}

		skb = dev_alloc_8190_skb(data, size);

		if (skb == NULL)
		{
			// free_rtl865x_eth_priv_buf(data);
			release_buf_to_poll(data, &eth_skbbuf_list, (unsigned int *)&eth_skb_free_num);
			DEBUG_ERR("alloc linux_skb buff failed!\n");
			return NULL;
		}

		/*
		 * NOTE: No destructor needed with synchronous allocation.
		 * Buffer is returned to pool when SKB freed, but descriptor refill
		 * happens immediately in swNic_receive() via alloc_rx_buf() +
		 * increase_rx_idx_release_pkthdr().
		 */

		return skb;
	}

	return NULL;
}

int is_rtl865x_eth_priv_buf(unsigned char *head)
{
	unsigned long offset = (unsigned long)(&((struct priv_skb_buf2 *)0)->buf);
	struct priv_skb_buf2 *priv_buf = (struct priv_skb_buf2 *)(((unsigned long)head) - offset);
	int magic_ok = !memcmp(priv_buf->magic, ETH_MAGIC_CODE, ETH_MAGIC_LEN);
	int ptr_ok = (priv_buf->buf_pointer == (void *)(priv_buf));

	return (magic_ok && ptr_ok);
}
EXPORT_SYMBOL(is_rtl865x_eth_priv_buf);

struct sk_buff *priv_skb_copy(struct sk_buff *skb)
{
	struct sk_buff *n;
	unsigned long flags;

	if (rx_skb_queue.qlen == 0)
	{
		n = dev_alloc_skb_priv_eth(CROSS_LAN_MBUF_LEN);
	}
	else
	{
		local_irq_save(flags);
		n = rtk_dequeue(&rx_skb_queue);
		local_irq_restore(flags);
	}

	if (n == NULL)
	{
		return NULL;
	}

	/* Set the tail pointer and length */
	skb_put(n, skb->len);
	n->csum = skb->csum;
	n->ip_summed = skb->ip_summed;
	memcpy(n->data, skb->data, skb->len);

	skb_copy_header(n, skb);
	return n;
}
EXPORT_SYMBOL(priv_skb_copy);

/**
 * rtl819x_parse_vlan_from_dt - Parse VLAN configuration from device tree
 * @pdev: Platform device
 *
 * Parses interface child nodes from device tree and populates vlanconfig[] array.
 * Each child node "interface@X" represents one network interface (eth0, eth1, etc).
 *
 * DT structure:
 *   interface@0 {
 *       reg = <0>;
 *       ifname = "eth0";
 *       local-mac-address = [02 14 B8 ...];
 *       vlan-id = <1>;
 *       member-ports = <0x10>;
 *       ...
 *   };
 *
 * Returns: Number of interfaces parsed, or negative errno on error
 */
static int rtl819x_parse_vlan_from_dt(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;
	const char *ifname_str;
	const u8 *mac_addr;
	u32 val;
	int idx = 0;
	int count = 0;

	if (!np)
		return -ENODEV;

	/* Iterate over all "interface@X" child nodes */
	for_each_available_child_of_node(np, child) {
		if (idx >= ETH_INTF_NUM) {
			dev_warn(&pdev->dev, "Too many interfaces in DT (max %d), ignoring extras\n", ETH_INTF_NUM);
			of_node_put(child);
			break;
		}

		/* Parse interface name */
		if (of_property_read_string(child, "ifname", &ifname_str) == 0) {
			strlcpy((char *)vlanconfig[idx].ifname, ifname_str, IFNAMSIZ);
		} else {
			dev_err(&pdev->dev, "Missing 'ifname' property in interface@%d\n", idx);
			of_node_put(child);
			return -EINVAL;
		}

		/* Parse MAC address (standard DT binding) */
		mac_addr = of_get_property(child, "local-mac-address", NULL);
		if (mac_addr) {
			memcpy(vlanconfig[idx].mac.octet, mac_addr, 6);
		} else {
			dev_warn(&pdev->dev, "Interface %s: No MAC in DT, using hardcoded\n", ifname_str);
		}

		/* Parse VLAN ID */
		if (of_property_read_u32(child, "vlan-id", &val) == 0)
			vlanconfig[idx].vid = (u16)val;

		/* Parse Forwarding ID */
		if (of_property_read_u32(child, "forwarding-id", &val) == 0)
			vlanconfig[idx].fid = (u16)val;

		/* Parse member ports mask */
		if (of_property_read_u32(child, "member-ports", &val) == 0)
			vlanconfig[idx].memPort = val;

		/* Parse untagged ports mask */
		if (of_property_read_u32(child, "untag-ports", &val) == 0)
			vlanconfig[idx].untagSet = val;

		/* Parse is-wan flag */
		if (of_property_read_u32(child, "is-wan", &val) == 0)
			vlanconfig[idx].isWan = (u8)val;

		/* Parse MTU */
		if (of_property_read_u32(child, "mtu", &val) == 0)
			vlanconfig[idx].mtu = val;

		/* Set fixed values */
		vlanconfig[idx].if_type = IF_ETHER;
		vlanconfig[idx].is_slave = 0;

		idx++;
		count++;
	}

	if (count == 0) {
		dev_info(&pdev->dev, "No interface nodes in DT, using hardcoded vlanconfig\n");
		return 0;  /* Use hardcoded config */
	}

	/* Add sentinel if we parsed interfaces */
	if (idx < ETH_INTF_NUM) {
		memset(&vlanconfig[idx], 0, sizeof(vlanconfig[idx]));
	}

	return count;
}

/**
 * re865x_probe - Platform driver probe function (DT integration)
 * @pdev: Platform device from device tree
 *
 * This is the new probe callback for platform_driver, called when the
 * device tree node with compatible = "realtek,rtl8196e-mac" is matched.
 * It wraps the legacy initialization function.
 *
 * Returns: 0 on success, negative errno on failure
 */
static int re865x_probe(struct platform_device *pdev)
{
	int ret;
	int vlan_count;

	/* DT v3.0: Parse full VLAN configuration from device tree
	 * Parses interface child nodes and populates vlanconfig[] array
	 * Falls back to hardcoded config if no DT nodes found
	 */
	vlan_count = rtl819x_parse_vlan_from_dt(pdev);
	if (vlan_count < 0) {
		dev_err(&pdev->dev, "Failed to parse VLAN config from DT: %d\n", vlan_count);
		return vlan_count;
	}

	/* Call legacy initialization function */
	ret = re865x_legacy_init();
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to initialize RTL8196E ethernet: %d\n", ret);
		return ret;
	}

	/* Store platform_device in driver data for potential future use */
	platform_set_drvdata(pdev, &_rtl86xx_dev);

	return 0;
}

/**
 * re865x_remove - Platform driver remove function
 * @pdev: Platform device
 *
 * Cleanup function called when driver is unloaded or device is removed.
 * Properly unregisters all network interfaces and frees allocated resources.
 *
 * Returns: 0 on success
 */
static int re865x_remove(struct platform_device *pdev)
{
	int i;

	/* Unregister and free all network devices */
	for (i = 0; i < ETH_INTF_NUM; i++) {
		if (_rtl86xx_dev.dev[i]) {
			struct net_device *dev = _rtl86xx_dev.dev[i];

			dev_info(&pdev->dev, "Removed %s\n", dev->name);

			/* Unregister from kernel (automatically stops interface if running)
			 * Note: unregister_netdev() handles RTNL locking internally and
			 * will call dev_close() if needed. No need to call it explicitly.
			 */
			unregister_netdev(dev);

			/* Free the device structure */
			free_netdev(dev);

			/* Clear pointer */
			_rtl86xx_dev.dev[i] = NULL;
		}
	}

	/* Clear device count */
	_rtl86xx_dev.devnum = 0;
	_rtl86xx_dev.ready = 0;

	return 0;
}

/* Device tree match table */
static const struct of_device_id rtl819x_eth_of_match[] = {
	{ .compatible = "realtek,rtl8196e-mac", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rtl819x_eth_of_match);

/* Platform driver structure */
static struct platform_driver rtl819x_eth_driver = {
	.probe = re865x_probe,
	.remove = re865x_remove,
	.driver = {
		.name = "rtl819x-ethernet",
		.of_match_table = rtl819x_eth_of_match,
	},
};

/* Register platform driver (replaces module_init/module_exit) */
module_platform_driver(rtl819x_eth_driver);

/**
 * rtl865x_init - Initialize L2 ASIC tables and driver structures
 *
 * Initializes the RTL865x ASIC for L2 switching operation:
 * - Network interface table
 * - VLAN table
 * - ACL (Access Control List)
 * - Event manager
 * - L2 forwarding database
 * - Queue-to-RX-ring mapping for packet prioritization
 *
 * Must be called once before using the driver. Cannot be called twice.
 *
 * Return: SUCCESS on success, error code on failure
 */
int32 rtl865x_init(void)
{
	int32 retval = 0;
	__865X_Config = 0;
	/*common*/
	retval = rtl865x_initNetifTable();
	retval = rtl865x_initVlanTable();
	retval = rtl865x_init_acl();
	retval = rtl865x_initEventMgr(NULL);

	/*l2*/
	retval = rtl865x_layer2_init();

	/*queue id & rx ring descriptor mapping*/
	/*queue id & rx ring descriptor mapping*/
	REG32(CPUQDM0) = QUEUEID1_RXRING_MAPPING | (QUEUEID0_RXRING_MAPPING << 16);
	REG32(CPUQDM2) = QUEUEID3_RXRING_MAPPING | (QUEUEID2_RXRING_MAPPING << 16);
	REG32(CPUQDM4) = QUEUEID5_RXRING_MAPPING | (QUEUEID4_RXRING_MAPPING << 16);

	rtl8651_setAsicOutputQueueNumber(CPU, RTL_CPU_RX_RING_NUM);


	rtl_ps_drv_netif_mapping_init();
	return SUCCESS;
}

/**
 * rtl865x_config - Configure VLANs and network interfaces in ASIC
 * @vlanconfig: Array of VLAN configuration structures (NULL-terminated)
 *
 * Creates VLANs and network interfaces in the hardware ASIC based on
 * the provided configuration array. For each entry:
 * - Creates VLAN with specified VID and port membership
 * - Sets VLAN filtering database (FID)
 * - Creates network interface entry with MAC address and MTU
 * - Sets port PVID (Port VLAN ID) for untagged traffic
 *
 * Sets ASIC to L2 operation mode (layer 2 switching only).
 *
 * Return: SUCCESS on success, RTL_EINVALIDVLANID if first VID is 0
 */
int32 rtl865x_config(struct rtl865x_vlanConfig vlanconfig[])
{
	uint16 pvid;
	int32 i, j;
	int32 retval = 0;
	uint32 valid_port_mask = 0;

	if (!vlanconfig[0].vid)
		return RTL_EINVALIDVLANID;

	INIT_CHECK(rtl8651_setAsicOperationLayer(2));

	for (i = 0; vlanconfig[i].vid != 0; i++)
	{
		rtl865x_netif_t netif;

		if (vlanconfig[i].memPort == 0)
			continue;

		valid_port_mask = vlanconfig[i].memPort;
		if (vlanconfig[i].isWan == 0)
			valid_port_mask |= 0x100;

		/*add vlan*/
		retval = rtl865x_addVlan(vlanconfig[i].vid);

		if (retval == SUCCESS)
		{
			rtl865x_addVlanPortMember(vlanconfig[i].vid, vlanconfig[i].memPort & valid_port_mask);
			rtl865x_setVlanFilterDatabase(vlanconfig[i].vid, vlanconfig[i].fid);
		}
		/*add network interface*/
		memset(&netif, 0, sizeof(rtl865x_netif_t));
		memcpy(netif.name, vlanconfig[i].ifname, MAX_IFNAMESIZE);
		memcpy(netif.macAddr.octet, vlanconfig[i].mac.octet, ETHER_ADDR_LEN);
		netif.mtu = vlanconfig[i].mtu;
		netif.if_type = vlanconfig[i].if_type;
		netif.vid = vlanconfig[i].vid;
		netif.is_wan = vlanconfig[i].isWan;
		netif.is_slave = vlanconfig[i].is_slave;
		retval = rtl865x_addNetif(&netif);

		if (netif.is_slave == 1)
			rtl865x_attachMasterNetif(netif.name, RTL_DRV_WAN0_NETIF_NAME);

		if (retval != SUCCESS && retval != RTL_EVLANALREADYEXISTS)
			return retval;
	}

	/*this is a one-shot config*/
	if ((++__865X_Config) == 1)
	{
		for (i = 0; i < RTL8651_PORT_NUMBER + 3; i++)
		{
			/* Set each port's PVID */
			for (j = 0, pvid = 0; vlanconfig[j].vid != 0; j++)
			{
				if ((1 << i) & vlanconfig[j].memPort)
				{
					pvid = vlanconfig[j].vid;
					break;
				}
			}

			if (pvid != 0)
			{

				CONFIG_CHECK(rtl8651_setAsicPvid(i, pvid));
			}
		}
	}

	return SUCCESS;
}

/*
 * GATEWAY_MODE simplification - removed mode switching code:
 * - rtl_reinit_hw_table() - hardware table reinitialization for mode change
 * - rtl_config_lanwan_dev_vlanconfig() - VLAN configuration (now inline in init)
 * - rtl_config_operation_layer() - operation layer setting (now inline in init)
 * - rtl_config_vlanconfig() - wrapper for VLAN config
 * - rtl865x_changeOpMode() - main mode switching function
 * - rtl865x_reChangeOpMode() - mode toggle function
 * - reinit_vlan_configure() - VLAN reconfiguration for mode changes
 *
 * Operation mode is now permanently fixed to GATEWAY_MODE (LAN/WAN routing).
 */

int re865x_reProbe(void)
{
	rtl8651_tblAsic_InitPara_t para;
	unsigned long flags;

	local_irq_save(flags);
	// WRITE_MEM32(PIN_MUX_SEL_2, 0x7<<21);
	/*Initial ASIC table*/
	FullAndSemiReset();

	memset(&para, 0, sizeof(rtl8651_tblAsic_InitPara_t));

	INIT_CHECK(rtl865x_initAsicL2(&para));

	/*init PHY LED style*/
	/*2007-12-19*/
	/*queue id & rx ring descriptor mapping*/
	REG32(CPUQDM0) = QUEUEID1_RXRING_MAPPING | (QUEUEID0_RXRING_MAPPING << 16);
	REG32(CPUQDM2) = QUEUEID3_RXRING_MAPPING | (QUEUEID2_RXRING_MAPPING << 16);
	REG32(CPUQDM4) = QUEUEID5_RXRING_MAPPING | (QUEUEID4_RXRING_MAPPING << 16);
	rtl8651_setAsicOutputQueueNumber(CPU, RTL_CPU_RX_RING_NUM);
	local_irq_restore(flags);
	return 0;
}

int rtl865x_reinitSwitchCore(void)
{
	/*enable switch core clock*/
	rtl865x_duringReInitSwtichCore = 1;
	/*disable switch core interrupt*/
	REG32(CPUICR) = 0;
	REG32(CPUIIMR) = 0;
	REG32(GIMR) &= ~(BSP_SW_IE);

	re865x_reProbe();
	swNic_reInit();

	/*enable switch core interrupt*/

	/* Preserve TX length mode: hardware excludes CRC from packet length. */
    /* Match rtl865x_start(): 32-word burst + EXCLUDE_CRC (stable) */
    REG32(CPUICR) = TXCMD | RXCMD | BUSBURST_32WORDS | MBUF_2048BYTES | EXCLUDE_CRC;
	REG32(CPUIIMR) = RX_DONE_IE_ALL | TX_ALL_DONE_IE_ALL | LINK_CHANGE_IE | PKTHDR_DESC_RUNOUT_IE_ALL;
	REG32(SIRR) |= TRXRDY;
	REG32(GIMR) |= (BSP_SW_IE);

	rtl865x_duringReInitSwtichCore = 0;
	return 0;
}

void rtl_check_swCore_tx_hang(void)
{
	int32 tx_done_index_tmp = 0;

	if ((rtl_swCore_tx_hang_cnt > 0) || (((rtl_check_swCore_timer++) % rtl_check_swCore_tx_hang_interval) == 0))
	{
		if (rtl_check_tx_done_desc_swCore_own(&tx_done_index_tmp) == SUCCESS)
		{
			if (rtl_last_tx_done_idx != tx_done_index_tmp)
			{
				rtl_last_tx_done_idx = tx_done_index_tmp;
				rtl_swCore_tx_hang_cnt = 1;
			}
			else
			{
				rtl_swCore_tx_hang_cnt++;
			}
		}
		else
		{
			rtl_swCore_tx_hang_cnt = 0;
		}

		if (rtl_swCore_tx_hang_cnt >= rtl_reinit_swCore_threshold)
		{
			printk("SwCore tx hang is detected!\n");
			rtl_swCore_tx_hang_cnt = 0;

			if (rtl865x_duringReInitSwtichCore == 0)
			{
				printk("Switch will reinit now!\n");
				rtl_reinit_swCore_counter++;
				rtl865x_reinitSwitchCore();
			}
		}
	}

	return;
}


extern int eee_enabled;
extern void enable_EEE(void);
extern void disable_EEE(void);
