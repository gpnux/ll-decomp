#ifndef GBA_SRAM_H
#define GBA_SRAM_H

#include "type.h"

void ReadSram(const u8 *source, u8 *dest, u32 size);
void WriteSram(const u8 *source, u8 *dest, u32 size);
u32 VerifySram(const u8 *source, const u8 *dest, u32 size);

#endif
