/*
 * RTL865x ASIC L2 API
 * Copyright (c) 2009 Realtek Semiconductor Corporation
 * Author: hyking (hyking_liu@realsil.com.cn)
 * Adapted for Linux 5.10 & RTL8196E: Jacques Nilo (2025)
 *
 * Layer 2 ASIC function prototypes (PHY/MII/Port/STP/QoS).
 *
 * SPDX-License-Identifier: GPL-2.0
 */
 
#ifndef RTL865X_ASICL2_H
#define RTL865X_ASICL2_H

// Use sw gpio to control led of Port0~Port4
//#define PATCH_GPIO_FOR_LED		1

#define RTL865XC_QM_DESC_READROBUSTPARAMETER	10

 #define RTL8651_MII_PORTNUMBER                 	5
#define RTL8651_MII_PORTMASK                    	0x20
#define RTL8651_PHY_NUMBER				5

#define RTL8651_ETHER_AUTO_100FULL	0x00
#define RTL8651_ETHER_AUTO_100HALF	0x01
#define RTL8651_ETHER_AUTO_10FULL	0x02
#define RTL8651_ETHER_AUTO_10HALF	0x03
#define RTL8651_ETHER_AUTO_1000FULL	0x08
#define RTL8651_ETHER_AUTO_1000HALF	0x09
/* chhuang: patch for priority issue */
#define RTL8651_ETHER_FORCE_100FULL	0x04
#define RTL8651_ETHER_FORCE_100HALF	0x05
#define RTL8651_ETHER_FORCE_10FULL	0x06
#define RTL8651_ETHER_FORCE_10HALF	0x07


typedef struct rtl865xC_outputQueuePara_s {

	uint32	ifg;							/* default: Bandwidth Control Include/exclude Preamble & IFG */
	uint32	gap;							/* default: Per Queue Physical Length Gap = 20 */
	uint32	drop;						/* default: Descriptor Run Out Threshold = 500 */

	uint32	systemSBFCOFF;				/*System shared buffer flow control turn off threshold*/
	uint32	systemSBFCON;				/*System shared buffer flow control turn on threshold*/

	uint32	systemFCOFF;				/* system flow control turn off threshold */
	uint32	systemFCON;					/* system flow control turn on threshold */

	uint32	portFCOFF;					/* port base flow control turn off threshold */
	uint32	portFCON;					/* port base flow control turn on threshold */	

	uint32	queueDescFCOFF;				/* Queue-Descriptor=Based Flow Control turn off Threshold  */
	uint32	queueDescFCON;				/* Queue-Descriptor=Based Flow Control turn on Threshold  */

	uint32	queuePktFCOFF;				/* Queue-Packet=Based Flow Control turn off Threshold  */
	uint32	queuePktFCON;				/* Queue-Packet=Based Flow Control turn on Threshold  */
}	rtl865xC_outputQueuePara_t;


/*enum for duplex and speed*/
enum 
{
	PORT_DOWN=0,
	HALF_DUPLEX_10M,
	HALF_DUPLEX_100M,
	HALF_DUPLEX_1000M,
	DUPLEX_10M,
	DUPLEX_100M,
	DUPLEX_1000M,
	PORT_AUTO,
	PORT_UP
};

/* enum for port ID */
enum PORTID
{
	PHY0 = 0,
	PHY1 = 1,
	PHY2 = 2,
	PHY3 = 3,
	PHY4 = 4,
	PHY5 = 5,
	CPU = 6,
	EXT1 = 7,
	EXT2 = 8,
	EXT3 = 9,
	MULTEXT = 10,
};
enum GROUP
{
	GR0 = 0,
	GR1 = 1,
	GR2 = 2,
};

/* enum for queue ID */
enum QUEUEID
{
	QUEUE0 = 0,
	QUEUE1,
	QUEUE2,
	QUEUE3,
	QUEUE4,
	QUEUE5,
};

/* enum for queue type */
enum QUEUETYPE
{
	STR_PRIO = 0,
	WFQ_PRIO,
};

/* enum for output queue number */
enum QUEUENUM
{
	QNUM1 = 1,
	QNUM2,
	QNUM3,
	QNUM4,
	QNUM5,
	QNUM6,
};

/* enum for priority value type */
enum PRIORITYVALUE
{
	PRI0 = 0,
	PRI1,
	PRI2,
	PRI3,
	PRI4,
	PRI5,
	PRI6,
	PRI7,
};

typedef struct {
    /* word 0 */
    uint16          mac39_24;
    uint16          mac23_8;

    /* word 1 */
    uint32          reserv0: 6;
    uint32          auth: 1;
    uint32          fid:2;
    uint32          nxtHostFlag : 1;
    uint32          srcBlock    : 1;
    uint32          agingTime   : 2;
    uint32          isStatic    : 1;
    uint32          toCPU       : 1;
    uint32          extMemberPort   : 3;
    uint32          memberPort : 6;
    uint32          mac47_40    : 8;

    /* word 2 */
    uint32          reservw2;
    /* word 3 */
    uint32          reservw3;
    /* word 4 */
    uint32          reservw4;
    /* word 5 */
    uint32          reservw5;
    /* word 6 */
    uint32          reservw6;
    /* word 7 */
    uint32          reservw7;
} rtl865xc_tblAsic_l2Table_t;



typedef struct rtl865x_tblAsicDrv_l2Param_s {
	ether_addr_t	macAddr;
	uint32 		memberPortMask; /*extension ports [rtl8651_totalExtPortNum-1:0] are located at bits [RTL8651_PORT_NUMBER+rtl8651_totalExtPortNum-1:RTL8651_PORT_NUMBER]*/
	uint32 		ageSec;
	uint32	 	cpu:1,
				srcBlk:1,
				isStatic:1,				
				nhFlag:1,
				fid:2,
				auth:1;

} rtl865x_tblAsicDrv_l2Param_t;

typedef struct rtl8651_tblAsic_ethernet_s {
	uint8	linkUp: 1,
			phyId: 5,
			isGPHY: 1;
} rtl8651_tblAsic_ethernet_t;


extern int32 rtl865x_maxPreAllocRxSkb;
extern int32 rtl865x_rxSkbPktHdrDescNum;
extern int32 rtl865x_txSkbPktHdrDescNum;

extern int32	rtl865x_wanPortMask;
extern int32	rtl865x_lanPortMask;


/* ========================================
 * Function Declarations (compiler-driven)
 * ======================================== */

/* L2 Table functions */
int32 rtl8651_setAsicL2Table(uint32 row, uint32 column, rtl865x_tblAsicDrv_l2Param_t *l2p);
int32 rtl8651_delAsicL2Table(uint32 row, uint32 column);
int32 rtl8651_getAsicL2Table(uint32 row, uint32 column, rtl865x_tblAsicDrv_l2Param_t *l2p);
int32 rtl8651_setAsicL2Table_Patch(uint32 row, uint32 column, ether_addr_t * mac, int8 cpu, 
		int8 srcBlk, uint32 mbr, uint32 ageSec, int8 isStatic, int8 nhFlag, int8 fid, int8 auth);
uint32 rtl8651_filterDbIndex(ether_addr_t * macAddr,uint16 fid);

/* Spanning Tree functions */
int32 rtl8651_setAsicSpanningEnable(int8 spanningTreeEnabled);
int32 rtl865xC_setAsicSpanningTreePortState(uint32 port, uint32 portState);
int32 rtl8651_setAsicMulticastSpanningTreePortState(uint32 port, uint32 portState);

/* Port Pattern Match */
int32 rtl8651_setAsicPortPatternMatch(uint32 port, uint32 pattern, uint32 patternMask, int32 operation);

/* PHY/Ethernet functions */
int32 rtl8651_restartAsicEthernetPHYNway(uint32 port);
int32 rtl8651_getAsicEthernetPHYReg(uint32 phyId, uint32 regId, uint32 *rData);
int32 rtl8651_setAsicEthernetPHYReg(uint32 phyId, uint32 regId, uint32 wData);
int32 rtl865xC_setAsicEthernetMIIMode(uint32 port, uint32 mode);
int32 rtl865xC_setAsicEthernetRGMIITiming(uint32 port, uint32 Tcomp, uint32 Rcomp);
int32 rtl8651_setAsicEthernetMII(uint32 phyAddress, int32 mode, int32 enabled);
int32 rtl8651_setAsicEthernetLinkStatus(uint32 port, int8 linkUp);

/* QoS/Priority functions */
int32 rtl8651_setAsicPriorityDecision( uint32 portpri, uint32 dot1qpri, uint32 dscppri, uint32 aclpri, uint32 natpri );
int32 rtl8651_setAsicLBParameter( uint32 token, uint32 tick, uint32 hiThreshold );
int32 rtl8651_getAsicLBParameter( uint32* pToken, uint32* pTick, uint32* pHiThreshold );

/* Flow Control / Bandwidth functions */
int32 rtl865xC_setAsicPortPauseFlowControl(uint32 port, uint8 rxEn, uint8 txEn);
int32 rtl8651_setAsicEthernetBandwidthControl(uint32 port, int8 input, uint32 rate);
int32 rtl8651_setAsicFlowControlRegister(uint32 port, uint32 enable);
int32 rtl8651_setAsicSystemInputFlowControlRegister(uint32 fcON, uint32 fcOFF);

/* Initialization functions (added for 5.4 migration) */
int32 rtl865x_initAsicL2(rtl8651_tblAsic_InitPara_t *para);
int32 rtl8651_setAsicOutputQueueNumber(uint32 port, uint32 qnum);
int32 rtl8651_setAsicPvid(uint32 port, uint32 pvid);
int32 rtl865x_layer2_init(void);

#endif
