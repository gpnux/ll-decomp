#ifndef GUARD_BATTLE_SPRITE_WIPE_H
#define GUARD_BATTLE_SPRITE_WIPE_H

#include "battle_types.h"

/* Read-only resources are defined in battle_sprite_wipe.c and linked into ROM. */
extern const u32 gBattleStatusIconTiles[0xA0]; /* 0861AAA4; twenty 4bpp tiles. */
extern const u16 gBattleStatusIconPalette[16]; /* 0861C764; OBJ palette 15. */

/** @brief Refresh sprite bounds and advance the bottom-to-top wipe. */
void BattleSpriteWipe_Update(void);
/** @brief Apply the current wipe to hardware OAM during HBlank. */
void BattleSpriteWipe_UpdateHBlank(void);
/** @brief Append one bobbing status icon, returning the next free OAM slot. */
u8 Battle_DrawStatusIcon(struct BattleCombatant *combatant, u8 oamCursor, u16 controlFlags);
/** @brief Disable both wipe updates and HBlank writes. */
void BattleSpriteWipe_Reset(void);
/** @brief Start wiping an animation whose descending OAM range is already built.
 *  @note The animation must remain alive while the wipe is active. */
void BattleSpriteWipe_Start(struct BattleAnimation *animation);
/** @brief Upload requested status-icon tiles/palette and consume request bit 80. */
void Battle_UploadStatusIconGraphics(u16 *controlFlags);

#endif
