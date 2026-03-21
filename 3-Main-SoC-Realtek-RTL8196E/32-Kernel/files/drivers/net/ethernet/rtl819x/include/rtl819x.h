/*
 * RTL819x consolidated driver header
 *
 * This header merges the minimal set of declarations used by the driver
 * from former include/ headers, except rtl865xc_asicregs.h which remains
 * unchanged.
 *
 * SPDX-License-Identifier: GPL-2.0
 */
#ifndef RTL819X_H
#define RTL819X_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>

/* =============================================================================
 * Basic Types and Common Macros (from rtl_types.h)
 * =============================================================================
 */
#ifndef _RTL_TYPES_H
#define _RTL_TYPES_H

typedef unsigned long long uint64;
typedef long long int64;
typedef unsigned int uint32;

#ifdef int32
#undef int32
#endif
typedef int int32;

typedef unsigned short uint16;
typedef short int16;
typedef unsigned char uint8;
typedef char int8;

typedef uint32 memaddr;
typedef uint32 ipaddr_t;

#define ETHER_ADDR_LEN 6

typedef struct ether_addr_s {
    uint8 octet[ETHER_ADDR_LEN];
} ether_addr_t;

#define RX_OFFSET 2
#define MBUF_LEN 1700
#define CROSS_LAN_MBUF_LEN (MBUF_LEN + RX_OFFSET + 10)

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef SUCCESS
#define SUCCESS 0
#endif
#ifndef FAILED
#define FAILED -1
#endif

#ifndef ISSET
#define ISSET(a, b) (((a) & (b)) != 0)
#endif

#ifndef __offsetof
#define __offsetof(type, field) ((unsigned long)(&((type *)0)->field))
#endif
#ifndef offsetof
#define offsetof(type, field) __offsetof(type, field)
#endif

#define rtlglue_printf printk

#define ASSERT_CSP(x)                                                                 \
    if (!(x)) {                                                                      \
        rtlglue_printf("\nAssert Fail: %s %d", __FILE__, __LINE__);                  \
        while (1)                                                                    \
            ;                                                                        \
    }

#endif /* _RTL_TYPES_H */

/* =============================================================================
 * Glue/OS Abstractions (from rtl_glue.h)
 * =============================================================================
 */
#define RTL_DECLARE_MUTEX(name) DEFINE_SEMAPHORE(name)

#ifndef RTL865X_DEBUG
#define assert(expr)    \
    do {                \
    } while (0)
#else
#define assert(expr)                                                        \
    if (!(expr)) {                                                          \
        printk("\033[33;41m%s:%d: assert(%s)\033[m\n",                      \
               __FILE__, __LINE__, #expr);                                  \
    }
#endif

#define TBL_MEM_ALLOC(tbl, type, size)                                      \
    {                                                                       \
        (tbl) = (type *)kmalloc((size) * sizeof(type), GFP_ATOMIC);         \
        if (!(tbl)) {                                                      \
            printk("MEM alloc failed at line %d\n", __LINE__);             \
            while (1)                                                      \
                ;                                                          \
            return FAILED;                                                 \
        }                                                                   \
    }

#ifndef bzero
#define bzero(p, s) memset(p, 0, s)
#endif

/* =============================================================================
 * Mbuf Structures and Flags (from mbuf.h, minimal)
 * =============================================================================
 */
#define BUF_FREE 0x00       /* Buffer is Free */
#define BUF_USED 0x80       /* Buffer is occupied */
#define BUF_ASICHOLD 0x80   /* Buffer is hold by ASIC */
#define BUF_DRIVERHOLD 0xc0 /* Buffer is hold by driver */

struct rtl_mBuf {
    struct rtl_mBuf *m_next;
    struct rtl_pktHdr *m_pkthdr; /* Points to the pkthdr structure */
    uint16 m_len;                /* data bytes used in this cluster */
    int8 m_flags;                /* mbuf flags; see below */
#define MBUF_FREE BUF_FREE       /* Free. Not occupied. should be on free list */
#define MBUF_USED BUF_USED       /* Buffer is occupied */
#define MBUF_EXT 0x10            /* has associated with an external cluster */
#define MBUF_PKTHDR 0x08         /* is the 1st mbuf of this packet */
#define MBUF_EOR 0x04            /* is the last mbuf of this packet. Set only by ASIC */
    uint8 *m_data;               /* location of data in the cluster */
    uint8 *m_extbuf;             /* start of buffer */
    uint16 m_extsize;            /* sizeof the cluster */
    int8 m_reserved[2];          /* padding */
    void *skb;
    uint32 pending0; /* for cache line alignment */
};

struct rtl_pktHdr {
    union {
        struct rtl_pktHdr *pkthdr_next; /* next pkthdr in free list */
        struct rtl_mBuf *mbuf_first;    /* 1st mbuf of this pkt */
    } PKTHDRNXT;
#define ph_nextfree PKTHDRNXT.pkthdr_next
#define ph_mbuf PKTHDRNXT.mbuf_first
    uint16 ph_len;             /* total packet length */
    uint16 ph_reserved1 : 1;   /* reserved */
    uint16 ph_queueId : 3;     /* bit 2~0: Queue ID */
    uint16 ph_extPortList : 4; /* dest extension port list. must be 0 for TX */

#define PKTHDR_EXTPORT_LIST_CPU 3

    uint16 ph_reserved2 : 3;   /* reserved */
    uint16 ph_hwFwd : 1;       /* hwFwd - copy from HSA bit 200 */
    uint16 ph_isOriginal : 1;  /* isOriginal - DP included cpu port or more than one ext port */
    uint16 ph_l2Trans : 1;     /* l2Trans - copy from HSA bit 129 */
    uint16 ph_srcExtPortNum : 2; /* Both in RX & TX. Source extension port number. */
    uint16 ph_type : 3;
#define ph_proto ph_type
#define PKTHDR_ETHERNET 0
    uint16 ph_vlanTagged : 1; /* the tag status after ALE */
    uint16 ph_LLCTagged : 1;  /* the tag status after ALE */
    uint16 ph_pppeTagged : 1; /* the tag status after ALE */
    uint16 ph_pppoeIdx : 3;
    uint16 ph_linkID : 7; /* for WLAN WDS multiple tunnel */
    uint16 ph_reason;    /* indicates what the packet is received by CPU */
    uint16 ph_flags;     /* packet header status bits */
#define PKTHDR_FREE (BUF_FREE << 8)
#define PKTHDR_USED (BUF_USED << 8)
#define PKTHDR_ASICHOLD (BUF_ASICHOLD << 8)
#define PKTHDR_DRIVERHOLD (BUF_DRIVERHOLD << 8)
#define PKTHDR_CPU_OWNED 0x4000
#define PKT_INCOMING 0x1000
#define PKT_OUTGOING 0x0800
#define PKT_BCAST 0x0100
#define PKT_MCAST 0x0080
#define PKTHDR_BRIDGING 0x0040
#define PKTHDR_HWLOOKUP 0x0020
#define PKTHDR_PPPOE_AUTOADD 0x0004
#define CSUM_TCPUDP_OK 0x0001
#define CSUM_IP_OK 0x0002
    uint8 ph_orgtos;   /* RX: original TOS */
    uint8 ph_portlist; /* RX: source port number, TX: destination portmask */
    uint16 ph_vlanId_resv : 1;
    uint16 ph_txPriority : 3;
    uint16 ph_vlanId : 12;
    union {
        uint16 _flags2; /* RX/TX flags2 */
        struct {
            uint16 _reserved : 1;
            uint16 _rxPktPriority : 3;
            uint16 _svlanId : 12;
        } _rx;
        struct {
            uint16 _reserved : 10;
            uint16 _txCVlanTagAutoAdd : 6;
        } _tx;
    } _flags2;
#define ph_flags2 _flags2._flags2
#define ph_svlanId _flags2._rx._svlanId
#define ph_rxPktPriority _flags2._rx._rxPktPriority
#define ph_txCVlanTagAutoAdd _flags2._tx._txCVlanTagAutoAdd
    uint8 ph_ptpResv : 1;
    uint8 ph_ptpMsgType : 4; /* message type */
    uint8 ph_ptpVer : 2;     /* PTP version */
    uint8 ph_ptpPkt : 1;     /* 1: PTP */
    int8 ph_reserved[3];     /* padding */

    uint32 pending0; /* for cache line alignment */
    uint32 pending1;
};

/* =============================================================================
 * BSD-style Queue Macros (subset from rtl_queue.h)
 * =============================================================================
 */

/* Singly-linked List definitions. */
#define SLIST_HEAD(name, type)                      \
    struct name {                                   \
        struct type *slh_first; /* first element */ \
    }

#define SLIST_HEAD_INITIALIZER(head) { NULL }

#define SLIST_ENTRY(type)                       \
    struct {                                    \
        struct type *sle_next; /* next element */ \
    }

#define SLIST_EMPTY(head) ((head)->slh_first == NULL)
#define SLIST_FIRST(head) ((head)->slh_first)
#define SLIST_FOREACH(var, head, field)                                  \
    for ((var) = (head)->slh_first; (var); (var) = (var)->field.sle_next)

#define SLIST_INIT(head)    \
    {                       \
        (head)->slh_first = NULL; \
    }

#define SLIST_INSERT_AFTER(slistelm, elm, field)         \
    do {                                                 \
        (elm)->field.sle_next = (slistelm)->field.sle_next; \
        (slistelm)->field.sle_next = (elm);              \
    } while (0)

#define SLIST_INSERT_HEAD(head, elm, field)          \
    do {                                             \
        (elm)->field.sle_next = (head)->slh_first;   \
        (head)->slh_first = (elm);                   \
    } while (0)

#define SLIST_NEXT(elm, field) ((elm)->field.sle_next)

#define SLIST_REMOVE_HEAD(head, field)              \
    do {                                            \
        (head)->slh_first = (head)->slh_first->field.sle_next; \
    } while (0)

#define SLIST_REMOVE(head, elm, type, field)                \
    do {                                                    \
        if ((head)->slh_first == (elm)) {                   \
            SLIST_REMOVE_HEAD((head), field);               \
        } else {                                            \
            struct type *curelm = (head)->slh_first;        \
            while (curelm->field.sle_next != (elm))         \
                curelm = curelm->field.sle_next;            \
            curelm->field.sle_next =                        \
                curelm->field.sle_next->field.sle_next;     \
        }                                                   \
    } while (0)

/* Tail queue definitions. */
#define TAILQ_HEAD(name, type)                             \
    struct name {                                          \
        struct type *tqh_first; /* first element */         \
        struct type **tqh_last; /* addr of last next element */ \
    }

#define TAILQ_HEAD_INITIALIZER(head) { NULL, &(head).tqh_first }

#define TAILQ_ENTRY(type)                                  \
    struct {                                               \
        struct type *tqe_next; /* next element */           \
        struct type **tqe_prev; /* addr of previous next element */ \
    }

#define TAILQ_EMPTY(head) ((head)->tqh_first == NULL)
#define TAILQ_FIRST(head) ((head)->tqh_first)
#define TAILQ_LAST(head, headname) (*(((struct headname *)((head)->tqh_last))->tqh_last))
#define TAILQ_NEXT(elm, field) ((elm)->field.tqe_next)
#define TAILQ_PREV(elm, headname, field) (*(((struct headname *)((elm)->field.tqe_prev))->tqh_last))

#define TAILQ_INIT(head)                                   \
    do {                                                   \
        (head)->tqh_first = NULL;                          \
        (head)->tqh_last = &(head)->tqh_first;             \
    } while (0)

#define TAILQ_INSERT_HEAD(head, elm, field)                \
    do {                                                   \
        if (((elm)->field.tqe_next = (head)->tqh_first) != NULL) \
            (head)->tqh_first->field.tqe_prev =            \
                &(elm)->field.tqe_next;                    \
        else                                               \
            (head)->tqh_last = &(elm)->field.tqe_next;     \
        (head)->tqh_first = (elm);                         \
        (elm)->field.tqe_prev = &(head)->tqh_first;        \
    } while (0)

#define TAILQ_INSERT_TAIL(head, elm, field)                \
    do {                                                   \
        (elm)->field.tqe_next = NULL;                      \
        (elm)->field.tqe_prev = (head)->tqh_last;          \
        *(head)->tqh_last = (elm);                         \
        (head)->tqh_last = &(elm)->field.tqe_next;         \
    } while (0)

#define TAILQ_INSERT_AFTER(head, listelm, elm, field)      \
    do {                                                   \
        if (((elm)->field.tqe_next = (listelm)->field.tqe_next) != NULL) \
            (elm)->field.tqe_next->field.tqe_prev =        \
                &(elm)->field.tqe_next;                    \
        else                                               \
            (head)->tqh_last = &(elm)->field.tqe_next;     \
        (listelm)->field.tqe_next = (elm);                 \
        (elm)->field.tqe_prev = &(listelm)->field.tqe_next; \
    } while (0)

#define TAILQ_INSERT_BEFORE(listelm, elm, field)           \
    do {                                                   \
        (elm)->field.tqe_prev = (listelm)->field.tqe_prev; \
        (elm)->field.tqe_next = (listelm);                 \
        *(listelm)->field.tqe_prev = (elm);                \
        (listelm)->field.tqe_prev = &(elm)->field.tqe_next; \
    } while (0)

#define TAILQ_REMOVE(head, elm, field)                     \
    do {                                                   \
        if (((elm)->field.tqe_next) != NULL)               \
            (elm)->field.tqe_next->field.tqe_prev =        \
                (elm)->field.tqe_prev;                     \
        else                                               \
            (head)->tqh_last = (elm)->field.tqe_prev;      \
        *(elm)->field.tqe_prev = (elm)->field.tqe_next;    \
    } while (0)

#define TAILQ_FOREACH(var, head, field)                    \
    for ((var) = TAILQ_FIRST(head); (var); (var) = TAILQ_NEXT(var, field))

#define TAILQ_FOREACH_REVERSE(var, head, headname, field)  \
    for ((var) = TAILQ_LAST((head), headname); (var); (var) = TAILQ_PREV((var), headname, field))

/* Counted tail queue definitions (CTAILQ). */
#define CTAILQ_HEAD(name, type)                            \
    struct name {                                          \
        struct type *tqh_first; /* first element */         \
        struct type **tqh_last; /* addr of last next element */ \
        int tqh_count;                                     \
    }

#define CTAILQ_HEAD_INITIALIZER(head) { NULL, &(head).tqh_first, 0 }

#define CTAILQ_ENTRY(type)                                 \
    struct {                                               \
        struct type *tqe_next; /* next element */           \
        struct type **tqe_prev; /* addr of previous next element */ \
    }

#define CTAILQ_EMPTY(head) ((head)->tqh_first == NULL)
#define CTAILQ_FIRST(head) ((head)->tqh_first)
#define CTAILQ_TOTAL(head) ((head)->tqh_count)
#define CTAILQ_LAST(head, headname) (*(((struct headname *)((head)->tqh_last))->tqh_last))
#define CTAILQ_NEXT(elm, field) ((elm)->field.tqe_next)
#define CTAILQ_PREV(elm, headname, field) (*(((struct headname *)((elm)->field.tqe_prev))->tqh_last))

#define CTAILQ_INIT(head)                                  \
    do {                                                   \
        (head)->tqh_first = NULL;                          \
        (head)->tqh_last = &(head)->tqh_first;             \
        (head)->tqh_count = 0;                             \
    } while (0)

#define CTAILQ_INSERT_HEAD(head, elm, field)               \
    do {                                                   \
        if (((elm)->field.tqe_next = (head)->tqh_first) != NULL) \
            (head)->tqh_first->field.tqe_prev =            \
                &(elm)->field.tqe_next;                    \
        else                                               \
            (head)->tqh_last = &(elm)->field.tqe_next;     \
        (head)->tqh_first = (elm);                         \
        (elm)->field.tqe_prev = &(head)->tqh_first;        \
        (head)->tqh_count++;                               \
    } while (0)

#define CTAILQ_INSERT_TAIL(head, elm, field)               \
    do {                                                   \
        (elm)->field.tqe_next = NULL;                      \
        (elm)->field.tqe_prev = (head)->tqh_last;          \
        *(head)->tqh_last = (elm);                         \
        (head)->tqh_last = &(elm)->field.tqe_next;         \
        (head)->tqh_count++;                               \
    } while (0)

#define CTAILQ_INSERT_AFTER(head, listelm, elm, field)     \
    do {                                                   \
        if (((elm)->field.tqe_next = (listelm)->field.tqe_next) != NULL) \
            (elm)->field.tqe_next->field.tqe_prev =        \
                &(elm)->field.tqe_next;                    \
        else                                               \
            (head)->tqh_last = &(elm)->field.tqe_next;     \
        (listelm)->field.tqe_next = (elm);                 \
        (elm)->field.tqe_prev = &(listelm)->field.tqe_next; \
        (head)->tqh_count++;                               \
    } while (0)

#define CTAILQ_INSERT_BEFORE(head, listelm, elm, field)    \
    do {                                                   \
        (elm)->field.tqe_prev = (listelm)->field.tqe_prev; \
        (elm)->field.tqe_next = (listelm);                 \
        *(listelm)->field.tqe_prev = (elm);                \
        (listelm)->field.tqe_prev = &(elm)->field.tqe_next; \
        (head)->tqh_count++;                               \
    } while (0)

#define CTAILQ_REMOVE(head, elm, field)                    \
    do {                                                   \
        if (((elm)->field.tqe_next) != NULL)               \
            (elm)->field.tqe_next->field.tqe_prev =        \
                (elm)->field.tqe_prev;                     \
        else                                               \
            (head)->tqh_last = (elm)->field.tqe_prev;      \
        *(elm)->field.tqe_prev = (elm)->field.tqe_next;    \
        (head)->tqh_count--;                               \
    } while (0)

#define CTAILQ_FOREACH(var, head, field)                   \
    for ((var) = TAILQ_FIRST(head); (var); (var) = TAILQ_NEXT(var, field))

#define CTAILQ_FOREACH_REVERSE(var, head, headname, field) \
    for ((var) = TAILQ_LAST((head), headname); (var); (var) = TAILQ_PREV((var), headname, field))

/* =============================================================================
 * FDB API (from rtl865x_fdb_api.h)
 * =============================================================================
 */
#define RTL_LAN_FID 0
#define RTL_WAN_FID 0
#define FDB_STATIC 0x01
#define FDB_DYNAMIC 0x02
#define RTL865x_FDB_NUMBER 4
#define RTL865x_L2_TYPEI 0x0001
#define RTL865x_L2_TYPEII 0x0002
#define RTL865x_L2_TYPEIII 0x0004

/* =============================================================================
 * Netif and ACL Definitions (from rtl865x_netif.h)
 * =============================================================================
 */
#define IF_NONE 0
#define IF_ETHER 1
#define IF_PPPOE 2
#define IF_PPTP 3
#define IF_L2TP 4

#define RTL865X_ACL_USER_USED 0

typedef struct _rtl865x_AclRule_s {
    union {
        struct {
            ether_addr_t _dstMac, _dstMacMask;
            ether_addr_t _srcMac, _srcMacMask;
            uint16 _typeLen, _typeLenMask;
        } MAC;
        struct {
            ipaddr_t _srcIpAddr, _srcIpAddrMask;
            ipaddr_t _dstIpAddr, _dstIpAddrMask;
            uint8 _tos, _tosMask;
            union {
                struct {
                    uint8 _proto, _protoMask, _flagMask;
                    uint32 _FOP : 1, _FOM : 1, _httpFilter : 1, _httpFilterM : 1,
                        _identSrcDstIp : 1, _identSrcDstIpM : 1;
                    union {
                        uint8 _flag;
                        struct {
                            uint8 pend1 : 5, pend2 : 1, _DF : 1, _MF : 1;
                        } s;
                    } un;
                } ip;
                struct {
                    uint8 _type, _typeMask, _code, _codeMask;
                } icmp;
                struct {
                    uint8 _type, _typeMask;
                } igmp;
                struct {
                    uint8 _flagMask;
                    uint16 _srcPortUpperBound, _srcPortLowerBound;
                    uint16 _dstPortUpperBound, _dstPortLowerBound;
                    union {
                        uint8 _flag;
                        struct {
                            uint8 _pend : 2, _urg : 1, _ack : 1, _psh : 1, _rst : 1, _syn : 1, _fin : 1;
                        } s;
                    } un;
                } tcp;
                struct {
                    uint16 _srcPortUpperBound, _srcPortLowerBound;
                    uint16 _dstPortUpperBound, _dstPortLowerBound;
                } udp;
            } is;
        } L3L4;
        struct {
            ether_addr_t _srcMac, _srcMacMask;
            uint16 _srcPort, _srcPortMask;
            uint16 _srcVlanIdx, _srcVlanIdxMask;
            ipaddr_t _srcIpAddr, _srcIpAddrMask;
            uint16 _srcPortUpperBound, _srcPortLowerBound;
            uint32 _ignoreL4 : 1, _ignoreL3L4 : 1;
        } SRCFILTER;
        struct {
            ether_addr_t _dstMac, _dstMacMask;
            uint16 _vlanIdx, _vlanIdxMask;
            ipaddr_t _dstIpAddr, _dstIpAddrMask;
            uint16 _dstPortUpperBound, _dstPortLowerBound;
            uint32 _ignoreL4 : 1, _ignoreL3L4 : 1;
        } DSTFILTER;
    } un_ty;

    uint32 ruleType_ : 5,
        actionType_ : 4,
        pktOpApp_ : 3,
        priority_ : 3,
        direction_ : 2,
        nexthopIdx_ : 5,
        ratelimtIdx_ : 4;

    uint32 netifIdx_ : 3,
        pppoeIdx_ : 3,
        L2Idx_ : 10,
        inv_flag : 8,
        aclIdx : 7;

    struct _rtl865x_AclRule_s *pre, *next;

} rtl865x_AclRule_t;

#define dstMac_ un_ty.MAC._dstMac
#define dstMacMask_ un_ty.MAC._dstMacMask
#define srcMac_ un_ty.MAC._srcMac
#define srcMacMask_ un_ty.MAC._srcMacMask
#define typeLen_ un_ty.MAC._typeLen
#define typeLenMask_ un_ty.MAC._typeLenMask

#define srcIpAddr_ un_ty.L3L4._srcIpAddr
#define srcIpAddrMask_ un_ty.L3L4._srcIpAddrMask
#define srcIpAddrUB_ un_ty.L3L4._srcIpAddr
#define srcIpAddrLB_ un_ty.L3L4._srcIpAddrMask
#define dstIpAddr_ un_ty.L3L4._dstIpAddr
#define dstIpAddrMask_ un_ty.L3L4._dstIpAddrMask
#define dstIpAddrUB_ un_ty.L3L4._dstIpAddr
#define dstIpAddrLB_ un_ty.L3L4._dstIpAddrMask
#define tos_ un_ty.L3L4._tos
#define tosMask_ un_ty.L3L4._tosMask
#define srcIpAddrStart_ un_ty.L3L4._srcIpAddrMask
#define srcIpAddrEnd_ un_ty.L3L4._srcIpAddr
#define dstIpAddrStart_ un_ty.L3L4._dstIpAddrMask
#define dstIpAddrEnd_ un_ty.L3L4._dstIpAddr

#define ipProto_ un_ty.L3L4.is.ip._proto
#define ipProtoMask_ un_ty.L3L4.is.ip._protoMask
#define ipFlagMask_ un_ty.L3L4.is.ip._flagMask
#define ipFOP_ un_ty.L3L4.is.ip._FOP
#define ipFOM_ un_ty.L3L4.is.ip._FOM
#define ipHttpFilter_ un_ty.L3L4.is.ip._httpFilter
#define ipHttpFilterM_ un_ty.L3L4.is.ip._httpFilterM
#define ipIdentSrcDstIp_ un_ty.L3L4.is.ip._identSrcDstIp
#define ipIdentSrcDstIpM_ un_ty.L3L4.is.ip._identSrcDstIpM
#define ipFlag_ un_ty.L3L4.is.ip.un._flag
#define ipDF_ un_ty.L3L4.is.ip.un.s._DF
#define ipMF_ un_ty.L3L4.is.ip.un.s._MF

#define icmpType_ un_ty.L3L4.is.icmp._type
#define icmpTypeMask_ un_ty.L3L4.is.icmp._typeMask
#define icmpCode_ un_ty.L3L4.is.icmp._code
#define icmpCodeMask_ un_ty.L3L4.is.icmp._codeMask

#define igmpType_ un_ty.L3L4.is.igmp._type
#define igmpTypeMask_ un_ty.L3L4.is.igmp._typeMask

#define tcpSrcPortUB_ un_ty.L3L4.is.tcp._srcPortUpperBound
#define tcpSrcPortLB_ un_ty.L3L4.is.tcp._srcPortLowerBound
#define tcpDstPortUB_ un_ty.L3L4.is.tcp._dstPortUpperBound
#define tcpDstPortLB_ un_ty.L3L4.is.tcp._dstPortLowerBound
#define tcpFlagMask_ un_ty.L3L4.is.tcp._flagMask
#define tcpFlag_ un_ty.L3L4.is.tcp.un._flag
#define tcpURG_ un_ty.L3L4.is.tcp.un.s._urg
#define tcpACK_ un_ty.L3L4.is.tcp.un.s._ack
#define tcpPSH_ un_ty.L3L4.is.tcp.un.s._psh
#define tcpRST_ un_ty.L3L4.is.tcp.un.s._rst
#define tcpSYN_ un_ty.L3L4.is.tcp.un.s._syn
#define tcpFIN_ un_ty.L3L4.is.tcp.un.s._fin

#define udpSrcPortUB_ un_ty.L3L4.is.udp._srcPortUpperBound
#define udpSrcPortLB_ un_ty.L3L4.is.udp._srcPortLowerBound
#define udpDstPortUB_ un_ty.L3L4.is.udp._dstPortUpperBound
#define udpDstPortLB_ un_ty.L3L4.is.udp._dstPortLowerBound

#define srcFilterMac_ un_ty.SRCFILTER._srcMac
#define srcFilterMacMask_ un_ty.SRCFILTER._srcMacMask
#define srcFilterPort_ un_ty.SRCFILTER._srcPort
#define srcFilterPortMask_ un_ty.SRCFILTER._srcPortMask
#define srcFilterVlanIdx_ un_ty.SRCFILTER._srcVlanIdx
#define srcFilterVlanId_ un_ty.SRCFILTER._srcVlanIdx
#define srcFilterVlanIdxMask_ un_ty.SRCFILTER._srcVlanIdxMask
#define srcFilterVlanIdMask_ un_ty.SRCFILTER._srcVlanIdxMask
#define srcFilterIpAddr_ un_ty.SRCFILTER._srcIpAddr
#define srcFilterIpAddrMask_ un_ty.SRCFILTER._srcIpAddrMask
#define srcFilterIpAddrUB_ un_ty.SRCFILTER._srcIpAddr
#define srcFilterIpAddrLB_ un_ty.SRCFILTER._srcIpAddrMask
#define srcFilterPortUpperBound_ un_ty.SRCFILTER._srcPortUpperBound
#define srcFilterPortLowerBound_ un_ty.SRCFILTER._srcPortLowerBound
#define srcFilterIgnoreL3L4_ un_ty.SRCFILTER._ignoreL3L4
#define srcFilterIgnoreL4_ un_ty.SRCFILTER._ignoreL4

#define dstFilterMac_ un_ty.DSTFILTER._dstMac
#define dstFilterMacMask_ un_ty.DSTFILTER._dstMacMask
#define dstFilterVlanIdx_ un_ty.DSTFILTER._vlanIdx
#define dstFilterVlanIdxMask_ un_ty.DSTFILTER._vlanIdxMask
#define dstFilterVlanId_ un_ty.DSTFILTER._vlanIdx
#define dstFilterVlanIdMask_ un_ty.DSTFILTER._vlanIdxMask
#define dstFilterIpAddr_ un_ty.DSTFILTER._dstIpAddr
#define dstFilterIpAddrMask_ un_ty.DSTFILTER._dstIpAddrMask
#define dstFilterPortUpperBound_ un_ty.DSTFILTER._dstPortUpperBound
#define dstFilterIpAddrUB_ un_ty.DSTFILTER._dstIpAddr
#define dstFilterIpAddrLB_ un_ty.DSTFILTER._dstIpAddrMask
#define dstFilterPortLowerBound_ un_ty.DSTFILTER._dstPortLowerBound
#define dstFilterIgnoreL3L4_ un_ty.DSTFILTER._ignoreL3L4
#define dstFilterIgnoreL4_ un_ty.DSTFILTER._ignoreL4

#define RTL865X_ACL_PERMIT 0x00
#define RTL865X_ACL_REDIRECT_ETHER 0x01
#define RTL865X_ACL_DROP 0x02
#define RTL865X_ACL_TOCPU 0x03
#define RTL865X_ACL_LEGACY_DROP 0x04
#define RTL865X_ACL_DROPCPU_LOG 0x05
#define RTL865X_ACL_MIRROR 0x06
#define RTL865X_ACL_REDIRECT_PPPOE 0x07
#define RTL865X_ACL_DEFAULT_REDIRECT 0x08
#define RTL865X_ACL_MIRROR_KEEP_MATCH 0x09
#define RTL865X_ACL_DROP_RATE_EXCEED_PPS 0x0a
#define RTL865X_ACL_LOG_RATE_EXCEED_PPS 0x0b
#define RTL865X_ACL_DROP_RATE_EXCEED_BPS 0x0c
#define RTL865X_ACL_LOG_RATE_EXCEED_BPS 0x0d
#define RTL865X_ACL_PRIORITY 0x0e

#define RTL865X_ACL_MAC 0x00
#define RTL865X_ACL_DSTFILTER_IPRANGE 0x01
#define RTL865X_ACL_IP 0x02
#define RTL865X_ACL_ICMP 0x04
#define RTL865X_ACL_IGMP 0x05
#define RTL865X_ACL_TCP 0x06
#define RTL865X_ACL_UDP 0x07
#define RTL865X_ACL_SRCFILTER 0x08
#define RTL865X_ACL_DSTFILTER 0x09
#define RTL865X_ACL_IP_RANGE 0x0A
#define RTL865X_ACL_SRCFILTER_IPRANGE 0x0B
#define RTL865X_ACL_ICMP_IPRANGE 0x0C
#define RTL865X_ACL_IGMP_IPRANGE 0x0D
#define RTL865X_ACL_TCP_IPRANGE 0x0E
#define RTL865X_ACL_UDP_IPRANGE 0x0F

#define RTL865X_ACL_ALL_LAYER 7

#define RTL865X_ACL_MAX_NUMBER 125
#define RTL865X_ACL_RESERVED_NUMBER 3

#define RTL865X_ACLTBL_ALL_TO_CPU 127
#define RTL865X_ACLTBL_DROP_ALL 126
#define RTL865X_ACLTBL_PERMIT_ALL 125
#define RTL865X_ACLTBL_IPV6_TO_CPU 124

#define MAX_IFNAMESIZE 16
#define NETIF_NUMBER 8

#define RTL865X_ACL_INGRESS 0
#define RTL865X_ACL_EGRESS 1

#define RTL_DEV_NAME_NUM(name, num) name #num

#define RTL_BR_NAME "br0"
#define RTL_WLAN_NAME "wlan"

#define RTL_DRV_LAN_NETIF_NAME "eth0"
#define RTL_DRV_WAN0_NETIF_NAME "eth1"

#define RTL_WANVLANID 8
#define RTL_LANVLANID 9
#define RTL_WANPORT_MASK 0x10
#define RTL_LANPORT_MASK 0x10f

#define ETH_INTF_NUM 1

typedef struct rtl865x_netif_s {
    uint16 vid;
    uint16 mtu;
    uint32 if_type : 5;
    ether_addr_t macAddr;
    uint32 is_wan : 1,
        dmz : 1,
        is_slave : 1;
    uint8 name[MAX_IFNAMESIZE];
    uint16 enableRoute;
} rtl865x_netif_t;

int32 rtl865x_regist_aclChain(char *netifName, int32 priority, uint32 flag);
int32 rtl865x_initNetifTable(void);
int32 rtl865x_addNetif(rtl865x_netif_t *netif);
int32 rtl865x_attachMasterNetif(char *slave, char *master);
int32 rtl865x_setNetifMac(rtl865x_netif_t *netif);
int32 rtl865x_setNetifMtu(rtl865x_netif_t *netif);

/* =============================================================================
 * ASIC Basic (from rtl865x_asicBasic.h, minimal)
 * =============================================================================
 */

enum {
    TYPE_L2_SWITCH_TABLE = 0,
    TYPE_ARP_TABLE,
    TYPE_L3_ROUTING_TABLE,
    TYPE_MULTICAST_TABLE,
    TYPE_NETINTERFACE_TABLE,
    TYPE_EXT_INT_IP_TABLE,
    TYPE_VLAN_TABLE,
    TYPE_VLAN1_TABLE,
    TYPE_SERVER_PORT_TABLE,
    TYPE_L4_TCP_UDP_TABLE,
    TYPE_L4_ICMP_TABLE,
    TYPE_PPPOE_TABLE,
    TYPE_ACL_RULE_TABLE,
    TYPE_NEXT_HOP_TABLE,
    TYPE_RATE_LIMIT_TABLE,
    TYPE_ALG_TABLE,
};

#define RTL8651_RATELIMITTBL_SIZE 32

extern int8 RtkHomeGatewayChipName[16];
extern int32 RtkHomeGatewayChipNameID;
extern int32 RtkHomeGatewayChipRevisionID;

int32 _rtl8651_forceAddAsicEntry(uint32 tableType, uint32 eidx, void *entryContent_P);
int32 _rtl8651_readAsicEntry(uint32 tableType, uint32 eidx, void *entryContent_P);

/* =============================================================================
 * HW Patch Macros (from rtl865x_hwPatch.h, minimal)
 * =============================================================================
 */
#define RTL865X_CHIP_VER_RTL865XB 0x01
#define RTL865X_CHIP_VER_RTL865XC 0x02
#define RTL865X_CHIP_VER_RTL8196B 0x03
#define RTL865X_CHIP_VER_RTL8196C 0x04

#define RTL865X_CHIP_REV_A 0x00
#define RTL865X_CHIP_REV_B 0x01
#define RTL865X_CHIP_REV_C 0x02
#define RTL865X_CHIP_REV_D 0x03
#define RTL865X_CHIP_REV_E 0x04

#define RTL865X_PHY6_DSP_BUG ((RtkHomeGatewayChipNameID == RTL865X_CHIP_VER_RTL865XC) && \
                              (RtkHomeGatewayChipRevisionID == RTL865X_CHIP_REV_A))

#define RTL865X_IQFCTCR_DEFAULT_VALUE_BUG ((RtkHomeGatewayChipNameID == RTL865X_CHIP_VER_RTL865XC) || \
                                           (RtkHomeGatewayChipNameID == RTL865X_CHIP_VER_RTL8196B))

/* =============================================================================
 * Error Codes (from rtl_errno.h, minimal)
 * =============================================================================
 */
#define RTL_EENTRYALREADYEXIST -2
#define RTL_EENTRYNOTFOUND -3
#define RTL_EINVALIDVLANID -5
#define RTL_EINVALIDINPUT -6
#define RTL_ENOFREEBUFFER -9
#define RTL_EINVALIDFID -1800
#define RTL_EVLANALREADYEXISTS -2000
#define RTL_ENETIFINVALID -2601
#define RTL_EREFERENCEDBYOTHER -5200

/* =============================================================================
 * BSP Compatibility (from bspchip.h, minimal)
 * =============================================================================
 */
#define BSP_SW_IE (1 << 15)

/* =============================================================================
 * ASIC Registers
 * =============================================================================
 */
#include "rtl865xc_asicregs.h"

#endif /* RTL819X_H */
