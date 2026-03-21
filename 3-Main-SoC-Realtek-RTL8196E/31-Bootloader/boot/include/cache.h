#ifndef _CACHE_H_
#define _CACHE_H_

/*
 * Cache operations for the RLX4181 (RTL8196E).
 *
 * Lexra uses CP0 $20 (CCTL) instead of standard MIPS cache instructions.
 * Bit definitions are in asm/lexraregs.h (CCTL_DCACHE_*, CCTL_ICACHE_*).
 */

void flush_cache(void);
void invalidate_iram(void);

#endif
