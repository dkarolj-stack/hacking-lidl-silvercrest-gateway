#include "../boot/include/ver.h"
typedef unsigned int UINT32;
#define DECOMP_ADDR 0x80400000
#define LZMA_STATUS_ADDR 0x80300000

/* Convert little-endian 32-bit to CPU endian (boot is big-endian). */
#define ___swab32(x)                                                           \
	({                                                                     \
		UINT32 __x = (x);                                              \
		((UINT32)((((UINT32)(__x) & (UINT32)0x000000ffUL) << 24) |     \
			  (((UINT32)(__x) & (UINT32)0x0000ff00UL) << 8) |      \
			  (((UINT32)(__x) & (UINT32)0x00ff0000UL) >> 8) |      \
			  (((UINT32)(__x) & (UINT32)0xff000000UL) >> 24)));    \
	})
#define le32_to_cpu(x) ___swab32(x)
void *memcpy(void *dest, const void *src, int count)
{
	char *tmp = (char *)dest, *s = (char *)src;

	while (count--)
		*tmp++ = *s++;

	return dest;
}
/* Extern VARIABLE DECLARATIONS */
extern char __boot_start[];
extern char __boot_end[];
void boot_entry(void);

// Flush D-cache (write-back + invalidate) then invalidate I-cache.
static void flush_cache_all(void)
{
	__asm__ volatile(
	    ".set noreorder\n\t"
	    "mtc0 $0, $20\n\t"
	    "nop\n\t"
	    "li $8, 0x100\n\t" /* CCTL_DCACHE_WB: D-cache write-back */
	    "mtc0 $8, $20\n\t"
	    "nop\n\tnop\n\t"
	    "mtc0 $0, $20\n\t"
	    "nop\n\t"
	    "li $8, 0x1\n\t" /* CCTL_DCACHE_INVAL: D-cache invalidate */
	    "mtc0 $8, $20\n\t"
	    "nop\n\tnop\n\t"
	    "mtc0 $0, $20\n\t"
	    "nop\n\t"
	    "li $8, 0x2\n\t" /* CCTL_ICACHE_INVAL: I-cache invalidate */
	    "mtc0 $8, $20\n\t"
	    "nop\n\tnop\n\t"
	    "mtc0 $0, $20\n\t"
	    ".set reorder\n\t"
	    :
	    :
	    : "$8", "memory");
}
void boot_entry(void)
{
	unsigned char *outbuf;
	void (*jumpF)(void);

	outbuf = (unsigned char *)(DECOMP_ADDR);
	{
#include "LzmaDecode.h"

		unsigned char *startBuf = (unsigned char *)__boot_start;
		unsigned char *outBuf = outbuf;
		unsigned int inLen = __boot_end - __boot_start;
		SizeT compressedSize;
		unsigned char *inStream;
		UInt32 outSize = 0;
		UInt32 outSizeHigh = 0;
		SizeT outSizeFull;
		int res;
		SizeT inProcessed;
		SizeT outProcessed;
		CLzmaDecoderState state; /* it's about 24-80 bytes structure, if
					    int is 32-bit */
		unsigned char properties[LZMA_PROPERTIES_SIZE];
		compressedSize = (SizeT)(inLen - (LZMA_PROPERTIES_SIZE + 8));

		memcpy(properties, startBuf, sizeof(properties));
		startBuf += sizeof(properties);

		memcpy((char *)&outSize, startBuf, 4);
		outSize = le32_to_cpu(outSize);

		memcpy((char *)&outSizeHigh, startBuf + 4, 4);
		outSizeHigh = le32_to_cpu(outSizeHigh);

		outSizeFull = (SizeT)outSize;
		if (outSizeHigh != 0 || (UInt32)(SizeT)outSize != outSize) {
			// printf("LZMA: Too big uncompressed stream\n");
			return;
		}
		startBuf += 8;

		/* Decode LZMA properties and allocate memory */
		if (LzmaDecodeProperties(&state.Properties, properties,
					 LZMA_PROPERTIES_SIZE) !=
		    LZMA_RESULT_OK) {
			// puts("LZMA: Incorrect stream properties\n");
			return;
		}
		state.Probs = (CProb *)((void *)(LZMA_STATUS_ADDR));

		res = LzmaDecode(&state, startBuf, compressedSize, &inProcessed,
				 outBuf, outSizeFull, &outProcessed);
		if (res != 0) {
			return;
		}
	}
	flush_cache_all();
	jumpF = (void (*)(void))(DECOMP_ADDR);
	(*jumpF)();
}
