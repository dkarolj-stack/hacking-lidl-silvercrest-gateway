#include "start.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

#define REG32(reg) (*(volatile unsigned int *)(reg))

#define CHECK_DCR_READY()                                                      \
	{                                                                      \
		while (REG32(DCR) & 1) {                                       \
		};                                                             \
	} // 1=busy, 0=ready
//---------------------------------------------------
void uart_outc(char c)
{
	while (1) {
		if (REG32(UART_LSR) & 0x60000000)
			break;
	}

	REG32(UART_THR) = (unsigned int)(c) << 24;

	if (c == 0x0a)
		REG32(UART_THR) = (unsigned int)(0x0d) << 24;
}
//-----------------------------------------------------
static inline unsigned char uart_inc(void)
{
	unsigned register ch;

	while (1) {
		if (REG32(UART_LSR) & (1 << 24))
			break;
	}
	ch = REG32(UART_RBR);
	ch = ch >> 24;
	return ch;
}
//-----------------------------------------------------
unsigned int kbhit(unsigned int loops)
{
	int i;
	for (i = 0; i < loops; i++) {
		if (REG32(UART_LSR) & (1 << 24))
			return 1;
	}
	return 0;
}
//-----------------------------------------------------

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap) __builtin_va_end(ap)
#define va_arg(ap, type) __builtin_va_arg(ap, type)

struct print_ctx {
	char **buf;
	int count;
};

static void outc(struct print_ctx *ctx, char c)
{
	if (ctx->buf && *ctx->buf) {
		*(*ctx->buf)++ = c;
	} else {
		uart_outc(c);
	}
	ctx->count++;
}

static void outs(struct print_ctx *ctx, const char *s, int width, char pad)
{
	int len = 0;
	const char *p = s;
	int i;

	if (!s)
		s = "(null)";

	while (*p++)
		len++;
	if (width > len) {
		for (i = 0; i < width - len; i++)
			outc(ctx, pad);
	}
	for (; *s; s++)
		outc(ctx, *s);
}

static void outnum(struct print_ctx *ctx, unsigned long val, int base,
		   int width, char pad, int upper, int neg)
{
	char tmp[32];
	int len = 0;
	int i;
	int total, padlen;

	do {
		unsigned int d = (unsigned int)(val % (unsigned long)base);
		val /= (unsigned long)base;
		if (d < 10)
			tmp[len++] = (char)('0' + d);
		else
			tmp[len++] = (char)((upper ? 'A' : 'a') + (d - 10));
	} while (val);

	total = len + (neg ? 1 : 0);
	padlen = (width > total) ? (width - total) : 0;

	if (pad != '0') {
		for (i = 0; i < padlen; i++)
			outc(ctx, pad);
	}
	if (neg)
		outc(ctx, '-');
	if (pad == '0') {
		for (i = 0; i < padlen; i++)
			outc(ctx, pad);
	}
	while (len--)
		outc(ctx, tmp[len]);
}

static int vprintf_internal(struct print_ctx *ctx, const char *fmt, va_list ap)
{
	ctx->count = 0;
	for (; *fmt; fmt++) {
		int width = 0;
		char pad = ' ';

		if (*fmt != '%') {
			outc(ctx, *fmt);
			continue;
		}
		fmt++;
		if (*fmt == '%') {
			outc(ctx, '%');
			continue;
		}

		if (*fmt == '0') {
			pad = '0';
			fmt++;
		}
		while (*fmt >= '0' && *fmt <= '9') {
			width = width * 10 + (*fmt - '0');
			fmt++;
		}

		switch (*fmt) {
		case 'c':
			outc(ctx, (char)va_arg(ap, int));
			break;
		case 's':
			outs(ctx, va_arg(ap, const char *), width, pad);
			break;
		case 'd': {
			int v = va_arg(ap, int);
			unsigned long u =
			    (v < 0) ? (unsigned long)(-v) : (unsigned long)v;
			outnum(ctx, u, 10, width, pad, 0, v < 0);
			break;
		}
		case 'u': {
			unsigned int v = va_arg(ap, unsigned int);
			outnum(ctx, v, 10, width, pad, 0, 0);
			break;
		}
		case 'x':
		case 'X': {
			unsigned int v = va_arg(ap, unsigned int);
			outnum(ctx, v, 16, width, pad, (*fmt == 'X'), 0);
			break;
		}
		default:
			outc(ctx, *fmt);
			break;
		}
	}
	return ctx->count;
}

int vsprintf(char *buf, const char *fmt, va_list ap)
{
	struct print_ctx ctx;
	char *p = buf;
	int ret;

	ctx.buf = buf ? &p : NULL;
	ret = vprintf_internal(&ctx, fmt, ap);
	if (buf)
		*p = '\0';
	return ret;
}

int printf(const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vsprintf(NULL, fmt, ap);
	va_end(ap);
	return ret;
}
//--------------------------------------------------

//==========================================

const unsigned int SDR_DTR_TAB[][4] = {
    0,		337,	    312,	250,	    2,		0x6ce2a5a0,
    0x48c26190, 0x48a1f910, 8,		0x6ce2a5a0, 0x48c26190, 0x48a1f910,
    16,		0x6ce2a5a0, 0x48c26190, 0x48a1f910, 32,		0x6ce2a520,
    0x48c26110, 0x48a1f890, 64,		0x6ce2a520, 0x48c26110, 0x48a1f890};
#define SDR_DTR_TAB_ROW 6
#define SDR_DTR_TAB_COL 4

//--------------------------------------------------

const unsigned int DDR1_DTR_TAB[][9] = {
    0,		475,	    462,	425,	    387,	362,
    337,	312,	    250,	16,	    0x912435B0, 0x912435B0,
    0x9103ADB0, 0x6CE369A0, 0x6CE329A0, 0x48c2e5a0, 0x48c521a0, 0x24a21990,
    32,		0x91243530, 0x91243530, 0x9103AD30, 0x6CE36920, 0x6CE32920,
    0x48c2e520, 0x48c52120, 0x24a21910, 64,	    0x91243530, 0x91243530,
    0x9103AD30, 0x6CE36920, 0x6CE32920, 0x48c2e520, 0x48c52120, 0x24a21910,
    128,	0x91273530, 0x9126F530, 0x91066D30, 0x6CE5E920, 0x6CE56920,
    0x48c52520, 0x48c4a120, 0x24a3d910};
#define DDR1_DTR_TAB_ROW 5
#define DDR1_DTR_TAB_COL 9

//--------------------------------------------------

const unsigned int DDR2_DTR_TAB[][9] = {
    0,		475,	    462,	425,	    387,	362,
    337,	312,	    250,	16,	    0x914475B0, 0x914475B0,
    0x9123EDB0, 0x6D03A9A0, 0x6D0369A0, 0x48c325a0, 0x48c2e1a0, 0x24a25990,
    32,		0x91447530, 0x91447530, 0x9123ED30, 0x6D03A920, 0x6D036920,
    0x48c32520, 0x48c2e120, 0x24a25910, 64,	    0x91463530, 0x91463530,
    0x9125AD30, 0x6D052920, 0x6D04E920, 0x48c46520, 0x48c42120, 0x24a35910,
    128,	0x9147B530, 0x91477530, 0x9126ED30, 0x6D062920, 0x6D05E920,
    0x48c56520, 0x48c52120, 0x24a41910, 256,	    0x914BB530, 0x914B7530,
    0x912A6D30, 0x6D09A920, 0x6D08E920, 0x48c86520, 0x48c7e120, 0x24a61910,
    512,	0x91337530, 0x9132F530, 0x91116D30, 0x6CEFE920, 0x6CEEE920,
    0x48ce2520, 0x48cd2120, 0x24aa1910,
};
#define DDR2_DTR_TAB_ROW 5
#define DDR2_DTR_TAB_COL 9

//====================================================================
const unsigned int SDR_DCR_TAB[][2] = {
    2,	0x50000000, 8,	0x52080000, 16,	 0x52480000,
    32, 0x54480000, 64, 0x54880000, 128, 0x54880000,
};
#define SDR_DCR_TAB_COL 2
#define SDR_DCR_TAB_ROW 6
//--------------------------------------------------
#define DRAM_SDR 0
#define DRAM_DDR1 1
#define DRAM_DDR2 2

unsigned char DRAM_SIZE_TAB[4][3] = {16, 16, 16, 32, 32,  32,
				     8,	 64, 64, 2,  128, 128};

//---------------------------------------------------

//==========================================================================
// Use this c code, be careful.
void LookUp_MemTimingTable(unsigned int dramtype, unsigned int dramsize,
			   unsigned int m2xclk)
{

	unsigned int dcr, dtr, i, ext = 0;
	unsigned int row = 0, col = 0;
	if (dramtype == DRAM_SDR) {
		for (i = 1; i < SDR_DTR_TAB_ROW; i++) {
			if (SDR_DTR_TAB[i][0] == dramsize)
				row = i;
		}
		for (i = 1; i < SDR_DTR_TAB_COL; i++) {
			if (SDR_DTR_TAB[0][i] == m2xclk)
				col = i;
		}
		dtr = SDR_DTR_TAB[row][col];

		printf("SDR DTR=%x\n", dtr);
		REG32(0xb8001008) = dtr; // ASIC
	} else if (dramtype == DRAM_DDR1) {
		for (i = 1; i < DDR1_DTR_TAB_ROW; i++) {
			if (DDR1_DTR_TAB[i][0] == dramsize)
				row = i;
		}
		for (i = 1; i < DDR1_DTR_TAB_COL; i++) {
			if (DDR1_DTR_TAB[0][i] == m2xclk)
				col = i;
		}
		dtr = DDR1_DTR_TAB[row][col];

		printf("DDR1 DTR=%x\n", dtr);
		REG32(0xb8001008) = dtr; // ASIC

	} else if (dramtype == DRAM_DDR2) {

		for (i = 1; i < DDR2_DTR_TAB_ROW; i++) {
			if (DDR2_DTR_TAB[i][0] == dramsize)
				row = i;
		}
		for (i = 1; i < DDR2_DTR_TAB_COL; i++) {
			if (DDR2_DTR_TAB[0][i] == m2xclk)
				col = i;
		}
		dtr = DDR2_DTR_TAB[row][col];

		printf("DDR2 DTR=%x\n", dtr);
		REG32(0xb8001008) = dtr; // ASIC
	}

	REG32(0xb8001004) = REG32(0xb8001004); // DCR
}

//==============================================================
//==============================================================
unsigned int Calc_Dram_Size(unsigned int dramtype)
{
#define ROW_MIN 11
#define ROW_MAX 14
#define COL_MIN 8
#define COL_MAX 12

	unsigned int cs1 = 0;
	// power 2
	unsigned int width = 1; // fix 16 bit
	unsigned int bank;
	unsigned int row;
	unsigned int col;

	unsigned int DCR_VALUE = (1 << 30) | (width << 28); // CL3

#define DCR 0xb8001004
#define EDTCR 0xb800100c
#define WRITE_DATA 0x12345678

	printf("w%d,", width);

	//------------------------------------
	// find bank
	REG32(DCR) =
	    DCR_VALUE |
	    (1 << 19); //[0]:2 bank,  [1]: 4 bank,  set max BANK=1(4 bank)
	CHECK_DCR_READY();

	// W1*R11*C8=1M bytes
	volatile unsigned int *t1 =
	    (volatile unsigned int *)(0xa0000000 | (1U << (width + 19)));
	volatile unsigned int *t3 =
	    (volatile unsigned int *)(0xa0000000 | (3U << (width + 19)));
	volatile unsigned int *t9 =
	    (volatile unsigned int *)(0xa0000000 | (9U << (width + 19)));

	{
		REG32(t3) = 0;
		REG32(t1) = 0;

		REG32(t3) = WRITE_DATA;

		if (REG32(t1) == REG32(t3)) { // DCR_VALUE|=(0<<19)	;
			bank = 1;	      // 2 bank
		} else {
			DCR_VALUE |= (1 << 19);
			bank = 2; // 4 bank
		}
	}

	if ((bank == 2) && ((dramtype == 1) || (dramtype == 2))) {
		REG32(EDTCR) &= ~(3 << 30);
		REG32(EDTCR) |= (1 << 30); // two bit, [00]=4 bank  [01]=8 bank

		unsigned int tmp = REG32(DCR);
		REG32(DCR) = tmp | (1 << 22); // set COL=1;  addr*=2

		REG32(t3) = 0;
		REG32(t9) = 0;
		REG32(t1) = 0;

		REG32(t9) = WRITE_DATA;

		if (REG32(t1) == REG32(t9)) {
			bank = 2; // 4 bank, 4M boundary, but double
		} else {
			bank = 3;
		}
		REG32(EDTCR) &= ~(3 << 30);
		REG32(DCR) = tmp; // set COL=0;
	}
	printf("b%d,", bank);
	//--------------------------------------------------------
	// find row
	REG32(DCR) =
	    DCR_VALUE | (3 << 25) | (0 << 22); // set max ROW=3(16K), COL=0(256)
	CHECK_DCR_READY();
	unsigned int i = 0;

	for (row = ROW_MIN; row <= ROW_MAX; row++) {
		volatile unsigned int *t7 =
		    (volatile unsigned int *)(0xa0000000 |
					      (1U << (width + row + 7)));
		REG32(t7) = 0;
		REG32(0xa0000000) = WRITE_DATA;
		if (WRITE_DATA == REG32(t7))
			break;
	}
	row--;
	printf("r%d,", row);
	//--------------------------------------------------------
	// find col
	REG32(DCR) =
	    DCR_VALUE | (0 << 25) | (4 << 22); // ROW=0(2K), set max COL=4(4K)
	CHECK_DCR_READY();

	for (col = COL_MIN; col <= COL_MAX; col++) {
		volatile unsigned int *t7 =
		    (volatile unsigned int *)(0xa0000000 |
					      (1U << (width + col - 1)));
		REG32(t7) = 0;
		REG32(0xa0000000) = WRITE_DATA;
		if (WRITE_DATA == REG32(t7))
			break;
	}
	col--;
	printf("c%d,", col);
	//--------------------------------------------------------
	// find chip select
	REG32(DCR) = (1 << 30) |		 // CL3
		     (width << 28) | (1 << 27) | // set max chip count
		     ((row - 11) << 25) | ((col - 8) << 22) |
		     (((bank < 2) ? 0 : 1) << 19); // [0] : 2bank, [1]: 4bank

	CHECK_DCR_READY();

	volatile unsigned int *t7 =
	    (volatile unsigned int *)(0xa0000000 |
				      (1U << (width + bank + row + col)));
	REG32(t7) = WRITE_DATA;
	REG32(0xa0000000) = 0;
	REG32(0xa0000000);
	if (WRITE_DATA == REG32(t7))
		cs1 = 1;

	unsigned int size = 1 << (width + bank + row + col - 20);
	printf("size=%d MBytes x %d\n", size, cs1 + 1);

	if (bank == 3) {
		REG32(EDTCR) &= ~(3 << 30);
		REG32(EDTCR) |= (1 << 30); // 8 bank
	}

	REG32(DCR) = (1 << 30) | // CL3
		     (width << 28) | (cs1 << 27) | ((row - 11) << 25) |
		     ((col - 8) << 22) |
		     (((bank < 2) ? 0 : 1) << 19); // [0] : 2bank, [1]: 4bank

	printf("DCR=%x\n", REG32(DCR));

	return size;
}
//==============================================================
void DDR_cali_API7(void)
{
	register int i, j, k;

	register int L0 = 0, R0 = 33, L1 = 0, R1 = 33;
	const register int DRAM_ADR = 0xA0100000;
	const register int DRAM_VAL = 0x5A5AA5A5;
	const register int DDCR_ADR = 0xB8001050;
	register int DDCR_VAL = 0x00000000;

	REG32(DRAM_ADR) = DRAM_VAL;

	while ((REG32(0xb8001050) & 0x40000000) != 0x40000000)
		;
	while ((REG32(0xb8001050) & 0x40000000) != 0x40000000)
		;

	// Calibrate for DQS0
	for (i = 1; i <= 31; i++) {
		REG32(DDCR_ADR) = (DDCR_VAL & 0x80000000) | ((i - 1) << 25);

		if (L0 == 0) {
			if ((REG32(DRAM_ADR) & 0x00FF00FF) == 0x005A00A5) {
				L0 = i;
			}
		} else {
			if ((REG32(DRAM_ADR) & 0x00FF00FF) != 0x005A00A5) {
				R0 = i - 1;
				break;
			}
		}
	}
	DDCR_VAL = (DDCR_VAL & 0xC0000000) | (((L0 + R0) >> 1) << 25);
	REG32(DDCR_ADR) = DDCR_VAL;

	// Calibrate for DQS1
	for (i = 1; i <= 31; i++) {
		REG32(DDCR_ADR) = (DDCR_VAL & 0xFE000000) | ((i - 1) << 20);
		if (L1 == 0) {
			if ((REG32(DRAM_ADR) & 0xFF00FF00) == 0x5A00A500) {
				L1 = i;
			}
		} else {
			if ((REG32(DRAM_ADR) & 0xFF00FF00) != 0x5A00A500) {
				R1 = i - 1;
				break;
			}
		}
	}

	// ASIC
	DDCR_VAL = (DDCR_VAL & 0xFE000000) | (((L1 + R1) >> 1) << 20);
	REG32(DDCR_ADR) = DDCR_VAL;

	printf("L0:%d R0:%d C0:%d\n", L0, R0, (L0 + R0) >> 1);
	printf("L1:%d R1:%d C1:%d\n", L1, R1, (L1 + R1) >> 1);
}

//==============================================================
#define RA 32

const unsigned char c0[1][2] = {RA / 2, RA / 2};
const unsigned char c1[3 * 3 - 1][2] = {
    {(RA / 2 + RA) / 2, RA / 4},
    {(RA / 2 + RA) / 2, RA / 2},
    {(RA / 2 + RA) / 2, (RA / 2 + RA) / 2},
    {RA / 2, RA / 4},
    {RA / 2, (RA / 2 + RA) / 2},
    {RA / 4, RA / 4},
    {RA / 4, RA / 2},
    {RA / 4, (RA / 2 + RA) / 2},
};
//------------------------------------------------------------------------------------
void ShowTxRxDelayMap()
{
	unsigned register tx, rx;
#undef REG32_ANDOR
#define REG32_ANDOR(x, y, z)                                                   \
	{                                                                      \
		REG32(x) = (REG32(x) & (y)) | (z);                             \
	}

#undef ADDR
#define ADDR 0xA0080000
#define PATT0 0x00000000
#define PATT1 0xffffffff
#define PATT2 0x12345678
#define PATT3 0x5A5AA5A5
#define PATT4 0xAAAAAAAA
#define CLK_MANAGER 0xb8000010
#define DCR 0xb8001004

	for (tx = 0; tx <= 31; tx++) {
		printf("Tx=%02x : ", tx);
		for (rx = 0; rx <= 31; rx++) {
			REG32(DCR) = REG32(DCR);
			CHECK_DCR_READY();
			CHECK_DCR_READY();

			REG32_ANDOR(CLK_MANAGER, ~((0x1f << 5) | (0x1f << 0)),
				    (tx << 5) | (rx << 0));

			REG32(ADDR) = PATT0;
			if (REG32(ADDR) != PATT0)
				goto failc0;
			REG32(ADDR) = PATT1;
			if (REG32(ADDR) != PATT1)
				goto failc0;
			REG32(ADDR) = PATT2;
			if (REG32(ADDR) != PATT2)
				goto failc0;
			REG32(ADDR) = PATT3;
			if (REG32(ADDR) != PATT3)
				goto failc0;
			REG32(ADDR) = PATT4;
			if (REG32(ADDR) != PATT4)
				goto failc0;

			printf("%02x,", rx);
			continue;

		failc0:
			printf("--,");
			continue;
		}
		printf("\n");
	}
	printf("\n");
}
//------------------------------------------------------------------------------------
void Calc_TRxDly()
{
	unsigned register tx, rx;
#undef REG32_ANDOR
#define REG32_ANDOR(x, y, z)                                                   \
	{                                                                      \
		REG32(x) = (REG32(x) & (y)) | (z);                             \
	}

#undef ADDR
#define ADDR 0xA0100000
#define PATT0 0x00000000
#define PATT1 0xffffffff
#define PATT2 0x12345678
#define PATT3 0x5A5AA5A5
#define PATT4 0xAAAAAAAA
#define CLK_MANAGER 0xb8000010
#define DCR 0xb8001004

#define delta 5
	unsigned register i = 0;

	//----------------------------------------------
	// test
	// ShowTxRxDelayMap();

	// for(i=0; i<1; i++)
	{
		printf("c0=(%d,%d) ", c0[i][0], c0[i][1]);

		for (tx = c0[i][0] - delta; tx <= c0[i][0] + delta; tx++)
			for (rx = c0[i][1] - delta; rx <= c0[i][1] + delta;
			     rx++) {
				REG32(DCR) = REG32(DCR);
				CHECK_DCR_READY();
				CHECK_DCR_READY();

				printf("(%d,%d) ", tx, rx);
				REG32_ANDOR(CLK_MANAGER,
					    ~((0x1f << 5) | (0x1f << 0)),
					    (tx << 5) | (rx << 0));

				REG32(ADDR) = PATT0;
				if (REG32(ADDR) != PATT0)
					goto failc0;
				REG32(ADDR) = PATT1;
				if (REG32(ADDR) != PATT1)
					goto failc0;
				REG32(ADDR) = PATT2;
				if (REG32(ADDR) != PATT2)
					goto failc0;
				REG32(ADDR) = PATT3;
				if (REG32(ADDR) != PATT3)
					goto failc0;
				REG32(ADDR) = PATT4;
				if (REG32(ADDR) != PATT4)
					goto failc0;
			}

		printf("c0=(%d,%d) pass\n", c0[i][0], c0[i][1]);
		REG32_ANDOR(CLK_MANAGER, ~((0x1f << 5) | (0x1f << 0)),
			    (c0[i][0] << 5) | (c0[i][1] << 0));
		return;
	}

failc0:
	//----------------------------------------------

	for (i = 0; i < 3 * 3 - 1; i++) {
		printf("\nc1=(%d,%d) ", c1[i][0], c1[i][1]);

		for (tx = c1[i][0] - delta; tx <= c1[i][0] + delta; tx++)
			for (rx = c1[i][1] - delta; rx <= c1[i][1] + delta;
			     rx++) {
				REG32(DCR) = REG32(DCR);
				CHECK_DCR_READY();
				CHECK_DCR_READY();

				printf("(%d,%d) ", tx, rx);
				REG32_ANDOR(CLK_MANAGER,
					    ~((0x1f << 5) | (0x1f << 0)),
					    (tx << 5) | (rx << 0));

				REG32(ADDR) = PATT0;
				if (REG32(ADDR) != PATT0)
					goto next_c1;
				REG32(ADDR) = PATT1;
				if (REG32(ADDR) != PATT1)
					goto next_c1;
				REG32(ADDR) = PATT2;
				if (REG32(ADDR) != PATT2)
					goto next_c1;
				REG32(ADDR) = PATT3;
				if (REG32(ADDR) != PATT3)
					goto next_c1;
				REG32(ADDR) = PATT4;
				if (REG32(ADDR) != PATT4)
					goto next_c1;
			}

		printf("c1=(%d,%d) pass\n", c1[i][0], c1[i][1]);
		REG32_ANDOR(CLK_MANAGER, ~((0x1f << 5) | (0x1f << 0)),
			    (c1[i][0] << 5) | (c1[i][1] << 0));

		return;

	next_c1:
		continue;
	}
	//----------------------------------------------
}
//==============================================================
#define PAD_CONTROL 0xb8000048

void EnableIP_PADControl(unsigned int dramtype)
{
	if (dramtype == DRAM_DDR2) {
		REG32(PAD_CONTROL) |= (2 << 22);
		printf("Add clock driving for DDR2,PAD_CONTROL(%x)=%x\n",
		       PAD_CONTROL, REG32(PAD_CONTROL));
	}
}
//==============================================================

//==============================================================
unsigned int uart_rx4b_val()
{
	unsigned int val;
	unsigned char ch;

	ch = uart_inc();
	val = ch << 24;

	ch = uart_inc();
	val += ch << 16;

	ch = uart_inc();
	val += ch << 8;

	ch = uart_inc();
	val += ch;

	return val;
}
//==============================================================

void RescueMode(void)
{
#define JUMPADDR 0x80100000

	unsigned char ch;
	unsigned char *p = (unsigned char *)(JUMPADDR);
	unsigned int len = 0, csum = 0;
	unsigned int i;
	unsigned int addr, val;
	void (*jumpF)(void);

	printf("Rescue:\n");

	// header, 1 line, offset [00-15]
	while (uart_inc() != 'b') {
	}

	for (i = 0; i < 3; i++) {
		uart_inc();
	}

	uart_rx4b_val(); // dummy
	uart_rx4b_val(); // dummy
	len = uart_rx4b_val();

	printf("Len=%d\n", len);

	// jump code, 0.5 line,  offset [00-07]
	for (i = 2; i > 0; i--) {
		uart_rx4b_val();
	}

	// mem patch, 4.5 lines,  9 record,
	for (i = 9; i > 0; i--) {
		addr = uart_rx4b_val();
		val = uart_rx4b_val();
		REG32(addr) = val;
	}

	// fill 5 line to memory
	for (i = 5 * 16; i > 0; i--) {
		*p = 0;
		p++;
	}

	// content
	for (i = len - 5 * 16; i > 0; i--) {
		ch = uart_inc();
		*p = ch;
		p++;
	}
	printf("Jmp");
	jumpF = (void *)(JUMPADDR);
	jumpF();

	printf("Hang");
	while (1)
		;
}

//==============================================================
const unsigned char *boot_type_tab[] = {"SPI",	"NOR",	"NFBI", "NAND",
					"ROM1", "ROM2", "ROM3", "ROM4"};
const unsigned char *dram_type_tab[] = {"SDR", "SDR", "DDR2", "DDR1"};
const unsigned int m2x_clksel_table[] = {312, 387, 362, 462,
					 425, 250, 475, 337};
//------------------------------------------------------------------
void start_c()
{
	printf("c start\n");

	if (kbhit(0x2000)) {
		if (uart_inc() == 'r')
			RescueMode();
	}
//-----------------------------------------------------------------
#define SYS_HW_STRAP (0xb8000000 + 0x08)
	unsigned int v = REG32(SYS_HW_STRAP);
	printf("Strap=%x\n", v);
//-----------
#define GET_BITVAL(v, bitpos, pat)                                             \
	((v & ((unsigned int)pat << bitpos)) >> bitpos)
#define RANG1 1
#define RANG2 3
#define RANG3 7
#define RANG4 0xf
	unsigned char boot_sel = GET_BITVAL(v, 0, RANG3);
	unsigned char dramtype_sel = GET_BITVAL(v, 3, RANG2);
	unsigned char m2x_freq_sel = GET_BITVAL(v, 10, RANG3);

	//-----------
	unsigned char dram_type_remap[] = {
	    DRAM_SDR,  // 00 -> SDR
	    DRAM_SDR,  // 01 -> SDR
	    DRAM_DDR2, // 10 ->DDR2
	    DRAM_DDR1  // 11-> DDR1
	};

	unsigned char dramtype = dram_type_remap[dramtype_sel];

	unsigned int m2xclk = m2x_clksel_table[m2x_freq_sel];

	printf("Mode=%s\n", boot_type_tab[boot_sel]);
	printf("RAM=%s\n", dram_type_tab[dramtype_sel]);
	printf("CLK=%d\n", m2x_clksel_table[m2x_freq_sel]);
	//--------------
	EnableIP_PADControl(dramtype);
	//--------------
	if ((dramtype == DRAM_DDR1) || (dramtype == DRAM_DDR2)) {
		REG32(0xb8000010) = (24 << 5) | (24);
		DDR_cali_API7();

		printf("DDCR=%x\n", REG32(0xb8001050));
	}
	//----------------

	Calc_TRxDly();

	printf("CLKMGR=%x\n", REG32(0xb8000010));
	//----------------
	unsigned int dramsize = Calc_Dram_Size(dramtype);

	LookUp_MemTimingTable(dramtype, dramsize, m2xclk);
}
