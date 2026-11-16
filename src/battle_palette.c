#include "include_asm.h"
#include "battle_palette.h"
#include "gba.h"

/** @brief Fixed-address views used by palette initialization and restoration.
 *  @note These alias the C-allocated buffers below; constant addresses retain
 *  the original DMA load order and OBJ-table base load after the DMA waits.
 */
#define BATTLE_OBJ_PALETTE_BACKUP ((u16 *)0x02036AC0)
#define BATTLE_BG_PALETTE_BACKUP ((u16 *)0x02036CC0)
#define BATTLE_OBJ_PALETTE_EFFECTS ((struct BattlePaletteEffect *)0x03000AE8)

/* .lcomm: private state in original address order; no RAM initializers. */
/** @brief Allocated OBJ output-bank bits.
 *  @note Upload and reserve set bits; release clears them.
 *  @address 0x03000AE0
 */
static u16 sBattleObjPaletteOccupiedMask;
/** @brief Allocated BG output-bank bits.
 *  @note Uses the same sixteen-bank representation as the OBJ mask.
 *  @address 0x03000AE2
 */
static u16 sBattleBgPaletteOccupiedMask;
/** @brief Byte cleared by palette initialization.
 *  @note No reader is identified; the address suffix avoids inventing a role.
 *  @address 0x03000AE4
 */
static u8 sBattlePaletteUnknownAE4;
/** @brief Second byte cleared by palette initialization.
 *  @note No reader is identified.
 *  @address 0x03000AE5
 */
static u8 sBattlePaletteUnknownAE5;
/** @brief Per-source-bank OBJ palette effects.
 *  @note The source bank indexes this table; outputBank may select another bank.
 *  @address 0x03000AE8
 */
static struct BattlePaletteEffect sBattleObjPaletteEffects[BATTLE_PALETTE_BANK_COUNT];
/** @brief Per-source-bank BG palette effects.
 *  @note Layout is shared with the OBJ updater, while storage is independent.
 *  @address 0x03000BE8
 */
static struct BattlePaletteEffect sBattleBgPaletteEffects[BATTLE_PALETTE_BANK_COUNT];
/** @brief Halfword cleared by palette initialization.
 *  @note No reader is identified; this is not folded into either allocation mask.
 *  @address 0x03000CE8
 */
static u16 sBattlePaletteUnknownCE8;

/* .comm: EWRAM workspaces placed separately by config/ram.ll.ld. */
/** @brief Saved OBJ palette colors used by the effect updaters.
 *  @note Holds sixteen banks of sixteen colors; it is ordinary RAM.
 *  @address 0x02036AC0
 */
u16 gBattleObjPaletteBackup[BATTLE_PALETTE_BANK_COUNT][BATTLE_PALETTE_COLORS_PER_BANK];
/** @brief Saved BG palette colors used by the effect updaters.
 *  @note Holds sixteen banks of sixteen colors; it is ordinary RAM.
 *  @address 0x02036CC0
 */
u16 gBattleBgPaletteBackup[BATTLE_PALETTE_BANK_COUNT][BATTLE_PALETTE_COLORS_PER_BANK];

/** @brief Test one bit in an OBJ or BG palette-bank allocation mask.
 *  @note Kept inline in both branches of the public occupancy query.
 *  @address 0x0804C214
 */
static inline u32 BattlePalette_TestOccupiedBitInline(u16 occupiedMask, u8 bank)
{
    return (occupiedMask >> bank) & 1;
}

/*
 * 0804B288..0804C78C：BG/OBJ 调色板初始化、渐变和上传。
 * 管理 03000AE8/03000BE8 两组 16 字节效果记录及调色板位图。
 * 对其他 battle 模块提供服务，直接向外调用只有 libgcc 算术助手。
 * 职责、运行覆盖和合并结论见 docs/BATTLE-MODULES.md（2026-11-11）。
 */

/** @brief Clear OBJ/BG palettes and backups, then reset all sixteen effect banks.
 *  @note Four DMA3 halfword fills retain readbacks and waits. Flags and output
 *  banks become FF; timing/cycle fields become zero. Padding and RGB adjustment
 *  fields are untouched. Ordinary C matches the original 0x138-byte function.
 *  @address 0x0804B288
 */
void BattlePalette_Init(void)
{
    u8 bank;
    u16 fillValue;
    struct BattlePaletteEffect *objEffects;

    sBattleObjPaletteOccupiedMask = 0;
    sBattleBgPaletteOccupiedMask = 0;
    sBattlePaletteUnknownCE8 = 0;
    sBattlePaletteUnknownAE4 = 0;
    sBattlePaletteUnknownAE5 = 0;
    {
        u16 *fillSource = &fillValue;

        *fillSource = 0;
        DmaSet(3, fillSource, (void *)OBJ_PLTT,
            DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS | 0x100);
    }
    DmaWait(3);
    {
        u16 *fillSource = &fillValue;

        *fillSource = 0;
        DmaSet(3, fillSource, (void *)PLTT,
            DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS | 0x100);
    }
    DmaWait(3);
    {
        u16 *fillSource = &fillValue;

        *fillSource = 0;
        DmaSet(3, fillSource, BATTLE_OBJ_PALETTE_BACKUP,
            DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS | 0x100);
    }
    DmaWait(3);
    {
        u16 *fillSource = &fillValue;

        *fillSource = 0;
        DmaSet(3, fillSource, BATTLE_BG_PALETTE_BACKUP,
            DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS | 0x100);
    }
    DmaWait(3);

    bank = 0;
    objEffects = BATTLE_OBJ_PALETTE_EFFECTS;
    do
    {
        struct BattlePaletteEffect *obj = (struct BattlePaletteEffect *)
            (bank * sizeof(struct BattlePaletteEffect) + (u32)objEffects);
        struct BattlePaletteEffect *bg;

        obj->flags |= BATTLE_PALETTE_EFFECT_NONE;
        obj->outputBank |= -1;
        obj->period = 0;
        obj->frame = 0;
        obj->cycleRange = 0;
        obj->cycleOffset = 0;
        obj->limit.reverseCycle = 0;
        bg = &sBattleBgPaletteEffects[bank];
        bg->flags |= BATTLE_PALETTE_EFFECT_NONE;
        bg->outputBank |= -1;
        bg->period = 0;
        bg->frame = 0;
        bg->cycleRange = 0;
        bg->cycleOffset = 0;
        bg->limit.reverseCycle = 0;
        bank++;
    } while (bank < BATTLE_PALETTE_BANK_COUNT);
}

/** @brief Advance a triangular pulse and render its selected output bank.
 *  @note Direction flips at period - 1 and zero; other flag bits are preserved.
 *  The entry-local int mask and u16 halving state, with explicit u8 casts,
 *  retain the original code generation. Ordinary C matches the 0x98-byte span.
 *  @address 0x0804B3C0
 */
void BattlePalette_UpdatePulse(struct BattlePaletteEffect *effect, u8 sourceBank,
    u16 *paletteMemory, const u16 *savedColors)
{
    int mask = ~BATTLE_PALETTE_PULSE_FRAME_INCREASING;
    u16 halfPeriod;

    if (effect->flags & BATTLE_PALETTE_PULSE_FRAME_INCREASING)
    {
        effect->frame += 1;
        if (effect->frame >= effect->period - 1)
            effect->flags = effect->flags & mask;
    }
    else
    {
        effect->frame -= 1;
        if ((u8)effect->frame == 0)
            effect->flags = effect->flags | BATTLE_PALETTE_PULSE_FRAME_INCREASING;
    }

    effect->adjustment.shift = 0;
    halfPeriod = effect->period;
    halfPeriod >>= 1;
    if (halfPeriod != 0)
    {
        do
        {
            effect->adjustment.shift += 1;
            if ((u8)effect->adjustment.shift > 7)
                break;
            halfPeriod = (u8)(halfPeriod >> 1);
        } while (halfPeriod != 0);
    }

    BattlePalette_AdjustColors(paletteMemory + effect->outputBank * BATTLE_PALETTE_COLORS_PER_BANK,
        savedColors + sourceBank * BATTLE_PALETTE_COLORS_PER_BANK,
        effect->period - effect->frame, &effect->adjustment);
}

/** @brief Advance a delayed rotation of a bank's color range.
 *  @note cycleRange packs the first color and color count; +8 selects direction.
 *  @address 0x0804B458
 */
void BattlePalette_UpdateCycle(struct BattlePaletteEffect *effect, u8 sourceBank,
    u16 *paletteMemory, const u16 *savedColors)
{
    effect->frame = (effect->frame + 1) % effect->period;
    if (effect->frame == 0)
    {
        u8 firstColor = effect->cycleRange & 0xF;
        u8 colorCount = effect->cycleRange >> 4;

        if (effect->limit.reverseCycle == 0)
            effect->cycleOffset = (u8)(((u8)effect->cycleOffset + 1) % colorCount);
        else if (effect->cycleOffset == 0)
            effect->cycleOffset = colorCount - 1;
        else
            effect->cycleOffset--;

        BattlePalette_RotateColors(paletteMemory + effect->outputBank * BATTLE_PALETTE_COLORS_PER_BANK,
            savedColors + sourceBank * BATTLE_PALETTE_COLORS_PER_BANK,
            firstColor, colorCount, (u8)effect->cycleOffset);
    }
}

/** @brief Advance one color fade or its return to the saved palette.
 *  @note Returning to the full period restores the source bank and stops the fade.
 *  @address 0x0804B4D0
 */
void BattlePalette_UpdateFade(struct BattlePaletteEffect *effect, u8 sourceBank,
    u16 *paletteMemory, const u16 *savedColors)
{
    u32 period = effect->period;

    effect->adjustment.shift = 0;
    goto halvePeriod;
    do
    {
        effect->adjustment.shift++;
        if (effect->adjustment.shift > 7)
            break;
halvePeriod:
        period >>= 1;
    } while (period != 0);

    BattlePalette_AdjustColors(paletteMemory + effect->outputBank * BATTLE_PALETTE_COLORS_PER_BANK,
        savedColors + sourceBank * BATTLE_PALETTE_COLORS_PER_BANK,
        effect->period - effect->frame, &effect->adjustment);

    if (!(effect->flags & BATTLE_PALETTE_FADE_RETURNING))
    {
        if (effect->frame > effect->period - effect->limit.fadeDepth)
            effect->frame--;
    }
    else if (effect->frame < effect->period)
        effect->frame++;
    else if (savedColors == &gBattleObjPaletteBackup[0][0])
        BattlePalette_StopObjFade(sourceBank, 1);
    else if (savedColors == &gBattleBgPaletteBackup[0][0])
        BattlePalette_StopBgFade(sourceBank, 1);
}

/** @brief Adjust sixteen saved RGB555 colors and clamp each channel to 0..31.
 *  @note Separate s32 channel bases and a shared s16 delta retain evaluation
 *  order; each sum truncates to s16 before clamping. Ordinary C matches all
 *  0xE8 bytes at the original address.
 *  @address 0x0804B56C
 */
void BattlePalette_AdjustColors(u16 *destination, const u16 *source, u8 amount,
    const struct BattlePaletteAdjustment *adjustment)
{
    u8 index;
    s32 redBase, greenBase, blueBase;
    s16 delta;

    for (index = 0; index < BATTLE_PALETTE_COLORS_PER_BANK; index++)
    {
        s16 red = source[index] & 0x1F;
        s16 green = (source[index] >> 5) & 0x1F;
        s16 blue = (source[index] >> 10) & 0x1F;

        redBase = red;
        delta = amount;
        delta *= adjustment->red;
        red = redBase + (delta >> adjustment->shift);
        if (red > 31)
            red = 31;
        else if (red < 0)
            red = 0;

        greenBase = green;
        delta = amount;
        delta *= adjustment->green;
        green = greenBase + (delta >> adjustment->shift);
        if (green > 31)
            green = 31;
        else if (green < 0)
            green = 0;

        blueBase = blue;
        delta = amount;
        delta *= adjustment->blue;
        blue = blueBase + (delta >> adjustment->shift);
        if (blue > 31)
            blue = 31;
        else if (blue < 0)
            blue = 0;

        destination[index] = red | (green << 5) | (blue << 10);
    }
}

/** @brief Start triangular RGB pulses on consecutive OBJ source banks.
 *  @note Caps only RGB values above 31 in the caller's buffer; existing pulses
 *  are skipped. Mode 2 uses the source bank; mode 3 rescans from (u8)firstFreeBank
 *  for each source. The signed argument preserves caller ABI; the signature
 *  correction leaves the matched code and RAM unchanged.
 *  Returns the first record's signed outputBank.
 *  Repeated bank-index expressions retain the original register allocation;
 *  ordinary C matches all 0x15C bytes.
 *  @address 0x0804B654
 */
s8 BattlePalette_StartObjPulse(u8 firstBank, u8 bankCount, s8 *rgbOffsets,
    u8 period, s8 firstFreeBank, u8 mode)
{
    u8 index;

    if (rgbOffsets[0] > 31)
        rgbOffsets[0] = 31;
    if (rgbOffsets[1] > 31)
        rgbOffsets[1] = 31;
    if (rgbOffsets[2] > 31)
        rgbOffsets[2] = 31;

    for (index = 0; index < bankCount; index++)
    {
        if ((sBattleObjPaletteEffects[firstBank + index].flags & BATTLE_PALETTE_EFFECT_TYPE_MASK) != BATTLE_PALETTE_EFFECT_PULSE)
        {
            switch (mode)
            {
            case BATTLE_PALETTE_USE_SOURCE_BANK:
                BattlePalette_SaveObjBank(firstBank + index);
                sBattleObjPaletteEffects[firstBank + index].flags = BATTLE_PALETTE_EFFECT_PULSE
                    | BATTLE_PALETTE_PULSE_FRAME_INCREASING | BATTLE_PALETTE_EFFECT_IN_PLACE;
                sBattleObjPaletteEffects[firstBank + index].outputBank = firstBank + index;
                sBattleObjPaletteEffects[firstBank + index].period = period;
                sBattleObjPaletteEffects[firstBank + index].frame = 0;
                sBattleObjPaletteEffects[firstBank + index].cycleOffset = 0;
                sBattleObjPaletteEffects[firstBank + index].adjustment.red = rgbOffsets[0];
                sBattleObjPaletteEffects[firstBank + index].adjustment.green = rgbOffsets[1];
                sBattleObjPaletteEffects[firstBank + index].adjustment.blue = rgbOffsets[2];
                sBattleObjPaletteEffects[firstBank + index].adjustment.shift = 0;
                break;
            case BATTLE_PALETTE_ALLOCATE_BANK:
            {
                u8 outputBank;

                for (outputBank = firstFreeBank; outputBank < BATTLE_PALETTE_BANK_COUNT; outputBank++)
                {
                    if (!((sBattleObjPaletteOccupiedMask >> outputBank) & 1))
                        break;
                }
                if (outputBank < BATTLE_PALETTE_BANK_COUNT)
                {
                    BattlePalette_ReserveObjBanks(outputBank, 1);
                    BattlePalette_SaveObjBank(firstBank + index);
                    sBattleObjPaletteEffects[firstBank + index].flags = BATTLE_PALETTE_EFFECT_PULSE
                        | BATTLE_PALETTE_PULSE_FRAME_INCREASING;
                    sBattleObjPaletteEffects[firstBank + index].outputBank = outputBank;
                    sBattleObjPaletteEffects[firstBank + index].period = period;
                    sBattleObjPaletteEffects[firstBank + index].frame = 0;
                    sBattleObjPaletteEffects[firstBank + index].cycleOffset = 0;
                    sBattleObjPaletteEffects[firstBank + index].adjustment.red = rgbOffsets[0];
                    sBattleObjPaletteEffects[firstBank + index].adjustment.green = rgbOffsets[1];
                    sBattleObjPaletteEffects[firstBank + index].adjustment.blue = rgbOffsets[2];
                    sBattleObjPaletteEffects[firstBank + index].adjustment.shift = 0;
                }
                else
                {
                    sBattleObjPaletteEffects[firstBank + index].flags = BATTLE_PALETTE_EFFECT_NONE;
                    sBattleObjPaletteEffects[firstBank + index].outputBank = -1;
                    sBattleObjPaletteEffects[firstBank + index].period = 0;
                    sBattleObjPaletteEffects[firstBank + index].frame = 0;
                }
                break;
            }
            }
        }
    }
    return sBattleObjPaletteEffects[firstBank].outputBank;
}

/** @brief Restore initialized OBJ source banks and stop their effects.
 *  @note Processes every non-FF record regardless of its effect type.
 *  Releases output banks only when IN_PLACE is clear; flags/outputBank become
 *  FF and period/frame become zero. OR-to-FF retains the original reads;
 *  ordinary C matches all 0x84 bytes.
 *  @address 0x0804B7B0
 */
void BattlePalette_StopObjPulse(u8 firstBank, u8 bankCount)
{
    u8 index;

    for (index = 0; index < bankCount; index++)
    {
        if ((s8)sBattleObjPaletteEffects[firstBank + index].flags != -1)
        {
            if (!(sBattleObjPaletteEffects[firstBank + index].flags & BATTLE_PALETTE_EFFECT_IN_PLACE))
                BattlePalette_ReleaseObjBanks((u8)sBattleObjPaletteEffects[firstBank + index].outputBank, 1);
            BattlePalette_RestoreObjBank(firstBank + index);
            sBattleObjPaletteEffects[firstBank + index].flags |= BATTLE_PALETTE_EFFECT_NONE;
            sBattleObjPaletteEffects[firstBank + index].outputBank |= -1;
            sBattleObjPaletteEffects[firstBank + index].period = 0;
            sBattleObjPaletteEffects[firstBank + index].frame = 0;
        }
    }
}

/** @brief Start in-place color cycling on consecutive OBJ source banks.
 *  @note Packs the magnitude and colorCount's low nibbles into cycleRange;
 *  negative encodings select reverse cycling. Existing cycles are skipped.
 *  Returns the first record's signed outputBank. The u8 magnitude and unsigned
 *  direction shift retain the original truncation; ordinary C matches 0xB4 bytes.
 *  @address 0x0804B834
 */
s8 BattlePalette_StartObjCycle(u8 firstBank, u8 bankCount, u8 period,
    s8 firstColorAndDirection, u8 colorCount)
{
    u8 firstColor;
    u8 index;

    if (firstColorAndDirection < 0)
        firstColor = -firstColorAndDirection;
    else
        firstColor = firstColorAndDirection;

    for (index = 0; index < bankCount; index++)
    {
        if ((sBattleObjPaletteEffects[firstBank + index].flags & BATTLE_PALETTE_EFFECT_TYPE_MASK) != BATTLE_PALETTE_EFFECT_CYCLE)
        {
            BattlePalette_SaveObjBank(firstBank + index);
            sBattleObjPaletteEffects[firstBank + index].flags = BATTLE_PALETTE_EFFECT_CYCLE
                | BATTLE_PALETTE_EFFECT_IN_PLACE;
            sBattleObjPaletteEffects[firstBank + index].outputBank = firstBank + index;
            sBattleObjPaletteEffects[firstBank + index].period = period;
            sBattleObjPaletteEffects[firstBank + index].frame = 0;
            sBattleObjPaletteEffects[firstBank + index].cycleRange = (colorCount << 4) | (firstColor & 0xF);
            sBattleObjPaletteEffects[firstBank + index].cycleOffset = 0;
            sBattleObjPaletteEffects[firstBank + index].limit.reverseCycle = (u8)firstColorAndDirection >> 7;
        }
    }
    return sBattleObjPaletteEffects[firstBank].outputBank;
}

/** @brief Restore initialized OBJ source banks and stop their effects.
 *  @note Matches pulse-stop behavior: every non-FF record is processed, and its
 *  output bank is released when IN_PLACE is clear. Flags/outputBank become FF;
 *  period/frame become zero. OR-to-FF retains the original reads; ordinary C
 *  matches all 0x84 bytes.
 *  @address 0x0804B8E8
 */
void BattlePalette_StopObjCycle(u8 firstBank, u8 bankCount)
{
    u8 index;

    for (index = 0; index < bankCount; index++)
    {
        if ((s8)sBattleObjPaletteEffects[firstBank + index].flags != -1)
        {
            if (!(sBattleObjPaletteEffects[firstBank + index].flags & BATTLE_PALETTE_EFFECT_IN_PLACE))
                BattlePalette_ReleaseObjBanks((u8)sBattleObjPaletteEffects[firstBank + index].outputBank, 1);
            BattlePalette_RestoreObjBank(firstBank + index);
            sBattleObjPaletteEffects[firstBank + index].flags |= BATTLE_PALETTE_EFFECT_NONE;
            sBattleObjPaletteEffects[firstBank + index].outputBank |= -1;
            sBattleObjPaletteEffects[firstBank + index].period = 0;
            sBattleObjPaletteEffects[firstBank + index].frame = 0;
        }
    }
}

/** @brief Start RGB fades on OBJ source banks and return the first output bank.
 *  @note Caps only offsets above 31 and skips existing fades. Mode 2 reuses
 *  source banks; mode 3 searches from max(firstFreeBank, 0) for each source.
 *  Depth zero or >= period uses the full period; frame starts at period - 1.
 *  The explicit signed-start branch and repeated bank indices retain the
 *  original code generation; ordinary C matches all 0x1F8 bytes.
 *  @address 0x0804B96C
 */
s8 BattlePalette_StartObjFade(u8 firstBank, u8 bankCount, s8 red, s8 green,
    s8 blue, u8 period, u8 depth, s8 firstFreeBank, u8 mode)
{
    u8 index;

    if (red > 31)
        red = 31;
    if (green > 31)
        green = 31;
    if (blue > 31)
        blue = 31;

    for (index = 0; index < bankCount; index++)
    {
        if ((sBattleObjPaletteEffects[firstBank + index].flags & BATTLE_PALETTE_EFFECT_TYPE_MASK) != BATTLE_PALETTE_EFFECT_FADE)
        {
            switch (mode)
            {
            case BATTLE_PALETTE_USE_SOURCE_BANK:
                BattlePalette_SaveObjBank(firstBank + index);
                sBattleObjPaletteEffects[firstBank + index].flags = BATTLE_PALETTE_EFFECT_FADE
                    | BATTLE_PALETTE_EFFECT_IN_PLACE;
                sBattleObjPaletteEffects[firstBank + index].outputBank = firstBank + index;
                sBattleObjPaletteEffects[firstBank + index].period = period;
                if (depth != 0 && depth < period)
                    sBattleObjPaletteEffects[firstBank + index].limit.fadeDepth = depth;
                else
                    sBattleObjPaletteEffects[firstBank + index].limit.fadeDepth = sBattleObjPaletteEffects[firstBank + index].period;
                sBattleObjPaletteEffects[firstBank + index].frame = period - 1;
                sBattleObjPaletteEffects[firstBank + index].cycleOffset = 0;
                sBattleObjPaletteEffects[firstBank + index].adjustment.red = red;
                sBattleObjPaletteEffects[firstBank + index].adjustment.green = green;
                sBattleObjPaletteEffects[firstBank + index].adjustment.blue = blue;
                sBattleObjPaletteEffects[firstBank + index].adjustment.shift = 0;
                break;
            case BATTLE_PALETTE_ALLOCATE_BANK:
            {
                u8 outputBank;

                if (firstFreeBank < 0)
                    outputBank = 0;
                else
                    outputBank = firstFreeBank;
                for (; outputBank < BATTLE_PALETTE_BANK_COUNT; outputBank++)
                {
                    if (!((sBattleObjPaletteOccupiedMask >> outputBank) & 1))
                        break;
                }
                if (outputBank < BATTLE_PALETTE_BANK_COUNT)
                {
                    BattlePalette_ReserveObjBanks(outputBank, 1);
                    BattlePalette_SaveObjBank(firstBank + index);
                    sBattleObjPaletteEffects[firstBank + index].flags = BATTLE_PALETTE_EFFECT_FADE;
                    sBattleObjPaletteEffects[firstBank + index].outputBank = outputBank;
                    sBattleObjPaletteEffects[firstBank + index].period = period;
                    if (depth != 0 && depth < period)
                        sBattleObjPaletteEffects[firstBank + index].limit.fadeDepth = depth;
                    else
                        sBattleObjPaletteEffects[firstBank + index].limit.fadeDepth = sBattleObjPaletteEffects[firstBank + index].period;
                    sBattleObjPaletteEffects[firstBank + index].frame = period - 1;
                    sBattleObjPaletteEffects[firstBank + index].cycleOffset = 0;
                    sBattleObjPaletteEffects[firstBank + index].adjustment.red = red;
                    sBattleObjPaletteEffects[firstBank + index].adjustment.green = green;
                    sBattleObjPaletteEffects[firstBank + index].adjustment.blue = blue;
                    sBattleObjPaletteEffects[firstBank + index].adjustment.shift = 0;
                }
                else
                {
                    sBattleObjPaletteEffects[firstBank + index].flags = BATTLE_PALETTE_EFFECT_NONE;
                    sBattleObjPaletteEffects[firstBank + index].outputBank = -1;
                    sBattleObjPaletteEffects[firstBank + index].period = 0;
                    sBattleObjPaletteEffects[firstBank + index].frame = 0;
                }
                break;
            }
            }
        }
    }
    return sBattleObjPaletteEffects[firstBank].outputBank;
}

/** @brief Stop active OBJ fades and restore their source-bank colors.
 *  @note Only type-three records are changed. Releases outputBank when IN_PLACE
 *  is clear, sets flags/outputBank to FF and clears period/frame. Initializing
 *  the table pointer before the source-bank sum retains the original load order;
 *  ordinary C matches all 0x78 bytes.
 *  @address 0x0804BB64
 */
void BattlePalette_StopObjFade(u8 firstBank, u8 bankCount)
{
    u8 index;

    for (index = 0; index < bankCount; index++)
    {
        struct BattlePaletteEffect *effects = sBattleObjPaletteEffects;
        s32 sourceBank = firstBank + index;
        struct BattlePaletteEffect *effect = &effects[sourceBank];

        if ((effect->flags & BATTLE_PALETTE_EFFECT_TYPE_MASK) == BATTLE_PALETTE_EFFECT_FADE)
        {
            if (!(effect->flags & BATTLE_PALETTE_EFFECT_IN_PLACE))
                BattlePalette_ReleaseObjBanks((u8)effect->outputBank, 1);
            BattlePalette_RestoreObjBank(sourceBank);
            effect->flags |= BATTLE_PALETTE_EFFECT_NONE;
            effect->outputBank |= -1;
            effect->period = 0;
            effect->frame = 0;
        }
    }
}

/** @brief Start triangular RGB pulses on consecutive BG source banks.
 *  @note Caps only offsets above 31 and skips existing pulses. Mode 2 reuses
 *  source banks; mode 3 rescans from max(firstFreeBank, 0) for each source.
 *  Returns the first BG record's signed outputBank; allocation failure leaves
 *  that record inactive with outputBank -1. The explicit signed-start branch
 *  and repeated bank indices retain the original code generation; C matches 0x178 bytes.
 *  @address 0x0804BBDC
 */
s8 BattlePalette_StartBgPulse(u8 firstBank, u8 bankCount, s8 red, s8 green,
    s8 blue, u8 period, s8 firstFreeBank, u8 mode)
{
    u8 index;

    if (red > 31)
        red = 31;
    if (green > 31)
        green = 31;
    if (blue > 31)
        blue = 31;

    for (index = 0; index < bankCount; index++)
    {
        if ((sBattleBgPaletteEffects[firstBank + index].flags & BATTLE_PALETTE_EFFECT_TYPE_MASK) != BATTLE_PALETTE_EFFECT_PULSE)
        {
            switch (mode)
            {
            case BATTLE_PALETTE_USE_SOURCE_BANK:
                BattlePalette_SaveBgBank(firstBank + index);
                sBattleBgPaletteEffects[firstBank + index].flags = BATTLE_PALETTE_EFFECT_PULSE
                    | BATTLE_PALETTE_PULSE_FRAME_INCREASING | BATTLE_PALETTE_EFFECT_IN_PLACE;
                sBattleBgPaletteEffects[firstBank + index].outputBank = firstBank + index;
                sBattleBgPaletteEffects[firstBank + index].period = period;
                sBattleBgPaletteEffects[firstBank + index].frame = 0;
                sBattleBgPaletteEffects[firstBank + index].cycleOffset = 0;
                sBattleBgPaletteEffects[firstBank + index].adjustment.red = red;
                sBattleBgPaletteEffects[firstBank + index].adjustment.green = green;
                sBattleBgPaletteEffects[firstBank + index].adjustment.blue = blue;
                sBattleBgPaletteEffects[firstBank + index].adjustment.shift = 0;
                break;
            case BATTLE_PALETTE_ALLOCATE_BANK:
            {
                u8 outputBank;

                if (firstFreeBank < 0)
                    outputBank = 0;
                else
                    outputBank = firstFreeBank;
                for (; outputBank < BATTLE_PALETTE_BANK_COUNT; outputBank++)
                {
                    if (!((sBattleBgPaletteOccupiedMask >> outputBank) & 1))
                        break;
                }
                if (outputBank < BATTLE_PALETTE_BANK_COUNT)
                {
                    BattlePalette_ReserveBgBanks(outputBank, 1);
                    BattlePalette_SaveBgBank(firstBank + index);
                    sBattleBgPaletteEffects[firstBank + index].flags = BATTLE_PALETTE_EFFECT_PULSE
                        | BATTLE_PALETTE_PULSE_FRAME_INCREASING;
                    sBattleBgPaletteEffects[firstBank + index].outputBank = outputBank;
                    sBattleBgPaletteEffects[firstBank + index].period = period;
                    sBattleBgPaletteEffects[firstBank + index].frame = 0;
                    sBattleBgPaletteEffects[firstBank + index].cycleOffset = 0;
                    sBattleBgPaletteEffects[firstBank + index].adjustment.red = red;
                    sBattleBgPaletteEffects[firstBank + index].adjustment.green = green;
                    sBattleBgPaletteEffects[firstBank + index].adjustment.blue = blue;
                    sBattleBgPaletteEffects[firstBank + index].adjustment.shift = 0;
                }
                else
                {
                    sBattleBgPaletteEffects[firstBank + index].flags = BATTLE_PALETTE_EFFECT_NONE;
                    sBattleBgPaletteEffects[firstBank + index].outputBank = -1;
                    sBattleBgPaletteEffects[firstBank + index].period = 0;
                    sBattleBgPaletteEffects[firstBank + index].frame = 0;
                }
                break;
            }
            }
        }
    }
    return sBattleBgPaletteEffects[firstBank].outputBank;
}

/** @brief Restore initialized BG source banks and stop their effects.
 *  @note Processes every non-FF record; releases outputBank when IN_PLACE is
 *  clear. Flags/outputBank become FF and period/frame become zero. OR-to-FF
 *  retains the original reads; ordinary C matches all 0x84 bytes.
 *  @address 0x0804BD54
 */
void BattlePalette_StopBgPulse(u8 firstBank, u8 bankCount)
{
    u8 index;

    for (index = 0; index < bankCount; index++)
    {
        if ((s8)sBattleBgPaletteEffects[firstBank + index].flags != -1)
        {
            if (!(sBattleBgPaletteEffects[firstBank + index].flags & BATTLE_PALETTE_EFFECT_IN_PLACE))
                BattlePalette_ReleaseBgBanks((u8)sBattleBgPaletteEffects[firstBank + index].outputBank, 1);
            BattlePalette_RestoreBgBank(firstBank + index);
            sBattleBgPaletteEffects[firstBank + index].flags |= BATTLE_PALETTE_EFFECT_NONE;
            sBattleBgPaletteEffects[firstBank + index].outputBank |= -1;
            sBattleBgPaletteEffects[firstBank + index].period = 0;
            sBattleBgPaletteEffects[firstBank + index].frame = 0;
        }
    }
}

/** @brief Start in-place color cycling on consecutive BG source banks.
 *  @note Packs the magnitude and colorCount's low nibbles into cycleRange;
 *  negative encodings select reverse cycling. Existing cycles are skipped.
 *  Returns the first source's OBJ record outputBank, preserving the original
 *  table selection; ordinary C matches all 0xB8 bytes.
 *  @address 0x0804BDD8
 */
s8 BattlePalette_StartBgCycle(u8 firstBank, u8 bankCount, u8 period,
    s8 firstColorAndDirection, u8 colorCount)
{
    u8 firstColor;
    u8 index;

    if (firstColorAndDirection < 0)
        firstColor = -firstColorAndDirection;
    else
        firstColor = firstColorAndDirection;

    for (index = 0; index < bankCount; index++)
    {
        if ((sBattleBgPaletteEffects[firstBank + index].flags & BATTLE_PALETTE_EFFECT_TYPE_MASK) != BATTLE_PALETTE_EFFECT_CYCLE)
        {
            BattlePalette_SaveBgBank(firstBank + index);
            sBattleBgPaletteEffects[firstBank + index].flags = BATTLE_PALETTE_EFFECT_CYCLE
                | BATTLE_PALETTE_EFFECT_IN_PLACE;
            sBattleBgPaletteEffects[firstBank + index].outputBank = firstBank + index;
            sBattleBgPaletteEffects[firstBank + index].period = period;
            sBattleBgPaletteEffects[firstBank + index].frame = 0;
            sBattleBgPaletteEffects[firstBank + index].cycleRange = (colorCount << 4) | (firstColor & 0xF);
            sBattleBgPaletteEffects[firstBank + index].cycleOffset = 0;
            sBattleBgPaletteEffects[firstBank + index].limit.reverseCycle = (u8)firstColorAndDirection >> 7;
        }
    }
    return sBattleObjPaletteEffects[firstBank].outputBank;
}

/** @brief Restore initialized BG source banks and stop their effects.
 *  @note Matches BG pulse-stop behavior: every non-FF record is processed, and
 *  outputBank is released when IN_PLACE is clear. Flags/outputBank become FF;
 *  period/frame become zero. OR-to-FF retains the original reads; C matches 0x84 bytes.
 *  @address 0x0804BE90
 */
void BattlePalette_StopBgCycle(u8 firstBank, u8 bankCount)
{
    u8 index;

    for (index = 0; index < bankCount; index++)
    {
        if ((s8)sBattleBgPaletteEffects[firstBank + index].flags != -1)
        {
            if (!(sBattleBgPaletteEffects[firstBank + index].flags & BATTLE_PALETTE_EFFECT_IN_PLACE))
                BattlePalette_ReleaseBgBanks((u8)sBattleBgPaletteEffects[firstBank + index].outputBank, 1);
            BattlePalette_RestoreBgBank(firstBank + index);
            sBattleBgPaletteEffects[firstBank + index].flags |= BATTLE_PALETTE_EFFECT_NONE;
            sBattleBgPaletteEffects[firstBank + index].outputBank |= -1;
            sBattleBgPaletteEffects[firstBank + index].period = 0;
            sBattleBgPaletteEffects[firstBank + index].frame = 0;
        }
    }
}

/** @brief Start RGB fades on BG source banks and return the first output bank.
 *  @note Caps only offsets above 31 and skips existing fades. Mode 2 reuses
 *  source banks; mode 3 searches from max(firstFreeBank, 0) for each source.
 *  Depth zero or >= period uses the full period; frame starts at period - 1.
 *  The explicit signed-start branch and repeated bank indices retain the
 *  original code generation; ordinary C matches all 0x1F8 bytes.
 *  @address 0x0804BF14
 */
s8 BattlePalette_StartBgFade(u8 firstBank, u8 bankCount, s8 red, s8 green,
    s8 blue, u8 period, u8 depth, s8 firstFreeBank, u8 mode)
{
    u8 index;

    if (red > 31)
        red = 31;
    if (green > 31)
        green = 31;
    if (blue > 31)
        blue = 31;

    for (index = 0; index < bankCount; index++)
    {
        if ((sBattleBgPaletteEffects[firstBank + index].flags & BATTLE_PALETTE_EFFECT_TYPE_MASK) != BATTLE_PALETTE_EFFECT_FADE)
        {
            switch (mode)
            {
            case BATTLE_PALETTE_USE_SOURCE_BANK:
                BattlePalette_SaveBgBank(firstBank + index);
                sBattleBgPaletteEffects[firstBank + index].flags = BATTLE_PALETTE_EFFECT_FADE
                    | BATTLE_PALETTE_EFFECT_IN_PLACE;
                sBattleBgPaletteEffects[firstBank + index].outputBank = firstBank + index;
                sBattleBgPaletteEffects[firstBank + index].period = period;
                if (depth != 0 && depth < period)
                    sBattleBgPaletteEffects[firstBank + index].limit.fadeDepth = depth;
                else
                    sBattleBgPaletteEffects[firstBank + index].limit.fadeDepth = sBattleBgPaletteEffects[firstBank + index].period;
                sBattleBgPaletteEffects[firstBank + index].frame = period - 1;
                sBattleBgPaletteEffects[firstBank + index].cycleOffset = 0;
                sBattleBgPaletteEffects[firstBank + index].adjustment.red = red;
                sBattleBgPaletteEffects[firstBank + index].adjustment.green = green;
                sBattleBgPaletteEffects[firstBank + index].adjustment.blue = blue;
                sBattleBgPaletteEffects[firstBank + index].adjustment.shift = 0;
                break;
            case BATTLE_PALETTE_ALLOCATE_BANK:
            {
                u8 outputBank;

                if (firstFreeBank < 0)
                    outputBank = 0;
                else
                    outputBank = firstFreeBank;
                for (; outputBank < BATTLE_PALETTE_BANK_COUNT; outputBank++)
                {
                    if (!((sBattleBgPaletteOccupiedMask >> outputBank) & 1))
                        break;
                }
                if (outputBank < BATTLE_PALETTE_BANK_COUNT)
                {
                    BattlePalette_ReserveBgBanks(outputBank, 1);
                    BattlePalette_SaveBgBank(firstBank + index);
                    sBattleBgPaletteEffects[firstBank + index].flags = BATTLE_PALETTE_EFFECT_FADE;
                    sBattleBgPaletteEffects[firstBank + index].outputBank = outputBank;
                    sBattleBgPaletteEffects[firstBank + index].period = period;
                    if (depth != 0 && depth < period)
                        sBattleBgPaletteEffects[firstBank + index].limit.fadeDepth = depth;
                    else
                        sBattleBgPaletteEffects[firstBank + index].limit.fadeDepth = sBattleBgPaletteEffects[firstBank + index].period;
                    sBattleBgPaletteEffects[firstBank + index].frame = period - 1;
                    sBattleBgPaletteEffects[firstBank + index].cycleOffset = 0;
                    sBattleBgPaletteEffects[firstBank + index].adjustment.red = red;
                    sBattleBgPaletteEffects[firstBank + index].adjustment.green = green;
                    sBattleBgPaletteEffects[firstBank + index].adjustment.blue = blue;
                    sBattleBgPaletteEffects[firstBank + index].adjustment.shift = 0;
                }
                else
                {
                    sBattleBgPaletteEffects[firstBank + index].flags = BATTLE_PALETTE_EFFECT_NONE;
                    sBattleBgPaletteEffects[firstBank + index].outputBank = -1;
                    sBattleBgPaletteEffects[firstBank + index].period = 0;
                    sBattleBgPaletteEffects[firstBank + index].frame = 0;
                }
                break;
            }
            }
        }
    }
    return sBattleBgPaletteEffects[firstBank].outputBank;
}

/** @brief Stop active BG fades and restore their source-bank colors.
 *  @note Only type-three records are changed. Releases outputBank when IN_PLACE
 *  is clear, sets flags/outputBank to FF and clears period/frame. Initializing
 *  the table pointer before the source-bank sum retains the original load order;
 *  ordinary C matches all 0x78 bytes.
 *  @address 0x0804C10C
 */
void BattlePalette_StopBgFade(u8 firstBank, u8 bankCount)
{
    u8 index;

    for (index = 0; index < bankCount; index++)
    {
        struct BattlePaletteEffect *effects = sBattleBgPaletteEffects;
        s32 sourceBank = firstBank + index;
        struct BattlePaletteEffect *effect = &effects[sourceBank];

        if ((effect->flags & BATTLE_PALETTE_EFFECT_TYPE_MASK) == BATTLE_PALETTE_EFFECT_FADE)
        {
            if (!(effect->flags & BATTLE_PALETTE_EFFECT_IN_PLACE))
                BattlePalette_ReleaseBgBanks((u8)effect->outputBank, 1);
            BattlePalette_RestoreBgBank(sourceBank);
            effect->flags |= BATTLE_PALETTE_EFFECT_NONE;
            effect->outputBank |= -1;
            effect->period = 0;
            effect->frame = 0;
        }
    }
}

/** @brief Advance all OBJ effects, followed by all BG effects.
 *  @note The scene calls this after its other frame updates.
 *  @address 0x0804C184
 */
void BattlePalette_UpdateEffects(void)
{
    BattlePalette_UpdateObjEffects();
    BattlePalette_UpdateBgEffects();
}

/** @brief Return the first record of the sixteen-bank OBJ or BG effect table.
 *  @note Selectors 0/1 choose OBJ/BG; other values have no defined C return.
 *  The matched code leaves r0 unchanged for unsupported selectors. Ordinary C
 *  matches all 0x20 bytes, including the resolved table addresses.
 *  @address 0x0804C194
 */
struct BattlePaletteEffect *BattlePalette_GetEffects(u8 kind)
{
    switch (kind)
    {
    case BATTLE_PALETTE_OBJ:
        return sBattleObjPaletteEffects;
    case BATTLE_PALETTE_BG:
        return sBattleBgPaletteEffects;
    }
}

/** @brief Reserve a range of banks in the selected palette memory.
 *  @note Unknown kind values perform no operation.
 *  @address 0x0804C1B4
 */
void BattlePalette_ReserveBanks(u8 kind, u8 firstBank, u8 bankCount)
{
    switch (kind)
    {
    case BATTLE_PALETTE_OBJ:
        BattlePalette_ReserveObjBanks(firstBank, bankCount);
        break;
    case BATTLE_PALETTE_BG:
        BattlePalette_ReserveBgBanks(firstBank, bankCount);
        break;
    }
}

/** @brief Release a range of banks in the selected palette memory.
 *  @note This updates allocation bits; effect teardown is a separate operation.
 *  @address 0x0804C1E4
 */
void BattlePalette_ReleaseBanks(u8 kind, u8 firstBank, u8 bankCount)
{
    switch (kind)
    {
    case BATTLE_PALETTE_OBJ:
        BattlePalette_ReleaseObjBanks(firstBank, bankCount);
        break;
    case BATTLE_PALETTE_BG:
        BattlePalette_ReleaseBgBanks(firstBank, bankCount);
        break;
    }
}

/** @brief Test one OBJ or BG allocation bit.
 *  @note Returns zero for unsupported kind values.
 *  @address 0x0804C214
 */
u8 BattlePalette_IsBankOccupied(u8 kind, u8 bank)
{
    u8 occupied = 0;

    switch (kind)
    {
    case BATTLE_PALETTE_OBJ:
        if (BattlePalette_TestOccupiedBitInline(sBattleObjPaletteOccupiedMask, bank))
            occupied = 1;
        break;
    case BATTLE_PALETTE_BG:
        if (BattlePalette_TestOccupiedBitInline(sBattleBgPaletteOccupiedMask, bank))
            occupied = 1;
        break;
    }
    return occupied;
}

/** @brief Save one hardware palette bank into its RAM backup.
 *  @note OBJ uses 16-bit DMA; BG uses 32-bit DMA. Both copy 32 bytes.
 *  @address 0x0804C250
 */
void BattlePalette_SaveBank(u8 kind, u8 bank)
{
    switch (kind)
    {
    case BATTLE_PALETTE_OBJ:
        BattlePalette_SaveObjBank(bank);
        break;
    case BATTLE_PALETTE_BG:
        BattlePalette_SaveBgBank(bank);
        break;
    }
}

/** @brief Restore one hardware palette bank from its RAM backup.
 *  @note The original transfer uses eight words.
 *  @address 0x0804C278
 */
void BattlePalette_RestoreBank(u8 kind, u8 bank)
{
    switch (kind)
    {
    case BATTLE_PALETTE_OBJ:
        BattlePalette_RestoreObjBank(bank);
        break;
    case BATTLE_PALETTE_BG:
        BattlePalette_RestoreBgBank(bank);
        break;
    }
}

/** @brief Copy a cyclic permutation of a selected palette-color range.
 *  @note Only the selected range is written; source indices wrap modulo colorCount.
 *  @address 0x0804C2A0
 */
void BattlePalette_RotateColors(u16 *destination, const u16 *source,
    u8 firstColor, u8 colorCount, u8 rotation)
{
    u8 index;

    for (index = 0; index < colorCount; index++)
    {
        destination[index + firstColor] = source[rotation + firstColor];
        rotation = (rotation + 1) % colorCount;
    }
}

/** @brief Return the sixteen OBJ allocation bits.
 *  @note The mask is independent of each source bank's effect flags.
 *  @address 0x0804C2F0
 */
u16 BattlePalette_GetObjOccupiedMask(void)
{
    return sBattleObjPaletteOccupiedMask;
}

/** @brief Upload consecutive OBJ banks and reserve them.
 *  @note Copies bankCount * 32 bytes with 16-bit DMA3 and waits before setting
 *  previously free allocation bits. Ordinary C matches the original 0x68 bytes.
 *  @address 0x0804C2FC
 */
void BattlePalette_LoadObjBanks(const u16 *palette, u8 firstBank, u8 bankCount)
{
    u8 index;

    DmaCopy(3, palette, (u16 *)OBJ_PLTT + firstBank * BATTLE_PALETTE_COLORS_PER_BANK,
        bankCount * sizeof(gBattleObjPaletteBackup[0]), 16);
    DmaWait(3);

    for (index = 0; index < bankCount; index++)
    {
        if (!((sBattleObjPaletteOccupiedMask >> (firstBank + index)) & 1))
            sBattleObjPaletteOccupiedMask |= 1 << (firstBank + index);
    }
}

/** @brief Set allocation bits for consecutive OBJ banks.
 *  @note The per-bank test and halfword write order are retained.
 *  @address 0x0804C364
 */
void BattlePalette_ReserveObjBanks(u8 firstBank, u8 bankCount)
{
    u8 index;

    for (index = 0; index < bankCount; index++)
    {
        if (!((sBattleObjPaletteOccupiedMask >> (firstBank + index)) & 1))
            sBattleObjPaletteOccupiedMask |= 1 << (firstBank + index);
    }
}

/** @brief Clear allocation bits for consecutive OBJ banks.
 *  @note Bank colors and effect records are not reset by this helper.
 *  @address 0x0804C3A4
 */
void BattlePalette_ReleaseObjBanks(u8 firstBank, u8 bankCount)
{
    u8 index;

    for (index = 0; index < bankCount; index++)
    {
        if ((sBattleObjPaletteOccupiedMask >> (firstBank + index)) & 1)
            sBattleObjPaletteOccupiedMask &= ~(1 << (firstBank + index));
    }
}

/** @brief Save one OBJ bank before applying an effect.
 *  @note Copies 32 bytes with 16-bit DMA3, retaining readback and wait.
 *  Ordinary C matches the original 0x3C-byte function.
 *  @address 0x0804C3E4
 */
void BattlePalette_SaveObjBank(u8 bank)
{
    DmaCopy(3, (u16 *)OBJ_PLTT + bank * BATTLE_PALETTE_COLORS_PER_BANK,
        gBattleObjPaletteBackup[bank], sizeof(gBattleObjPaletteBackup[bank]), 16);
    DmaWait(3);
}

/** @brief Restore one OBJ bank from its saved colors.
 *  @note Copies 32 bytes with 32-bit DMA3, retaining readback and wait without
 *  changing allocation bits. Ordinary C matches the original 0x3C bytes.
 *  @address 0x0804C420
 */
void BattlePalette_RestoreObjBank(u8 bank)
{
    DmaCopy(3, BATTLE_OBJ_PALETTE_BACKUP + bank * BATTLE_PALETTE_COLORS_PER_BANK,
        (u16 *)OBJ_PLTT + bank * BATTLE_PALETTE_COLORS_PER_BANK,
        sizeof(gBattleObjPaletteBackup[bank]), 32);
    DmaWait(3);
}

/** @brief Dispatch each OBJ record to its pulse, cycle or fade updater.
 *  @note The sixteen source-bank records are processed in ascending order.
 *  @address 0x0804C45C
 */
void BattlePalette_UpdateObjEffects(void)
{
    u8 bank;

    for (bank = 0; bank < BATTLE_PALETTE_BANK_COUNT; bank++)
    {
        struct BattlePaletteEffect *effect = &sBattleObjPaletteEffects[bank];

        switch (effect->flags & BATTLE_PALETTE_EFFECT_TYPE_MASK)
        {
        case BATTLE_PALETTE_EFFECT_PULSE:
            BattlePalette_UpdatePulse(effect, bank, (u16 *)OBJ_PLTT, &gBattleObjPaletteBackup[0][0]);
            break;
        case BATTLE_PALETTE_EFFECT_CYCLE:
            BattlePalette_UpdateCycle(effect, bank, (u16 *)OBJ_PLTT, &gBattleObjPaletteBackup[0][0]);
            break;
        case BATTLE_PALETTE_EFFECT_FADE:
            BattlePalette_UpdateFade(effect, bank, (u16 *)OBJ_PLTT, &gBattleObjPaletteBackup[0][0]);
            break;
        }
    }
}

/** @brief Begin the return leg of active OBJ fades.
 *  @note Sets the return flag, replaces the period and resets the frame.
 *  @address 0x0804C4D8
 */
void BattlePalette_ReturnObjFade(u8 firstBank, u8 bankCount, u8 period)
{
    u8 index;

    for (index = 0; index < bankCount; index++)
    {
        struct BattlePaletteEffect *effect = &sBattleObjPaletteEffects[firstBank + index];

        if ((effect->flags & BATTLE_PALETTE_EFFECT_TYPE_MASK) == BATTLE_PALETTE_EFFECT_FADE)
        {
            effect->flags |= BATTLE_PALETTE_FADE_RETURNING;
            effect->period = period;
            effect->frame = 0;
        }
    }
}

/** @brief Return the sixteen BG allocation bits.
 *  @note This is the BG counterpart of the OBJ mask getter.
 *  @address 0x0804C53C
 */
u16 BattlePalette_GetBgOccupiedMask(void)
{
    return sBattleBgPaletteOccupiedMask;
}

/** @brief Upload consecutive BG banks and reserve them.
 *  @note Copies bankCount * 32 bytes with 32-bit DMA3 and waits before setting
 *  previously free allocation bits. Ordinary C matches the original 0x70 bytes.
 *  @address 0x0804C548
 */
void BattlePalette_LoadBgBanks(const u16 *palette, u8 firstBank, u8 bankCount)
{
    u8 index;

    DmaCopy(3, palette, (u16 *)PLTT + firstBank * BATTLE_PALETTE_COLORS_PER_BANK,
        bankCount * sizeof(gBattleBgPaletteBackup[0]), 32);
    DmaWait(3);

    for (index = 0; index < bankCount; index++)
    {
        if (!((sBattleBgPaletteOccupiedMask >> (firstBank + index)) & 1))
            sBattleBgPaletteOccupiedMask |= 1 << (firstBank + index);
    }
}

/** @brief Set allocation bits for consecutive BG banks.
 *  @note The per-bank test and halfword write order are retained.
 *  @address 0x0804C5B8
 */
void BattlePalette_ReserveBgBanks(u8 firstBank, u8 bankCount)
{
    u8 index;

    for (index = 0; index < bankCount; index++)
    {
        if (!((sBattleBgPaletteOccupiedMask >> (firstBank + index)) & 1))
            sBattleBgPaletteOccupiedMask |= 1 << (firstBank + index);
    }
}

/** @brief Clear allocation bits for consecutive BG banks.
 *  @note Bank colors and effect records are not reset by this helper.
 *  @address 0x0804C5F8
 */
void BattlePalette_ReleaseBgBanks(u8 firstBank, u8 bankCount)
{
    u8 index;

    for (index = 0; index < bankCount; index++)
    {
        if ((sBattleBgPaletteOccupiedMask >> (firstBank + index)) & 1)
            sBattleBgPaletteOccupiedMask &= ~(1 << (firstBank + index));
    }
}

/** @brief Save one BG bank before applying an effect.
 *  @note Unlike OBJ save, uses 32-bit DMA3 for the 32-byte copy. Readback and
 *  wait are retained. Ordinary C matches the original 0x3C-byte function.
 *  @address 0x0804C638
 */
void BattlePalette_SaveBgBank(u8 bank)
{
    DmaCopy(3, (u16 *)PLTT + bank * BATTLE_PALETTE_COLORS_PER_BANK,
        gBattleBgPaletteBackup[bank], sizeof(gBattleBgPaletteBackup[bank]), 32);
    DmaWait(3);
}

/** @brief Restore one BG bank from its saved colors.
 *  @note Copies 32 bytes with 32-bit DMA3, retaining readback and wait without
 *  changing allocation bits. Ordinary C matches the original 0x3C bytes.
 *  @address 0x0804C674
 */
void BattlePalette_RestoreBgBank(u8 bank)
{
    DmaCopy(3, BATTLE_BG_PALETTE_BACKUP + bank * BATTLE_PALETTE_COLORS_PER_BANK,
        (u16 *)PLTT + bank * BATTLE_PALETTE_COLORS_PER_BANK,
        sizeof(gBattleBgPaletteBackup[bank]), 32);
    DmaWait(3);
}

/** @brief Dispatch each BG record to its pulse, cycle or fade updater.
 *  @note Uses the same record layout as OBJ effects and a separate backup.
 *  @address 0x0804C6B0
 */
void BattlePalette_UpdateBgEffects(void)
{
    u8 bank;

    for (bank = 0; bank < BATTLE_PALETTE_BANK_COUNT; bank++)
    {
        struct BattlePaletteEffect *effect = &sBattleBgPaletteEffects[bank];

        switch (effect->flags & BATTLE_PALETTE_EFFECT_TYPE_MASK)
        {
        case BATTLE_PALETTE_EFFECT_PULSE:
            BattlePalette_UpdatePulse(effect, bank, (u16 *)PLTT, &gBattleBgPaletteBackup[0][0]);
            break;
        case BATTLE_PALETTE_EFFECT_CYCLE:
            BattlePalette_UpdateCycle(effect, bank, (u16 *)PLTT, &gBattleBgPaletteBackup[0][0]);
            break;
        case BATTLE_PALETTE_EFFECT_FADE:
            BattlePalette_UpdateFade(effect, bank, (u16 *)PLTT, &gBattleBgPaletteBackup[0][0]);
            break;
        }
    }
}

/** @brief Begin the return leg of active BG fades.
 *  @note Sets the return flag, replaces the period and resets the frame.
 *  @address 0x0804C728
 */
void BattlePalette_ReturnBgFade(u8 firstBank, u8 bankCount, u8 period)
{
    u8 index;

    for (index = 0; index < bankCount; index++)
    {
        struct BattlePaletteEffect *effect = &sBattleBgPaletteEffects[firstBank + index];

        if ((effect->flags & BATTLE_PALETTE_EFFECT_TYPE_MASK) == BATTLE_PALETTE_EFFECT_FADE)
        {
            effect->flags |= BATTLE_PALETTE_FADE_RETURNING;
            effect->period = period;
            effect->frame = 0;
        }
    }
}
