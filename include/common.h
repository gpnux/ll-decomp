#ifndef COMMON_H
#define COMMON_H

#include "gba/type.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Preserve the bundled compiler's unsigned long size type. */
#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((unsigned long)&((TYPE *)0)->MEMBER)
#endif

void *memcpy(void *destination, const void *source, unsigned long size);

#endif
