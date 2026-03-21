/*
 * Various ISA level dependant constants.
 * Most of the following constants reflect the different layout
 * of Coprocessor 0 registers.
 *
 * Copyright (c) 1998 Harald Koerfgen
 *
 * $Id: isadep.h,v 1.1 2009/11/13 13:22:46 jasonwang Exp $
 */
#include <linux/config.h>

#ifndef __ASM_MIPS_ISADEP_H
#define __ASM_MIPS_ISADEP_H

/*
 * R2000 or R3000
 */

/*
 * kernel or user mode? (CP0_STATUS)
 */
#define KU_MASK 0x08
#define KU_USER 0x08
#define KU_KERN 0x00

#endif /* __ASM_MIPS_ISADEP_H */
