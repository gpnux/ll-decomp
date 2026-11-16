#ifndef GBA_H
#define GBA_H

#include "common.h"
#include "gba/mmap.h"
#include "gba/define.h"
#include "gba/syscall.h"
#include "gba/macro.h"

/* Typed views of the SDK memory regions. */
#define BIOS_IRQ_CHECK (*(u16 *)INTR_CHECK_BUF)
#define BIOS_IRQ_HANDLER (*(u32 *)INTR_VECTOR_BUF)

#define OBJ_TILE_ROW_COUNT 8
#define OBJ_GRAPHICS ((u32 (*)[OBJ_TILE_ROW_COUNT])OBJ_MODE0_VRAM)
#define OBJ_PALETTE_COLOR_COUNT 16
#define OBJ_PALETTE_FIRST_BANK 16
#define OBJ_PALETTES ((u16 (*)[OBJ_PALETTE_COLOR_COUNT])OBJ_PLTT)
#define PALETTE_RAM ((u16 (*)[OBJ_PALETTE_COLOR_COUNT])PLTT)
#define BG_SCREEN_BLOCK_SIZE 0x800
#define BG_SCREEN_ADDR(block) ((void *)(BG_VRAM + (block) * BG_SCREEN_BLOCK_SIZE))

#endif /* GBA_H */
