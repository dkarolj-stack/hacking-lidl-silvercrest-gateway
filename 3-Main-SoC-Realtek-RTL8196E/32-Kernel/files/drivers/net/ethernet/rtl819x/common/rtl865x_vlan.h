/*
 * RTL865x VLAN Driver Header
 * Copyright (c) 2008 Realtek Semiconductor Corporation
 * Author: hyking (hyking_liu@realsil.com.cn)
 * Adapted for Linux 5.10 & RTL8196E: Jacques Nilo (2025)
 *
 * VLAN table management structures and function prototypes.
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef	RTL865X_VLAN_H
#define	RTL865X_VLAN_H

#include "rtl819x.h"

#define REDUCE_MEMORY_SIZE_FOR_16M


#define VLAN_NUMBER			4096


#define VLANTBL_SIZE				16

#define RTL865X_FDB_NUMBER	4

#define RTL865X_PPTP_HWACC_PORTMASK		0x80
#define RTL865X_PPTP_HWACC_VLANID		10


typedef struct rtl865x_vlan_entry_s {
	uint32	memberPortMask; /*extension ports [rtl8651_totalExtPortNum-1:0] are located at bits [RTL8651_PORT_NUMBER+rtl8651_totalExtPortNum-1:RTL8651_PORT_NUMBER]*/
	uint32	untagPortMask; /*extension ports [rtl8651_totalExtPortNum-1:0] are located at bits [RTL8651_PORT_NUMBER+rtl8651_totalExtPortNum-1:RTL8651_PORT_NUMBER]*/
	uint32	valid:1,
			fid:2,	/*there are 4 fdbs in 865x*/
			vid:12,
			refCnt:5; /*reference count: this vlan entry is referenced by networkInterface...*/
}rtl865x_vlan_entry_t;

int32 rtl865x_initVlanTable(void);
int32 rtl865x_reinitVlantable(void);
int32 rtl865x_addVlan(uint16 vid);
int32 rtl865x_delVlan(uint16 vid);
int32 rtl865x_addVlanPortMember(uint16 vid, uint32 portMask);
int32 rtl865x_delVlanPortMember(uint16 vid,uint32 portMask);


int32 rtl865x_setVlanPortTag(uint16 vid,uint32 portMask,uint8 tag);
int32 rtl865x_setVlanFilterDatabase(uint16 vid, uint32 fid);
int32 rtl865x_getVlanFilterDatabaseId(uint16 vid, uint32 *fid);
rtl865x_vlan_entry_t *_rtl8651_getVlanTableEntry(uint16 vid);
int32 rtl865x_referVlan(uint16 vid);
int32 rtl865x_deReferVlan(uint16 vid);

#endif
	
