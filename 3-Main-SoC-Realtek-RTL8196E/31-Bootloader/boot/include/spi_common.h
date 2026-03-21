/* SPI Flash driver
 *
 * Written by sam (sam@realtek.com)
 * 2010-05-01
 *
 */

// #define SPI_KERNEL 1

extern const char *g_flash_chip_name;

typedef unsigned int (*FUNC_ERASE)(unsigned char ucChip, unsigned int uiAddr);
typedef unsigned int (*FUNC_READ)(unsigned char ucChip, unsigned int uiAddr,
				  unsigned int uiLen, unsigned char *pucBuffer);
typedef unsigned int (*FUNC_WRITE)(unsigned char ucChip, unsigned int uiAddr,
				   unsigned int uiLen,
				   unsigned char *pucBuffer);
typedef unsigned int (*FUNC_SETQEBIT)(unsigned char ucChip);
typedef unsigned int (*FUNC_PAGEWRITE)(unsigned char ucChip,
				       unsigned int uiAddr, unsigned int uiLen,
				       unsigned char *pucBuffer);

struct spi_flash_type {
	unsigned int chip_id;
	unsigned char mfr_id;
	unsigned char dev_id;

	unsigned char capacity_id;
	unsigned char size_shift;

	unsigned char device_size; // 2 ^ N (bytes)
	unsigned int chip_size;

	unsigned int block_size;
	unsigned int block_cnt;

	unsigned int sector_size;
	unsigned int sector_cnt;

	unsigned int page_size;
	unsigned int page_cnt;
	unsigned int chip_clk;
	char *chip_name;

	unsigned int uiClk;
	FUNC_ERASE pfErase;
	FUNC_WRITE pfWrite;
	FUNC_READ pfRead;
	FUNC_SETQEBIT pfQeBit;
	FUNC_PAGEWRITE pfPageWrite;
};

/****************************** Common ******************************/
// get Dram Frequence
unsigned int CheckDramFreq(void); // JSW:For 8196C
// Set FSCR register
void setFSCR(unsigned char ucChip, unsigned int uiClkMhz, unsigned int uiRBO,
	     unsigned int uiWBO, unsigned int uiTCS);
// Calculate write address group
void calAddr(unsigned int uiStart, unsigned int uiLenth,
	     unsigned int uiSectorSize, unsigned int *uiStartAddr,
	     unsigned int *uiStartLen, unsigned int *uiSectorAddr,
	     unsigned int *uiSectorCount, unsigned int *uiEndAddr,
	     unsigned int *uiEndLen);
// Calculate chip capacity shift bit
unsigned char calShift(unsigned char ucCapacityId, unsigned char ucChipSize);
// Print spi_flash_type
void prnFlashInfo(unsigned char ucChip, struct spi_flash_type sftInfo);
// Print SPI Register
// void prnInterfaceInfo();
// pirnt when writing
// void prnDispAddr(unsigned int uiAddr);
// Check WIP bit
unsigned int spiFlashReady(unsigned char ucChip);
// toggle CS
void rstSPIFlash(unsigned char ucChip);

/****************************** Layer 1 ******************************/
// set cs low
void SFCSR_CS_L(unsigned char ucChip, unsigned char ucLen,
		unsigned char ucIOWidth);
// set cs high
void SFCSR_CS_H(unsigned char ucChip, unsigned char ucLen,
		unsigned char ucIOWidth);
// Read Identification (RDID) Sequence (Command 9F)
unsigned int ComSrlCmd_RDID(unsigned char ucChip, unsigned int uiLen);
// One byte Command
void SeqCmd_Order(unsigned char ucChip, unsigned char ucIOWidth,
		  unsigned int uiCmd);
// One byte Command Write
void SeqCmd_Write(unsigned char ucChip, unsigned char ucIOWidth,
		  unsigned int uiCmd, unsigned int uiValue,
		  unsigned char ucValueLen);
// One byte Command Read
unsigned int SeqCmd_Read(unsigned char ucChip, unsigned char ucIOWidth,
			 unsigned int uiCmd, unsigned char ucRDLen);

/****************************** Layer 2 ******************************/
// Sector Erase (SE) Sequence (Command 20)
unsigned int ComSrlCmd_SE(unsigned char ucChip, unsigned int uiAddr);
// Block Erase (BE) Sequence (Command D8)
unsigned int ComSrlCmd_BE(unsigned char ucChip, unsigned int uiAddr);
// Chip Erase (CE) Sequence (Command 60 or C7)
unsigned int ComSrlCmd_CE(unsigned char ucChip);
// without QE bit
unsigned int ComSrlCmd_NoneQeBit(unsigned char ucChip);
// uiIsFast: = 0 cmd, address, dummy single IO ; =1 cmd single IO, address and
// dummy multi IO; =2 cmd, address and dummy multi IO;
void ComSrlCmd_InputCommand(unsigned char ucChip, unsigned int uiAddr,
			    unsigned int uiCmd, unsigned char ucIsFast,
			    unsigned char ucIOWidth,
			    unsigned char ucDummyCount);
// Set SFCR2 for memery map read
unsigned int SetSFCR2(unsigned int uiCmd, unsigned char ucIsFast,
		      unsigned char ucIOWidth, unsigned char ucDummyCount);
// read function template
unsigned int ComSrlCmd_ComRead(unsigned char ucChip, unsigned int uiAddr,
			       unsigned int uiLen, unsigned char *pucBuffer,
			       unsigned int uiCmd, unsigned char ucIsFast,
			       unsigned char ucIOWidth,
			       unsigned char ucDummyCount);
// write template
unsigned int ComSrlCmd_ComWrite(unsigned char ucChip, unsigned int uiAddr,
				unsigned int uiLen, unsigned char *pucBuffer,
				unsigned int uiCmd, unsigned char ucIsFast,
				unsigned char ucIOWidth,
				unsigned char ucDummyCount);
// write a sector once
unsigned int ComSrlCmd_ComWriteSector(unsigned char ucChip, unsigned int uiAddr,
				      unsigned char *pucBuffer);
// write sector use malloc buffer
unsigned int ComSrlCmd_BufWriteSector(unsigned char ucChip, unsigned int uiAddr,
				      unsigned int uiLen,
				      unsigned char *pucBuffer);
// write function
unsigned int ComSrlCmd_ComWriteData(unsigned char ucChip, unsigned int uiAddr,
				    unsigned int uiLen,
				    unsigned char *pucBuffer);

/****************************** JEDEC read/write ******************************/
// Fast Read (Command 0B) — single-IO with 1 dummy byte
unsigned int mxic_cmd_read_s1(unsigned char ucChip, unsigned int uiAddr,
			      unsigned int uiLen, unsigned char *pucBuffer);
// Page Program (PP) Sequence (Command 02) — single-IO
unsigned int mxic_cmd_write_s1(unsigned char ucChip, unsigned int uiAddr,
			       unsigned int uiLen, unsigned char *pucBuffer);
