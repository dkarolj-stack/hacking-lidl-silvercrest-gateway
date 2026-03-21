/*
* ----------------------------------------------------------------
* Copyright  Realtek Semiconductor Corporation, 2002
* All rights reserved.
*
*
* Abstract: Switch core polling mode NIC header file.
*

*
* ---------------------------------------------------------------
*/

#ifndef _SWNIC_TABLE_H
#define _SWNIC_TABLE_H

void tableAccessForeword(uint32, uint32, void *);
int32 swTable_addEntry(uint32 tableType, uint32 eidx, void *entryContent_P);
int32 swTable_modifyEntry(uint32 tableType, uint32 eidx, void *entryContent_P);
int32 swTable_forceAddEntry(uint32 tableType, uint32 eidx,
			    void *entryContent_P);
int32 swTable_readEntry(uint32 tableType, uint32 eidx, void *entryContent_P);

#endif /* _SWNIC_H */
