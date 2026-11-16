#ifndef GUARD_BATTLE_PALETTE_H
#define GUARD_BATTLE_PALETTE_H

#include "common.h"

#define BATTLE_PALETTE_BANK_COUNT 16
#define BATTLE_PALETTE_COLORS_PER_BANK 16

/** @brief Palette-memory selector used by the shared effect and bank helpers.
 *  @note Arguments remain bytes; OBJ is selector zero even though its hardware
 *  palette follows the BG palette.
 *  @address 0x0804C194, 0x0804C214
 */
enum BattlePaletteKind
{
    BATTLE_PALETTE_OBJ = 0,
    BATTLE_PALETTE_BG = 1,
};

/** @brief Effect type and control bits in BattlePaletteEffect.flags.
 *  @note The low nibble selects the updater; FF means no effect.
 *  @address 0x0804C45C, 0x0804C6B0
 */
enum BattlePaletteEffectFlags
{
    BATTLE_PALETTE_EFFECT_TYPE_MASK = 0x0F,
    BATTLE_PALETTE_EFFECT_PULSE = 1,
    BATTLE_PALETTE_EFFECT_CYCLE = 2,
    BATTLE_PALETTE_EFFECT_FADE = 3,
    BATTLE_PALETTE_PULSE_FRAME_INCREASING = 0x10,
    BATTLE_PALETTE_EFFECT_IN_PLACE = 0x20,
    BATTLE_PALETTE_FADE_RETURNING = 0x40,
    BATTLE_PALETTE_EFFECT_NONE = 0xFF,
};

/** @brief Destination-bank policy accepted by pulse and fade setup.
 *  @note Fade and BG pulse accept a signed starting bank; OBJ pulse accepts
 *  an unsigned starting bank. Allocated output banks are released on stop.
 *  @address 0x0804B654, 0x0804B96C
 */
enum BattlePaletteDestinationMode
{
    BATTLE_PALETTE_USE_SOURCE_BANK = 2,
    BATTLE_PALETTE_ALLOCATE_BANK = 3,
};

/** @brief Signed RGB offsets with a power-of-two interpolation divisor.
 *  @note Each channel adds (offset * amount) >> shift and clamps to 0..31.
 *  @address 0x0804B56C
 */
struct BattlePaletteAdjustment
{
    s8 red;
    s8 green;
    s8 blue;
    u8 shift;
};

/** @brief One palette bank's pulse, color cycle or fade state.
 *  @note Both tables have sixteen 0x10-byte records. Unaccessed padding stays
 *  explicit; +8 is a direction for cycling and an extent for fading.
 *  @address 0x03000AE8, 0x03000BE8
 */
struct BattlePaletteEffect
{
    u8 flags;                             /* 0x00; enum BattlePaletteEffectFlags */
    s8 outputBank;                        /* 0x01; -1 when no bank is assigned */
    u8 period;                            /* 0x02 */
    u8 frame;                             /* 0x03 */
    u8 cycleRange;                        /* 0x04; start in low nibble, count in high */
    u8 unused05;
    u16 cycleOffset;                      /* 0x06; cycle updater also reads its low byte */
    union
    {
        u8 reverseCycle;
        u8 fadeDepth;
    } __attribute__((packed)) limit;      /* 0x08; one byte, not agbcc's default four-byte union */
    u8 unused09[3];
    struct BattlePaletteAdjustment adjustment; /* 0x0C */
};

typedef char BattlePaletteAdjustmentSizeCheck[(sizeof(struct BattlePaletteAdjustment) == 4) ? 1 : -1];
typedef char BattlePaletteEffectSizeCheck[(sizeof(struct BattlePaletteEffect) == 0x10) ? 1 : -1];
typedef char BattlePaletteOutputBankCheck[(offsetof(struct BattlePaletteEffect, outputBank) == 1) ? 1 : -1];
typedef char BattlePaletteCycleOffsetCheck[(offsetof(struct BattlePaletteEffect, cycleOffset) == 6) ? 1 : -1];
typedef char BattlePaletteLimitCheck[(offsetof(struct BattlePaletteEffect, limit) == 8) ? 1 : -1];
typedef char BattlePaletteAdjustmentCheck[(offsetof(struct BattlePaletteEffect, adjustment) == 0xC) ? 1 : -1];

/** @brief Original colors saved before OBJ effects modify palette memory.
 *  @note COMMON storage is defined in battle_palette.c; rows are 32 bytes.
 *  @address 0x02036AC0
 */
extern u16 gBattleObjPaletteBackup[BATTLE_PALETTE_BANK_COUNT][BATTLE_PALETTE_COLORS_PER_BANK];
/** @brief Original colors saved before BG effects modify palette memory.
 *  @note COMMON storage is defined in battle_palette.c; rows are 32 bytes.
 *  @address 0x02036CC0
 */
extern u16 gBattleBgPaletteBackup[BATTLE_PALETTE_BANK_COUNT][BATTLE_PALETTE_COLORS_PER_BANK];

/** @brief Clear OBJ/BG palettes and backups, then reset all sixteen effect banks.
 *  @note Resets allocation masks and timing/cycle state; flags and output banks
 *  become FF. Effect padding and RGB adjustments are not cleared.
 *  @address 0x0804B288
 */
void BattlePalette_Init(void);
/** @brief Advance a triangular pulse and render its selected output bank.
 *  @note Flips direction at period - 1 and zero while preserving other flags.
 *  Reads sourceBank's saved colors and writes to the signed outputBank.
 *  @address 0x0804B3C0
 */
void BattlePalette_UpdatePulse(struct BattlePaletteEffect *effect, u8 sourceBank, u16 *paletteMemory, const u16 *savedColors);
/** @brief Advance a delayed rotation of a bank's color range.
 *  @note cycleRange packs the first color and color count; +8 selects direction.
 *  @address 0x0804B458
 */
void BattlePalette_UpdateCycle(struct BattlePaletteEffect *effect, u8 sourceBank, u16 *paletteMemory, const u16 *savedColors);
/** @brief Advance one color fade or its return to the saved palette.
 *  @note Returning to the full period restores the source bank and stops the fade.
 *  @address 0x0804B4D0
 */
void BattlePalette_UpdateFade(struct BattlePaletteEffect *effect, u8 sourceBank, u16 *paletteMemory, const u16 *savedColors);
/** @brief Apply signed RGB adjustments to sixteen saved colors.
 *  @note Each result channel is clamped to the GBA range 0..31.
 *  @address 0x0804B56C
 */
void BattlePalette_AdjustColors(u16 *destination, const u16 *source, u8 amount, const struct BattlePaletteAdjustment *adjustment);
/** @brief Start triangular RGB pulses on consecutive OBJ source banks.
 *  @note Only offsets above 31 are clamped in place; existing pulses are skipped.
 *  Mode 2 uses source banks; mode 3 converts the signed firstFreeBank argument
 *  to an unsigned scan index for each source. Returns the signed outputBank.
 *  @address 0x0804B654
 */
s8 BattlePalette_StartObjPulse(u8 firstBank, u8 bankCount, s8 *rgbOffsets, u8 period, s8 firstFreeBank, u8 mode);
/** @brief Restore initialized OBJ source banks and stop their effects.
 *  @note Stops every non-FF record; releases its output bank when IN_PLACE is
 *  clear. Flags/outputBank become FF and period/frame become zero.
 *  @address 0x0804B7B0
 */
void BattlePalette_StopObjPulse(u8 firstBank, u8 bankCount);
/** @brief Start in-place color cycling on consecutive OBJ source banks.
 *  @note The sign selects direction; magnitude and count use only their low
 *  nibbles. Existing cycles are skipped; returns the first record's signed outputBank.
 *  @address 0x0804B834
 */
s8 BattlePalette_StartObjCycle(u8 firstBank, u8 bankCount, u8 period, s8 firstColorAndDirection, u8 colorCount);
/** @brief Restore initialized OBJ source banks and stop their effects.
 *  @note Processes every non-FF record, as the pulse-stop entry does. Releases
 *  its output bank when IN_PLACE is clear; flags/outputBank become FF and period/frame zero.
 *  @address 0x0804B8E8
 */
void BattlePalette_StopObjCycle(u8 firstBank, u8 bankCount);
/** @brief Start RGB fades on OBJ source banks and return the first output bank.
 *  @note Caps only offsets above 31 and skips existing fades. A negative
 *  allocation start becomes zero; depth zero or >= period uses the full period.
 *  Allocation failure marks that record inactive with outputBank -1.
 *  @address 0x0804B96C
 */
s8 BattlePalette_StartObjFade(u8 firstBank, u8 bankCount, s8 red, s8 green, s8 blue, u8 period, u8 depth, s8 firstFreeBank, u8 mode);
/** @brief Stop active OBJ fades and restore their source-bank colors.
 *  @note Only type-three records are changed. Releases outputBank when IN_PLACE
 *  is clear, sets flags/outputBank to FF and clears period/frame.
 *  @address 0x0804BB64
 */
void BattlePalette_StopObjFade(u8 firstBank, u8 bankCount);
/** @brief Start triangular RGB pulses on consecutive BG source banks.
 *  @note Caps only offsets above 31 and skips existing pulses. Mode 2 uses
 *  source banks; mode 3 searches from max(firstFreeBank, 0) for each source.
 *  Returns the first BG record's signed outputBank; allocation failure marks
 *  that record inactive with outputBank -1.
 *  @address 0x0804BBDC
 */
s8 BattlePalette_StartBgPulse(u8 firstBank, u8 bankCount, s8 red, s8 green, s8 blue, u8 period, s8 firstFreeBank, u8 mode);
/** @brief Restore initialized BG source banks and stop their effects.
 *  @note Processes every non-FF record; releases outputBank when IN_PLACE is
 *  clear. Flags/outputBank become FF and period/frame become zero.
 *  @address 0x0804BD54
 */
void BattlePalette_StopBgPulse(u8 firstBank, u8 bankCount);
/** @brief Start in-place color cycling on consecutive BG source banks.
 *  @note The sign selects direction; magnitude/count use only their low nibbles.
 *  Existing cycles are skipped; returns the same-index OBJ record's signed outputBank.
 *  @address 0x0804BDD8
 */
s8 BattlePalette_StartBgCycle(u8 firstBank, u8 bankCount, u8 period, s8 firstColorAndDirection, u8 colorCount);
/** @brief Restore initialized BG source banks and stop their effects.
 *  @note Processes every non-FF record, as BG pulse-stop does. Releases
 *  outputBank when IN_PLACE is clear; flags/outputBank become FF and period/frame zero.
 *  @address 0x0804BE90
 */
void BattlePalette_StopBgCycle(u8 firstBank, u8 bankCount);
/** @brief Start RGB fades on BG source banks and return the first output bank.
 *  @note Caps only offsets above 31 and skips existing fades. A negative
 *  allocation start becomes zero; depth zero or >= period uses the full period.
 *  Allocation failure marks that record inactive with outputBank -1.
 *  @address 0x0804BF14
 */
s8 BattlePalette_StartBgFade(u8 firstBank, u8 bankCount, s8 red, s8 green, s8 blue, u8 period, u8 depth, s8 firstFreeBank, u8 mode);
/** @brief Stop active BG fades and restore their source-bank colors.
 *  @note Only type-three records are changed. Releases outputBank when IN_PLACE
 *  is clear, sets flags/outputBank to FF and clears period/frame.
 *  @address 0x0804C10C
 */
void BattlePalette_StopBgFade(u8 firstBank, u8 bankCount);
/** @brief Advance all OBJ effects, followed by all BG effects.
 *  @note The scene calls this after its other frame updates.
 *  @address 0x0804C184
 */
void BattlePalette_UpdateEffects(void);
/** @brief Return the first record of the sixteen-bank OBJ or BG effect table.
 *  @note Selectors 0/1 choose OBJ/BG; other values have no defined C return.
 *  @address 0x0804C194
 */
struct BattlePaletteEffect *BattlePalette_GetEffects(u8 kind);
/** @brief Reserve a range of banks in the selected palette memory.
 *  @note Unknown kind values perform no operation.
 *  @address 0x0804C1B4
 */
void BattlePalette_ReserveBanks(u8 kind, u8 firstBank, u8 bankCount);
/** @brief Release a range of banks in the selected palette memory.
 *  @note This updates allocation bits; effect teardown is a separate operation.
 *  @address 0x0804C1E4
 */
void BattlePalette_ReleaseBanks(u8 kind, u8 firstBank, u8 bankCount);
/** @brief Test one OBJ or BG allocation bit.
 *  @note Returns zero for unsupported kind values.
 *  @address 0x0804C214
 */
u8 BattlePalette_IsBankOccupied(u8 kind, u8 bank);
/** @brief Save one hardware palette bank into its RAM backup.
 *  @note OBJ uses 16-bit DMA; BG uses 32-bit DMA. Both copy 32 bytes.
 *  @address 0x0804C250
 */
void BattlePalette_SaveBank(u8 kind, u8 bank);
/** @brief Restore one hardware palette bank from its RAM backup.
 *  @note The original transfer uses eight words.
 *  @address 0x0804C278
 */
void BattlePalette_RestoreBank(u8 kind, u8 bank);
/** @brief Copy a cyclic permutation of a selected palette-color range.
 *  @note Only the selected range is written; source indices wrap modulo colorCount.
 *  @address 0x0804C2A0
 */
void BattlePalette_RotateColors(u16 *destination, const u16 *source, u8 firstColor, u8 colorCount, u8 rotation);
/** @brief Return the sixteen OBJ allocation bits.
 *  @note The mask is independent of each source bank's effect flags.
 *  @address 0x0804C2F0
 */
u16 BattlePalette_GetObjOccupiedMask(void);
/** @brief Upload consecutive OBJ banks and reserve them.
 *  @note Uses 16-bit DMA3 at OBJ palette memory plus firstBank * 32, then waits
 *  and sets the corresponding allocation bits.
 *  @address 0x0804C2FC
 */
void BattlePalette_LoadObjBanks(const u16 *palette, u8 firstBank, u8 bankCount);
/** @brief Set allocation bits for consecutive OBJ banks.
 *  @note The per-bank test and halfword write order are retained.
 *  @address 0x0804C364
 */
void BattlePalette_ReserveObjBanks(u8 firstBank, u8 bankCount);
/** @brief Clear allocation bits for consecutive OBJ banks.
 *  @note Bank colors and effect records are not reset by this helper.
 *  @address 0x0804C3A4
 */
void BattlePalette_ReleaseObjBanks(u8 firstBank, u8 bankCount);
/** @brief Save one OBJ bank before applying an effect.
 *  @note Copies 32 bytes into gBattleObjPaletteBackup with 16-bit DMA3 and waits.
 *  @address 0x0804C3E4
 */
void BattlePalette_SaveObjBank(u8 bank);
/** @brief Restore one OBJ bank from its saved colors.
 *  @note Copies 32 bytes with 32-bit DMA3 and waits; allocation bits are unchanged.
 *  @address 0x0804C420
 */
void BattlePalette_RestoreObjBank(u8 bank);
/** @brief Dispatch each OBJ record to its pulse, cycle or fade updater.
 *  @note The sixteen source-bank records are processed in ascending order.
 *  @address 0x0804C45C
 */
void BattlePalette_UpdateObjEffects(void);
/** @brief Begin the return leg of active OBJ fades.
 *  @note Sets the return flag, replaces the period and resets the frame.
 *  @address 0x0804C4D8
 */
void BattlePalette_ReturnObjFade(u8 firstBank, u8 bankCount, u8 period);
/** @brief Return the sixteen BG allocation bits.
 *  @note This is the BG counterpart of the OBJ mask getter.
 *  @address 0x0804C53C
 */
u16 BattlePalette_GetBgOccupiedMask(void);
/** @brief Upload consecutive BG banks and reserve them.
 *  @note Uses 32-bit DMA3 at BG palette memory plus firstBank * 32, then waits
 *  and sets the corresponding allocation bits.
 *  @address 0x0804C548
 */
void BattlePalette_LoadBgBanks(const u16 *palette, u8 firstBank, u8 bankCount);
/** @brief Set allocation bits for consecutive BG banks.
 *  @note The per-bank test and halfword write order are retained.
 *  @address 0x0804C5B8
 */
void BattlePalette_ReserveBgBanks(u8 firstBank, u8 bankCount);
/** @brief Clear allocation bits for consecutive BG banks.
 *  @note Bank colors and effect records are not reset by this helper.
 *  @address 0x0804C5F8
 */
void BattlePalette_ReleaseBgBanks(u8 firstBank, u8 bankCount);
/** @brief Save one BG bank before applying an effect.
 *  @note Copies 32 bytes into gBattleBgPaletteBackup with 32-bit DMA3 and waits.
 *  @address 0x0804C638
 */
void BattlePalette_SaveBgBank(u8 bank);
/** @brief Restore one BG bank from its saved colors.
 *  @note Copies 32 bytes with 32-bit DMA3 and waits; allocation bits are unchanged.
 *  @address 0x0804C674
 */
void BattlePalette_RestoreBgBank(u8 bank);
/** @brief Dispatch each BG record to its pulse, cycle or fade updater.
 *  @note Uses the same record layout as OBJ effects and a separate backup.
 *  @address 0x0804C6B0
 */
void BattlePalette_UpdateBgEffects(void);
/** @brief Begin the return leg of active BG fades.
 *  @note Sets the return flag, replaces the period and resets the frame.
 *  @address 0x0804C728
 */
void BattlePalette_ReturnBgFade(u8 firstBank, u8 bankCount, u8 period);

#endif
