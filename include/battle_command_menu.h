#ifndef GUARD_BATTLE_COMMAND_MENU_H
#define GUARD_BATTLE_COMMAND_MENU_H

#include "battle_types.h"

#define BATTLE_COMMAND_HISTORY_COUNT 10
#define BATTLE_MENU_SKILL_CHOICE_COUNT (BATTLE_SKILL_COUNT + 1)
#define BATTLE_MENU_SKILL_PAGE_ROWS 3
#define BATTLE_MENU_COMMAND_COUNT 10
#define BATTLE_MENU_ICON_COUNT 8
#define BATTLE_MENU_FORMATION_LINE_SIZE 3

/** @brief Graphics records used by the two command-icon rows.
 *  @note Descriptor graphics indices select one of these eight records.
 *  @address 0x08393A54
 */
enum BattleMenuIconId
{
    BATTLE_MENU_ICON_ATTACK = 0,
    BATTLE_MENU_ICON_SKILL = 1,
    BATTLE_MENU_ICON_ITEM = 2,
    BATTLE_MENU_ICON_DEFEND = 3,
    BATTLE_MENU_ICON_ESCAPE = 4,
    BATTLE_MENU_ICON_AUTO = 5,
    BATTLE_MENU_ICON_TACTICS = 6,
    BATTLE_MENU_ICON_MANUAL = 7,
};

/** @brief Command descriptor indices used by the input state machine.
 *  @note The live state is s8; -1 disables the command menu.
 *  @address 0x03000768
 */
enum BattleMenuState
{
    BATTLE_MENU_INACTIVE = -1,
    BATTLE_MENU_ROOT = 0,
    BATTLE_MENU_AUTO = 1,
    BATTLE_MENU_TACTICS = 2,
    BATTLE_MENU_MANUAL = 3,
    BATTLE_MENU_ESCAPE_ALL = 4,
    BATTLE_MENU_ATTACK = 5,
    BATTLE_MENU_SKILL = 6,
    BATTLE_MENU_ITEM = 7,
    BATTLE_MENU_DEFEND = 8,
    BATTLE_MENU_ESCAPE = 9,
};

/** @brief Move a command/list cursor or save its current selection.
 *  @note Movement returns zero for a page change or a rejected boundary move.
 *  @address 0x080212B4
 */
enum BattleMenuSelectionOperation
{
    BATTLE_MENU_SELECTION_NEXT = 0,
    BATTLE_MENU_SELECTION_PREVIOUS = 1,
    BATTLE_MENU_SELECTION_SAVE = 2,
};

/** @brief Deferred command-hint request values.
 *  @note A skip request is consumed without changing the active hint flag.
 *  @address 0x03000816
 */
enum BattleMenuHintRequest
{
    BATTLE_MENU_HINT_UPDATE = 0,
    BATTLE_MENU_HINT_ENABLE = 1,
    BATTLE_MENU_HINT_SKIP = 2,
};

/** @brief Restore a former defend command to its idle animation.
 *  @note One party member is processed at a time by the byte-sized phase.
 *  @address 0x03000813
 */
enum BattleMenuCommandResetPhase
{
    BATTLE_MENU_RESET_START = 0,
    BATTLE_MENU_RESET_WAIT_ANIMATION = 1,
};

/** @brief Direction encoded by the battle menu's repeated input events.
 *  @note Passed as u8; party targets only move on the horizontal axis.
 *  @address 0x08022710
 */
enum BattleMenuDirection
{
    BATTLE_MENU_UP = 0,
    BATTLE_MENU_DOWN = 1,
    BATTLE_MENU_LEFT = 2,
    BATTLE_MENU_RIGHT = 3,
};

/** @brief Phases used while choosing a normal-attack target.
 *  @note Stored in the shared signed-byte menu phase, not an enum-sized object.
 *  @address 0x03000769
 */
enum BattleMenuTargetPhase
{
    BATTLE_MENU_TARGET_INIT = 0,
    BATTLE_MENU_TARGET_SELECT = 6,
    BATTLE_MENU_TARGET_CONFIRMED = 30,
};

/** @brief Skill-list, window-transition and target-selection phases.
 *  @note Shared with the menu's byte-sized phase; unused phase values do nothing.
 *  @address 0x03000769
 */
enum BattleMenuSkillPhase
{
    BATTLE_MENU_SKILL_INIT = 0,
    BATTLE_MENU_SKILL_OPEN = 7,
    BATTLE_MENU_SKILL_WAIT_OPEN = 8,
    BATTLE_MENU_SKILL_DRAW = 9,
    BATTLE_MENU_SKILL_WAIT_UPLOAD = 10,
    BATTLE_MENU_SKILL_SELECT = 11,
    BATTLE_MENU_SKILL_WAIT_CURSOR = 12,
    BATTLE_MENU_SKILL_CLOSE_FOR_TARGET = 13,
    BATTLE_MENU_SKILL_WAIT_TARGET = 14,
    BATTLE_MENU_SKILL_SELECT_TARGET = 15,
    BATTLE_MENU_SKILL_CANCEL_CLOSE = 16,
    BATTLE_MENU_SKILL_CANCEL_WAIT = 17,
    BATTLE_MENU_SKILL_CANCEL_FINISH = 18,
    BATTLE_MENU_SKILL_COMMIT = 30,
};

/** @brief Item-list, cancellation and target-selection phases.
 *  @note The phase byte is shared with skill and attack selection; these values
 *  describe the item path only and do not allocate enum-sized RAM storage.
 *  @address 0x03000769
 */
enum BattleMenuItemPhase
{
    BATTLE_MENU_ITEM_INIT = 0,
    BATTLE_MENU_ITEM_OPEN = 19,
    BATTLE_MENU_ITEM_WAIT_OPEN = 20,
    BATTLE_MENU_ITEM_DRAW = 21,
    BATTLE_MENU_ITEM_SELECT = 22,
    BATTLE_MENU_ITEM_WAIT_CURSOR = 23,
    BATTLE_MENU_ITEM_CANCEL_CLOSE = 24,
    BATTLE_MENU_ITEM_CANCEL_WAIT = 25,
    BATTLE_MENU_ITEM_CANCEL_FINISH = 26,
    BATTLE_MENU_ITEM_CLOSE_FOR_TARGET = 27,
    BATTLE_MENU_ITEM_WAIT_TARGET = 28,
    BATTLE_MENU_ITEM_SELECT_TARGET = 29,
    BATTLE_MENU_ITEM_FINISH = 30,
};

/** @brief Command input flags and encoded party movement states.
 *  @note Values 0x10..0x50 share the transition nibble and are mutually exclusive.
 *  @address 0x0300076C
 */
enum BattleCommandMenuFlags
{
    BATTLE_MENU_RETURN_TO_PARENT = 0x0001,
    BATTLE_MENU_REFRESH_SELECTION = 0x0002,
    BATTLE_MENU_COMMANDS_READY = 0x0004,
    BATTLE_MENU_PARTY_COMMAND_ACTIVE = 0x0008,
    BATTLE_MENU_MOVE_TO_PREVIOUS_PARTY = 0x0010,
    BATTLE_MENU_MOVE_TO_NEXT_PARTY = 0x0020,
    BATTLE_MENU_MOVE_TO_ROOT = 0x0030,
    BATTLE_MENU_RETURN_LAST_PARTY = 0x0040,
    BATTLE_MENU_MOVE_FROM_ROOT = 0x0050,
    BATTLE_MENU_PARTY_TRANSITION_MASK = 0x00F0,
    BATTLE_MENU_LOCK_COMMAND_INPUT = 0x0100,
    BATTLE_MENU_CANCEL_COMMAND = 0x0200,
    BATTLE_MENU_REFRESH_PARENT_ICONS = 0x0400,
    BATTLE_MENU_SELECTING_TARGETS = 0x0800,
};

/** @brief List-arrow visibility and scrolling directions.
 *  @note Zero preserves the current tiles and timer; nonzero flags advance the timer.
 *  @address 0x08025650
 */
enum BattleMenuScrollFlags
{
    BATTLE_MENU_SCROLL_VISIBLE = 0x01,
    BATTLE_MENU_SCROLL_UP = 0x10,
    BATTLE_MENU_SCROLL_DOWN = 0x20,
};

/** @brief Remembered command, skill and item selection for one party character.
 *  @note Five bytes; restore clamps saved list indices to the current list size.
 *  @address 0x03000788
 */
struct BattleCommandHistory
{
    s8 command;
    u8 skillScroll;
    s8 skillIndex;
    u8 itemScroll;
    s8 itemIndex;
} __attribute__((packed));

typedef char BattleCommandHistorySizeCheck[(sizeof(struct BattleCommandHistory) == 5) ? 1 : -1];

/** @brief One command icon's source tile and OBJ dimensions.
 *  @note Four bytes; sourceTile is an offset in the shared battle graphics.
 *  @address 0x08393A54
 */
struct BattleCommandIcon
{
    u8 sourceTile;
    u8 shape;
    u8 size;
    u8 reserved03;
};

/** @brief Navigation and graphics data for one command-menu state.
 *  @note Five option slots; negative options are disabled sentinels.
 *  @address 0x08393A74
 */
struct BattleCommandMenuDescriptor
{
    s8 count;
    s8 parent;
    u8 x;
    s8 options[5];
    u8 graphics[5];
    u8 reserved0D[3];
};

/** @brief One command-menu descriptor in the ten-entry ROM table.
 *  @note Negative count/parent/option values are sentinels. The word view keeps
 *  the original aligned read of options 1..4 when testing for an empty menu.
 *  @address 0x08393A74
 */
union BattleCommandMenu
{
    struct BattleCommandMenuDescriptor fields;
    s32 words[4];
};

typedef char BattleCommandIconSizeCheck[(sizeof(struct BattleCommandIcon) == 4) ? 1 : -1];
typedef char BattleCommandMenuDescriptorSizeCheck[(sizeof(struct BattleCommandMenuDescriptor) == 0x10) ? 1 : -1];
typedef char BattleCommandMenuSizeCheck[(sizeof(union BattleCommandMenu) == 0x10) ? 1 : -1];
typedef char BattleCommandMenuOptionsCheck[(offsetof(union BattleCommandMenu, fields.options) == 3) ? 1 : -1];
typedef char BattleCommandMenuGraphicsCheck[(offsetof(union BattleCommandMenu, fields.graphics) == 8) ? 1 : -1];
/* .rodata: definitions in battle_command_menu.c follow ROM address order. */
/** @brief Tile offsets and OBJ dimensions of the eight command icons.
 *  @note The command-menu owner declares the shared source records.
 *  @address 0x08393A54
 */
extern const struct BattleCommandIcon gBattleMenuCommandIcons[BATTLE_MENU_ICON_COUNT];
/** @brief Navigation and icon choices for the ten command states.
 *  @note Each record is 16 bytes and also supports aligned word reads.
 *  @address 0x08393A74
 */
extern const union BattleCommandMenu gBattleMenuDescriptors[BATTLE_MENU_COMMAND_COUNT];
/** @brief Short command labels indexed with a five-byte stride.
 *  @note Only command ids 1..9 are drawn; the base is retained for the original indexing.
 *  @address 0x0839DF1E
 */
/* Nine five-byte records, indexed by command number minus one. */
extern const u8 gBattleMenuCommandHints[9][5];
/** @brief Two animation frames for the list scroll arrows.
 *  @note Two 4bpp tiles; the upward arrow uses a vertically flipped tilemap entry.
 *  @address 0x0861A7A4
 */
extern const u8 gBattleMenuScrollArrowTiles[0x40];
/** @brief The two OBJ palettes used by command icons.
 *  @note Loaded into banks 13 and 14 when command input begins.
 *  @address 0x0861C624
 */
extern const u16 gBattleMenuCommandPalettes[32];

union SpriteOamEntry;
u8 BattleMenu_MoveSelection(s8 state, const union BattleCommandMenu *menu,
    const struct BattleCombatant *combatant, u8 operation);
void BattleMenu_UpdateCommandHint(u8 option);
void BattleMenu_DrawCommandHint(u16 *tilemap, u8 x, u8 y, u8 option);
void BattleMenu_BuildFormationLine(const struct BattleCombatant *combatants, u8 formation, u8 side,
    s8 slots[BATTLE_MENU_FORMATION_LINE_SIZE]);
void BattleMenu_UpdatePartyCommandPhase(struct BattleCombatant **party);
void BattleMenu_UpdatePartyTransition(struct BattleCombatant **party, u16 input);
void BattleMenu_DrawTargetName(u16 *tilemap, const struct BattleCombatant *combatant, u8 x, u8 y);
u8 BattleMenu_UpdateAttackTarget(struct BattleCombatant *combatant, struct BattleCombatant *combatants,
    struct BattleCombatant *cursor, u16 input);
u8 BattleMenu_UpdateSkillSelection(struct BattleCombatant *combatant, struct BattleCombatant *combatants,
    struct BattleCombatant *cursor, u16 input);
void BattleMenu_DrawSkillName(u16 *tilemap, const struct BattleCombatant *combatant, u8 row, u16 selection);
void BattleMenu_DrawSkillMpCost(u16 *tilemap, const struct BattleCombatant *combatant, u8 row, u16 selection);
void BattleMenu_DrawSkillDescription(u16 *tilemap, const struct BattleCombatant *combatant, u8 selection);
void BattleMenu_DrawScrollArrows(u16 *tilemap, u8 flags);
void BattleMenu_UpdateSkillListPalette(u16 *tilemap);
void BattleMenu_UpdateItemListPalette(u16 *tilemap);
u8 BattleMenu_UpdateItemSelection(struct BattleCombatant *combatant, struct BattleCombatant *combatants,
    struct BattleCombatant *cursor, u16 input);
void BattleMenu_ResetDirectionalSelection(void);
u16 BattleMenu_DrawCommandIcon(union SpriteOamEntry *oam, u8 x, u8 y, u16 tile,
    u8 shape, u8 size, u8 paletteOffset);

u8 BattleMenu_BeginInput(u8 mode, struct BattleCombatant *cursor);
u8 BattleMenu_UpdateCommandReset(void);
u8 BattleMenu_UpdateInput(struct BattleCombatant *combatants, struct BattleCombatant *cursor, u8 *oamCursor);
u8 BattleMenu_UploadCommandIcons(u8 oamCursor);
void BattleMenu_ResetHistory(void);
void BattleMenu_RestoreSelection(s8 mode, const struct BattleCombatant *combatant);
u8 BattleMenu_FindDirectionalTarget(const struct BattleCombatant *current,
    const struct BattleCombatant *combatants, u8 direction);
void BattleMenu_ResetTargetMemory(void);

/** @brief Set the deferred command-hint request byte.
 *  @note One enables the hint and two skips the current update; processing clears the request.
 *  @address 0x0802550C
 */
void BattleMenu_SetHintRequest(u8 request);

#endif
