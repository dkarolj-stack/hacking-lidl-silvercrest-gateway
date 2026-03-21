// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * flash.c - SPI flash driver (probe, read, write, erase)
 *
 * RTL8196E stage-2 bootloader
 *
 * Copyright (c) 2009-2020 Realtek Semiconductor Corp.
 * Copyright (c) 2024-2026 J. Nilo
 */

#include "boot_common.h"
#include "boot_soc.h"
#include "spi_common.h"
#include "spi_flash.h"
#include <rtl_types.h>

#define NDEBUG(args...) printf(args)
#define KDEBUG(args...)
#define LDEBUG(args...)

const char *g_flash_chip_name = "UNKNOWN";

static unsigned int g_flash_write_total = 0;
static unsigned int g_flash_write_done = 0;
static int g_flash_write_last_pct = -1;

static void flash_write_progress_add(unsigned int bytes);

// Reset flash-write progress counter
static void flash_write_progress_reset(unsigned int total)
{
	g_flash_write_total = total;
	g_flash_write_done = 0;
	g_flash_write_last_pct = -1;
	if (total == 0) {
		return;
	}
	flash_write_progress_add(0);
}

// Update flash-write progress by bytes written
static void flash_write_progress_add(unsigned int bytes)
{
	unsigned int pct;

	if (g_flash_write_total == 0) {
		return;
	}
	if (g_flash_write_done + bytes >= g_flash_write_total) {
		g_flash_write_done = g_flash_write_total;
	} else {
		g_flash_write_done += bytes;
	}
	pct = (g_flash_write_done * 100U) / g_flash_write_total;
	if ((int)pct != g_flash_write_last_pct) {
		g_flash_write_last_pct = (int)pct;
		NDEBUG("\rFlashing: %d%%", (int)pct);
	}
}

#define SIZEN_01M 0x14
#define SIZEN_02M 0x15
#define SIZEN_04M 0x16
#define SIZEN_08M 0x17
#define SIZEN_16M 0x18
#define SIZEN_32M 0x19
#define SIZEN_64M 0x20
#define SIZEN_CAL 0xff
#define SIZE_256B 0x100
#define SIZE_004K 0x1000
#define SIZE_064K 0x10000

/* SPI Flash Configuration Register(SFCR) (0xb800-1200) */
#define SFCR 0xb8001200 /*SPI Flash Configuration Register*/
#define SFCR_SPI_CLK_DIV(val) ((val) << 29)
#define SFCR_RBO(val) ((val) << 28)
#define SFCR_WBO(val) ((val) << 27)
// #define SFCR_SPI_TCS(val)		((val) << 23)	//8196B		/*4 bit,
// 1111 */
#define SFCR_SPI_TCS(val)                                                      \
	((val) << 22) // 8196C Later		/*5 bit, 11111 */

/* SPI Flash Configuration Register(SFCR2) (0xb800-1204) */
#define SFCR2 0xb8001204
#define SFCR2_SFCMD(val) ((val) << 24)	/*8 bit, 1111_1111 */
#define SFCR2_SFSIZE(val) ((val) << 21) /*3 bit, 111 */
#define SFCR2_RD_OPT(val) ((val) << 20)
#define SFCR2_CMD_IO(val) ((val) << 18)	     /*2 bit, 11 */
#define SFCR2_ADDR_IO(val) ((val) << 16)     /*2 bit, 11 */
#define SFCR2_DUMMY_CYCLE(val) ((val) << 13) /*3 bit, 111 */
#define SFCR2_DATA_IO(val) ((val) << 11)     /*2 bit, 11 */
#define SFCR2_HOLD_TILL_SFDR2(val) ((val) << 10)

/* SPI Flash Control and Status Register(SFCSR)(0xb800-1208) */
#define SFCSR 0xb8001208
#define SFCSR_SPI_CSB0(val) ((val) << 31)
#define SFCSR_SPI_CSB1(val) ((val) << 30)
#define SFCSR_LEN(val) ((val) << 28) /*2 bits*/
#define SFCSR_SPI_RDY(val) ((val) << 27)
#define SFCSR_IO_WIDTH(val) ((val) << 25) /*2 bits*/
#define SFCSR_CHIP_SEL(val) ((val) << 24)
#define SFCSR_CMD_BYTE(val) ((val) << 16) /*8 bit, 1111_1111 */

#define SFCSR_SPI_CSB(val) ((val) << 30)

/* SPI Flash Data Register(SFDR)(0xb800-120c) */
#define SFDR 0xb800120c

/* SPI Flash Data Register(SFDR2)(0xb8001210) */
#define SFDR2 0xb8001210

#define SPI_BLOCK_SIZE 0x10000 /* 64KB */
#define SPI_SECTOR_SIZE 0x1000 /* 4KB */
#define SPI_PAGE_SIZE 0x100    /* 256B */

#define FLASH_CHIP_PRIMARY 0

#define SPICMD_WREN                                                            \
	(0x06 << 24) /* 06 xx xx xx xx sets the (WEL) write enable latch bit   \
		      */
#define SPICMD_WRDI                                                            \
	(0x04 << 24) /* 04 xx xx xx xx resets the (WEL) write enable latch     \
			bit*/
#define SPICMD_RDID                                                            \
	(0x9f << 24) /* 9f xx xx xx xx outputs JEDEC ID: 1 byte manufacturer   \
			ID & 2 byte device ID */
#define SPICMD_RDSR                                                            \
	(0x05 << 24) /* 05 xx xx xx xx to read out the values of the status    \
			register */
#define SPICMD_FASTREAD                                                        \
	(0x0b << 24) /* 0b a1 a2 a3 dd n bytes read out until CS# goes high */
#define SPICMD_SE (0x20 << 24) /* 20 a1 a2 a3 xx to erase the selected sector  \
				*/
#define SPICMD_BE (0xd8 << 24) /* d8 a1 a2 a3 xx to erase the selected block   \
				*/
#define SPICMD_CE                                                              \
	(0x60 << 24) /* 60 xx xx xx xx to erase whole chip (cmd or 0xc7) */
#define SPICMD_PP (0x02 << 24) /* 02 a1 a2 a3 xx to program the selected page  \
				*/
#define SPI_STATUS_WIP 0x00	 /* write in process bit */

#define SPI_REG_READ(reg) *((volatile unsigned int *)(reg))
#define SPI_REG_LOAD(reg, val)                                                 \
	while ((*((volatile unsigned int *)SFCSR) & (SFCSR_SPI_RDY(1))) == 0)  \
		;                                                              \
	*((volatile unsigned int *)(reg)) = (val)

#define IOWIDTH_SINGLE 0x00
#define IOWIDTH_DUAL 0x01
#define IOWIDTH_QUAD 0x02
#define DATA_LENTH1 0x00
#define DATA_LENTH2 0x01
#define DATA_LENTH4 0x02
#define ISFAST_NO 0x00
#define ISFAST_YES 0x01
#define ISFAST_ALL 0x02
#define DUMMYCOUNT_0 0x00
#define DUMMYCOUNT_1 0x01
#define DUMMYCOUNT_2 0x02
#define DUMMYCOUNT_3 0x03
#define DUMMYCOUNT_4 0x04
#define DUMMYCOUNT_5 0x05
#define DUMMYCOUNT_6 0x06
#define DUMMYCOUNT_7 0x07
#define DUMMYCOUNT_8 0x08
#define DUMMYCOUNT_9 0x09

struct spi_flash_type spi_flash_info[2];
unsigned char ucSFCR2 = 154;

// Populate spi_flash_info[] entry from chip parameters
static void set_flash_info(unsigned char ucChip, unsigned int chip_id,
		    unsigned int device_cap, unsigned int block_size,
		    unsigned int sector_size, unsigned int page_size,
		    char *chip_name, FUNC_ERASE pfErase, FUNC_READ pfRead,
		    FUNC_SETQEBIT pfQeBit, FUNC_PAGEWRITE pfPageWrite)
{
	unsigned int ui = 1 << device_cap;
	spi_flash_info[ucChip].chip_id = chip_id;
	spi_flash_info[ucChip].mfr_id = (chip_id >> 16) & 0xff;
	spi_flash_info[ucChip].dev_id = (chip_id >> 8) & 0xff;
	spi_flash_info[ucChip].capacity_id = (chip_id) & 0xff;
	spi_flash_info[ucChip].size_shift =
	    calShift(spi_flash_info[ucChip].capacity_id, device_cap);
	spi_flash_info[ucChip].device_size = device_cap; // 2 ^ N (bytes)
	spi_flash_info[ucChip].chip_size = ui;
	spi_flash_info[ucChip].block_size = block_size;
	spi_flash_info[ucChip].block_cnt = ui / block_size;
	spi_flash_info[ucChip].sector_size = sector_size;
	spi_flash_info[ucChip].sector_cnt = ui / sector_size;
	spi_flash_info[ucChip].page_size = page_size;
	spi_flash_info[ucChip].page_cnt = sector_size / page_size;
	spi_flash_info[ucChip].chip_name = chip_name;
	g_flash_chip_name = chip_name;
	spi_flash_info[ucChip].pfErase = pfErase;
	spi_flash_info[ucChip].pfWrite = ComSrlCmd_ComWriteData;
	spi_flash_info[ucChip].pfRead = pfRead;
	spi_flash_info[ucChip].pfQeBit = pfQeBit;
	spi_flash_info[ucChip].pfPageWrite = pfPageWrite;
	// SPI_REG_LOAD(SFCR2, 0x0bb08000);
	LDEBUG("set_flash_info: ucChip=%x; chip_id=%x; device_cap=%x; "
	       "block_size=%x; sector_size=%x; page_size=%x; chip_name=%s\n",
	       ucChip, chip_id, device_cap, block_size, sector_size, page_size,
	       chip_name);
}
// Probe JEDEC ID and configure flash — hardcoded for GD25Q128
static void spi_regist(unsigned char ucChip)
{
	unsigned int ui;
	unsigned char pucBuffer[4];

	ui = ComSrlCmd_RDID(ucChip, 4);
	ui = ComSrlCmd_RDID(ucChip, 4);
	ui = ui >> 8;

	/* GD25Q128: 16MB, 84MHz, 64KB blocks, 4KB sectors, 256B pages */
	setFSCR(ucChip, 84, 1, 1, 31);
	set_flash_info(ucChip, ui, SIZEN_16M, SIZE_064K, SIZE_004K,
		       SIZE_256B, "GD25Q128", ComSrlCmd_SE,
		       mxic_cmd_read_s1, ComSrlCmd_NoneQeBit,
		       mxic_cmd_write_s1);

	spi_flash_info[ucChip].pfQeBit(ucChip);
	prnFlashInfo(ucChip, spi_flash_info[ucChip]);
	ui = spi_flash_info[ucChip].pfRead(ucChip, 0x00, 4, pucBuffer);
}

/****************************** Common function ******************************/
// Read DRAM clock frequency from SoC register (MHz)
unsigned int CheckDramFreq(void)
{
	unsigned short usFreqBit;
	unsigned short usFreqVal[] = {156, 193, 181, 231,
				      212, 125, 237, 168}; // 8196D
	usFreqBit = (0x00001C00 & (*(unsigned int *)0xb8000008)) >> 10;
	return usFreqVal[usFreqBit];
}
// Configure SPI clock divider in SFCR register
void setFSCR(unsigned char ucChip, unsigned int uiClkMhz, unsigned int uiRBO,
	     unsigned int uiWBO, unsigned int uiTCS)
{
	unsigned int ui, uiClk;
	uiClk = CheckDramFreq();
	ui = uiClk / uiClkMhz;
	if ((uiClk % uiClkMhz) > 0) {
		ui = ui + 1;
	}
	if ((ui % 2) > 0) {
		ui = ui + 1;
	}
	spi_flash_info[ucChip].chip_clk = uiClk / ui;
	SPI_REG_LOAD(SFCR, SFCR_SPI_CLK_DIV((ui - 2) / 2) | SFCR_RBO(uiRBO) |
			       SFCR_WBO(uiWBO) | SFCR_SPI_TCS(uiTCS));
	LDEBUG("setFSCR:uiClkMhz=%d, uiRBO=%d, uiWBO=%d, uiTCS=%d, resMhz=%d, "
	       "vale=%8x\n",
	       uiClkMhz, uiRBO, uiWBO, uiTCS, spi_flash_info[ucChip].chip_clk,
	       SPI_REG_READ(SFCR));
}
// Split a write range into start/middle/end sector-aligned parts
void calAddr(unsigned int uiStart, unsigned int uiLenth,
	     unsigned int uiSectorSize, unsigned int *uiStartAddr,
	     unsigned int *uiStartLen, unsigned int *uiSectorAddr,
	     unsigned int *uiSectorCount, unsigned int *uiEndAddr,
	     unsigned int *uiEndLen)
{
	unsigned int ui;
	// only one sector
	if ((uiStart + uiLenth) <
	    ((uiStart / uiSectorSize + 1) * uiSectorSize)) { // start
		*uiStartAddr = uiStart;
		*uiStartLen = uiLenth;
		// middle
		*uiSectorAddr = 0x00;
		*uiSectorCount = 0x00;
		// end
		*uiEndAddr = 0x00;
		*uiEndLen = 0x00;
	}
	// more then one sector
	else {
		// start
		*uiStartAddr = uiStart;
		*uiStartLen = uiSectorSize - (uiStart % uiSectorSize);
		if (*uiStartLen == uiSectorSize) {
			*uiStartLen = 0x00;
		}
		// middle
		ui = uiLenth - *uiStartLen;
		*uiSectorAddr = *uiStartAddr + *uiStartLen;
		*uiSectorCount = ui / uiSectorSize;
		// end
		*uiEndAddr = *uiSectorAddr + (*uiSectorCount * uiSectorSize);
		*uiEndLen = ui % uiSectorSize;
	}
	LDEBUG("calAddr:uiStart=%x; uiSectorSize=%x; uiLenth=%x;-> "
	       "uiStartAddr=%x; uiStartLen=%x; uiSectorAddr=%x; "
	       "uiSectorCount=%x; uiEndAddr=%x; uiEndLen=%x;\n",
	       uiStart, uiSectorSize, uiLenth, *uiStartAddr, *uiStartLen,
	       *uiSectorAddr, *uiSectorCount, *uiEndAddr, *uiEndLen);
}
// Compute capacity_id to device_size shift offset
unsigned char calShift(unsigned char ucCapacityId, unsigned char ucChipSize)
{
	unsigned int ui;
	if (ucChipSize > ucCapacityId) {
		ui = ucChipSize - ucCapacityId;
	} else {
		ui = ucChipSize + 0x100 - ucCapacityId;
	}
	LDEBUG("calShift: ucCapacityId=%x; ucChipSize=%x; ucReturnVal=%x\n",
	       ucCapacityId, ucChipSize, ui);
	return (unsigned char)ui;
}

// Publish detected flash chip name to global
void prnFlashInfo(unsigned char ucChip, struct spi_flash_type sftInfo)
{
	(void)ucChip;
	g_flash_chip_name = sftInfo.chip_name;
}

// Poll status register WIP bit until flash is ready
unsigned int spiFlashReady(unsigned char ucChip)
{
	unsigned int uiCount, ui;
	uiCount = 0;
	while (1) {
		uiCount++;
		ui = SeqCmd_Read(ucChip, IOWIDTH_SINGLE, SPICMD_RDSR, 1);
		if ((ui & (1 << SPI_STATUS_WIP)) == 0) {
			break;
		}
	}
	KDEBUG("spiFlashReady: uiCount=%x\n", uiCount);
	return uiCount;
}

// Toggle CS to reset SPI flash state machine
void rstSPIFlash(unsigned char ucChip)
{
	SFCSR_CS_L(ucChip, 0, IOWIDTH_SINGLE);
	SFCSR_CS_H(ucChip, 0, IOWIDTH_SINGLE);
	SFCSR_CS_L(ucChip, 0, IOWIDTH_SINGLE);
	SFCSR_CS_H(ucChip, 0, IOWIDTH_SINGLE);
	LDEBUG("rstFPIFlash: ucChip=%x;\n", ucChip);
}

// Assert chip-select (CS low) with given length and IO width
void SFCSR_CS_L(unsigned char ucChip, unsigned char ucLen,
		unsigned char ucIOWidth)
{
	LDEBUG("SFCSR_CS_L: ucChip=%x; uiLen=%x; ucIOWidth=%x;\n", ucChip,
	       ucLen, ucIOWidth);
	while ((*((volatile unsigned int *)SFCSR) & (SFCSR_SPI_RDY(1))) == 0)
		;
	*((volatile unsigned int *)(SFCSR)) =
	    SFCSR_SPI_CSB(1 + (ucChip)) | SFCSR_LEN(ucLen) | SFCSR_SPI_RDY(1) |
	    SFCSR_IO_WIDTH(ucIOWidth) | SFCSR_CHIP_SEL(0) | SFCSR_CMD_BYTE(5);
	// 20101215 *((volatile unsigned int *)(SFCSR)) = SFCSR_SPI_CSB(1 +
	// (ucChip)) | SFCSR_LEN(ucLen) | SFCSR_SPI_RDY(1) |
	// SFCSR_IO_WIDTH(ucIOWidth);
}

// Deassert chip-select (CS high)
void SFCSR_CS_H(unsigned char ucChip, unsigned char ucLen,
		unsigned char ucIOWidth)
{
	LDEBUG("SFCSR_CS_H: ucChip=%x; uiLen=%x; ucIOWidth=%x;\n", ucChip,
	       ucLen, ucIOWidth);
	if (ucLen == 0)
		ucLen = 1;
	while ((*((volatile unsigned int *)SFCSR) & (SFCSR_SPI_RDY(1))) == 0)
		;
	*((volatile unsigned int *)(SFCSR)) =
	    SFCSR_SPI_CSB(3) | SFCSR_LEN(ucLen) | SFCSR_SPI_RDY(1) |
	    SFCSR_IO_WIDTH(ucIOWidth) | SFCSR_CHIP_SEL(0) | SFCSR_CMD_BYTE(5);
	// 20101215 *((volatile unsigned int *)(SFCSR)) = SFCSR_SPI_CSB(3) |
	// SFCSR_LEN(ucLen) | SFCSR_SPI_RDY(1) |  SFCSR_IO_WIDTH(ucIOWidth);
}

// Read JEDEC ID (Command 9F) — returns 3-byte manufacturer+device ID
unsigned int ComSrlCmd_RDID(unsigned char ucChip, unsigned int uiLen)
{
	unsigned int ui;
	SPI_REG_LOAD(SFCR, (SFCR_SPI_CLK_DIV(7) | SFCR_RBO(1) | SFCR_WBO(1) |
			    SFCR_SPI_TCS(31))); // SFCR default setting
	rstSPIFlash(ucChip);
	SFCSR_CS_L(ucChip, 0, IOWIDTH_SINGLE);
	SPI_REG_LOAD(SFDR, SPICMD_RDID);
	SFCSR_CS_L(ucChip, (uiLen - 1), IOWIDTH_SINGLE);
	ui = SPI_REG_READ(SFDR);
	SFCSR_CS_H(ucChip, 0, IOWIDTH_SINGLE);
	LDEBUG("ComSrlCmd_RDID: ucChip=%x; uiLen=%x; returnValue=%x; "
	       "SPICMD_RDID=%x;\n",
	       ucChip, uiLen, ui, SPICMD_RDID);
	return ui;
}

// Send a single-byte SPI command (no data phase)
void SeqCmd_Order(unsigned char ucChip, unsigned char ucIOWidth,
		  unsigned int uiCmd)
{
	LDEBUG("SeqCmd_Type1: ucChip=%x; ucIOWidth=%x; SPICMD=%x;\n", ucChip,
	       ucIOWidth, uiCmd);
	SFCSR_CS_L(ucChip, ucIOWidth, IOWIDTH_SINGLE);
	SPI_REG_LOAD(SFDR, uiCmd);
	SFCSR_CS_H(ucChip, ucIOWidth, IOWIDTH_SINGLE);
}

// Send a SPI command followed by a data write
void SeqCmd_Write(unsigned char ucChip, unsigned char ucIOWidth,
		  unsigned int uiCmd, unsigned int uiValue,
		  unsigned char ucValueLen)
{
	SFCSR_CS_L(ucChip, DATA_LENTH1, ucIOWidth);
	SPI_REG_LOAD(SFDR, uiCmd);
	SFCSR_CS_L(ucChip, ucValueLen - 1, ucIOWidth);
	SPI_REG_LOAD(SFDR, (uiValue << ((4 - ucValueLen) * 8)));
	SFCSR_CS_H(ucChip, DATA_LENTH1, IOWIDTH_SINGLE);
	LDEBUG("SeqCmd_Write: ucChip=%x; ucIOWidth=%x; uiCmd=%x; uiValue=%x; "
	       "ucValueLen=%x;\n",
	       ucChip, ucIOWidth, uiCmd, uiValue, ucValueLen);
}

// Send a SPI command and read back data
unsigned int SeqCmd_Read(unsigned char ucChip, unsigned char ucIOWidth,
			 unsigned int uiCmd, unsigned char ucRDLen)
{
	unsigned int ui;
	SFCSR_CS_L(ucChip, DATA_LENTH1, ucIOWidth);
	SPI_REG_LOAD(SFDR, uiCmd);
	SFCSR_CS_L(ucChip, ucRDLen - 1, ucIOWidth);
	ui = SPI_REG_READ(SFDR);
	SFCSR_CS_H(ucChip, DATA_LENTH1, ucIOWidth);
	ui = ui >> ((4 - ucRDLen) * 8);
	LDEBUG("SeqCmd_Read: ucChip=%x; ucIOWidth=%x; uiCmd=%x; ucRDLen=%x; "
	       "RetVal=%x\n",
	       ucChip, ucIOWidth, uiCmd, ucRDLen, ui);
	return ui;
}

// Sector Erase (Command 20) — erase one 4 KB sector
unsigned int ComSrlCmd_SE(unsigned char ucChip, unsigned int uiAddr)
{
	SeqCmd_Order(ucChip, IOWIDTH_SINGLE, SPICMD_WREN);
	SeqCmd_Write(ucChip, IOWIDTH_SINGLE, SPICMD_SE, uiAddr, 3);
	KDEBUG("ComSrlCmd_SE: ucChip=%x; uiSector=%x; uiSectorSize=%x; "
	       "SPICMD_SE=%x\n",
	       ucChip, uiAddr, spi_flash_info[ucChip].sector_size, SPICMD_SE);
	return spiFlashReady(ucChip);
}

// Block Erase (Command D8) — erase one 64 KB block
unsigned int ComSrlCmd_BE(unsigned char ucChip, unsigned int uiAddr)
{
	SeqCmd_Order(ucChip, IOWIDTH_SINGLE, SPICMD_WREN);
	SeqCmd_Write(ucChip, IOWIDTH_SINGLE, SPICMD_BE, uiAddr, 3);
	KDEBUG("ComSrlCmd_BE: ucChip=%x; uiBlock=%x; uiBlockSize=%x; "
	       "SPICMD_BE=%x\n",
	       ucChip, uiAddr, spi_flash_info[ucChip].block_size, SPICMD_BE);
	return spiFlashReady(ucChip);
}

// Chip Erase (Command 60) — erase entire flash
unsigned int ComSrlCmd_CE(unsigned char ucChip)
{
	SeqCmd_Order(ucChip, IOWIDTH_SINGLE, SPICMD_WREN);
	SeqCmd_Order(ucChip, IOWIDTH_SINGLE, SPICMD_CE);
	KDEBUG("ComSrlCmd_CE: ucChip=%x; SPICMD_CE=%x\n", ucChip, SPICMD_CE);
	return spiFlashReady(ucChip);
}

// No-op QE bit handler — quad mode not used
unsigned int ComSrlCmd_NoneQeBit(unsigned char ucChip)
{
	KDEBUG("ComSrlCmd_NoneQeBit: ucChip=%x;\n", ucChip);
	return 0;
}

// Send SPI command + 3-byte address + dummy cycles
void ComSrlCmd_InputCommand(unsigned char ucChip, unsigned int uiAddr,
			    unsigned int uiCmd, unsigned char ucIsFast,
			    unsigned char ucIOWidth, unsigned char ucDummyCount)
{
	int i;
	LDEBUG("ComSrlCmd_InputCommand: ucChip=%x; uiAddr=%x; uiCmd=%x; "
	       "uiIsfast=%x; ucIOWidth=%x; ucDummyCount=%x\n",
	       ucChip, uiAddr, uiCmd, ucIsFast, ucIOWidth, ucDummyCount);

	// input command
	if (ucIsFast == ISFAST_ALL) {
		SFCSR_CS_L(ucChip, 0, ucIOWidth);
	} else {
		SFCSR_CS_L(ucChip, 0, IOWIDTH_SINGLE);
	}
	SPI_REG_LOAD(SFDR, uiCmd); // Read Command

	// input 3 bytes address
	if (ucIsFast == ISFAST_NO) {
		SFCSR_CS_L(ucChip, 0, IOWIDTH_SINGLE);
	} else {
		SFCSR_CS_L(ucChip, 0, ucIOWidth);
	}
	SPI_REG_LOAD(SFDR, (uiAddr << 8));
	SPI_REG_LOAD(SFDR, (uiAddr << 16));
	SPI_REG_LOAD(SFDR, (uiAddr << 24));

	// input dummy cycle
	for (i = 0; i < ucDummyCount; i++) {
		SPI_REG_LOAD(SFDR, 0);
	}

	SFCSR_CS_L(ucChip, 3, ucIOWidth);
}

// Configure SFCR2 for memory-mapped read mode
unsigned int SetSFCR2(unsigned int uiCmd, unsigned char ucIsFast,
		      unsigned char ucIOWidth, unsigned char ucDummyCount)
{
	unsigned int ui, uiDy;
	ucSFCR2 = 0;
	ui = SFCR2_SFCMD(uiCmd) |
	     SFCR2_SFSIZE(spi_flash_info[0].device_size - 17) |
	     SFCR2_RD_OPT(0) | SFCR2_HOLD_TILL_SFDR2(0);
	switch (ucIsFast) {
	case ISFAST_NO: {
		ui = ui | SFCR2_CMD_IO(IOWIDTH_SINGLE) |
		     SFCR2_ADDR_IO(IOWIDTH_SINGLE) | SFCR2_DATA_IO(ucIOWidth);
		uiDy = 1;
		break;
	}
	case ISFAST_YES: {
		ui = ui | SFCR2_CMD_IO(IOWIDTH_SINGLE) |
		     SFCR2_ADDR_IO(ucIOWidth) | SFCR2_DATA_IO(ucIOWidth);
		uiDy = ucIOWidth * 2;
		break;
	}
	case ISFAST_ALL: {
		ui = ui | SFCR2_CMD_IO(ucIOWidth) | SFCR2_ADDR_IO(ucIOWidth) |
		     SFCR2_DATA_IO(ucIOWidth);
		uiDy = ucIOWidth * 2;
		break;
	}
	default: {
		ui = ui | SFCR2_CMD_IO(IOWIDTH_SINGLE) |
		     SFCR2_ADDR_IO(IOWIDTH_SINGLE) | SFCR2_DATA_IO(ucIOWidth);
		uiDy = 1;
		break;
	}
	}
	if (uiDy == 0) {
		uiDy = 1;
	}
	ui = ui |
	     SFCR2_DUMMY_CYCLE((
		 ucDummyCount * 4 /
		 uiDy)); // ucDummyCount is Byte Count ucDummyCount*8 / (uiDy*2)
	SPI_REG_LOAD(SFCR2, ui);
	LDEBUG("SetSFCR2: uiCmd=%x; ucIsFast=%; ucIOWidth=%x; ucDummyCount=%x; "
	       "ucSFCR2=%x; SFCR2=%x\n;",
	       uiCmd, ucIsFast, ucIOWidth, ucDummyCount, ucSFCR2, ui);
	return ui;
}

// Generic SPI flash read — command + address + dummy + data
unsigned int ComSrlCmd_ComRead(unsigned char ucChip, unsigned int uiAddr,
			       unsigned int uiLen, unsigned char *pucBuffer,
			       unsigned int uiCmd, unsigned char ucIsFast,
			       unsigned char ucIOWidth,
			       unsigned char ucDummyCount)
{

	unsigned int ui, uiCount, i;
	unsigned char *puc = pucBuffer;
	LDEBUG(
	    "ComSrlCmd_ComRead: ucChip=%x; uiAddr=%x; uiLen=%x; pucBuffer=%x; "
	    "uiCmd=%x; uiIsfast=%x; ucIOWidth=%x; ucDummyCount=%x\n",
	    ucChip, uiAddr, uiLen, (unsigned int)pucBuffer, uiCmd, ucIsFast,
	    ucIOWidth, ucDummyCount);
	ComSrlCmd_InputCommand(ucChip, uiAddr, uiCmd, ucIsFast, ucIOWidth,
			       ucDummyCount);
	if (ucSFCR2 != 0) // set SFCR2
	{
		ui = SetSFCR2((uiCmd >> 24), ucIsFast, ucIOWidth, ucDummyCount);
	}

	uiCount = uiLen / 4;
	for (i = 0; i < uiCount; i++) // Read 4 bytes every time.
	{
		ui = SPI_REG_READ(SFDR);
		memcpy(puc, &ui, 4);
		puc += 4;
	}

	i = uiLen % 4;
	if (i > 0) {
		ui = SPI_REG_READ(SFDR); // another bytes.
		memcpy(puc, &ui, i);
		puc += i;
	}
	SFCSR_CS_H(ucChip, 0, IOWIDTH_SINGLE);
	return uiLen;
}

// Generic SPI flash write — WREN + command + address + data
unsigned int ComSrlCmd_ComWrite(unsigned char ucChip, unsigned int uiAddr,
				unsigned int uiLen, unsigned char *pucBuffer,
				unsigned int uiCmd, unsigned char ucIsFast,
				unsigned char ucIOWidth,
				unsigned char ucDummyCount)
{
	unsigned int ui, uiCount, i;
	unsigned char *puc = pucBuffer;
	LDEBUG(
	    "ComSrlCmd_ComWrite: ucChip=%x; uiAddr=%x; uiLen=%x; pucBuffer=%x; "
	    "uiCmd=%x; uiIsfast=%x; ucIOWidth=%x; ucDummyCount=%x\n",
	    ucChip, uiAddr, uiLen, (unsigned int)pucBuffer, uiCmd, ucIsFast,
	    ucIOWidth, ucDummyCount);
	SeqCmd_Order(ucChip, IOWIDTH_SINGLE, SPICMD_WREN);

	ComSrlCmd_InputCommand(ucChip, uiAddr, uiCmd, ucIsFast, ucIOWidth,
			       ucDummyCount);

	uiCount = uiLen / 4;
	for (i = 0; i < uiCount; i++) {
		memcpy(&ui, puc, 4);
		puc += 4;
		SPI_REG_LOAD(SFDR, ui);
	}

	i = uiLen % 4;
	if (i > 0) {
		memcpy(&ui, puc, i);
		puc += i;
		SFCSR_CS_L(ucChip, i - 1, ucIOWidth);
		SPI_REG_LOAD(SFDR, ui);
	}
	SFCSR_CS_H(ucChip, 0, IOWIDTH_SINGLE);
	ui = spiFlashReady(ucChip);
	return uiLen;
}

// Erase and rewrite one full sector (page by page)
unsigned int ComSrlCmd_ComWriteSector(unsigned char ucChip, unsigned int uiAddr,
				      unsigned char *pucBuffer)
{
	unsigned int i, ui;
	unsigned char *puc = pucBuffer;
	LDEBUG("ComSrlCmd_ComWriteSector: ucChip=%x; uiAddr=%x; pucBuffer=%x; "
	       "returnValue=%x;\n",
	       ucChip, uiAddr, (unsigned int)pucBuffer,
	       spi_flash_info[ucChip].sector_size);
	// prnDispAddr(uiAddr);

	ui = spi_flash_info[ucChip].pfErase(ucChip, uiAddr);
	for (i = 0; i < spi_flash_info[ucChip].page_cnt; i++) {
		ui = spi_flash_info[ucChip].pfPageWrite(
		    ucChip, uiAddr, spi_flash_info[ucChip].page_size, puc);
		uiAddr += spi_flash_info[ucChip].page_size;
		puc += spi_flash_info[ucChip].page_size;
	}
	return spi_flash_info[ucChip].sector_size;
}

// Read-modify-write a partial sector via temp buffer
unsigned int ComSrlCmd_BufWriteSector(unsigned char ucChip, unsigned int uiAddr,
				      unsigned int uiLen,
				      unsigned char *pucBuffer)
{
	unsigned char pucSector[spi_flash_info[ucChip].sector_size];
	unsigned int ui, uiStartAddr, uiOffset;
	LDEBUG("ComSrlCmd_BufWriteSector:ucChip=%x; uiAddr=%x; uiLen=%x; "
	       "pucBuffer=%x;\n",
	       ucChip, uiAddr, uiLen, pucBuffer);
	uiOffset = uiAddr % spi_flash_info[ucChip].sector_size;
	uiStartAddr = uiAddr - uiOffset;
	// get
	ui = spi_flash_info[ucChip].pfRead(
	    ucChip, uiStartAddr, spi_flash_info[ucChip].sector_size, pucSector);
	// modify
	memcpy(pucSector + uiOffset, pucBuffer, uiLen);
	// write back
	ui = ComSrlCmd_ComWriteSector(ucChip, uiStartAddr, pucSector);
	return ui;
}

// Write arbitrary data to flash — handles sector alignment
unsigned int ComSrlCmd_ComWriteData(unsigned char ucChip, unsigned int uiAddr,
				    unsigned int uiLen,
				    unsigned char *pucBuffer)
{
	unsigned int uiStartAddr, uiStartLen, uiSectorAddr, uiSectorCount,
	    uiEndAddr, uiEndLen, i;
	unsigned char *puc = pucBuffer;
	LDEBUG("ComSrlCmd_ComWriteData:ucChip=%x; uiAddr=%x; uiLen=%x; "
	       "pucBuffer=%x\n",
	       ucChip, uiAddr, uiLen, (unsigned int)pucBuffer);
	flash_write_progress_reset(uiLen);
	calAddr(uiAddr, uiLen, spi_flash_info[ucChip].sector_size, &uiStartAddr,
		&uiStartLen, &uiSectorAddr, &uiSectorCount, &uiEndAddr,
		&uiEndLen);
	if ((uiSectorCount == 0x00) &&
	    (uiEndLen == 0x00)) // all data in the same sector
	{
		ComSrlCmd_BufWriteSector(ucChip, uiStartAddr, uiStartLen, puc);
		flash_write_progress_add(uiStartLen);
	} else {
		if (uiStartLen > 0) {
			ComSrlCmd_BufWriteSector(ucChip, uiStartAddr,
						 uiStartLen, puc);
			flash_write_progress_add(uiStartLen);
			puc += uiStartLen;
		}
		for (i = 0; i < uiSectorCount; i++) {
			ComSrlCmd_ComWriteSector(ucChip, uiSectorAddr, puc);
			flash_write_progress_add(
			    spi_flash_info[ucChip].sector_size);
			puc += spi_flash_info[ucChip].sector_size;
			uiSectorAddr += spi_flash_info[ucChip].sector_size;
		}
		if (uiEndLen > 0) {
			ComSrlCmd_BufWriteSector(ucChip, uiEndAddr, uiEndLen,
						 puc);
			flash_write_progress_add(uiEndLen);
		}
	}
	SeqCmd_Order(ucChip, IOWIDTH_SINGLE, SPICMD_WRDI);
	return uiLen;
}

// Fast Read (Command 0B) — single-IO with 1 dummy byte
unsigned int mxic_cmd_read_s1(unsigned char ucChip, unsigned int uiAddr,
			      unsigned int uiLen, unsigned char *pucBuffer)
{
	KDEBUG("mxic_cmd_read_s1: ucChip=%x; uiAddr=%x; uiLen=%x; "
	       "pucBuffer=%x; SPICMD_FASTREAD=%x;\n",
	       ucChip, uiAddr, uiLen, (unsigned int)pucBuffer, SPICMD_FASTREAD);
	return ComSrlCmd_ComRead(ucChip, uiAddr, uiLen, pucBuffer,
				 SPICMD_FASTREAD, ISFAST_YES, IOWIDTH_SINGLE,
				 DUMMYCOUNT_1);
}
// Page Program (PP) Sequence (Command 02) — single-IO
unsigned int mxic_cmd_write_s1(unsigned char ucChip, unsigned int uiAddr,
			       unsigned int uiLen, unsigned char *pucBuffer)
{
	KDEBUG("mxic_cmd_write_s1: ucChip=%x; uiAddr=%x; uiLen=%x; "
	       "pucBuffer=%x; SPICMD_PP=%x;\n",
	       ucChip, uiAddr, uiLen, (unsigned int)pucBuffer, SPICMD_PP);
	return ComSrlCmd_ComWrite(ucChip, uiAddr, uiLen, pucBuffer, SPICMD_PP,
				  ISFAST_NO, IOWIDTH_SINGLE, DUMMYCOUNT_0);
}

/* ===== SPI flash top-level API (spi_flash.c) ===== */

/**
 * spi_pio_init - Switch SPI flash controller to PIO mode
 *
 * Configures the SFCR register for programmed I/O access, used
 * before direct flash write/erase operations.
 */
void spi_pio_init(void)
{
	KDEBUG("spi_pio_init: rstSPIFlash(0)");
	rstSPIFlash(0);
}

/**
 * spi_probe - Detect and register SPI flash chip(s)
 *
 * Reads the JEDEC ID via RDID command, configures read mode and
 * timing for GD25Q128.  Sets the global g_flash_chip_name.
 */
void spi_probe(void)
{
	int i;
	KDEBUG("spi_probe: spi_regist(0, 1)\n");
	for (i = 0; i < 1; i++) {
		spi_regist(i);
	}
}

/**
 * spi_sector_erase - Erase a 4 KB sector
 * @uiChip: flash chip index (0 or 1)
 * @uiAddr: byte offset of the sector to erase
 *
 * Return: non-zero on success
 */
unsigned int spi_sector_erase(unsigned int uiChip, unsigned int uiAddr)
{
	KDEBUG("spi_sector_erase: uiChip=%x; uiAddr=%x\n", uiChip, uiAddr);
	return spi_flash_info[uiChip].pfErase(uiChip, uiAddr);
}

/**
 * spi_block_erase - Erase a 64 KB block
 * @uiChip: flash chip index (0 or 1)
 * @uiAddr: byte offset of the block to erase
 *
 * Return: non-zero on success
 */
unsigned int spi_block_erase(unsigned int uiChip, unsigned int uiAddr)
{
	KDEBUG("spi_block_erase: uiChip=%x; uiAddr=%x\n", uiChip, uiAddr);
	return ComSrlCmd_BE(uiChip, uiAddr);
}

/**
 * spi_erase_chip - Erase the entire flash chip
 * @uiChip: flash chip index (0 or 1)
 *
 * Return: non-zero on success
 */
unsigned int spi_erase_chip(unsigned int uiChip)
{
	// Spansion
	KDEBUG("spi_erase_chip: uiChip=%x\n", uiChip);
	return ComSrlCmd_CE(uiChip);
}

// Read 4 bytes from flash (legacy interface)
unsigned int spi_read(unsigned int uiChip, unsigned int uiAddr,
		      unsigned int *puiDataOut)
{
	KDEBUG("spi_read: uiChip=%x; uiAddr=%x; uiLen=4; puiDataOut=%x\n",
	       uiChip, uiAddr, (unsigned long)puiDataOut);
	return spi_flash_info[uiChip].pfRead(uiChip, uiAddr, 4,
					     (unsigned char *)puiDataOut);
}

/**
 * flashread - Read data from SPI flash into RAM
 * @dst: destination RAM address
 * @src: source flash offset (relative to flash base)
 * @length: number of bytes to read
 *
 * Return: 1 on success
 */
int flashread(unsigned long dst, unsigned int src, unsigned long length)
{

	KDEBUG("flashread: chip=%d; dst=%x; src=%x; length=%x\n",
	       FLASH_CHIP_PRIMARY, dst, src, length);
	return spi_flash_info[FLASH_CHIP_PRIMARY].pfRead(
	    FLASH_CHIP_PRIMARY, src, length, (unsigned char *)dst);
}

/**
 * flashwrite - Write data from RAM to SPI flash
 * @dst: destination flash offset
 * @src: source RAM address
 * @length: number of bytes to write
 *
 * Return: 1 on success
 */
int flashwrite(unsigned long dst, unsigned long src, unsigned long length)
{

	KDEBUG("flashwrite: chip=%d; dst=%x; src=%x; length=%x\n",
	       FLASH_CHIP_PRIMARY, dst, src, length);
	return spi_flash_info[FLASH_CHIP_PRIMARY].pfWrite(
	    FLASH_CHIP_PRIMARY, dst, length, (unsigned char *)src);
}

/**
 * spi_flw_image - Write a firmware image to flash (single-IO mode)
 * @chip: flash chip index
 * @flash_addr_offset: destination offset in flash
 * @image_addr: source data in RAM
 * @image_size: number of bytes to write
 *
 * Erases the target region sector-by-sector, then writes the image
 * data using page programming.
 *
 * Return: 1 on success, 0 on failure
 */
int spi_flw_image(unsigned int chip, unsigned int flash_addr_offset,
		  unsigned char *image_addr, unsigned int image_size)
{
	KDEBUG("spi_flw_image: chip=%x; flash_addr_offset=%x; image_addr=%x; "
	       "image_size=%x\n",
	       chip, flash_addr_offset, (unsigned int)image_addr, image_size);
	return spi_flash_info[chip].pfWrite(chip, flash_addr_offset, image_size,
					    image_addr);
}
/**
 * spi_flw_image_mio_8198 - Write a firmware image to flash (multi-IO)
 * @cnt: flash chip index
 * @flash_addr_offset: destination offset in flash
 * @image_addr: source data in RAM
 * @image_size: number of bytes to write
 *
 * Similar to spi_flw_image() but uses the 8198-style multi-IO write
 * path with progress reporting.
 *
 * Return: 1 on success, 0 on failure
 */
int spi_flw_image_mio_8198(unsigned int cnt, unsigned int flash_addr_offset,
			   unsigned char *image_addr, unsigned int image_size)
{
	KDEBUG("spi_flw_image_mio_8198: cnt=%x; flash_addr_offset=%x; "
	       "image_addr=%x; image_size=%x\n",
	       cnt, flash_addr_offset, (unsigned int)image_addr, image_size);
	return spi_flash_info[cnt].pfWrite(cnt, flash_addr_offset, image_size,
					   image_addr);
}
