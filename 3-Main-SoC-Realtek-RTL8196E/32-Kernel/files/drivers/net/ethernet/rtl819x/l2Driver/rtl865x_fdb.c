/*
 * RTL865x L2 Forwarding Database
 * Copyright (c) 2011 Realtek Semiconductor Corporation
 * Adapted for Linux 5.10 & RTL8196E: Jacques Nilo (2025)
 *
 * L2 FDB (MAC address table) management for bridging.
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include "rtl819x.h"
#include "../AsicDriver/rtl865x_asicCom.h"
#include "../AsicDriver/rtl865x_asicL2.h"
#include "../common/rtl865x_eventMgr.h"
#include "rtl865x_fdb.h"

struct rtl865x_L2Tables sw_FDB_Table;
int32    arpAgingTime = 450;
ether_addr_t cpu_mac = { {0x00, 0x00, 0x0a, 0x00, 0x00, 0x0f} };
rtl865x_tblAsicDrv_l2Param_t tmpL2buff;

static RTL_DECLARE_MUTEX(l2_sem);
int32 _rtl865x_fdb_alloc(void)
{
	int32 index = 0;
	memset( &sw_FDB_Table, 0, sizeof( sw_FDB_Table ) );
	TBL_MEM_ALLOC(sw_FDB_Table.filterDB, rtl865x_filterDbTable_t, RTL865x_FDB_NUMBER);
	{//Initial free filter database entry
		rtl865x_filterDbTableEntry_t * tempFilterDb = NULL;
			TBL_MEM_ALLOC(tempFilterDb, rtl865x_filterDbTableEntry_t, RTL8651_L2TBL_ROW);
		SLIST_INIT(&sw_FDB_Table.freefdbList.filterDBentry);
		for(index=0; index<RTL8651_L2TBL_ROW; index++)
			SLIST_INSERT_HEAD(&sw_FDB_Table.freefdbList.filterDBentry, &tempFilterDb[index], nextFDB);
	}

	return SUCCESS;
	
}

int32 rtl865x_getReserveMacAddr(ether_addr_t *macAddr)
{
	memcpy( macAddr, &cpu_mac, sizeof(ether_addr_t));
	return SUCCESS;
}

int32 _rtl865x_layer2_patch(void)
{

	int32 retval = 0;
	ether_addr_t mac = { {0xff, 0xff, 0xff, 0xff, 0xff, 0xff} };
	uint32 portmask=RTL_WANPORT_MASK|RTL_LANPORT_MASK;

	retval = _rtl865x_addFilterDatabaseEntry(RTL865x_L2_TYPEII, RTL_LAN_FID, &mac, FDB_TYPE_TRAPCPU, portmask, TRUE, FALSE);

	retval = _rtl865x_addFilterDatabaseEntry(RTL865x_L2_TYPEII, RTL_LAN_FID, &cpu_mac, FDB_TYPE_TRAPCPU, 0, TRUE, FALSE);
	assert(retval == SUCCESS);

	return SUCCESS;
}

int32 _rtl865x_fdb_collect(void)
{
	int32 index = 0;
	int32 index1 = 0;

	rtl865x_filterDbTable_t *filterDbPtr;
	rtl865x_filterDbTableEntry_t * tempFilterDbPtr;	
	for(index=0,filterDbPtr=&sw_FDB_Table.filterDB[0]; index<RTL865x_FDB_NUMBER; index++,filterDbPtr++) {
		for(index1=0; index1<RTL8651_L2TBL_ROW; index1++)
			while(SLIST_FIRST(&(filterDbPtr->database[index1]))) {
				tempFilterDbPtr = SLIST_FIRST(&(filterDbPtr->database[index1]));
				SLIST_REMOVE_HEAD(&(filterDbPtr->database[index1]), nextFDB);
				SLIST_INSERT_HEAD(&sw_FDB_Table.freefdbList.filterDBentry, tempFilterDbPtr, nextFDB);
			}
	}
			
	return SUCCESS;
	
}

int32 _rtl865x_fdb_init(void)
{ 
	int32 index, index1;
	/*Initial Filter database*/
	rtl865x_filterDbTable_t *fdb_t = &sw_FDB_Table.filterDB[0];
	for(index=0; index<RTL865x_FDB_NUMBER; index++, fdb_t++) {
		for(index1=0; index1<RTL8651_L2TBL_ROW; index1++)
			SLIST_INIT(&(fdb_t->database[index1]));
		fdb_t->valid = 1;
	}

	return SUCCESS;
}

int32 _rtl865x_clearHWL2Table(void)
{
	int i, j;

	for (i = 0; i < RTL8651_L2TBL_ROW; i++)
		for (j = 0; j < RTL8651_L2TBL_COLUMN; j++)
		{
			rtl8651_delAsicL2Table(i, j);	
		}

	return SUCCESS;	
}

int32 rtl865x_layer2_init(void)
{
		
	_rtl865x_fdb_alloc();

	_rtl865x_fdb_init();

	_rtl865x_layer2_patch();
	
	return SUCCESS;
}

int32 rtl865x_layer2_reinit(void)
{
	_rtl865x_clearHWL2Table();

	_rtl865x_fdb_collect();
		
	//_rtl865x_fdb_alloc();

	_rtl865x_fdb_init();

	_rtl865x_layer2_patch();
	
	return SUCCESS;
}

 uint32 rtl865x_getHWL2Index(ether_addr_t * macAddr, uint16 fid)
 {
	return (rtl8651_filterDbIndex(macAddr, fid));
 }

int32 rtl865x_setHWL2Table(uint32 row, uint32 column, rtl865x_tblAsicDrv_l2Param_t *l2p)
{
	return (rtl8651_setAsicL2Table(row, column, l2p));
}

int32 rtl865x_getHWL2Table(uint32 row, uint32 column, rtl865x_tblAsicDrv_l2Param_t *l2p) 
{
	return rtl8651_getAsicL2Table(row, column, l2p);
}

int32 rtl865x_refleshHWL2Table(ether_addr_t * macAddr, uint32 flags,uint16 fid)
{

	rtl865x_tblAsicDrv_l2Param_t L2temp, *L2buff;
	uint32 rowIdx, col_num;
	uint32 colIdx = 0;
	uint32 found = FALSE;

	L2buff = &L2temp;
	memset(L2buff, 0, sizeof(rtl865x_tblAsicDrv_l2Param_t));
	rowIdx = rtl8651_filterDbIndex(macAddr, fid);

	for(colIdx=0; colIdx<RTL8651_L2TBL_COLUMN; colIdx++) {
		if ((rtl8651_getAsicL2Table(rowIdx, colIdx, L2buff))!=SUCCESS ||
			memcmp(&(L2buff->macAddr), macAddr, 6)!= 0)
			continue;
		
		if (((flags&FDB_STATIC) && L2buff->isStatic) ||
			((flags&FDB_DYNAMIC) && !L2buff->isStatic)) {
				assert(colIdx);
				col_num = colIdx;
				found = TRUE;
				break;
		}	
	} 

	if (found == TRUE)
	{
		L2buff->ageSec = 450;
		rtl8651_setAsicL2Table(rowIdx, colIdx, L2buff);
		return SUCCESS;
	}
	else
	{
		return FAILED;
	}
}

int32 rtl_get_hw_fdb_age(uint32 fid,ether_addr_t *mac, uint32 flags)
{
        uint32 rowIdx;
        uint32 colIdx = 0;
        int32 retval = 0;
        rtl865x_tblAsicDrv_l2Param_t L2buff;

        memset(&L2buff,0,sizeof(rtl865x_tblAsicDrv_l2Param_t));

        rowIdx = rtl8651_filterDbIndex(mac, fid);

        for(colIdx=0; colIdx<RTL8651_L2TBL_COLUMN; colIdx++)
        {
                retval = rtl8651_getAsicL2Table(rowIdx, colIdx, &L2buff);

                if (retval !=SUCCESS ||
                        memcmp(&(L2buff.macAddr), mac, 6)!= 0)
                        continue;
                if (((flags&FDB_DYNAMIC) && !L2buff.isStatic)||((flags&FDB_STATIC) && L2buff.isStatic))
                {
                        retval = L2buff.ageSec;
                        break;
                }

        }

        return retval;
}
int32 rtl865x_ConvertPortMasktoPortNum(int32 portmask)
{
	int32 i = 0;
	
	for (i = PHY0; i < EXT3; i++)
	{
		if(((portmask >> i) & 0x01) == 1)
		{
			return i;
		}		
	}
	return FAILED;
}


int32 rtl865x_getPortNum(const unsigned char *addr){
	ether_addr_t *macAddr;
	int32 column;
	rtl865x_tblAsicDrv_l2Param_t	fdbEntry;
	int8 port_num = -1;
	//fdb->ageing_timer = 300*HZ;
	macAddr = (ether_addr_t *)(addr);
			
	if (rtl865x_Lookup_fdb_entry(RTL_LAN_FID, macAddr, FDB_DYNAMIC, &column,&fdbEntry) == SUCCESS)
	{
		/*find the fdb entry*/
		port_num = rtl865x_ConvertPortMasktoPortNum(fdbEntry.memberPortMask);
	} else {
		/*can't find */
	}
	return port_num;
}

int32 rtl865x_Lookup_fdb_entry(uint32 fid, ether_addr_t *mac, uint32 flags, uint32 *col_num, rtl865x_tblAsicDrv_l2Param_t *L2buff)
{
	uint32 rowIdx;
	uint32 colIdx;
	int32 retval;

	rowIdx = rtl8651_filterDbIndex(mac, fid);
	colIdx = 0;
	
	for(colIdx=0; colIdx<RTL8651_L2TBL_COLUMN; colIdx++) 
	{
		retval = rtl8651_getAsicL2Table(rowIdx, colIdx, L2buff);

		if (retval !=SUCCESS ||
			memcmp(&(L2buff->macAddr), mac, 6)!= 0)
			continue;
		if (((flags&FDB_STATIC) && L2buff->isStatic) ||
			((flags&FDB_DYNAMIC) && !L2buff->isStatic)) {
			assert(colIdx);
			*col_num = colIdx;
			return SUCCESS;
		}
	
	}
	return FAILED;
}

/*
@func enum RTL_RESULT | rtl865x_addFdbEntry | Add an MAC address, said Filter Database Entry
@parm uint32 | fid | The filter database index (valid: 0~3)
@parm ether_addr_t * | mac | The MAC address to be added
@parm uint32 | portmask | The portmask of this MAC address belongs to
@parm uint32 | type | fdb entry type
@rvalue SUCCESS | Add success
@rvalue FAILED | General failure
@comm 
	Add a Filter Database Entry to L2 Table(1024-Entry)
@devnote
	(under construction)
*/
int32 rtl865x_addFilterDatabaseEntry( uint32 fid, ether_addr_t * mac, uint32 portmask, uint32 type )
{
        int32 retval;
	 unsigned long flags;	
        if (type != FDB_TYPE_FWD && type != FDB_TYPE_SRCBLK && type != FDB_TYPE_TRAPCPU)
                return RTL_EINVALIDINPUT; /* Invalid parameter */
 
        if (fid >= RTL865x_FDB_NUMBER)
                return RTL_EINVALIDINPUT;
        if (mac == (ether_addr_t *)NULL)
                return RTL_EINVALIDINPUT;
        if (sw_FDB_Table.filterDB[fid].valid == 0)
                return RTL_EINVALIDFID;
        /*l2 lock*/
	//rtl_down_interruptible(&l2_sem);
	local_irq_save(flags);	
        retval = _rtl865x_addFilterDatabaseEntry(RTL865x_L2_TYPEII, fid,  mac, type, portmask, FALSE, FALSE);
        /*l2 unlock*/
	//rtl_up(&l2_sem);
 	local_irq_restore(flags);
        return retval;
}

int32 rtl865x_addFilterDatabaseEntryExtension( uint16 fid, rtl865x_filterDbTableEntry_t * L2entry)
{
        int32 retval;
	 unsigned long flags;
        if (L2entry->process != FDB_TYPE_FWD && L2entry->process != FDB_TYPE_SRCBLK && L2entry->process != FDB_TYPE_TRAPCPU)
                return RTL_EINVALIDINPUT; /* Invalid parameter */
 
        if (fid >= RTL865x_FDB_NUMBER)
                return RTL_EINVALIDINPUT;
        if (&(L2entry->macAddr) == (ether_addr_t *)NULL)
                return RTL_EINVALIDINPUT;
        if (sw_FDB_Table.filterDB[fid].valid == 0)
                return RTL_EINVALIDFID;
        /*l2 lock*/
	//rtl_down_interruptible(&l2_sem);	
	local_irq_save(flags);	
	retval = _rtl865x_addFilterDatabaseEntry(	L2entry->l2type, 
											fid,  
											&(L2entry->macAddr),
											L2entry->process,
											L2entry->memberPortMask,
											L2entry->auth,
											L2entry->SrcBlk);

        /*l2 unlock*/
	//rtl_up(&l2_sem);
 	local_irq_restore(flags);
        return retval;
}




int32 _rtl865x_removeFilterDatabaseEntry(uint16 fid, ether_addr_t * mac, uint32 rowIdx)
{
	rtl865x_filterDbTable_t *fdb_t = &sw_FDB_Table.filterDB[fid];
	rtl865x_filterDbTableEntry_t * l2entry_t ;
	/*printk("rowIdx:%dfid:%d\n",rowIdx,fid);*/
	/*printk("\n mac address is %x %x %x %x %x %x\n", mac->octet[0],
													mac->octet[1],
													mac->octet[2],
													mac->octet[3],
													mac->octet[4],
													mac->octet[5]);
													*/
	if (SLIST_FIRST(&(fdb_t->database[rowIdx]))) 
	{	
		SLIST_FOREACH(l2entry_t, &(fdb_t->database[rowIdx]), nextFDB) 
		{
			 if (memcmp(&(l2entry_t->macAddr), mac, 6)== 0)
			{
				SLIST_REMOVE(
					&(fdb_t->database[rowIdx]), 
					l2entry_t, 
					 rtl865x_filterDbTableEntry_s,
					 nextFDB
				);
				SLIST_INSERT_HEAD(&sw_FDB_Table.freefdbList.filterDBentry, l2entry_t, nextFDB);
				
				rtl865x_raiseEvent(EVENT_DEL_FDB, (void *)(l2entry_t));

				return SUCCESS;
			}			
		}
	}

	return FAILED;
}

int32 rtl865x_lookup_FilterDatabaseEntry(uint16 fid, ether_addr_t * mac, rtl865x_filterDbTableEntry_t *l2_entry)
{
	rtl865x_filterDbTable_t *fdb_t = &sw_FDB_Table.filterDB[fid];
	rtl865x_filterDbTableEntry_t * l2entry_t ;
	uint32 rowIdx;
	
	rowIdx = rtl8651_filterDbIndex(mac, fid);

	if (SLIST_FIRST(&(fdb_t->database[rowIdx]))) 
	{	
		SLIST_FOREACH(l2entry_t, &(fdb_t->database[rowIdx]), nextFDB) 
		{
			 if (memcmp(&(l2entry_t->macAddr), mac, 6)== 0)
			{
				l2_entry->asicPos	= l2entry_t->asicPos;
				l2_entry->auth		= l2entry_t->auth;
				l2_entry->l2type		= l2entry_t->l2type;
				l2_entry->linkId		= l2entry_t->linkId;
				l2_entry->memberPortMask = l2entry_t->memberPortMask;
				l2_entry->nhFlag 	= l2entry_t->nhFlag;
				l2_entry->process	= l2entry_t->process;
				l2_entry->SrcBlk		= l2entry_t->SrcBlk;
				l2_entry->vid		= l2entry_t->vid;
				return SUCCESS;
			}			
		}
	}

	memset(l2_entry, 0, sizeof(rtl865x_filterDbTableEntry_t));
	return FAILED;
}

int32 _rtl865x_addFilterDatabaseEntry(uint16 l2Type, uint16 fid,  ether_addr_t * macAddr, uint32 type, uint32 portMask, uint8 auth, uint8 SrcBlk)
{
	uint32 rowIdx = 0;
	uint32 colIdx = 0;
	uint32 col_num = 0;
	uint32 col_tmp = 0;
	uint16 tmp_age = 0xffff;
	int32   found = FALSE;
	int32   flag = FALSE;
	int32   nexthp_flag = FALSE;
	int32  isStatic = FALSE;
	int32  toCpu = FALSE;
	rtl865x_filterDbTableEntry_t * l2entry_t = NULL;
	rtl865x_filterDbTableEntry_t *tmpL2;
	rtl865x_filterDbTable_t *fdb_t = &sw_FDB_Table.filterDB[fid];
	rtl865x_tblAsicDrv_l2Param_t l2entry_tmp,*L2buff;
	int32 retval = 0;

	L2buff = &l2entry_tmp;
	memset(L2buff,0,sizeof(rtl865x_tblAsicDrv_l2Param_t));
	rowIdx = rtl8651_filterDbIndex(macAddr, fid);
	for(colIdx=0; colIdx<RTL8651_L2TBL_COLUMN; colIdx++) 
	{
		retval = rtl8651_getAsicL2Table(rowIdx, colIdx, L2buff);		
		if ((retval)==SUCCESS)
		{
			/*check whether mac address has been learned  to HW or not*/
			if (memcmp(&(L2buff->macAddr), macAddr, 6)== 0)
			{	
				/*the entry has been auto learned*/
				if (L2buff->memberPortMask==portMask)
				{
					/*the entry has been auto learned        */
					found = TRUE;
					col_tmp = colIdx;
				}
				else
				/*portmask is different , it should be overwrited*/	
				{
					found = FALSE;
					flag = TRUE;
				}
				break;
			}
			/*no matched entry, try get minimum aging time L2 Asic entry*/
			if (tmp_age> L2buff->ageSec)
			{
				tmp_age = L2buff->ageSec;
				col_num = colIdx;
			}
		}
		else
		{
			/*there is still empty l2 asic entry*/
			flag = TRUE;
			break;
		}
	}
	
	switch(l2Type) {	
	case RTL865x_L2_TYPEI:
		nexthp_flag = FALSE;isStatic = FALSE;
		break;
	case RTL865x_L2_TYPEII:
		nexthp_flag = TRUE; isStatic = TRUE;
		break;
	case RTL865x_L2_TYPEIII:
		nexthp_flag = FALSE;isStatic = TRUE;
		break;
	default: assert(0);	
	}

	switch(type) {	
	case FDB_TYPE_FWD:
			toCpu =  FALSE;
			break;
	case FDB_TYPE_DSTBLK:
			toCpu =  FALSE;
			break;
	case FDB_TYPE_SRCBLK:
			toCpu =  FALSE;
			break;
	case FDB_TYPE_TRAPCPU:
			toCpu =  TRUE;
			break;
	default: assert(0);
	}

	if (found == FALSE)
	{
		/*portmask is different , so it should overwrite the original asic entry. Or there is empty entry, set it to asic*/
		if(flag == TRUE)
		{
			rtl8651_setAsicL2Table_Patch(
					rowIdx, 
					colIdx, 
					macAddr, 
					toCpu, 
					SrcBlk, 
					portMask, 
					arpAgingTime, 
					isStatic, 
					nexthp_flag,
					fid,
					auth);
			col_tmp = colIdx;
		}
	}
	/*find the same asic entry, should update the aging time*/
	else
	{
		rtl8651_setAsicL2Table_Patch(
				rowIdx, 
				col_tmp, 
				macAddr, 
				toCpu, 
				SrcBlk, 
				portMask, 
				arpAgingTime, 
				isStatic, 
				nexthp_flag,
				fid,
				auth);		
	}

/*	
	colIdx=0;

	tmpL2 = SLIST_FIRST(&(fdb_t->database[rowIdx]));
	tmpL2->nextFDB.sle_next  = NULL;
	
	for(colIdx=0; colIdx<RTL8651_L2TBL_COLUMN; colIdx++) 
	{
		if ((rtl8651_getAsicL2Table(rowIdx, colIdx, L2buff))==SUCCESS)
		{
			l2entry_t->asicPos =colIdx;
			l2entry_t->auth = L2buff->auth;
			l2entry_t->configToAsic = 0;
			l2entry_t->memberPortMask = L2buff->memberPortMask;
			l2entry_t->SrcBlk = L2buff->srcBlk;
			memcpy(&l2entry_t->macAddr, &L2buff->macAddr, sizeof(ether_addr_t));

			tmpL2->nextFDB.sle_next = l2entry_t;
			l2entry_t->nextFDB.sle_next = NULL;
			tmpL2 = l2entry_t;
		}
	}
*/

	if (SLIST_FIRST(&sw_FDB_Table.freefdbList.filterDBentry) == NULL)
		return RTL_ENOFREEBUFFER;
	
	/*config the SW l2 entry */
	l2entry_t = SLIST_FIRST(&sw_FDB_Table.freefdbList.filterDBentry);
	SLIST_REMOVE_HEAD(&sw_FDB_Table.freefdbList.filterDBentry, nextFDB);
	
	l2entry_t->asicPos =col_tmp;
	l2entry_t->auth = auth;
	l2entry_t->configToAsic = 0;
	l2entry_t->memberPortMask = portMask;
	l2entry_t->l2type = l2Type;
	l2entry_t->nhFlag = nexthp_flag;
	
	l2entry_t->SrcBlk = FALSE;
	memcpy(&l2entry_t->macAddr, macAddr, sizeof(ether_addr_t));
	switch(type) {	
	case FDB_TYPE_FWD:
	 	 l2entry_t->process = FDB_TYPE_FWD;
		 l2entry_t->memberPortMask = portMask;
		 break;
	case FDB_TYPE_DSTBLK:
		 l2entry_t->process = FDB_TYPE_DSTBLK;
		 l2entry_t->memberPortMask = 0;
		 break;
	case FDB_TYPE_SRCBLK:
		 l2entry_t->process = FDB_TYPE_SRCBLK;
		 l2entry_t->memberPortMask = 0;
		 break;
	case FDB_TYPE_TRAPCPU:
		 l2entry_t->process = FDB_TYPE_TRAPCPU;
		 l2entry_t->memberPortMask = portMask;
	 	 break;
	default: assert(0);
	}
	
	/*write the SW l2 entry */
	
	if (SLIST_FIRST(&(fdb_t->database[rowIdx]))) 
	{	
		SLIST_FOREACH(tmpL2, &(fdb_t->database[rowIdx]), nextFDB) 
		{
			 if (memcmp(&(tmpL2->macAddr), macAddr, 6)== 0)
			{
				if(	(tmpL2->auth != auth) ||
					(tmpL2->process != type) || 
					(tmpL2->SrcBlk != SrcBlk) ||	
					(tmpL2->memberPortMask != portMask) ||
					(tmpL2->l2type != l2Type)	)
				{
					tmpL2->auth				= auth;
					tmpL2->process 			= type;
					tmpL2->SrcBlk 			= SrcBlk;
					tmpL2->memberPortMask	= portMask;
					tmpL2->l2type 			= l2Type;

					/*duplicate entry,avoid memory leak*/
					SLIST_INSERT_HEAD(&sw_FDB_Table.freefdbList.filterDBentry, l2entry_t, nextFDB);
					break;
/*					tmpL2 ->refCount = 1;*/
				}			
				else
				{
/*					tmpL2->refCount +=1;*/

					/*duplicate entry,avoid memory leak*/
					SLIST_INSERT_HEAD(&sw_FDB_Table.freefdbList.filterDBentry, l2entry_t, nextFDB);
					return SUCCESS;
				}				
			}			
			else if (tmpL2->nextFDB.sle_next == NULL) 
			{
/*				l2entry_t ->refCount = 1;*/
				tmpL2->nextFDB.sle_next = l2entry_t;
				l2entry_t->nextFDB.sle_next = NULL;
				break;
			}
		}
	}
	else 
	{
/*		l2entry_t ->refCount = 1;*/
		SLIST_INSERT_HEAD(&(fdb_t->database[rowIdx]), l2entry_t, nextFDB);
	}
	
	/* TypeII entry can not exceed RTL8651_L2TBL_COLUMN */
/*	if (typeII == RTL8651_L2TBL_COLUMN && l2Type == RTL8651_L2_TYPEII) 
		return RTL_NOFREEBUFFER;
*/	
	//rtl865x_raiseEvent(EVENT_ADD_FDB, (void *)(l2entry_t));
	
	return SUCCESS;
}

/*
@func int32 | rtl8651_delFilterDatabaseEntry | Remove a filter database entry.
@parm uint16 | fid | Filter database entry.
@parm ether_addr_t * | macAddr | Pointer to a MAC Address.
@rvalue RTL_EINVALIDFID | Filter database ID.
@rvalue RTL_NULLMACADDR | The specified MAC address is NULL.
@rvalue RTL_EINVALIDINPUT | Invalid input parameter.
*/
int32 rtl865x_delFilterDatabaseEntry(uint16 l2Type, uint16 fid, ether_addr_t * macAddr) {
	int32 retval;
	unsigned long flags;
	if (fid >= RTL865x_FDB_NUMBER)
		return RTL_EINVALIDINPUT;
	if (macAddr == (ether_addr_t *)NULL)
		return RTL_EINVALIDINPUT;
	if (sw_FDB_Table.filterDB[fid].valid == 0)
		return RTL_EINVALIDFID;
	
/*	L2 lock		*/
	//rtl_down_interruptible(&l2_sem);
	local_irq_save(flags);
	retval = _rtl865x_delFilterDatabaseEntry(RTL865x_L2_TYPEII, fid, macAddr);
/*	L2 unlock		*/
	//rtl_up(&l2_sem);
	local_irq_restore(flags);
	return retval;
}

int32 _rtl865x_delFilterDatabaseEntry(uint16 l2Type, uint16 fid, ether_addr_t * macAddr) 
{
	uint32 rowIdx = 0;
	uint32 colIdx = 0;
	rtl865x_tblAsicDrv_l2Param_t L2temp, *L2buff ;
	rtl865x_filterDbTableEntry_t * l2entry_t = NULL;
	rtl865x_filterDbTable_t *fdb_t = &sw_FDB_Table.filterDB[fid];
	int32 found = FALSE;
	//printk("[%s][%d].\n", __FUNCTION__, __LINE__);
	rowIdx = rtl8651_filterDbIndex(macAddr, fid);

	L2buff = &L2temp;
	memset(L2buff, 0 , sizeof (rtl865x_tblAsicDrv_l2Param_t));
	for(colIdx=0; colIdx<RTL8651_L2TBL_COLUMN; colIdx++) 
	{
		if ((rtl8651_getAsicL2Table(rowIdx, colIdx, L2buff))==SUCCESS)
		{
			/*check whether mac address has been learned  to SW or not*/
			if (memcmp(&(L2buff->macAddr), macAddr, 6)== 0)
			{	
				/*the entry has been auto learned*/
				found = TRUE;
				break;
			}
		}
	}

	/*
	when a sta from eth0 to wlan0, the layer driver fdb will be deleted, but linux bridge fdb still exist,
	so delete the asic entry anyway to avoid layer connection issue.
	eg:
	first moment .sta is at eth0 
	second moment:connect sta to wlan0, the layer driver fdb will be deleted, but linux bridge fdb still there
	third moment:move sta to eth0 again, layer driver fdb won't be created due to linux bridge fdb already exist.
	*/
	if (found == TRUE)
	{
		rtl8651_delAsicL2Table(rowIdx, colIdx);
	}
	
	SLIST_FOREACH(l2entry_t, &(fdb_t->database[rowIdx]), nextFDB)
	{
		if (!memcmp(&l2entry_t->macAddr, macAddr, sizeof(ether_addr_t))) 
		{
	/*		l2entry_t->refCount -= 1;*/
	/*		if (l2entry_t->refCount == 0)*/
			{
				SLIST_REMOVE(
					&(fdb_t->database[rowIdx]), 
					l2entry_t, 
					 rtl865x_filterDbTableEntry_s,
					 nextFDB
				);
				SLIST_INSERT_HEAD(&sw_FDB_Table.freefdbList.filterDBentry, l2entry_t, nextFDB);	
				
				rtl865x_raiseEvent(EVENT_DEL_FDB, (void *)(l2entry_t));
			}
			break;
		}
	}
	return SUCCESS;	
}


int32 _rtl865x_ClearFDBEntryByPort(int32 port_num)
{
	int i, j;
	rtl865x_tblAsicDrv_l2Param_t l2entry_tmp,*L2buff;


	L2buff = &l2entry_tmp;
	
	for (i = 0; i < RTL8651_L2TBL_ROW; i++)
		for (j = 0; j < RTL8651_L2TBL_COLUMN; j++)
		{
			if ((rtl8651_getAsicL2Table(i, j, L2buff))!=SUCCESS)
				continue;
			
			if ((rtl865x_ConvertPortMasktoPortNum(L2buff->memberPortMask)) != port_num)
				continue;
			
			if (L2buff->isStatic == TRUE)
				continue;

			rtl8651_delAsicL2Table(i, j);
			_rtl865x_removeFilterDatabaseEntry(RTL_LAN_FID, &(L2buff->macAddr), i);
		}


	return SUCCESS;		

}

int32 rtl865x_LinkChange_Process(void)
{
	uint32 i, status;
	
	/* Check each port. */
	for ( i = 0; i < RTL8651_MAC_NUMBER; i++ )
	{
		/* Read Port Status Register to know the port is link-up or link-down. */
		status = READ_MEM32( PSRP0 + i * 4 );
		if ( ( status & PortStatusLinkUp ) == FALSE )
		{
			/* Link is down. */
			rtl8651_setAsicEthernetLinkStatus( i, FALSE );
		}
		else
		{
			/* Link is up. */
			rtl8651_setAsicEthernetLinkStatus( i, TRUE );
		}
	}

	return SUCCESS;

}

