#ifndef GUARD_BATTLE_H
#define GUARD_BATTLE_H

#include "battle_types.h"

enum BattleControlFlags
{
    BATTLE_CONTROL_RASTER_DISABLED = 0x0008,
    BATTLE_CONTROL_SPECIAL_ENCOUNTER = 0x0020,
    BATTLE_CONTROL_UPLOAD_STATUS_ICONS = 0x0080,
    BATTLE_CONTROL_SHOW_STATUS_ICONS = 0x0100,
    BATTLE_CONTROL_AUTO = 0x4000,
};

/** @brief Converted battle input, rather than raw GBA key bits.
 *  @note Low bits 0..3 hold directions; high bits contain press/repeat events. */
enum BattleInputFlags
{
    BATTLE_INPUT_HELD_DIRECTIONS = 0x000F,
    BATTLE_INPUT_REPEAT_UP = 0x0100,
    BATTLE_INPUT_REPEAT_DOWN = 0x0200,
    BATTLE_INPUT_REPEAT_LEFT = 0x0400,
    BATTLE_INPUT_REPEAT_RIGHT = 0x0800,
    BATTLE_INPUT_REPEAT_DIRECTIONS = 0x0F00,
    BATTLE_INPUT_PRESS_A = 0x1000,
};

enum BattleWindowState
{
    BATTLE_WINDOW_CLOSED = 0,
    BATTLE_WINDOW_OPENING = 1,
    BATTLE_WINDOW_OPEN = 4,
    BATTLE_WINDOW_CLOSING = 5,
};

/** @brief One expanding/contracting window, in tile coordinates.
 *  @note State is stored as u16; the public query deliberately reads its low byte. */
struct BattleWindow
{
    u8 targetX;
    u8 targetY;
    u8 targetWidth;
    u8 targetHeight;
    u8 x;
    u8 y;
    u8 width;
    u8 height;
    u8 duration;
    u8 frame;
    u8 curve;
    u8 unknown0B;
    u16 state;
    u16 baseTile;
    u16 *tilemap;
};

/** @brief Cached offsets for BG0 through BG3, in hardware register order.
 *  @note The eight halfwords occupy ordinary RAM at 03000500. */
typedef struct BattleBgScroll
{
    u16 bg0Hofs;
    u16 bg0Vofs;
    u16 bg1Hofs;
    u16 bg1Vofs;
    u16 bg2Hofs;
    u16 bg2Vofs;
    u16 bg3Hofs;
    u16 bg3Vofs;
} BattleBgScroll;

typedef char BattleWindowSizeCheck[(sizeof(struct BattleWindow) == 0x14) ? 1 : -1];
typedef char BattleWindowStateCheck[(offsetof(struct BattleWindow, state) == 0xC) ? 1 : -1];
typedef char BattleWindowTilemapCheck[(offsetof(struct BattleWindow, tilemap) == 0x10) ? 1 : -1];
typedef char BattleBgScrollSizeCheck[(sizeof(BattleBgScroll) == 0x10) ? 1 : -1];
typedef char BattleBgScrollBg0HofsCheck[(offsetof(BattleBgScroll, bg0Hofs) == 0x0) ? 1 : -1];
typedef char BattleBgScrollBg0VofsCheck[(offsetof(BattleBgScroll, bg0Vofs) == 0x2) ? 1 : -1];
typedef char BattleBgScrollBg1HofsCheck[(offsetof(BattleBgScroll, bg1Hofs) == 0x4) ? 1 : -1];
typedef char BattleBgScrollBg1VofsCheck[(offsetof(BattleBgScroll, bg1Vofs) == 0x6) ? 1 : -1];
typedef char BattleBgScrollBg2HofsCheck[(offsetof(BattleBgScroll, bg2Hofs) == 0x8) ? 1 : -1];
typedef char BattleBgScrollBg2VofsCheck[(offsetof(BattleBgScroll, bg2Vofs) == 0xA) ? 1 : -1];
typedef char BattleBgScrollBg3HofsCheck[(offsetof(BattleBgScroll, bg3Hofs) == 0xC) ? 1 : -1];
typedef char BattleBgScrollBg3VofsCheck[(offsetof(BattleBgScroll, bg3Vofs) == 0xE) ? 1 : -1];

/* EWRAM storage is defined by battle.c. IWRAM state stays private. */
extern u16 gBattleStatusTilemap[BATTLE_TILEMAP_SIZE]; /* 020352C0 */
extern u16 gBattleWindowTilemap[BATTLE_TILEMAP_SIZE]; /* 02035AC0 */
extern u16 gBattleAnimationTilemap[BATTLE_TILEMAP_SIZE]; /* 020362C0 */
extern s8 gBattleWaveSamples[360]; /* 02036EC0 */

/* Existing EWRAM graphics workspaces, resolved by the linker address fallbacks. */
extern u8 D_02020E00[];
extern u8 D_02020F20[];
extern u8 D_020212C0[];
extern u8 D_0202B2C0[];
extern u8 D_020302C0[];
extern u8 D_02037C28[];

/* Four LZ77-compressed 0x1000-byte chunks for BG character block 2. */
extern const void *const D_087EBDF0[4];

struct BattleBackgroundResources
{
    const void *tiles;
    const u16 *palette;
    const void *tilemap;
};

typedef char BattleBackgroundResourcesSizeCheck[(sizeof(struct BattleBackgroundResources) == 12) ? 1 : -1];
extern const struct BattleBackgroundResources D_087ED394[];
extern const u32 D_0861A4A4[0x50];
extern const u8 D_083939F8[13];
/* OBJ shape * 4 + size: width/height in tiles, then total tile count. */
extern const u8 D_08393A18[12];
extern const u8 gBattleObjHeightTiles[12]; /* shape * 4 + size; height in 8px tiles. */
/** @brief Tile counts for the twelve OBJ shape/size combinations.
 *  @note Indexed by shape * 4 + size and shared with command-icon upload.
 *  @address 0x08393A30
 */
extern const u8 gBattleObjTileCounts[12];
/* Background IDs for scene configs, encounters 58+, and encounters 28+. */
extern const u8 D_083989B0[];
extern const u8 D_083989CB[];
extern const u8 D_083989DC[];
extern const u8 D_08619AA4[];
extern const u8 D_08619FA4[];
extern const u8 D_0861A5E4[];
extern const u16 D_0861C664[];

s16 Battle_InterpolateS16(s16 start, s16 delta, s16 duration, s16 frame, s8 curve);
void Battle_Update(void);
void Battle_PrepareEncounter(s8 encounterId);
void Battle_UpdateVBlank(void);
void Battle_UpdateHBlank(void);
void Battle_UpdateTransitionVBlank(void);
void Battle_UpdateRasterEffect(void);
void Battle_ApplyRasterLine(void);
void Battle_LoadStatusGraphics(void);
void Battle_InitStatusTilemap(void);
void Battle_LoadBackground(u32 unused);
u32 Battle_GetBackgroundId(void);
void Battle_DrawStatusNumber(u16 value, u8 x, u8 y, u8 paletteOffset, u8 paletteBase);
void Battle_DrawStatusLabels(u8 nameSlot, u8 x, u8 y, u8 paletteOffset,
    u8 hpPaletteOffset, u8 mpPaletteOffset, u8 paletteBase);
void Battle_RestoreStatusGraphics(void);
/* Also used by script input; the second argument is ignored by the helper. */
void Battle_UpdateInputRepeat(u16 keys, u16 *unusedInputScratch);
void Battle_ResetInputDelay(void);
void Battle_ReadInput(void);
/** @brief Store the low halfword of a promoted selection value.
 *  @note Negative one disables a direction slot.
 *  @address 0x08018798
 */
void Battle_SetSelection(u8 slot, u32 selection);
u8 Battle_GetEncounterId(void);
u16 Battle_GetControlFlags(void);
void Battle_SetControlFlags(u16 flags);
void Battle_ClearControlFlags(u16 flags);
u16 Battle_GetInput(void);
u16 Battle_GetPressedKeys(void);
void Battle_InitDrawList(struct BattleDrawNode *head);
void Battle_InitDrawNode(struct BattleDrawNode *node, u8 sortKey);
void Battle_InsertDrawNode(struct BattleDrawNode *head, struct BattleDrawNode *node);
void Battle_SeedRandom(u32 seed);
u32 Battle_NextRandom(void);
struct BattleCombatant *Battle_GetCombatants(void);
struct BattleCombatant *Battle_GetCursor(void);
struct BattleCombatant *Battle_GetEnemies(void);
void Battle_InitHintText(void);
const u8 *Battle_GetHintText(void);
void Battle_ResetBgRequests(void);
void Battle_SetBgRequests(u16 flags);
u16 Battle_GetBgRequests(void);
void Battle_ClearBgRequests(u16 flags);
void Battle_InitWindowTilemap(void);
void Battle_UploadWindowTilemap(void);
void Battle_ClearAnimationTilemap(void);
void Battle_ClearUploadWorkspaces(void);
void Battle_LoadBackgroundBlock(void);
void Battle_ResetWindows(void);
void Battle_UpdateWindows(void);
void Battle_OpenWindow(u8 slot, u16 *tilemap, u16 baseTile, u8 duration, u8 curve,
                      u8 x, u8 y, u8 width, u8 height);
void Battle_SetWindowRect(u8 slot, u8 x, u8 y, u8 width, u8 height);
void Battle_DrawWindowBorder(u16 *tilemap, u16 baseTile, u8 x, u8 y, u8 width, u8 height);
void Battle_SetTilemapTiles(u16 *tilemap, u16 tile, u8 x, u8 y, u8 width, u8 height);
void Battle_ShiftTilemapRect(u16 *tilemap, u8 mode, s8 dx, s8 dy,
                           u8 x, u8 y, u8 width, u8 height);
void Battle_CloseWindow(u8 slot, u8 duration, u8 curve);
/** @brief Return the window state's low byte in a full-width result.
 *  @note Consumers with byte-sized comparisons explicitly cast to u8. */
u32 Battle_GetWindowState(u8 slot);
/** @brief Set a tile rectangle's palette using promoted drawing arguments.
 *  @note The callee truncates palette to u16 and coordinates/extents to u8.
 *  @address 0x0801A074
 */
void Battle_SetTilemapPalette(u16 *tilemap, u32 paletteArg, u32 xArg, u32 yArg,
    u32 widthArg, u32 heightArg);
void Battle_ResetRasterFlags(void);
u16 Battle_GetRasterFlags(void);
void Battle_ClearRasterFlags(u16 flags);
void Battle_StopWaveEffect(void);
void Battle_StartBrightnessTransition(u8 speed, u16 flags);
void Battle_EndRasterTransition(void);
void Battle_HideWindowLayer(void);
void Battle_SetBlend(u16 control, u8 eva, u8 evb);
void Battle_ResetBackgroundLoadProgress(void);
void Battle_FinishBackgroundLoadProgress(void);
u8 Battle_GetBackgroundLoadProgress(void);
void Battle_ResetBackgroundLoader(void);
u8 Battle_UpdateBackgroundLoader(u8 resourceId, u8 charBase, u8 paletteBank, u8 priority);
/** @brief Load the compressed graphics block selected by an animation.
 *  @note The render type chooses sprite or background graphics handling.
 *  @address 0x0801A3C4
 */
void Battle_LoadAnimationBlock(struct BattleAnimation *animation);
void Battle_UploadBgScroll(void);
/* Signed pixel offsets are stored in the selected layer's halfword cache. */
void Battle_SetBgScroll(u8 layer, s16 x, s16 y);
void Battle_CopyAnimation(struct BattleAnimation *destination, const struct BattleAnimation *source);
void Battle_BindAnimationTables(struct BattleAnimation *animation);
void Battle_InitBackgroundAnimation(struct BattleAnimation *animation);
void Battle_SetupAnimation(struct BattleAnimation *animation, u8 x, u8 y,
    u16 initialBaseTile, u8 initialPaletteSlot, const u16 *definition,
    const u16 *palettes, u16 graphicsId, u16 graphicsBlockCount, u16 flags);
u8 Battle_BuildSpriteAnimation(struct BattleAnimation *animation, u8 oamCursor, u8 *affineCursor);
void Battle_BuildBackgroundAnimation(struct BattleAnimation *animation);
u8 Battle_BuildAnimationFrame(struct BattleAnimation *animation, u8 oamCursor, u8 *affineCursor);
u8 Battle_UploadSpriteAnimation(struct BattleAnimation *animation, u8 oamCursor);
void Battle_UploadBackgroundAnimation(struct BattleAnimation *animation);
u8 Battle_UploadAnimationFrame(struct BattleAnimation *animation, u8 oamCursor);
const struct BattleKeyframe *Battle_FindKeyframe(const u16 *track, u16 frame);
const struct BattleKeyframe *Battle_GetTrackKeyframe(const struct BattleAnimation *animation, u8 track, u16 frame);
u8 Battle_GetAnimationPaletteCount(const struct BattleAnimation *animation);
u16 Battle_GetAnimationFrameCount(const struct BattleAnimation *animation);
void Battle_InterleaveAffineMatrices(void);
void Battle_MarkUploadedTile(u16 tile);
u8 Battle_IsTileUploaded(u16 tile);

/** @brief VRAM screen block used for battle-window tilemap uploads.
 *  @note This fixed address allocates no C storage.
 *  @address 0x06007000
 */
#define BATTLE_WINDOW_TILEMAP_VRAM ((u16 *)0x06007000)

/** @brief Blank 4bpp tile used by the battle status tilemap.
 *  @note Battle_InitStatusTilemap clears its 32 bytes; this fixed address
 *  names VRAM and allocates no C storage.
 *  @address 0x06005800
 */
#define BATTLE_STATUS_BLANK_TILE_VRAM ((u8 *)0x06005800)

/** @brief Read-only graphics uploaded by battle scene initialization.
 *  @note The 0x2C0 bytes are defined in battle.c and linked into ROM.
 *  @address 0x0861A7E4
 */
extern const u32 D_0861A7E4[0xB0];

/* Scene renderer services implemented in battle.c. */
void Battle_HideUnusedOam(u8 oamCursor);
void Battle_ApplyBgRequests(void);
void Battle_PlayEncounterBgm(void);
void Battle_UpdateAutoIndicator(void);
s32 Battle_BuildDrawListSprites(struct BattleDrawNode *first, u8 oamCursor);

struct PartyCharacter;

#define BATTLE_DAMAGE_NUMBER_COUNT 7
#define BATTLE_BOSS_DEFEAT_HANDLER_COUNT 17

/** @brief Animation modes shared by the party and extended-enemy selectors.
 *  @note Ordinary enemies use a different mode numbering.
 *  @address 0x0801CBA4, 0x0801CE80
 */
enum BattleCombatantAnimationMode
{
    BATTLE_COMBATANT_ANIM_IDLE = 0,
    BATTLE_COMBATANT_ANIM_ATTACK = 1,
    BATTLE_COMBATANT_ANIM_STATUS = 4,
    BATTLE_COMBATANT_ANIM_ALTERNATE_STATUS = 10,
};

/** @brief Pixel extent requested from the combatant size helper.
 *  @note The returned byte contains pixels, not tiles.
 *  @address 0x0801EC3C
 */
enum BattleCombatantDimension
{
    BATTLE_COMBATANT_WIDTH = 0,
    BATTLE_COMBATANT_HEIGHT = 1,
};

/** @brief Cursor movement subfield within BattleCombatant.displayState.
 *  @note Mode 0x60 begins a move to target homeX/homeY-48 on the next update.
 *  @address 0x0801FAB8, 0x08020FB8, 0x0802103C
 */
enum BattleCursorMotionState
{
    BATTLE_CURSOR_MOTION_MASK = 0x00F0,
    BATTLE_CURSOR_MOVING = 0x0010,
    BATTLE_CURSOR_BEGIN_TARGET_MOVE = 0x0060,
};

/** @brief Verified recovery selectors shared by the target and direct helpers.
 *  @note Values 3..9 select individual statuses. Revival acts on status eight;
 *  CURE_STATUS clears non-defeat statuses without applying the amount as HP.
 *  TURN_END_HP sets a flag and ignores the amount. Item/weapon numbering is separate.
 *  @address 0x0801EEE4, 0x0801F3FC, 0x0804612C
 */
enum BattleRecoveryEffect
{
    BATTLE_RECOVERY_HP = 0,
    BATTLE_RECOVERY_MP = 1,
    BATTLE_RECOVERY_HP_AND_MP = 2,
    BATTLE_RECOVERY_CURE_POISON = 3,
    BATTLE_RECOVERY_CURE_PARALYSIS = 4,
    BATTLE_RECOVERY_CURE_SLEEP = 5,
    BATTLE_RECOVERY_CURE_PETRIFICATION = 6,
    BATTLE_RECOVERY_CURE_CONFUSION = 7,
    BATTLE_RECOVERY_CURE_SILENCE = 8,
    BATTLE_RECOVERY_REVIVE = 10,
    BATTLE_RECOVERY_CURE_STATUS = 11,
    BATTLE_RECOVERY_TURN_END_HP = 12,
};

/** @brief Target-list mode for the shared recovery presentation.
 *  @note Side-list mode retains the original targetSlot <= 10 side test.
 *  @address 0x0801EEE4
 */
enum BattleRecoveryTargetMode
{
    BATTLE_RECOVERY_SELECTED_TARGET = 0,
    BATTLE_RECOVERY_SIDE_TARGETS = 1,
};

/** @brief One damage/recovery number and its screen position.
 *  @note Seven private records occupy 0x1C bytes; no extra allocation is introduced.
 *  @address 0x03000670
 */
struct BattleDamageNumber
{
    u16 value;
    u8 x;
    u8 y;
};

/** @brief A fainted party member in the sorted revival list.
 *  @note The link occupies the first 0xC bytes; the combatant pointer is at +0xC.
 *  @address 0x030006A0
 */
struct BattleFaintedNode
{
    struct BattleDrawNode link;
    struct BattleCombatant *combatant;
};

/** @brief Resources shared by the two fixed combatant-effect setup entries.
 *  @note The twelve-byte ROM record supplies a definition and palette variants;
 *  its graphics id is relative to the fixed base 0x541.
 *  @address 0x0839B2A4
 */
struct BattleSharedEffectResources
{
    const u16 *definition;
    const u16 *palettes;
    u16 graphicsId;
    u16 graphicsBlockCount;
};

typedef char BattleSharedEffectResourcesSizeCheck[(sizeof(struct BattleSharedEffectResources) == 12) ? 1 : -1];
typedef char BattleDamageNumberSizeCheck[(sizeof(struct BattleDamageNumber) == 4) ? 1 : -1];
typedef char BattleDamageNumberXCheck[(offsetof(struct BattleDamageNumber, x) == 2) ? 1 : -1];
typedef char BattleDamageNumberYCheck[(offsetof(struct BattleDamageNumber, y) == 3) ? 1 : -1];
typedef char BattleFaintedNodeSizeCheck[(sizeof(struct BattleFaintedNode) == 0x10) ? 1 : -1];
typedef char BattleFaintedNodeCombatantCheck[(offsetof(struct BattleFaintedNode, combatant) == 0xC) ? 1 : -1];

/** @brief Shared array of five party and seven enemy records.
 *  @note COMMON storage is defined only in battle.c; the enemy view
 *  below is an alias, not a second allocation. Private state is not exported.
 *  @address 0x02037028
 */
extern struct BattleCombatant gBattleCombatants[BATTLE_COMBATANT_COUNT];
#define BATTLE_ENEMY_COMBATANTS (gBattleCombatants + BATTLE_PARTY_COUNT)

/* Read-only tables are defined in battle.c and linked into ROM. */
/** @brief Home coordinates for the five party formation slots.
 *  @note Each table contains one byte per slot; command input interpolates from these positions.
 *  @address 0x08393A48, 0x08393A4D
 */
extern const u8 gBattlePartyHomeX[BATTLE_PARTY_COUNT];
extern const u8 gBattlePartyHomeY[BATTLE_PARTY_COUNT];
extern const u8 D_08393A3C[];
extern const struct BattleCharacterActionInfo gBattleCharacterActionInfo[BATTLE_CHARACTER_ID_COUNT]; /* 083988A8 */
extern const struct BattleEncounter gBattleEncounters[]; /* 08399C84 */
/** @brief OBJ palette pointer for each party character id, including both id 0/1 forms.
 *  @note The loader uploads the selected entries to consecutive banks starting at one.
 *  @address 0x087ED6A8
 */
extern const u16 *const gBattlePartySpritePalettes[BATTLE_CHARACTER_ID_COUNT];
extern const struct BattleSharedEffectResources gBattleSharedEffectResources;

void Battle_SetInitialCombatantAnimation(struct BattleCombatant *combatant, u8 unusedX, u8 unusedY,
    u16 baseTile, u8 paletteSlot);
void Battle_SetHitReactionAnimation(struct BattleCombatant *combatant);
void Battle_SetPartyAnimationMode3(struct BattleCombatant *combatant);
void Battle_SetSharedEffectAnimation(struct BattleCombatant *combatant, u8 paletteVariant);
u8 Battle_HasRemainingPartyAfterEscape(struct BattleCombatant *unused);
void Battle_SetSharedEffectAnimationAlternate(struct BattleCombatant *combatant, u8 paletteVariant);

/* Record and turn scheduling interfaces; detailed contracts accompany the bodies. */
void Battle_InitCombatants(struct BattleCombatant *combatants);
u8 Battle_UpdateActionQueue(struct BattleCombatant *combatants);
u8 Battle_UpdateActionQueueAlternate(struct BattleCombatant *combatants);
u8 Battle_UpdateTurnActions(struct BattleCombatant *combatants);
void Battle_BuildTurnOrder(struct BattleCombatant *unused);
u8 Battle_ArePartyMembersDisabled(struct BattleCombatant *combatants);
void Battle_ChooseEnemyCommands(struct BattleCombatant *enemies, struct BattleCombatant *combatants);

/* The three selectors use their own mode numbering; unused coordinates stay in the ABI. */
void Battle_SetEnemyAnimation(struct BattleCombatant *combatant, u8 mode, u16 baseTile,
    u8 paletteSlot, u8 animationOffset);
void Battle_SetPartyAnimation(struct BattleCombatant *combatant, u8 mode, u16 baseTile,
    u8 paletteSlot, u8 hitIndex);
void Battle_SetBossAnimation(struct BattleCombatant *combatant, u8 mode, u16 baseTile,
    u8 paletteSlot, u8 skillIndex);
void Battle_RestoreIdleAnimation(struct BattleCombatant *combatant, u8 unusedX,
    u8 unusedY, u16 baseTile, u8 paletteSlot);
void Battle_SetAnimationById(struct BattleAnimation *animation, u16 animationId, u16 baseTile,
    u8 paletteSlot, u16 flags);
void Battle_SetEffectAnimation(struct BattleCombatant *combatant, u8 x, u8 y, u16 baseTile,
    u8 paletteSlot, u16 animationId, u16 flags);
void Battle_UpdateEnemyAttackSpacingWidth(struct BattleCombatant *combatant);
u8 Battle_GetCombatantDimension(const struct BattleCombatant *combatant, u8 dimension);
void Battle_AddAttackGauge(struct BattleCombatant *combatant);
void Battle_ResetAttackGauge(struct BattleCombatant *combatant);

/* +A2 is a status-display palette offset; +A3 is an allocated highlight bank. */
void Battle_UpdateStatusPaletteOffset(struct BattleCombatant *combatant, u8 mode);
u8 Battle_GetStatusPaletteBank(const struct BattleCombatant *combatant, u8 mode);
void Battle_StartCombatantHighlight(struct BattleCombatant *combatant, u8 duration);
void Battle_StopCombatantHighlight(struct BattleCombatant *combatant);
void Battle_StartStatusHighlight(struct BattleCombatant *combatant, u8 mode, u8 unused);
void Battle_StopStatusHighlight(struct BattleCombatant *combatant, u8 mode);
void Battle_ResetPartyPaletteIds(void);
void Battle_AppendPartyPaletteId(u8 characterId);
void Battle_LoadPartyPalettes(void);
u8 Battle_GetSharedEffectPaletteSlot(void);

/* Recovery helpers share a private phase and return zero while busy, one when done. */
u32 Battle_UpdateTargetRecoveryEffect(struct BattleCombatant *selection, struct BattleCombatant *combatants,
    u8 targetMode, u8 effect, u16 amount);
u32 Battle_UpdateCombatantRecoveryEffect(struct BattleCombatant *combatant, u8 effect, u16 amount);
u8 Battle_GetActionRetargetClass(const struct BattleCombatant *combatant);
u8 Battle_GetActionFormationFilter(const struct BattleCombatant *combatant);
void Battle_ResetEffectPhase(void);
void Battle_BuildActionTargets(struct BattleCombatant *combatant);
u8 *Battle_GetActionTargets(void);
u8 Battle_GetActionTargetCount(void);
const struct BattleEncounter *Battle_GetEncounter(void);

void Battle_ResetCharacterMarks(void);
void Battle_MarkCharacter(struct BattleCombatant *combatant);
u8 Battle_IsCharacterMarked(const struct BattleCombatant *combatant);
void Battle_AddDamageNumber(struct BattleCombatant *combatant);
void Battle_AddTargetDamageNumbers(struct BattleCombatant *combatant);
/** @brief 绘制带反弹偏移的伤害数字，返回递减后的八位 OAM 游标。
 *  @note 每条目使用四个已上传图块；32x8、16 色 OBJ 的调色板为 15、优先级为 0。
 *  前两段反弹使用有符号除法和插值；数字条目以 s8 索引遍历。
 */
u8 Battle_DrawDamageNumbers(u8 oamCursor);
void Battle_ResetDamageNumbers(u8 paletteId);
void Battle_AddFaintedCombatant(struct BattleCombatant *combatant, u8 priority);
void Battle_ReviveCombatant(struct BattleCombatant *combatant, u8 slot, u16 hp);
void Battle_AdvanceFaintedTurnCounts(void);
void Battle_DecrementEnemyCount(void);
struct BattleCombatant *Battle_GetLastDefeatedEnemy(void);
void Battle_ResetDefeatQueue(void);
void Battle_QueueDefeatedCombatant(struct BattleCombatant *combatant);
u8 Battle_ApplyPendingDamage(struct BattleCombatant *combatant);

/* The scene cursor reuses the combatant layout but is outside gBattleCombatants. */
void Battle_InitCursor(struct BattleCombatant *cursor);
void Battle_SetCursorAnimation(struct BattleCombatant *cursor, u16 state);
void Battle_UpdateCursor(struct BattleCombatant *cursor, struct BattleCombatant *combatants);
void Battle_MoveCursor(struct BattleCombatant *cursor, s16 x, s16 y, u16 duration, u8 curve);
void Battle_SetCursorTarget(struct BattleCombatant *cursor, u8 targetSlot, u16 flags);

/* Display and revival helpers retain their original calling conventions. */
u8 Battle_UpdateDamageNumbers(void);
u8 Battle_ProcessDefeatQueue(void);
void Battle_DrawPartyStatus(struct BattleCombatant *combatant, u8 slot);
/** @brief 绘制可见队员的计量条，上传共用图块与调色板，返回递减的八位 OAM 游标。
 *  @note 跳过空槽和 displayState 的 0x20 位；坐标为 (32+40*slot, 144)，使用优先级 2、调色板 6。
 *        官方 DMA3 上传 0x280 字节并等待；普通 C 的完整 0x164 字节及已解析重定位 100% 匹配。
 */
u8 Battle_DrawPartyGauges(struct BattleCombatant *combatants, u8 oamCursor);
void Battle_ReturnCursorToRest(struct BattleCombatant *cursor, u16 duration, u8 curve);
s8 Battle_SelectRevivalCandidate(u8 mode);

/* Additional combatant initialization, effects and result processing. */
/** @brief 在角色原点偏移处绘制地面精灵，返回递减后的八位 OAM 游标。
 *  @note 坐标为 homeX-16、homeY-5；id 0..10 使用图块 0x140，其余使用 0x12E。
 *  32x8、16 色 OBJ 的优先级为 3；主动画标志选择调色板和半透明模式。
 */
u8 Battle_DrawCombatantGroundSprite(struct BattleCombatant *combatant, u8 oamCursor);
void Battle_AddActionDamageNumber(struct BattleCombatant *combatant, u8 mode);
void Battle_StartCombatantEffect(struct BattleCombatant *combatant, u8 timelineVariant, u16 effectId);
void Battle_StartObjWindowEffect(struct BattleCombatant *combatant, u8 timelineVariant);
void Battle_BuildActionDamageNumbers(struct BattleCombatant *unusedActor, struct BattleCombatant *unusedTarget);
void Battle_AddEnemyActionDamageNumbers(const struct BattleCombatant *actor, struct BattleCombatant *targets);
void Battle_AddBossActionDamageNumbers(const struct BattleCombatant *actor, struct BattleCombatant *targets);
u8 Battle_ApplyActionTargetResults(struct BattleCombatant *unusedActor, struct BattleCombatant *unusedTarget);
u8 Battle_ApplyEnemyActionDamage(const struct BattleCombatant *actor, struct BattleCombatant *targets);
u8 Battle_ApplyBossActionDamage(const struct BattleCombatant *actor, struct BattleCombatant *targets);
u8 Battle_UpdateDefeatRemovalEffects(void);
void Battle_PrepareCombatantAction(struct BattleCombatant *actor, struct BattleCombatant *combatants);
void Battle_InitPartyCombatantStats(struct BattleCombatant *combatant, const struct PartyCharacter *character, u8 formationBits);
void Battle_InitEnemyCombatantStats(u32 unused, struct BattleCombatant *combatant, u8 formationBits);
void Battle_InitBossCombatantStats(u32 unused, struct BattleCombatant *combatant);
void Battle_ScaleBossStats(u8 partyLevel, struct BattleCombatant *combatant, u16 hpPerLevel, u16 atcPerLevel, u16 defPerLevel, u16 aglPerLevel, u16 menPerLevel, u16 resPerLevel, u16 minimumLevel);
u8 Battle_UpdateQueuedEnemyDefeat(void);
u8 Battle_UpdateBossDefeat(void);
/** @brief Extended-enemy defeat callback selected by id minus 113.
 *  @note Seventeen ROM pointers cover ids 113..129; ids 123/124 share a callback.
 *  Entries receive the first queued combatant. The following word is data, not
 *  an eighteenth callback; no writable copy or extra RAM object is allocated.
 *  @address 0x0839CE7C
 */
typedef u8 (*BattleBossDefeatHandler)(struct BattleCombatant *combatant);
extern BattleBossDefeatHandler const gBattleBossDefeatHandlers[BATTLE_BOSS_DEFEAT_HANDLER_COUNT];
typedef char BattleBossDefeatHandlersSizeCheck[(sizeof(gBattleBossDefeatHandlers) == 0x44) ? 1 : -1];
/* Battle_ApplyActionDamageFromCallerTable @0801E30C 的 C 实现保留原入口
 * 未初始化的表指针，不声明普通 C 调用接口。 */

#endif
