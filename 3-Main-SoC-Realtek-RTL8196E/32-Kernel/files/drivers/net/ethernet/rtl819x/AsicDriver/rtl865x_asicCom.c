/*
 * RTL865x ASIC Common Functions
 * Copyright (c) 2011 Realtek Semiconductor Corporation
 * Author: hyking (hyking_liu@realsil.com.cn)
 * Adapted for Linux 5.10 & RTL8196E: Jacques Nilo (2025)
 *
 * ASIC register access and common hardware operations.
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/delay.h>
#include "rtl819x.h"
#include "rtl865x_asicCom.h"

/* RTL865xC ASIC regs: local-only defines (trimmed from rtl865xc_asicregs.h) */
#define MIB_COUNTER_BASE (SWCORE_BASE + 0x00001000)

#define MIB_CONTROL (0x00 + MIB_COUNTER_BASE) /*MIB control Register */

#define ALL_COUNTER_RESTART_MASK 0x0007ffff /* SYS_COUNTER_RESTART | IN_COUNTER_RESTART_P8 | ... | OUT_COUNTER_RESTART_P0 */

#define CRMR (0x08 + SWMISC_BASE)		/* Chip Revision Management Register */

#define SwitchFullRst (1 << 2) /* Reset all tables & queues */

#define FULL_RST SwitchFullRst /* Alias Name */

#define TEACR (0x00 + ALE_BASE)		 /* Table Entry Aging Control Register */

#define ALECR (0x0C + ALE_BASE)		 /* ALE Control Register */

#define PLITIMR (0x20 + ALE_BASE)	 /* Port to LAN Interface Table Index Mapping Register */

#define DACLRCR (0x24 + ALE_BASE)	 /* default ACL rule control register */

#define Fragment2CPU (1 << 15) /*	When ACL is enabled, enable all fragmented IP packet to be trapped to CPU (because L4 is needed)            \
								   When ACL is disabled, if this bit is set, L2 forwarding as before, L3 above operation will trapped to CPU. \
								   When ACL is disabled, if this bit is not set, IP fragment packet will be forwarded as normal via L3 routing or NAT. */

#define FRAG2CPU Fragment2CPU

#define Ingress_ACL (1 << 4)  /* Enable Ingress ACL. 0: disable, 1: enable */

#define Egress_ACL (1 << 3)	  /* Enable Egress ACL. 0: disable, 1: enable */

#define Mode_enL2 (1 << 0)	  /* Enable L2 */

#define Mode_enL3 (1 << 1)	  /* Enable L3 */

#define Mode_enL4 (1 << 2)	  /* Enable L4 */

#define EN_IN_ACL Ingress_ACL /* Alias Name */

#define EN_OUT_ACL Egress_ACL /* Alias Name */

#define EN_L4 Mode_enL4		  /* Alias Name */

#define EN_L3 Mode_enL3		  /* Alias Name */

#define EN_L2 Mode_enL2		  /* Alias Name */

#define LIMDBC_PORT (1 << 16)							 /* By Port base */

#define LIMDBC_MAC (2 << 16)							 /* By MAC base */

#define IBCR1 (0x08 + OQNCR_BASE)		  /* Ingress Bandwidth Control Register 1 */

#define IBCR2 (0x0C + OQNCR_BASE)		  /* Ingress Bandwidth Control Register 2 */

#define PVCR0 (0x08 + 0x4A00 + SWCORE_BASE)	  /* port base control register*/

#define MISC_BASE (SWCORE_BASE + 0x00007000)

#define FCREN (0x014 + MISC_BASE)							   /* Flow control enable control */

#define PCI_CTRL_BASE (SYSTEM_BASE + 0x3400) /* 0xB8003400 */

#define MCR_BASE (SYSTEM_BASE + 0x1000) /* 0xB8001000 */

#define MCR (0x000 + MCR_BASE)			/* Memory configuration register */

#define DCR (0x004 + MCR_BASE) /*	DRAM control register */

#define COLCNT_MASK (0x1c00000)

#define COLCNT_OFFSET 22

#define ROWCNT_MASK (0x6000000)

#define ROWCNT_OFFSET 25

#define CM_ACTIVE_SWCORE (1 << 11)

#define CM_PROTECT (1 << 27)


#define tick_Delay10ms(x)  \
	{                      \
		int i = x;         \
		while (i--)        \
			__delay(5000); \
	}

static int32 _rtl865xC_lockTLUCounter = 0;
static int32 _rtl865xC_lockTLUPHYREG[RTL8651_PORT_NUMBER + 1] = {0};
static int32 _rtl_inputBandWidth[RTL8651_PORT_NUMBER + 1] = {0};
static int32 _rtl_FCRegister = 0;
int32 rtl8651_totalExtPortNum = 0; // this replaces all RTL8651_EXTPORT_NUMBER defines
int32 rtl8651_allExtPortMask = 0;  // this replaces all RTL8651_EXTPORTMASK defines
rtl8651_tblAsic_InitPara_t rtl8651_tblAsicDrvPara;

int rtl8651_findAsicVlanIndexByVid(uint16 *vid)
{
	int i;
	rtl865x_tblAsicDrv_vlanParam_t vlan;

	for (i = 0; i < RTL865XC_VLANTBL_SIZE; i++)
	{
		if (rtl8651_getAsicVlan(i, &vlan) == SUCCESS)
		{
			if (*vid == vlan.vid)
			{
				*vid = i;
				return SUCCESS;
			}
		}
	}

	return FAILED;
}

static int rtl8651_getAsicVlanIndex(rtl865xc_tblAsic_vlanTable_t *entry, uint16 *vid)
{
	int i;
	int ret = FAILED;
	rtl865x_tblAsicDrv_vlanParam_t vlan;

	for (i = 0; i < RTL865XC_VLANTBL_SIZE; i++)
	{
		if ((rtl8651_getAsicVlan(i, &vlan) == SUCCESS) && (entry->vid == vlan.vid))
		{
			if ((entry->memberPort != vlan.memberPortMask) || (entry->egressUntag != vlan.untagPortMask) || (entry->fid != vlan.fid))
			{
				*vid = i;
				return SUCCESS;
			}
			else
			{
				return FAILED;
			}
		}
	}

	for (i = 0; i < RTL865XC_VLANTBL_SIZE; i++)
	{
		if (rtl8651_getAsicVlan(i, &vlan) == FAILED)
			break;
	}

	if (i == RTL865XC_VLANTBL_SIZE)
	{
		ret = FAILED; // vlan table is full
	}
	else
	{
		*vid = i;
		ret = SUCCESS;
	}

	return ret;
}

/*=========================================
 * ASIC DRIVER API: VLAN TABLE
 *=========================================*/
int32 rtl8651_setAsicVlan(uint16 vid, rtl865x_tblAsicDrv_vlanParam_t *vlanp)
{
	rtl865xc_tblAsic_vlanTable_t entry;
	int flag = FAILED;

	memset(&entry, 0, sizeof(entry));
	if (vlanp == NULL)
		return FAILED;
	if (vid >= 4096)
		return FAILED;
	if (vlanp->memberPortMask > RTL8651_PHYSICALPORTMASK)
		entry.extMemberPort = vlanp->memberPortMask >> RTL8651_PORT_NUMBER;
	if (vlanp->untagPortMask > RTL8651_PHYSICALPORTMASK)
		entry.extEgressUntag = vlanp->untagPortMask >> RTL8651_PORT_NUMBER;
	entry.memberPort = vlanp->memberPortMask & RTL8651_PHYSICALPORTMASK;
	entry.egressUntag = vlanp->untagPortMask & RTL8651_PHYSICALPORTMASK;
	entry.fid = vlanp->fid;

	entry.vid = vid;
	flag = rtl8651_getAsicVlanIndex(&entry, &vid);
	if (flag == FAILED)
		return FAILED;
	_rtl8651_forceAddAsicEntry(TYPE_VLAN_TABLE, vid, &entry);

	return SUCCESS;
}

int32 rtl8651_delAsicVlan(uint16 vid)
{
	rtl8651_tblAsic_vlanTable_t entry;
	int flag = FAILED;
	flag = rtl8651_findAsicVlanIndexByVid(&vid);
	if (flag == FAILED)
		return FAILED;
	memset(&entry, 0, sizeof(entry));
	entry.valid = 0;
	return _rtl8651_forceAddAsicEntry(TYPE_VLAN_TABLE, vid, &entry);
}

int32 rtl8651_getAsicVlan(uint16 vid, rtl865x_tblAsicDrv_vlanParam_t *vlanp)
{
	rtl865xc_tblAsic_vlanTable_t entry;
	if (vlanp == NULL || vid >= 4096)
		return FAILED;

	_rtl8651_readAsicEntry(TYPE_VLAN_TABLE, vid, &entry);
	if ((entry.extMemberPort | entry.memberPort) == 0)
	{
		return FAILED;
	}
	vlanp->memberPortMask = (entry.extMemberPort << RTL8651_PORT_NUMBER) | entry.memberPort;
	vlanp->untagPortMask = (entry.extEgressUntag << RTL8651_PORT_NUMBER) | entry.egressUntag;
	vlanp->fid = entry.fid;

	vlanp->vid = entry.vid;
	return SUCCESS;
}

int32 rtl8651_setAsicPvid(uint32 port, uint32 pvid)
{
	uint32 regValue, offset;

	if (port >= RTL8651_AGGREGATOR_NUMBER || pvid >= RTL865XC_VLAN_NUMBER)
		return FAILED;
	;
	offset = (port * 2) & (~0x3);
	regValue = READ_MEM32(PVCR0 + offset);
	if ((port & 0x1))
	{
		regValue = ((pvid & 0xfff) << 16) | (regValue & ~0xFFF0000);
	}
	else
	{
		regValue = (pvid & 0xfff) | (regValue & ~0xFFF);
	}
	WRITE_MEM32(PVCR0 + offset, regValue);
	return SUCCESS;
}


int32 rtl8651_setPortToNetif(uint32 port, uint32 netifidx)
{
	uint16 offset;

	if (port >= RTL8651_AGGREGATOR_NUMBER || netifidx > 8)
		return FAILED;
	offset = (port * 3);
	WRITE_MEM32(PLITIMR, ((READ_MEM32(PLITIMR) & (~(0x7 << offset))) | ((netifidx & 0x7) << offset)));
	return SUCCESS;
}

/*=========================================
 * ASIC DRIVER API: Protocol-based VLAN
 *=========================================*/




/*=========================================
 * ASIC DRIVER API: INTERFACE TABLE
 *=========================================*/

/*
@func int32		| rtl865xC_setNetDecisionPolicy	| Set Interface Multilayer-Decision-Base Control
@parm uint32 | policy | Possible values: NETIF_VLAN_BASED / NETIF_PORT_BASED / NETIF_MAC_BASED
@rvalue SUCCESS	| 	Success
@comm
RTL865xC supports Multilayer-Decision-Base for interface lookup.
 */
int32 rtl865xC_setNetDecisionPolicy(enum ENUM_NETDEC_POLICY policy)
{
	if (policy == NETIF_PORT_BASED)
		WRITE_MEM32(SWTCR0, (READ_MEM32(SWTCR0) & ~LIMDBC_MASK) | LIMDBC_PORT);
	else if (policy == NETIF_MAC_BASED)
		WRITE_MEM32(SWTCR0, (READ_MEM32(SWTCR0) & ~LIMDBC_MASK) | LIMDBC_MAC);
	else
		WRITE_MEM32(SWTCR0, (READ_MEM32(SWTCR0) & ~LIMDBC_MASK) | LIMDBC_VLAN);

	return SUCCESS;
}

/*
@func int32		| rtl865x_setDefACLForNetDecisionMiss	| ACL action when netif decision miss match
@parm uint8 | start_ingressAclIdx |acl index
@parm uint8 | end_ingressAclIdx |acl index
@parm uint8 | start_egressAclIdx |acl index
@parm uint8 | end_egressAclIdx |acl index
@rvalue SUCCESS	| 	Success
@comm
RTL865xC supports Multilayer-Decision-Base for interface lookup.
 */
int32 rtl865x_setDefACLForNetDecisionMiss(uint8 start_ingressAclIdx, uint8 end_ingressAclIdx, uint8 start_egressAclIdx, uint8 end_egressAclIdx)
{
	if (start_ingressAclIdx >= RTL8651_ACLHWTBL_SIZE || end_ingressAclIdx >= RTL8651_ACLHWTBL_SIZE ||
		start_egressAclIdx >= RTL8651_ACLHWTBL_SIZE || end_egressAclIdx >= RTL8651_ACLHWTBL_SIZE)
		return FAILED;

	WRITE_MEM32(DACLRCR, start_ingressAclIdx | end_ingressAclIdx << 7 | start_egressAclIdx << 14 | end_egressAclIdx << 21);
	return SUCCESS;
}


/*
@func int32		| rtl865x_delNetInterfaceByVid	| Delete ASIC Interface Table according to Vlan ID
@parm uint16 | vid | vlan id .
@rvalue SUCCESS	| 	Success
@rvalue FAILED	| 	Failed
@comm
 */
int32 rtl865x_delNetInterfaceByVid(uint16 vid)
{
	rtl865xc_tblAsic_netifTable_t entry;
	uint32 i, netIfIdx;
	int32 retVal = FAILED;

	netIfIdx = RTL865XC_NETIFTBL_SIZE;

	if (vid < 1 || vid > 4095)
		return FAILED;

	/*search...*/
	for (i = 0; i < RTL865XC_NETIFTBL_SIZE; i++)
	{
		_rtl8651_readAsicEntry(TYPE_NETINTERFACE_TABLE, i, &entry);
		if (entry.valid && entry.vid == vid)
		{
			netIfIdx = i;
			break;
		}
	}

	if (netIfIdx < RTL865XC_NETIFTBL_SIZE)
	{
		memset(&entry, 0, sizeof(entry));
		retVal = _rtl8651_forceAddAsicEntry(TYPE_NETINTERFACE_TABLE, netIfIdx, &entry);
	}

	return retVal;
}

/*
@func int32		| rtl8651_setAsicNetInterface	| Set ASIC Interface Table
@parm uint32 | idx | Table index. Specific RTL865XC_NETIFTBL_SIZE to auto-search.
@parm rtl865x_tblAsicDrv_intfParam_t* | intfp | pointer to interface structure to add
@rvalue SUCCESS	| 	Success
@rvalue FAILED	| 	Failed
@comm
To read an interface entry, we provide two ways:
1. given the index which we want to force set
2. leave the index with RTL865XC_NETIFTBL_SIZE, we will search the whole table to find out existed entry or empty entry.
 */
int32 rtl8651_setAsicNetInterface(uint32 idx, rtl865x_tblAsicDrv_intfParam_t *intfp)
{
	rtl865xc_tblAsic_netifTable_t entry;
	uint32 i;

	if (intfp == NULL)
		return FAILED;

	if (idx == RTL865XC_NETIFTBL_SIZE)
	{
		/* User does not specific idx, we shall find out idx first. */
		/* search Interface table to see if exists */
		for (i = 0; i < RTL865XC_NETIFTBL_SIZE; i++)
		{
			/* Since FPGA only has entry 0,1,6,7, we ignore null entry. */
			if (i > 1 && (i < (RTL865XC_NETIFTBL_SIZE - 2)))
				continue;

			_rtl8651_readAsicEntry(TYPE_NETINTERFACE_TABLE, i, &entry);
			if (entry.valid)
				if (entry.vid == intfp->vid)
				{
					idx = i;
					goto exist;
				}
		}
		/* Not existed, find an empty entry */
		for (i = 0; i < RTL865XC_NETIFTBL_SIZE; i++)
		{
			/* Since FPGA only has entry 0,1,6,7, we ignore null entry. */
			if (i > 1 && (i < (RTL865XC_NETIFTBL_SIZE - 2)))
				continue;

			_rtl8651_readAsicEntry(TYPE_NETINTERFACE_TABLE, i, &entry);
			if (!entry.valid)
			{
				break;
			}
		}
		if (i >= RTL865XC_NETIFTBL_SIZE)
			return FAILED; /* no empty entry */
		idx = i;
	}

exist:
	assert(idx < RTL865XC_NETIFTBL_SIZE);

	memset(&entry, 0, sizeof(entry));
	entry.valid = intfp->valid;
	entry.vid = intfp->vid;
	entry.mac47_19 = (intfp->macAddr.octet[0] << 21) | (intfp->macAddr.octet[1] << 13) | (intfp->macAddr.octet[2] << 5) |
					 (intfp->macAddr.octet[3] >> 3);
	entry.mac18_0 = (intfp->macAddr.octet[3] << 16) | (intfp->macAddr.octet[4] << 8) | (intfp->macAddr.octet[5]);

	entry.inACLStartH = (intfp->inAclStart >> 2) & 0x1f;
	entry.inACLStartL = intfp->inAclStart & 0x3;
	entry.inACLEnd = intfp->inAclEnd;
	entry.outACLStart = intfp->outAclStart;
	entry.outACLEnd = intfp->outAclEnd;

	entry.enHWRoute = (rtl8651_getAsicOperationLayer() > 2) ? (intfp->enableRoute == TRUE ? 1 : 0) : 0;

	switch (intfp->macAddrNumber)
	{
	case 0:
	case 1:
		entry.macMask = 7;
		break;
	case 2:
		entry.macMask = 6;
		break;
	case 4:
		entry.macMask = 4;
		break;
	case 8:
		entry.macMask = 0;
		break;
	default:
		return FAILED; // Not permitted macNumber value
	}
	entry.mtuH = intfp->mtu >> 3;
	entry.mtuL = intfp->mtu & 0x7;

	return _rtl8651_forceAddAsicEntry(TYPE_NETINTERFACE_TABLE, idx, &entry);
}

/*
@func int32		| rtl8651_getAsicNetInterface	| Get ASIC Interface Table
@parm uint32 | idx | Table index.
@parm rtl865x_tblAsicDrv_intfParam_t* | intfp | pointer to store interface structure
@rvalue SUCCESS	| 	Success
@rvalue FAILED	| 	Failed. Possible reason: idx error, or invalid entry.
@comm
To read an interface entry, we provide two ways:
1. given the index which we want to read
2. leave the index with RTL865XC_NETIFTBL_SIZE, we will search the whole table according the given intfp->vid
 */

/*=========================================
 * ASIC DRIVER API: ACL Table
 *=========================================*/

/*
@func int32	| rtl865xC_setDefaultACLReg	| This function sets default ACL Rule Control Register.
@parm uint32	| isIngress	| TRUE if you want to set default ingress ACL register. FLASE if egress ACL register.
@parm uint32	| start	| The starting address in the ACL table.
@parm uint32	| end	| The ending address in the ACL table.
@rvalue SUCCESS	| Done
@comm
This function sets the ACL range (starting & ending address) of default ACL Rule Control Register.
*/

/*
@func int32	| rtl865xC_getDefaultACLReg	| This function gets default ACL Rule Control Register.
@parm uint32	| isIngress	| TRUE if you want to set default ingress ACL register. FLASE if egress ACL register.
@parm uint32 *	| start	| Memory to store the starting address in the ACL table.
@parm uint32 *	| end	| Memory to store the ending address in the ACL table.
@rvalue SUCCESS	| Done
@comm
This function gets the ACL range (starting & ending address) of default ACL Rule Control Register.
*/

/*
@func int32 | rtl865xC_lockSWCore | stop TLU operation
@rvalue SUCCESS |
@comm
	When TLU operation stopped, all the received pkt will be queue in rx buffer.
 */

/*
@func int32 | rtl865xC_unLockSWCore | restart TLU operation
@rvalue SUCCESS |
@comm
	restore the system operation.
 */
int32 rtl865xC_unLockSWCore(void)
{
	if (_rtl865xC_lockTLUCounter == 1)
	{
		// REG32(IQFCTCR) = _rtl_inputThrehold;
		REG32(FCREN) = _rtl_FCRegister;
		// REG32(QNUMCR) = _rtl_QNumRegister;
		REG32(IBCR0) = _rtl_inputBandWidth[0];
		REG32(IBCR1) = _rtl_inputBandWidth[1];
		REG32(IBCR2) = _rtl_inputBandWidth[2];

		WRITE_MEM32(PCRP0, _rtl865xC_lockTLUPHYREG[0]); /* Jumbo Frame */
		TOGGLE_BIT_IN_REG_TWICE(PCRP0, EnForceMode);
		WRITE_MEM32(PCRP1, _rtl865xC_lockTLUPHYREG[1]); /* Jumbo Frame */
		TOGGLE_BIT_IN_REG_TWICE(PCRP1, EnForceMode);
		WRITE_MEM32(PCRP2, _rtl865xC_lockTLUPHYREG[2]); /* Jumbo Frame */
		TOGGLE_BIT_IN_REG_TWICE(PCRP2, EnForceMode);
		WRITE_MEM32(PCRP3, _rtl865xC_lockTLUPHYREG[3]); /* Jumbo Frame */
		TOGGLE_BIT_IN_REG_TWICE(PCRP3, EnForceMode);
		WRITE_MEM32(PCRP4, _rtl865xC_lockTLUPHYREG[4]); /* Jumbo Frame */
		TOGGLE_BIT_IN_REG_TWICE(PCRP4, EnForceMode);

		if (rtl8651_tblAsicDrvPara.externalPHYProperty & RTL8651_TBLASIC_EXTPHYPROPERTY_PORT5_RTL8211B)
		{
			WRITE_MEM32(PCRP5, _rtl865xC_lockTLUPHYREG[5]); /* Jumbo Frame */
		}

		if (RTL865X_PHY6_DSP_BUG)
			WRITE_MEM32(PCRP6, _rtl865xC_lockTLUPHYREG[6]);
	}
	_rtl865xC_lockTLUCounter--;
	return SUCCESS;
}

/*define for version control --Mark*/
#define RLRevID_OFFSET 12
#define RLRevID_MASK 0x0f
#define A_DIFF_B_ADDR (PCI_CTRL_BASE + 0x08) /*B800-3408*/
/* get CHIP version */
int32 rtl8651_getChipVersion(int8 *name, uint32 size, int32 *rev)
{
	int32 revID;
	uint32 val;

	revID = ((READ_MEM32(CRMR)) >> RLRevID_OFFSET) & RLRevID_MASK;
	strncpy(name, "8196C", size);
	if (rev == NULL)
		return SUCCESS;

	/*modified by Mark*/
	/*if RLRevID >= 1 V.B  *rev = RLRevID*/
	/*RLRevID == 0 ,then need to check [B800-3408]*/

	if (revID >= RTL865X_CHIP_REV_B)
		*rev = revID;
	else /*A-CUT or B-CUT*/
	{
		val = READ_MEM32(A_DIFF_B_ADDR);
		if (val == 0)
			*rev = RTL865X_CHIP_REV_A; /* RTL865X_CHIP_REV_A*/
		else
			*rev = RTL865X_CHIP_REV_B; /* RTL865X_CHIP_REV_B*/
	}
	return SUCCESS;
}

int32 rtl8651_getChipNameID(int32 *id)
{

	*id = RTL865X_CHIP_VER_RTL8196C;

	return SUCCESS;
}

/*=========================================
 * ASIC DRIVER API: SYSTEM INIT
 *=========================================*/
#define RTL865X_ASIC_DRIVER_SYSTEM_INIT_API

/*
@func void | rtl8651_clearRegister | Clear ALL registers in ASIC
@comm
	Clear ALL registers in ASIC.
	for RTL865xC only
*/


void rtl8651_clearSpecifiedAsicTable(uint32 type, uint32 count)
{
	rtl865xc_tblAsic_aclTable_t entry;
	uint32 idx;

	memset(&entry, 0, sizeof(entry));
	for (idx = 0; idx < count; idx++) // Write into hardware
		_rtl8651_forceAddAsicEntry(type, idx, &entry);
}

int32 rtl8651_clearAsicCommTable(void)
{
	rtl8651_clearSpecifiedAsicTable(TYPE_NETINTERFACE_TABLE, RTL865XC_NETINTERFACE_NUMBER);
	rtl8651_clearSpecifiedAsicTable(TYPE_VLAN_TABLE, RTL865XC_VLANTBL_SIZE);
	rtl8651_clearSpecifiedAsicTable(TYPE_ACL_RULE_TABLE, RTL8651_ACLTBL_SIZE);

	{
		rtl865xc_tblAsic_aclTable_t rule;
		int32 aclIdx;

		memset(&rule, 0, sizeof(rtl865xc_tblAsic_aclTable_t));
		rule.actionType = 0x0;
		rule.ruleType = 0x0;
		for (aclIdx = 0; aclIdx <= RTL8651_ACLHWTBL_SIZE; aclIdx++)
			_rtl8651_forceAddAsicEntry(TYPE_ACL_RULE_TABLE, aclIdx, &rule);
	}

	return SUCCESS;
}

/*=========================================
 * ASIC DRIVER API: SWITCH MODE
 *=========================================*/
static int32 rtl8651_operationlayer = 0;
int32 rtl8651_setAsicOperationLayer(uint32 layer)
{
	if (layer < 1 || layer > 4)
		return FAILED;
	/*   for bridge mode ip multicast patch
		When  in bridge mode,
		only one vlan(8) is available,
		if rtl8651_operationlayer is set less than 3,
		(please refer to rtl8651_setAsicVlan() )
		the "enable routing bit" of VLAN table will be set to 0 according to rtl8651_operationlayer.
		On the one hand, multicast data is flooded in vlan(8) by hardware,
		on the other hand,it will be also trapped to cpu.
		In romedriver process,
		it will do _rtl8651_l2PhysicalPortRelay() in _rtl8651_l2switch(),
		and results in same multicast packet being flooded twice:
		one is by hardware, the other is by romedriver.
		so the minimum  rtl8651_operationlayer will be set 3.
	*/
	if (layer == 1)
	{
		WRITE_MEM32(MSCR, READ_MEM32(MSCR) & ~(EN_L2 | EN_L3 | EN_L4));
		WRITE_MEM32(MSCR, READ_MEM32(MSCR) & ~(EN_IN_ACL));
		WRITE_MEM32(MSCR, READ_MEM32(MSCR) & ~(EN_OUT_ACL));
	}
	else
	{
		/*
		 * Egress acl check should never enable, because of some hardware bug
		 * reported by alpha    2007/12/05
		 */
		WRITE_MEM32(MSCR, READ_MEM32(MSCR) | (EN_IN_ACL));

		if (layer == 2)
		{
			WRITE_MEM32(MSCR, READ_MEM32(MSCR) | (EN_L2));
			WRITE_MEM32(MSCR, READ_MEM32(MSCR) & ~(EN_L3 | EN_L4));
		}
		else
		{ // options for L3/L4 enable
			WRITE_MEM32(ALECR, READ_MEM32(ALECR) & ~FRAG2CPU);

			if (layer == 3)
			{
				WRITE_MEM32(MSCR, READ_MEM32(MSCR) | (EN_L2 | EN_L3));
				WRITE_MEM32(MSCR, READ_MEM32(MSCR) & ~(EN_L4));
			}
			else
			{ // layer 4
				WRITE_MEM32(MSCR, READ_MEM32(MSCR) | (EN_L2 | EN_L3 | EN_L4));
			}
		}
	}
	if (layer == 1)
		rtl8651_setAsicAgingFunction(FALSE, FALSE);
	else if (layer == 2 || layer == 3)
		rtl8651_setAsicAgingFunction(TRUE, FALSE);
	else
		rtl8651_setAsicAgingFunction(TRUE, TRUE);
	rtl8651_operationlayer = layer;
	return SUCCESS;
}

int32 rtl8651_getAsicOperationLayer(void)
{
	return rtl8651_operationlayer;
}

int32 rtl8651_setAsicAgingFunction(int8 l2Enable, int8 l4Enable)
{
	WRITE_MEM32(TEACR, (READ_MEM32(TEACR) & ~0x3) | (l2Enable == TRUE ? 0x0 : 0x1) | (l4Enable == TRUE ? 0x0 : 0x2));
	return SUCCESS;
}

#define BSP_SW_IE (1 << 15)
/* Optional TX length mode: exclude CRC from length (default off) */
#ifndef RTL_TX_EXCLUDE_CRC
#define RTL_TX_EXCLUDE_CRC 1
#endif

void rtl865x_start(void)
{
    /* Use conservative 32-word bus burst (stable on RTL8196E). */
    REG32(CPUICR) = TXCMD | RXCMD | BUSBURST_32WORDS | MBUF_2048BYTES
                  | (RTL_TX_EXCLUDE_CRC ? EXCLUDE_CRC : 0);
	REG32(CPUIISR) = REG32(CPUIISR);
	REG32(CPUIIMR) = RX_DONE_IE_ALL | TX_ALL_DONE_IE_ALL | LINK_CHANGE_IE | PKTHDR_DESC_RUNOUT_IE_ALL;
	REG32(SIRR) = TRXRDY;
	REG32(GIMR) |= (BSP_SW_IE);
}

void rtl865x_down(void)
{
	REG32(CPUIIMR) = 0;
	REG32(CPUIISR) = REG32(CPUIISR);
	REG32(GIMR) &= ~(BSP_SW_IE);
	REG32(CPUICR) = 0;
	REG32(SIRR) = 0;
}


unsigned int rtl865x_probeSdramSize(void)
{
	unsigned int memsize;
	unsigned int MCRsdram;
	unsigned int colcnt, rowcnt;
	unsigned int colsize, rowsize;
	MCRsdram = READ_MEM32(MCR);
	{
		/*	96c & 98	*/
		colcnt = (READ_MEM32(DCR) & COLCNT_MASK) >> COLCNT_OFFSET;
		rowcnt = (READ_MEM32(DCR) & ROWCNT_MASK) >> ROWCNT_OFFSET;
		switch (colcnt)
		{
		case 0:
			colsize = 256;
			break;
		case 1:
			colsize = 512;
			break;
		case 2:
			colsize = 1024;
			break;
		case 3:
			colsize = 2048;
			break;
		case 4:
			colsize = 4096;
			break;
		default:
			printk("DDR SDRAM unknown(0x%08X):column cnt(0x%x)\n", MCRsdram, colcnt);
			memsize = 0;
			goto out;
		}
		switch (rowcnt)
		{
		case 0:
			rowsize = 2048;
			break;
		case 1:
			rowsize = 4096;
			break;
		case 2:
			rowsize = 8192;
			break;
		case 3:
			rowsize = 16384;
			break;
		default:
			printk("DDR SDRAM unknown(0x%08X):row cnt(0x%x)\n", MCRsdram, rowcnt);
			memsize = 0;
			goto out;
		}
		memsize = (colsize * rowsize) << 3;
	}
out:
	return memsize;
}

/*=========================================
 * ASIC DRIVER API: ASIC Counter
 *=========================================*/
static void _rtl8651_initialRead(void)
{ // RTL8651 read counter for the first time will get value -1 and this is meaningless
	uint32 i;
	for (i = 0; i <= 0xac; i += 0x4)
	{
		rtl8651_returnAsicCounter(i);
	}
}

uint32 rtl8651_returnAsicCounter(uint32 offset)
{
	if (offset & 0x3)
		return 0;
	return READ_MEM32(MIB_COUNTER_BASE + offset);
}


int32 rtl8651_clearAsicCounter(void)
{
	WRITE_MEM32(MIB_CONTROL, ALL_COUNTER_RESTART_MASK);
	return SUCCESS;
}




/*
@func int32 | rtl865xC_dumpAsicCounter | Dump common counters of all ports (CPU port included).
@rvalue SUCCESS | Finish showing the counters.
@comm
Dump common counters of all ports. Includes Rx/Tx Bytes, Rx/Tx pkts, Rx/Tx Pause frames, Rx Drops.
*/

/*
@func int32 | rtl865xC_dumpAsicDiagCounter | Dump complex counters of all ports (CPU port included).
@rvalue SUCCESS | Finish showing the counters.
@comm
Dump complex counters of all ports.
*/

/*
@func int32 | rtl8651_resetAsicCounterMemberPort | Clear the specified counter value and its member port
@parm	uint32	|	counterIdx |  Specify the counter to clear
@rvalue SUCCESS | When counter index is valid, return SUCCESS
@rvalue FAILED | When counter index is invalid, return FAILED
@comm
	When specify a vlid counter, the member port of the specified counter will be cleared to null set.
*/
int32 rtl8651_resetAsicCounterMemberPort(uint32 counterIdx)
{

	rtlglue_printf("attention!this function is obsolete, please use new api:rtl8651_resetAsicMIBCounter()  or rtl8651_clearAsicCounter()\n");
	return FAILED;
	switch (counterIdx)
	{
	case 0:
		WRITE_MEM32(MIB_CONTROL, 0x0);
		break;
	default:
		rtlglue_printf("Not Comptable Counter Index  %d\n", counterIdx);
		return FAILED; // counter index out of range
	}
	_rtl8651_initialRead();
	return SUCCESS;
}

/*
@func int32 | rtl8651_addAsicCounterMemberPort | The specified counter value add the specified port port into counter monitor member
@parm	uint32	|	counterIdx |  Specify the counter to add member port
@parm	uint32	|	port |  The added member port
@rvalue SUCCESS | When counter index is valid, return SUCCESS
@rvalue FAILED | When counter index is invalid, return FAILED
@comm
	When specify a vlid counter and a valid port number, the specified port will be added to the counter coverage.
*/

// sync from rtl865x kernel 2.4
void FullAndSemiReset(void)
{
	REG32(SIRR) |= FULL_RST;
	mdelay(300);

	REG32(SYS_CLK_MAG) |= CM_PROTECT;
	REG32(SYS_CLK_MAG) &= ~CM_ACTIVE_SWCORE;
	mdelay(300);

	REG32(SYS_CLK_MAG) |= CM_ACTIVE_SWCORE;
	REG32(SYS_CLK_MAG) &= ~CM_PROTECT;
	mdelay(50);

	// 08-15-2012, set TRXRDY bit in rtl865x_start() in rtl865x_asicCom.c
	/* Enable TRXRDY */
	// REG32(SIRR) |= TRXRDY;
}
