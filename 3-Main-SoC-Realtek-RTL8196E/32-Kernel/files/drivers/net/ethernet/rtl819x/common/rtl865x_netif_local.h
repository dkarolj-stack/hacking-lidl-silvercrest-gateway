/*
 * RTL865x Network Interface Local Header
 * Copyright (c) 2008 Realtek Semiconductor Corporation
 * Author: hyking (hyking_liu@realsil.com.cn)
 * Adapted for Linux 5.10 & RTL8196E: Jacques Nilo (2025)
 *
 * Internal structures for network interface management.
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef RTL865X_NETIF_LOCAL_H
#define RTL865X_NETIF_LOCAL_H

#include "rtl819x.h"

#define REDUCE_MEMORY_SIZE_FOR_16M

#define RTL865X_ACL_CHAIN_NUMBER	16
#define RTL865X_ACL_SYSTEM_USED	-10000

typedef struct _rtl865x_acl_chain_s
{
	int32 ruleCnt;
	int32 priority; /*chain priosity: the minimum value is the highest proirity*/
	rtl865x_AclRule_t *head,*tail;
	struct _rtl865x_acl_chain_s *preChain,*nextChain;
}rtl865x_acl_chain_t;

/*the following fields are invalid when the interface is slave interface:
* inAclStart, inAclEnd, outAclStart, outAclEnd,asicIdx,chainListHead
*/
typedef struct rtl865x_netif_local_s
{
	uint16 	vid; /*netif->vid*/
	uint16 	mtu; /*netif's MTU*/	
	uint16 	macAddrNumber; /*how many continuous mac is attached*/
	uint16 	inAclStart, inAclEnd, outAclStart, outAclEnd; /*acl index*/
	uint16 	enableRoute; /*enable route*/
	uint32 	valid:1,	/*valid?*/
		if_type:5, /*interface type, IF_ETHER, IF_PPPOE*/
		refCnt:5, /*referenc count by other table entry*/			
		asicIdx:3, /*asic index, total 8 entrys in asic*/
		is_wan:1, /*this interface is wan?*/
		is_defaultWan:1, /*if there is multiple wan interface, which interface is default wan*/
		dmz:1,	/*dmz interface?*/
		is_slave:1; /*is slave interface*/
	
	ether_addr_t macAddr;
	uint8	name[MAX_IFNAMESIZE];
	rtl865x_acl_chain_t  *chainListHead[2]; /*0: ingress acl chain, 1: egress acl chain*/
	struct rtl865x_netif_local_s *master; /*point master interface when this interface is slave interface*/
}rtl865x_netif_local_t;

#define	RTL_ACL_INGRESS	0
#define	RTL_ACL_EGRESS	1

typedef struct rtl865x_aclBuf_s
{
	int16 totalCnt;
	int16 freeCnt;
	rtl865x_AclRule_t *freeHead;
}rtl865x_aclBuf_t;

int32 rtl865x_enableNetifRouting(rtl865x_netif_local_t *netif);
int32 rtl865x_disableNetifRouting(rtl865x_netif_local_t *netif);
rtl865x_netif_local_t *_rtl865x_getNetifByName(char *name);
rtl865x_netif_local_t *_rtl865x_getSWNetifByName(char * name);
rtl865x_netif_local_t *_rtl865x_getDefaultWanNetif(void);
int32 _rtl865x_setDefaultWanNetif(char *name);
int32 _rtl865x_clearDefaultWanNetif(char * name);
int32 _rtl865x_getNetifIdxByVid(uint16 vid);
int32 _rtl865x_getNetifIdxByName(uint8 *name);
int32 _rtl865x_getNetifIdxByNameExt(uint8 *name);
int32 rtl865x_get_drvNetifName_by_psName(const char *psName,char *netifName);

#endif
