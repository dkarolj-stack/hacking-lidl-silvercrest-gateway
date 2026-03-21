/*
 * LZMA Kernel Loader for Realtek RTL819X SoC - Optimized & Secured
 *
 * This bootloader stage-2 loader decompresses an LZMA-compressed Linux kernel
 * from embedded data and transfers control to it.
 *
 * Copyright (C) 2011 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2017 Weijie Gao <hackpascal@gmail.com>
 *
 * Based on OpenWrt LZMA loaders for BCM47xx and ADM5120:
 *   Copyright (C) 2004 Manuel Novoa III (mjn3@codepoet.org)
 *   Copyright (C) 2005 Mineharu Takahara <mtakahar@yahoo.com>
 *   Copyright (C) 2005 Oleg I. Vdovikin <oleg@cs.msu.su>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <stddef.h>
#include <stdint.h>

#include "cache.h"
#include "printf.h"
#include "LzmaDecode.h"

/**
 * Configuration and memory layout
 *
 * LOADADDR (0x80000000): Target address for decompressed kernel
 * LZMA_TEXT_START (0x81000000): This loader's execution address
 *
 * Memory map during boot:
 *   0x80000000 - Decompressed kernel (written by this loader)
 *   0x80c00000 - This loader (loaded by bootloader)
 *   0x81000000 - LZMA workspace (temporary decompression state)
 */

/* Configuration: Enable verbose debugging (disabled for production) */
#undef LZMA_DEBUG

#ifdef LZMA_DEBUG
#  define DBG(f, a...)	printf(f, ## a)
#else
#  define DBG(f, a...)	do {} while (0)
#endif

/*
 * External symbols and workspace
 * workspace[]: LZMA decompression state allocated in linker script
 */
extern unsigned char workspace[];

/*
 * LZMA decompression state (static allocation for embedded environment)
 */
static CLzmaDecoderState lzma_state;
static unsigned char *lzma_data;       /* Pointer to compressed kernel data */
static unsigned long lzma_datasize;    /* Size of compressed data */
static unsigned long lzma_outsize;     /* Expected decompressed size */
static unsigned long kernel_la;        /* Kernel load address (LOADADDR) */

/*
 * Kernel command line arguments (optional)
 * Passed to kernel via MIPS calling convention (a0=argc, a1=argv)
 */
#ifdef CONFIG_KERNEL_CMDLINE
#define kernel_argc	2
static const char kernel_cmdline[] = CONFIG_KERNEL_CMDLINE;
static const char *kernel_argv[] = {
	NULL,
	kernel_cmdline,
	NULL,
};
#endif /* CONFIG_KERNEL_CMDLINE */

/**
 * halt - Stop execution with infinite loop
 *
 * Used when fatal errors occur during decompression.
 * Marked as noreturn to help compiler optimize.
 */
static void halt(void) __attribute__((noreturn));
static void halt(void)
{
	printf("\nSystem halted!\n");
	for(;;)
		; /* Infinite loop */
}

/**
 * get_be32 - Read 32-bit big-endian value from buffer
 * @buf: Buffer to read from (must be at least 4 bytes)
 *
 * Returns: 32-bit value in host byte order
 *
 * Note: Used for parsing LZMA header fields
 */
static inline unsigned long get_be32(const void *buf)
{
	const unsigned char *p = buf;

	return (((unsigned long) p[0] << 24) |
	        ((unsigned long) p[1] << 16) |
	        ((unsigned long) p[2] << 8) |
	        ((unsigned long) p[3]));
}

/**
 * lzma_get_byte - Read next byte from compressed stream
 *
 * Returns: Next byte from lzma_data stream
 *
 * SECURITY: Caller must ensure sufficient data remains via lzma_init_props()
 * checks before calling this function multiple times.
 */
static inline unsigned char lzma_get_byte(void)
{
	unsigned char c;

	lzma_datasize--;  /* Decrement before read (validated in lzma_init_props) */
	c = *lzma_data++;

	return c;
}

/**
 * lzma_init_props - Parse LZMA stream properties from header
 *
 * Reads and validates LZMA stream header:
 *   - 5 bytes: LZMA properties (lc, lp, pb, dictionary size)
 *   - 8 bytes: Uncompressed size (little-endian)
 *
 * Returns: LZMA_RESULT_OK on success, error code otherwise
 *
 * SECURITY NOTE: This function validates the header exists but does not
 * validate that lzma_outsize is reasonable. In embedded context, we trust
 * the bootloader has validated the image signature.
 */
static int lzma_init_props(void)
{
	unsigned char props[LZMA_PROPERTIES_SIZE];
	int res;
	int i;

	/* Ensure we have enough data for header */
	if (lzma_datasize < LZMA_PROPERTIES_SIZE + 8) {
		printf("LZMA stream too short!\n");
		return LZMA_RESULT_DATA_ERROR;
	}

	/* Read LZMA properties (5 bytes) */
	for (i = 0; i < LZMA_PROPERTIES_SIZE; i++)
		props[i] = lzma_get_byte();

	/* Read uncompressed size (lower 32 bits, little-endian) */
	lzma_outsize = ((SizeT) lzma_get_byte()) |
		       ((SizeT) lzma_get_byte() << 8) |
		       ((SizeT) lzma_get_byte() << 16) |
		       ((SizeT) lzma_get_byte() << 24);

	/* Skip upper 32 bits of size (we don't support >4GB kernels) */
	for (i = 0; i < 4; i++)
		lzma_get_byte();

	/* Basic sanity check: kernel should be at least 1KB and less than 32MB */
	if (lzma_outsize < 1024 || lzma_outsize > 32 * 1024 * 1024) {
		printf("Invalid kernel size: %lu bytes\n", lzma_outsize);
		return LZMA_RESULT_DATA_ERROR;
	}

	/* Parse and validate LZMA properties */
	res = LzmaDecodeProperties(&lzma_state.Properties, props,
					LZMA_PROPERTIES_SIZE);

	return res;
}

/**
 * lzma_decompress - Decompress LZMA stream to output buffer
 * @outStream: Target buffer (must be at least lzma_outsize bytes)
 *
 * Returns: LZMA_RESULT_OK on success, error code otherwise
 *
 * Decompresses the kernel from lzma_data to outStream using workspace
 * for temporary decompression state.
 */
static int lzma_decompress(unsigned char *outStream)
{
	SizeT ip, op;  /* Input/output processed byte counts */
	int ret;

	/* Use pre-allocated workspace for LZMA probability tables */
	lzma_state.Probs = (CProb *) workspace;

	/* Perform decompression */
	ret = LzmaDecode(&lzma_state, lzma_data, lzma_datasize, &ip, outStream,
			 lzma_outsize, &op);

	/* Debug output on error (only if LZMA_DEBUG defined) */
	if (ret != LZMA_RESULT_OK) {
		DBG("LzmaDecode error %d at %08x, osize:%lu ip:%lu op:%lu\n",
		    ret, lzma_data + ip, lzma_outsize, (unsigned long)ip,
		    (unsigned long)op);

		#ifdef LZMA_DEBUG
		int i;
		for (i = 0; i < 16 && (ip + i) < lzma_datasize; i++)
			DBG("%02x ", lzma_data[ip + i]);
		DBG("\n");
		#endif
	}

	return ret;
}

/**
 * lzma_init_data - Initialize pointers to embedded kernel data
 *
 * Sets up lzma_data and lzma_datasize from linker-provided symbols.
 * These symbols are defined in lzma-data.lds and point to the embedded
 * compressed kernel image.
 */
static void lzma_init_data(void)
{
	extern unsigned char _lzma_data_start[];
	extern unsigned char _lzma_data_end[];

	kernel_la = LOADADDR;
	lzma_data = _lzma_data_start;
	lzma_datasize = _lzma_data_end - _lzma_data_start;
}

/**
 * loader_main - Main entry point for LZMA loader
 * @reg_a0: MIPS register a0 (passed from bootloader)
 * @reg_a1: MIPS register a1
 * @reg_a2: MIPS register a2
 * @reg_a3: MIPS register a3
 *
 * This function:
 *   1. Initializes the hardware platform
 *   2. Displays boot banner
 *   3. Parses LZMA stream header
 *   4. Decompresses kernel to LOADADDR (0x80000000)
 *   5. Flushes caches
 *   6. Transfers control to kernel
 *
 * This function never returns (either jumps to kernel or halts on error).
 */
void loader_main(unsigned long reg_a0, unsigned long reg_a1,
		 unsigned long reg_a2, unsigned long reg_a3)
{
	void (*kernel_entry) (unsigned long, unsigned long, unsigned long,
			      unsigned long);
	int res;

	/* Initialize compressed data pointers */
	lzma_init_data();

	/* Parse and validate LZMA stream header */
	res = lzma_init_props();
	if (res != LZMA_RESULT_OK) {
		printf("ERROR: Invalid LZMA stream header (code %d)\n", res);
		halt();
	}

	printf("\n\nDecompressing kernel (%lu bytes compressed -> %lu bytes)... ",
	       lzma_datasize, lzma_outsize);

	/* Decompress kernel to memory */
	res = lzma_decompress((unsigned char *) kernel_la);

	if (res != LZMA_RESULT_OK) {
		printf("FAILED!\n");
		switch (res) {
		case LZMA_RESULT_DATA_ERROR:
			printf("ERROR: Corrupted LZMA data\n");
			break;
		default:
			printf("ERROR: Decompression failed (code %d)\n", res);
		}
		halt();
	}

	printf("done!\n");

	/* Ensure decompressed kernel is visible to CPU (flush caches) */
	flush_cache(kernel_la, lzma_outsize);

	printf("Transferring control to kernel at 0x%08x...\n", (unsigned int)kernel_la);

	/* Setup kernel arguments (if configured) */
#ifdef CONFIG_KERNEL_CMDLINE
	reg_a0 = kernel_argc;
	reg_a1 = (unsigned long) kernel_argv;
	reg_a2 = 0;
	reg_a3 = 0;
#endif

	/* Jump to decompressed kernel (never returns) */
	kernel_entry = (void *) kernel_la;
	kernel_entry(reg_a0, reg_a1, reg_a2, reg_a3);

	/* Unreachable - kernel_entry never returns */
	__builtin_unreachable();
}
