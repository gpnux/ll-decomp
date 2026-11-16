#include "battle_sprite_wipe.h"
#include "battle.h"
#include "battle_palette.h"
#include "battle_rules.h"
#include "core.h"
#include "gba.h"

/* Private .lcomm objects in original address order. The four-byte gap after
 * the pointer is the natural alignment of the 256-byte saved-X array. */
static struct BattleAnimation *sBattleSpriteWipeAnimation; /* 030009D0 */
static u16 sBattleSpriteWipeSavedX[CORE_SPRITE_OAM_COUNT]; /* 030009D8 */
static u8 sBattleSpriteWipeFrame; /* 03000AD8; modulo five */
static u8 sBattleSpriteWipeFirstOam; /* 03000AD9; inclusive upper index */
static u8 sBattleSpriteWipeEndOam; /* 03000ADA; exclusive lower index */
static u8 sBattleSpriteWipeTop; /* 03000ADB */
static u8 sBattleSpriteWipeBottom; /* 03000ADC */
static u8 sBattleSpriteWipeHeight; /* 03000ADD; pixels removed from the bottom */
static u16 sBattleSpriteWipeFlags; /* 03000ADE */

enum BattleSpriteWipeFlags
{
    BATTLE_SPRITE_WIPE_ACTIVE = 0x01,
    BATTLE_SPRITE_WIPE_HBLANK = 0x02,
    BATTLE_SPRITE_WIPE_BOTTOM_TO_TOP = 0x10,
    BATTLE_SPRITE_WIPE_MODE_MASK = 0xF0,
};

enum BattleStatusIconGraphics
{
    BATTLE_STATUS_ICON_TILE_BASE = 0x174,
    BATTLE_STATUS_ICON_PALETTE = 15,
    BATTLE_STATUS_ICON_MASK = 0x6E, /* Status numbers 1, 2, 3, 5 and 6. */
};

/** @brief Recompute the animated OBJ bounds and advance the wipe one pixel per five frames.
 *  @note Saved X values come from software OAM; the HBlank path only changes hardware OAM.
 *  Bounds retain the original byte truncation and the thirty-line trailing pattern. */
void BattleSpriteWipe_Update(void)
{
    u16 index;
    s32 bottom;

    if ((sBattleSpriteWipeFlags & BATTLE_SPRITE_WIPE_ACTIVE)
        && (sBattleSpriteWipeFlags & BATTLE_SPRITE_WIPE_MODE_MASK) == BATTLE_SPRITE_WIPE_BOTTOM_TO_TOP)
    {
        sBattleSpriteWipeFirstOam = sBattleSpriteWipeAnimation->oamStart;
        sBattleSpriteWipeEndOam = sBattleSpriteWipeAnimation->oamStart - sBattleSpriteWipeAnimation->oamCount;
        sBattleSpriteWipeTop = 160;
        sBattleSpriteWipeBottom = 0;
        for (index = sBattleSpriteWipeFirstOam; index > sBattleSpriteWipeEndOam; index--)
        {
            if (sBattleSpriteWipeTop > gSpriteOamBuffer[index].fields.y)
                sBattleSpriteWipeTop = gSpriteOamBuffer[index].fields.y;
            if (sBattleSpriteWipeBottom < (bottom = *(gBattleObjHeightTiles
                    + (gSpriteOamBuffer[index].fields.size
                       + (u8)(gSpriteOamBuffer[index].fields.shape * 4))) * 8
                    + gSpriteOamBuffer[index].fields.y))
                sBattleSpriteWipeBottom = bottom;
            sBattleSpriteWipeSavedX[index] = gSpriteOamBuffer[index].fields.x;
        }
        sBattleSpriteWipeFlags |= BATTLE_SPRITE_WIPE_HBLANK;
        sBattleSpriteWipeFrame = (sBattleSpriteWipeFrame + 1) % 5;
        if (sBattleSpriteWipeFrame == 0)
            sBattleSpriteWipeHeight++;
        if (sBattleSpriteWipeBottom - sBattleSpriteWipeHeight < sBattleSpriteWipeTop - 30)
        {
            sBattleSpriteWipeFlags &= ~BATTLE_SPRITE_WIPE_ACTIVE;
            sBattleSpriteWipeFlags &= ~BATTLE_SPRITE_WIPE_HBLANK;
        }
    }
}

/** @brief Read the byte-sized VCOUNT used by the sprite wipe.
 *  @note The nonvolatile view lets agbcc fold the HBlank queries into the
 *  original single sample; this helper has no out-of-line function body. */
static inline u16 BattleSpriteWipe_ReadScanline(void)
{
    return *(const u8 *)REG_VCOUNT;
}

/** @brief Hide wiped scanlines by moving the selected OBJ range to X=240.
 *  @note Seven sparse lines restore saved X on odd phases. The subtraction is
 *  deliberately u16, so a negative wipe boundary wraps before comparison. */
void BattleSpriteWipe_UpdateHBlank(void)
{
    u16 index;
    struct SpriteOamAttributes *oam;

    if (sBattleSpriteWipeFlags & BATTLE_SPRITE_WIPE_HBLANK)
    switch (sBattleSpriteWipeFlags & BATTLE_SPRITE_WIPE_MODE_MASK)
    {
    case BATTLE_SPRITE_WIPE_BOTTOM_TO_TOP:
        if (sBattleSpriteWipeTop <= BattleSpriteWipe_ReadScanline()
            && sBattleSpriteWipeBottom >= BattleSpriteWipe_ReadScanline())
        {
            index = sBattleSpriteWipeBottom - sBattleSpriteWipeHeight;
            if (index <= BattleSpriteWipe_ReadScanline())
            {
                oam = ((void *)OAM);
                if ((BattleSpriteWipe_ReadScanline() == index + 1 || BattleSpriteWipe_ReadScanline() == index + 3
                     || BattleSpriteWipe_ReadScanline() == index + 6 || BattleSpriteWipe_ReadScanline() == index + 13
                     || BattleSpriteWipe_ReadScanline() == index + 18 || BattleSpriteWipe_ReadScanline() == index + 23
                     || BattleSpriteWipe_ReadScanline() == index + 30) && (sBattleSpriteWipeFrame & 1))
                {
                    for (index = sBattleSpriteWipeFirstOam; index > sBattleSpriteWipeEndOam; index--)
                    {
                        struct SpriteOamAttributes *sprite = &oam[index];
                        u32 x = sBattleSpriteWipeSavedX[index];
                        sprite->x = x;
                    }
                }
                else
                {
                    for (index = sBattleSpriteWipeFirstOam; index > sBattleSpriteWipeEndOam; index--)
                        oam[index].x = 240;
                }
            }
        }
    }
}

/** @brief Emit the current status icon above a combatant and advance its bobbing phase.
 *  @note The accepted status mask is 6E; status 2 uses the default four-tile icon.
 *  Only the low nine X bits and named OBJ fields are changed; matrix padding is untouched. */
u8 Battle_DrawStatusIcon(struct BattleCombatant *combatant, u8 oamCursor, u16 controlFlags)
{
    u16 tile;
    u32 bob;
    struct SpriteOamAttributes *sprite;

    if ((controlFlags & BATTLE_CONTROL_SHOW_STATUS_ICONS)
        && !(combatant->displayState & BATTLE_DISPLAY_HIDE_STATUS_ICON)
        && Battle_CheckStatusMask(combatant, BATTLE_STATUS_ICON_MASK) == BATTLE_STATUS_MATCH_ACCEPTED)
    {
        if (combatant->displayState & BATTLE_DISPLAY_STATUS_ICON_RISING)
        {
            combatant->statusIconPhase++;
            if (combatant->statusIconPhase > 15)
                combatant->displayState &= ~BATTLE_DISPLAY_STATUS_ICON_RISING;
        }
        else
        {
            combatant->statusIconPhase--;
            if (combatant->statusIconPhase == 0)
                combatant->displayState |= BATTLE_DISPLAY_STATUS_ICON_RISING;
        }
        bob = combatant->statusIconPhase >> 2;
        switch (combatant->status)
        {
        case 1:
            tile = 0x180;
            break;
        case 3:
            tile = 0x184;
            break;
        case 5:
            tile = 0x178;
            break;
        case 6:
            tile = 0x17C;
            break;
        case 2:
        case 4:
        default:
            tile = BATTLE_STATUS_ICON_TILE_BASE;
            break;
        }
        sprite = &gSpriteOamBuffer[oamCursor].fields;
        sprite->y = combatant->homeY - (u16)(bob + 34);
        sprite->affineMode = 0;
        sprite->objMode = 0;
        sprite->mosaic = 0;
        sprite->bpp = 0;
        sprite->shape = 0;
        sprite->x = combatant->homeX + 4;
        sprite->affineIndexLow = 0;
        sprite->hFlip = 0;
        sprite->vFlip = 0;
        sprite->size = 1;
        sprite->tile = tile;
        sprite->priority = 1;
        sprite->palette = BATTLE_STATUS_ICON_PALETTE;
        oamCursor--;
    }
    return oamCursor;
}

/** @brief Disable the wipe without modifying the saved animation or OAM coordinates. */
void BattleSpriteWipe_Reset(void)
{
    sBattleSpriteWipeFlags = 0;
}

/** @brief Select an animation and start a bottom-to-top wipe.
 *  @note OR preserves other flag bits, matching the original startup contract. */
void BattleSpriteWipe_Start(struct BattleAnimation *animation)
{
    sBattleSpriteWipeAnimation = animation;
    sBattleSpriteWipeFlags |= BATTLE_SPRITE_WIPE_ACTIVE | BATTLE_SPRITE_WIPE_BOTTOM_TO_TOP;
    sBattleSpriteWipeFrame = 0;
    sBattleSpriteWipeHeight = 0;
}

/** @brief Consume the scene's status-icon graphics upload request during VBlank.
 *  @note The DMA source contains twenty tiles; OBJ palette bank 15 is marked occupied. */
void Battle_UploadStatusIconGraphics(u16 *controlFlags)
{
    if (*controlFlags & BATTLE_CONTROL_UPLOAD_STATUS_ICONS)
    {
        DmaCopy(3, gBattleStatusIconTiles, &OBJ_GRAPHICS[BATTLE_STATUS_ICON_TILE_BASE], sizeof(gBattleStatusIconTiles), 32);
        DmaWait(3);
        BattlePalette_LoadObjBanks(gBattleStatusIconPalette, BATTLE_STATUS_ICON_PALETTE, 1);
        *controlFlags &= ~BATTLE_CONTROL_UPLOAD_STATUS_ICONS;
    }
}

/* Split read-only data, in ROM address order. */

/* ROM 0x0861AAA4..0x0861AD24; .rodata. */
const u32 gBattleStatusIconTiles[0xA0] __attribute__((aligned(1))) =
{
    0x11000000, 0xEE110000, 0xFFED1000, 0xFF9ED100, 0xF9AFE100, 0x9AFFFE10, 0x99999E10, 0xFFA9FE10, 0x00000111,
    0x00011EEE, 0x001DEFFF, 0x01DE9FFF, 0x01EFA9FF, 0x1EFFFA9F, 0x1E99999F, 0x1EF9AFFF, 0xFA9FFE10, 0xF9F9FE10,
    0xA99FE100, 0xA9FED100, 0x9FED1000, 0xFFE10000, 0xDEFE1000, 0x11111100, 0x1EFF9AFF, 0x1EF9F9FF, 0x01EF99AF,
    0x01DEF9AF, 0x001DEF9F, 0x00011DEE, 0x00000111, 0x00000000, 0x11100000, 0xEEE11000, 0xFFFED100, 0x223FED10,
    0xFF23FE10, 0x3EF23FE1, 0x23FE2FE1, 0xF3FE2FE1, 0x00000011, 0x000011EE, 0x0001DEFF, 0x001DEF32, 0x001EF32E,
    0x01EFF2EF, 0x01EFF2EF, 0x01EFF32E, 0x3EFE2FE1, 0xFFE23FE1, 0xFE23FE10, 0x223FED10, 0xFFFED100, 0xEEFE1000,
    0x11DFE100, 0x00111110, 0x01EFEF33, 0x01EF3EFF, 0x001E32EF, 0x001DE322, 0x0001DEFF, 0x000011DE, 0x00000011,
    0x00000000, 0x11100000, 0xEEE11000, 0xFFFED100, 0xFFFFED10, 0xFF55FE10, 0xF565FFE1, 0x565FFFE1, 0x65FFFFE1,
    0x00000011, 0x000011EE, 0x0001DEFF, 0x001DEFFF, 0x001EF55F, 0x01EFF565, 0x01EFFF56, 0x01EFFFF5, 0x565FFFE1,
    0xF565FFE1, 0xFF55FE10, 0xFFFFED10, 0xFFFED100, 0xEFFE1000, 0x1DEFE100, 0x01111110, 0x01EFFF56, 0x01EFF565,
    0x001EF55F, 0x001DEFFF, 0x0001DEFF, 0x000011DE, 0x00000011, 0x00000000, 0x11100000, 0xEEE11000, 0xFFFED100,
    0xBBBFED10, 0xCC4BFE10, 0xCCC4BFE1, 0xCBBCBFE1, 0xCBBCBFE1, 0x00000011, 0x000011EE, 0x0001DEFF, 0x001DEFBB,
    0x001EFB4C, 0x01EFB4CC, 0x01FFBCBB, 0x01EFBCBB, 0xCCC4BFE1, 0xCCCBFFE1, 0xBBBFFE10, 0xCCBFED10, 0xBBFED100,
    0xEFFE1000, 0x1DEFE100, 0x01111110, 0x01EFB4CC, 0x01EFFBCC, 0x001EFFBB, 0x001DEFBC, 0x0001DEFB, 0x000011DE,
    0x00000011, 0x00000000, 0x11100000, 0xEEE11000, 0xFFFED100, 0x778FED10, 0x77778E10, 0xE7F877E1, 0x777FFFE1,
    0x8777FFE1, 0x00000011, 0x000011EE, 0x0001DEFF, 0x001DEFF8, 0x001EFFFE, 0x01EFFFFF, 0x01EFFF87, 0x01EFFFEE,
    0xFF877FE1, 0x8FFFFFE1, 0xFFFFFE10, 0xEFFFFD10, 0x7EFFD100, 0xEFFE1000, 0x1DEFE100, 0x01111110, 0x01EF778E,
    0x01EFE777, 0x001EFE7E, 0x001D7777, 0x0001DE87, 0x000011DE, 0x00000011, 0x00000000,
};

/* ROM 0x0861C764..0x0861C784; .rodata.0061C764. */
const u16 gBattleStatusIconPalette[16] __attribute__((section(".rodata.0061C764"), aligned(1))) =
{
    0xC6BB, 0x35AD, 0x512D, 0x61B2, 0x96A8, 0x08D3, 0x1119, 0x59E8, 0xEA8D, 0x01B8, 0x825E, 0x0DE6, 0x9730,
    0xD294, 0xEB5A, 0xFFFF,
};

/* End of split read-only data. */
