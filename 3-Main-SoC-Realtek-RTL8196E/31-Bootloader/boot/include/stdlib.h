#ifndef _STDLIB_H
#define _STDLIB_H

#include "boot_common.h"
#include <rtl_types.h>

void *malloc(uint32 nbytes);
void free(void *ap);

#endif /* _STDLIB_H */
