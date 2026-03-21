#ifndef __OSDEP_H__
#define __OSDEP_H__

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 */

#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN
#endif

/* Taken from /usr/include/linux/hfs_sysdep.h */
#if defined(__BIG_ENDIAN)
#if !defined(__constant_htonl)
#define __constant_htonl(x) (x)
#endif
#if !defined(__constant_htons)
#define __constant_htons(x) (x)
#endif
#elif defined(__LITTLE_ENDIAN)
#if !defined(__constant_htonl)
#define __constant_htonl(x)                                                    \
	((unsigned long int)((((unsigned long int)(x) & 0x000000ffU) << 24) |  \
			     (((unsigned long int)(x) & 0x0000ff00U) << 8) |   \
			     (((unsigned long int)(x) & 0x00ff0000U) >> 8) |   \
			     (((unsigned long int)(x) & 0xff000000U) >> 24)))
#endif
#if !defined(__constant_htons)
#define __constant_htons(x)                                                    \
	((unsigned short int)((((unsigned short int)(x) & 0x00ff) << 8) |      \
			      (((unsigned short int)(x) & 0xff00) >> 8)))
#endif
#else
#error "Don't know if bytes are big- or little-endian!"
#endif

#ifdef __LITTLE_ENDIAN
#ifndef ntohl
#define ntohl(x) (__builtin_constant_p(x) ? __constant_htonl((x)) : __swap32(x))
#endif
#ifndef htonl
#define htonl(x) (__builtin_constant_p(x) ? __constant_htonl((x)) : __swap32(x))
#endif
#ifndef ntohs
#define ntohs(x) (__builtin_constant_p(x) ? __constant_htons((x)) : __swap16(x))
#endif
#ifndef htons
#define htons(x) (__builtin_constant_p(x) ? __constant_htons((x)) : __swap16(x))
#endif
#endif

static inline unsigned long int __swap32(unsigned long int x)
{
	volatile unsigned long rx;

	rx = ((
	    unsigned long int)((((unsigned long int)(x) & 0x000000ffU) << 24) |
			       (((unsigned long int)(x) & 0x0000ff00U) << 8) |
			       (((unsigned long int)(x) & 0x00ff0000U) >> 8) |
			       (((unsigned long int)(x) & 0xff000000U) >> 24)));
	return rx;
}

static inline unsigned short int __swap16(unsigned short int x)
{
	volatile unsigned short rx;
	rx = ((volatile unsigned short int)((((volatile unsigned short int)(x) &
					      0x00ff)
					     << 8) |
					    (((volatile unsigned short int)(x) &
					      0xff00) >>
					     8)));

	return rx;
}

#ifdef __BIG_ENDIAN
#ifndef ntohl
#define ntohl(x) (x)
#define ntohs(x) (x)
#define htonl(x) (x)
#define htons(x) (x)
#endif
#endif

#include <asm/io.h>

#endif
