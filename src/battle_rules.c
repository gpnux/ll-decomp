#include <stdlib.h>

#include "include_asm.h"
#include "battle_rules.h"
#include "battle_action_effects.h"
#include "battle.h"
#include "battle_items.h"
#include "battle_palette.h"
#include "party.h"
#include "sound.h"
#include "battle_command_menu.h"
#include "core.h"
#include "gba.h"
#include "menu.h"
#include "script.h"

/* .lcomm: private objects in original address order. */
/** @brief Inventory snapshot used while restoring a saved tactics command.
 *  @note Filled by BattleItem_BuildMenuList; reservation reads four-byte rows.
 *  @address 0x030008A8
 */
static struct BattleItemStack sBattleTacticsItems[BATTLE_TACTICS_ITEM_COUNT];
/** @brief Number of available tactics-item rows.
 *  @note Written by the list builder before the saved item id is searched.
 *  @address 0x030008E8
 */
static u8 sBattleTacticsItemCount;
/** @brief Combatant base borrowed by the slot quicksort.
 *  @note The sorter compares slots through this pointer; it owns no combatant records.
 *  @address 0x030008EC
 */
static struct BattleCombatant *sBattleSortCombatants;
/** @brief Record field selected by the slot quicksort.
 *  @note Selectors 13 and 14 compare formation column and row.
 *  @address 0x030008F0
 */
static u8 sBattleSortKey;
/** @brief Shared party/enemy stat-boost presentation phase.
 *  @note Reset by the stat-boost initializer before animation updates.
 *  @address 0x030008F1
 */
static u8 sBattleStatBoostPhase;
/** @brief ATC/DEF/AGL/MEN/RES modifier selected for a boost.
 *  @note Stored as a byte; enum BattleModifierStat provides the selector values.
 *  @address 0x030008F2
 */
static u8 sBattleStatBoostStat;
/** @brief Percentage of the base stat added by the boost.
 *  @note The arithmetic helper converts this byte to a percentage.
 *  @address 0x030008F3
 */
static u8 sBattleStatBoostPercent;
/** @brief Party slots whose draw priority is adjusted for the boost.
 *  @note The party animation path sorts these slots, then restores priority three.
 *  @address 0x030008F4
 */
static u8 sBattleStatBoostPartySlots[BATTLE_PARTY_COUNT];
/** @brief Number of party slots in the stat-boost priority list.
 *  @note Populated using the all-status mask; empty records are excluded.
 *  @address 0x030008F9
 */
static u8 sBattleStatBoostPartyCount;
/** @brief Enemy slots collected at stat-boost setup.
 *  @note This is a distinct seven-byte list, not a continuation of the party list.
 *  @address 0x030008FC
 */
static u8 sBattleStatBoostEnemySlots[BATTLE_ENEMY_COUNT];
/** @brief Number of enemy slots collected at stat-boost setup.
 *  @note The current setup stores this count even when later stages do not use it.
 *  @address 0x03000903
 */
static u8 sBattleStatBoostEnemyCount;
/** @brief Delay counter shared by the stat-boost animation paths.
 *  @note The party path waits through frame seven before changing phases.
 *  @address 0x03000904
 */
static u8 sBattleStatBoostFrame;
/** @brief Animation id for the stat-boost effect.
 *  @note The paired cursor effect uses animationId + 1.
 *  @address 0x03000906
 */
static u16 sBattleStatBoostAnimationId;
/** @brief Last damage calculated from a boss action selector.
 *  @note The result includes the selector-specific additive attack bonus.
 *  @address 0x03000908
 */
static u16 sBattleLastBossAttackDamage;
/** @brief Low byte of the most recently updated ATC modifier.
 *  @note Only the ATC branch writes this byte; no reader is identified.
 *  @address 0x0300090A
 */
static u8 sBattleLastAtcBoostLowByte;

/** @brief Fixed EWRAM view of the shared battle window tilemap.
 *  @note Aliases gBattleWindowTilemap at its original address; allocates no storage.
 */
#define BATTLE_RESULT_TILEMAP ((u16 *)0x02035AC0)

/* Keep result and turn-end state in its original NOBITS block at 03000910.
 * The named section preserves the five-byte gap after the rules workspace.
 * agbcc needs explicit zero initializers to honor section attributes; the
 * alignments reproduce the former GAS .lcomm allocation, without ROM data.
 * Existing reserved arrays preserve unidentified gaps, not inferred objects. */
#define BATTLE_RESULT_BSS(alignment) \
    __attribute__((section(".bss.battle_rules.result"), aligned(alignment)))
/** @brief Result-screen subphase.
 *  @note Reused by victory, defeat and the introductory banner.
 *  @address 0x03000910
 */
static u8 sBattleResultPhase BATTLE_RESULT_BSS(1) = 0;
/** @brief Result delay and continuation-arrow frame.
 *  @note The arrow advances modulo sixteen; other presentation phases reuse the byte.
 *  @address 0x03000911
 */
static u8 sBattleResultFrame BATTLE_RESULT_BSS(1) = 0;
/** @brief Standalone result-banner animation.
 *  @note Setup, block loading and drawing use the shared 0x30-byte animation layout.
 *  @address 0x03000918
 */
static struct BattleAnimation sBattleResultBanner BATTLE_RESULT_BSS(8) = {0};
/** @brief Presentation mode for the result layer.
 *  @note Zero selects the banner, one the victory path, two the defeat path.
 *  @address 0x03000948
 */
static u8 sBattleResultMode BATTLE_RESULT_BSS(1) = 0;
/** @brief Party slot whose growth is being presented.
 *  @note Used for level, stat and name queries, and the palette highlight.
 *  @address 0x03000949
 */
static u8 sBattleResultPartySlot BATTLE_RESULT_BSS(1) = 0;
/** @brief Halfword index into the current result message.
 *  @note A substitution token remains selected until its inserted text finishes.
 *  @address 0x0300094A
 */
static u8 sBattleResultTextIndex BATTLE_RESULT_BSS(1) = 0;
/** @brief Next output column within the result message.
 *  @note One column draws a pair of vertically adjacent font tiles.
 *  @address 0x0300094B
 */
static u8 sBattleResultTextColumn BATTLE_RESULT_BSS(1) = 0;
/** @brief Progress through an inserted decimal number.
 *  @note Reset together with the text indices when a message is selected.
 *  @address 0x0300094C
 */
static u8 sBattleResultNumberDigit BATTLE_RESULT_BSS(1) = 0;
/** @brief Character index for an inserted party, item or card name.
 *  @note Several insertion handlers reuse this byte; it is not a separate name buffer.
 *  @address 0x0300094D
 */
static u8 sBattleResultInsertIndex BATTLE_RESULT_BSS(1) = 0;
/** @brief Selected halfword-encoded result-message stream.
 *  @note Normally points into ROM; original initialization also writes through its old value.
 *  @address 0x03000950
 */
static const u16 *sBattleResultMessage BATTLE_RESULT_BSS(4) = 0;
/** @brief Phase resumed after the displayed message is acknowledged.
 *  @note The main result state machine saves this before waiting for input.
 *  @address 0x03000954
 */
static u8 sBattleResultNextPhase BATTLE_RESULT_BSS(1) = 0;
/** @brief Numeric substitution for experience, level, stat or gold text.
 *  @note The same halfword is reused by each result-message category.
 *  @address 0x03000956
 */
static u16 sBattleResultNumber BATTLE_RESULT_BSS(2) = 0;
/** @brief Party-slot bitmask reporting level increases.
 *  @note Returned by experience application and tested using the current party slot.
 *  @address 0x03000958
 */
static u8 sBattleResultLevelUpMask BATTLE_RESULT_BSS(1) = 0;
/** @brief Difference between the new and old displayed stat.
 *  @note The presentation tests the signed halfword and shows positive gains.
 *  @address 0x0300095A
 */
static u16 sBattleResultStatGain BATTLE_RESULT_BSS(2) = 0;
/** @brief Skills learned during the current member's level increase.
 *  @note Filled by the rules-layer learned-skill query.
 *  @address 0x03000960
 */
static u8 sBattleResultLearnedSkills[BATTLE_SKILL_COUNT] BATTLE_RESULT_BSS(8) = {0};
/** @brief Signed result of the learned-skill query.
 *  @note The result dispatcher accepts counts one through seven.
 *  @address 0x03000968
 */
static s8 sBattleResultLearnedSkillCount BATTLE_RESULT_BSS(1) = 0;
/** @brief Next learned skill whose name is shown.
 *  @note Indexes the eight-byte learned-skill workspace.
 *  @address 0x03000969
 */
static u8 sBattleResultLearnedSkillIndex BATTLE_RESULT_BSS(1) = 0;
/** @brief Current position in an inserted skill-name stream.
 *  @note The glyph renderer advances this pointer by one halfword.
 *  @address 0x0300096C
 */
static const u16 *sBattleResultSkillName BATTLE_RESULT_BSS(4) = 0;
/** @brief Combatant base borrowed by victory presentation.
 *  @note Obtained from Battle_GetCombatants; the result module does not allocate records.
 *  @address 0x03000970
 */
static struct BattleCombatant *sBattleResultCombatants BATTLE_RESULT_BSS(4) = 0;
/** @brief Eligible party slots for victory animation.
 *  @note Filled using status mask 0x7F.
 *  @address 0x03000974
 */
static u8 sBattleResultPartySlots[BATTLE_PARTY_COUNT] BATTLE_RESULT_BSS(4) = {0};
/** @brief Number of eligible victory-animation slots.
 *  @note Bounds the separate iteration cursor.
 *  @address 0x03000979
 */
static u8 sBattleResultPartyCount BATTLE_RESULT_BSS(1) = 0;
/** @brief Current index in the victory-animation slot list.
 *  @note Each selected animation must finish loading before the cursor advances.
 *  @address 0x0300097A
 */
static u8 sBattleResultPartyIndex BATTLE_RESULT_BSS(1) = 0;
/** @brief Level-up palette-highlight phase.
 *  @note The updater starts a tint, waits four frames, then returns over sixteen.
 *  @address 0x0300097B
 */
static u8 sBattleResultHighlightPhase BATTLE_RESULT_BSS(1) = 0;
/** @brief Frame counter within the level-up highlight.
 *  @note Reset on both the outward and return transitions.
 *  @address 0x0300097C
 */
static u8 sBattleResultHighlightFrame BATTLE_RESULT_BSS(1) = 0;
/** @brief OBJ bank used for the current member's highlight.
 *  @note Read from BattleCombatant.animation.paletteSlot.
 *  @address 0x0300097D
 */
static u8 sBattleResultHighlightPalette BATTLE_RESULT_BSS(1) = 0;
/** @brief Current selection in the defeat retry menu.
 *  @note Reset when the defeat presentation starts.
 *  @address 0x0300097E
 */
static u8 sBattleDefeatSelection BATTLE_RESULT_BSS(1) = 0;
/** @brief Turn-end recovery, damage and status-processing phase.
 *  @note Independent of the result-screen phase at 0x03000910.
 *  @address 0x0300097F
 */
static u8 sBattleTurnEndPhase BATTLE_RESULT_BSS(1) = 0;
/** @brief Preserved gap before the turn-end target workspace.
 *  @note No access or original object boundary is identified; this only preserves layout.
 *  @address 0x03000980
 */
static u8 sBattleResultReserved980[8] BATTLE_RESULT_BSS(8) = {0};
/** @brief Slots selected for the current turn-end effect.
 *  @note The filter helper rebuilds this list for each effect kind.
 *  @address 0x03000988
 */
static u8 sBattleTurnEndTargets[BATTLE_COMBATANT_COUNT] BATTLE_RESULT_BSS(8) = {0};
/** @brief Length of the current turn-end target list.
 *  @note Rebuilt for individual effect kinds.
 *  @address 0x03000994
 */
static u8 sBattleTurnEndTargetCount BATTLE_RESULT_BSS(1) = 0;
/** @brief Cursor through turn-end targets.
 *  @note The gauge-increment branch processes one slot at a time.
 *  @address 0x03000995
 */
static u8 sBattleTurnEndTargetIndex BATTLE_RESULT_BSS(1) = 0;
/** @brief Pending turn-end effect kinds.
 *  @note Uses enum BattleTurnEndEffect; completed branches clear one bit.
 *  @address 0x03000996
 */
static u16 sBattleTurnEndEffectMask BATTLE_RESULT_BSS(2) = 0;
/** @brief Preserved gap before the turn-end defeat counts.
 *  @note No identified access; the byte count is a layout requirement only.
 *  @address 0x03000998
 */
static u8 sBattleResultReserved998[2] BATTLE_RESULT_BSS(2) = {0};
/** @brief Party slots reduced to zero HP by turn-end damage.
 *  @note The damage branch records the full remaining HP as pending damage.
 *  @address 0x0300099A
 */
static u8 sBattleTurnEndFaintedPartyCount BATTLE_RESULT_BSS(1) = 0;
/** @brief Enemy slots reduced to zero HP by turn-end damage.
 *  @note Processed after the party defeat queue completes.
 *  @address 0x0300099B
 */
static u8 sBattleTurnEndDefeatedEnemyCount BATTLE_RESULT_BSS(1) = 0;
/** @brief Party defeat queue input for turn-end HP damage.
 *  @note Contains combatant slot numbers, not character ids.
 *  @address 0x0300099C
 */
static u8 sBattleTurnEndFaintedPartySlots[BATTLE_PARTY_COUNT] BATTLE_RESULT_BSS(4) = {0};
/** @brief Enemy defeat queue input for turn-end HP damage.
 *  @note Seven slots cover the enemy side of the combatant array.
 *  @address 0x030009A4
 */
static u8 sBattleTurnEndDefeatedEnemySlots[BATTLE_ENEMY_COUNT] BATTLE_RESULT_BSS(4) = {0};
/** @brief Slots selected for status recovery at turn end.
 *  @note Built from status mask 0x2C and per-side recovery checks.
 *  @address 0x030009B0
 */
static u8 sBattleStatusRecoverySlots[BATTLE_COMBATANT_COUNT] BATTLE_RESULT_BSS(8) = {0};
/** @brief Number of pending status-recovery slots.
 *  @note The single-party-target path preserves the original append behavior.
 *  @address 0x030009BC
 */
static u8 sBattleStatusRecoveryCount BATTLE_RESULT_BSS(1) = 0;
/** @brief Cursor through pending status recoveries.
 *  @note Advanced only after the current combatant's recovery presentation completes.
 *  @address 0x030009BD
 */
static u8 sBattleStatusRecoveryIndex BATTLE_RESULT_BSS(1) = 0;
/** @brief Number of item-drop rows returned by battle_items.
 *  @note Rows remain owned by battle_items.c.
 *  @address 0x030009BE
 */
static u8 sBattleResultItemDropCount BATTLE_RESULT_BSS(1) = 0;
/** @brief Current item-drop row, initially -1.
 *  @note The result dispatcher increments it before showing a row.
 *  @address 0x030009BF
 */
static s8 sBattleResultItemDropIndex BATTLE_RESULT_BSS(1) = 0;
/** @brief Borrowed pointer to item-drop rows.
 *  @note Assigned by BattleItem_AwardItemDrops through its output parameter.
 *  @address 0x030009C0
 */
static struct BattleItemStack *sBattleResultItemDrops BATTLE_RESULT_BSS(4) = 0;
/** @brief Number of card-drop rows returned by battle_items.
 *  @note Card rows use display indices, not collection ids.
 *  @address 0x030009C4
 */
static u8 sBattleResultCardDropCount BATTLE_RESULT_BSS(1) = 0;
/** @brief Current card-drop row, initially -1.
 *  @note Signed indexing is preserved by the name and glyph-cache helpers.
 *  @address 0x030009C5
 */
static s8 sBattleResultCardDropIndex BATTLE_RESULT_BSS(1) = 0;
/** @brief Borrowed pointer to card-drop rows.
 *  @note Assigned by BattleItem_AwardCardDrops through its output parameter.
 *  @address 0x030009C8
 */
static struct BattleCardDrop *sBattleResultCardDrops BATTLE_RESULT_BSS(4) = 0;
/** @brief Last glyph-cache tile index emitted in a result message.
 *  @note Stored after adding the cache's 0xE0 tile offset.
 *  @address 0x030009CC
 */
static u16 sBattleResultCachedGlyph BATTLE_RESULT_BSS(2) = 0;

#undef BATTLE_RESULT_BSS

/** @brief Battle rules, turn-end effects, victory/defeat and reward presentation.
 *  @note All 103 ROM entries compile from C in original address order. Rules
 *  and result workspaces retain their original private RAM blocks; caller-owned
 *  buffers and other modules' COMMON objects are borrowed. Inline helpers add
 *  no ROM entries. ROM tables and public interfaces are in battle_rules.h.
 *  @address 0x0804473C..0x0804AE2C
 */

/** @brief Dispatch the selected action to physical, magic or item damage.
 *  @note Skills 17..20 return zero; the selected calculator supplies a halfword result. Other party skill ids dispatch magic damage, while enemy actors dispatch physical damage.
 *  @address 0x0804473C
 */
u32 Battle_CalculateActionDamage(struct BattleCombatant *attacker, struct BattleCombatant *target)
{
    u16 damage;

    if (attacker->id <= 10)
    {
        switch (attacker->action)
        {
        case BATTLE_ACTION_ATTACK:
            damage = Battle_CalculatePhysicalDamage(attacker, target);
            break;
        case BATTLE_ACTION_SKILL:
            switch (Battle_GetSelectedSkill(attacker))
            {
            case 17:
            case 18:
            case 19:
            case 20:
                damage = 0;
                break;
            case 0:
            case 1:
            case 2:
            case 38:
            case 39:
            case 40:
            case 42:
            case 48:
            case 53:
            case 55:
            case 58:
            case 59:
                damage = Battle_CalculatePhysicalDamage(attacker, target);
                break;
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
            case 10:
            case 11:
            case 12:
            case 13:
            case 14:
            case 15:
            case 16:
            case 21:
            case 22:
            case 23:
            case 24:
            case 25:
            case 26:
            case 27:
            case 28:
            case 29:
            case 30:
            case 31:
            case 32:
            case 33:
            case 34:
            case 35:
            case 36:
            case 37:
            case 41:
            case 43:
            case 44:
            case 45:
            case 46:
            case 47:
            case 49:
            case 50:
            case 51:
            case 52:
            case 54:
            case 56:
            case 57:
            default:
                damage = Battle_CalculateMagicDamage(attacker, target);
                break;
            }
            break;
        case BATTLE_ACTION_ITEM:
            damage = Battle_CalculateItemDamage(attacker, target);
            break;
        }
    }
    else
        damage = Battle_CalculatePhysicalDamage(attacker, target);
    return damage;
}

/** @brief Calculate ordinary attack damage plus the boss action's fixed bonus.
 *  @note Stores the returned halfword; the attack helper may also consume a target
 *  protection flag and clear its pending damage.
 *  @address 0x080448A8
 */
u16 Battle_CalculateBossAttackDamage(struct BattleCombatant *attacker, struct BattleCombatant *target)
{
    u32 handler;
    u16 damage = 0;
    const struct BattleBossInfo *boss = attacker->source.boss;

    handler = boss->attackHandlerId;
    switch (attacker->action)
    {
    case BATTLE_ACTION_ATTACK:
        handler = boss->attackHandlerId;
        break;
    case BATTLE_ACTION_SKILL:
        handler = boss->skillHandlerIds[attacker->enemySkillIndex];
        break;
    }
    switch (handler)
    {
    case 0: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 1: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 2: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 3: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 4: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 5: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 6: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 7: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 8: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 9: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 10: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 11: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 12: damage = Battle_CalculatePhysicalDamage(attacker, target) + 15; break;
    case 13: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 14: damage = Battle_CalculatePhysicalDamage(attacker, target) + 30; break;
    case 15: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 16: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 17: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 18: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 19: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 20: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 21: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 22: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 23: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 24: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 25: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 26: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 27: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 28: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 29: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 30: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 31: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 32: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 33: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 34: damage = Battle_CalculatePhysicalDamage(attacker, target) + 15; break;
    case 35: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 36: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 37: damage = Battle_CalculatePhysicalDamage(attacker, target) + 30; break;
    case 38: damage = Battle_CalculatePhysicalDamage(attacker, target) + 15; break;
    case 39: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 40: damage = Battle_CalculatePhysicalDamage(attacker, target) + 30; break;
    case 41: damage = Battle_CalculatePhysicalDamage(attacker, target) + 15; break;
    case 42: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 43: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 44: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 45: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 46: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 47: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 48: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 49: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 50: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 51: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 52: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 53: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    case 54: damage = Battle_CalculatePhysicalDamage(attacker, target); break;
    }
    sBattleLastBossAttackDamage = damage;
    return damage;
}

/** @brief Calculate ATC-versus-DEF damage, clamp to 0..999 and queue additional status.
 *  @note Protection clears pending damage and returns zero. Signed-halfword steps
 *  and unsigned double scaling are preserved: NOA 0..2 applies four skill factors,
 *  while 255 wraps to multiplier one. Only the listed skills initialize the factor.
 *  Reusing the adjustment halfword preserves registers; all 0x50C linked bytes match.
 *  @address 0x08044A40
 */
u16 Battle_CalculatePhysicalDamage(struct BattleCombatant *attacker, struct BattleCombatant *target)
{
    const u8 *stats;
    u32 random;
    u16 defense;
    u32 scaledDamage;
    u16 divisor;
    u16 randomBonus;
    u16 adjustment;
    s16 damage;
    double multiplier;
    u8 count;

    if (Battle_MatchesEnemyActionCategory(attacker, BATTLE_ENEMY_ACTION_MAGICAL)
        && (target->displayState & BATTLE_DISPLAY_MAGIC_GUARD))
    {
        target->displayState &= ~BATTLE_DISPLAY_MAGIC_GUARD;
        target->pendingDamage = 0;
        return 0;
    }
    if (target->displayState & BATTLE_DISPLAY_DAMAGE_PROTECTED)
    {
        target->pendingDamage = 0;
        return 0;
    }
    stats = (const u8 *)attacker + offsetof(struct BattleCombatant, hp);
    random = Battle_NextRandom();
    divisor = Battle_GetEffectiveCombatantStat(attacker, BATTLE_STAT_ATC);
    divisor /= 10u;
    randomBonus = random % (divisor + 2);
    adjustment = Battle_GetEffectiveCombatantStat(attacker, BATTLE_STAT_ATC);
    adjustment += Battle_GetActionPowerBonus(attacker, Battle_GetSelectedSkill(attacker), adjustment);
    adjustment += randomBonus;
    defense = Battle_GetEffectiveCombatantStat(target, BATTLE_STAT_DEF);
    damage = adjustment - defense;
    if (attacker->level > target->level)
        damage += (attacker->level - target->level) * 5;
    damage /= 2;
    if (BattleItem_FindEquipmentByProperty(target, BATTLE_ITEM_PROPERTY_BYTE1_LOW4, 15) >= 0)
    {
        if (Battle_MatchesEnemyActionCategory(attacker, BATTLE_ENEMY_ACTION_MAGICAL) == 1)
            damage /= 2;
    }
    else if (BattleItem_FindEquipmentByProperty(target, BATTLE_ITEM_PROPERTY_BYTE1_LOW4, 2) >= 0)
    {
        if (Battle_MatchesEnemyActionCategory(attacker, BATTLE_ENEMY_ACTION_MAGICAL) == 1)
            damage -= damage / 4;
    }
    switch (Battle_GetElementResponse(target, Battle_GetActionElementMask(attacker)))
    {
    case BATTLE_ELEMENT_RESPONSE_NEUTRAL: break;
    case BATTLE_ELEMENT_RESPONSE_WEAK: damage *= 2; break;
    case BATTLE_ELEMENT_RESPONSE_RESISTANT: damage /= 2; break;
    }
    scaledDamage = damage;
    if (attacker->id < BATTLE_CHARACTER_ID_COUNT && attacker->action == BATTLE_ACTION_SKILL)
    {
        switch (Battle_GetSelectedSkill(attacker))
        {
        case 0: multiplier = 1.5; break;
        case 1: multiplier = 1.3; break;
        case 2: multiplier = 1.3; break;
        case 38: multiplier = 1.3; break;
        case 39: multiplier = 1.2; break;
        case 40: multiplier = 1.2; break;
        case 42: multiplier = 1.1; break;
        case 48: multiplier = 1.4; break;
        case 53: multiplier = 1.4; break;
        case 55: multiplier = 1.7; break;
        case 58: multiplier = 1.7; break;
        case 59: multiplier = 1.5; break;
        }
        count = stats[offsetof(struct BattleCombatant, noa) - offsetof(struct BattleCombatant, hp)];
        if (count <= 2)
            count = 3;
        else
            count++;
        if (count == 0)
            multiplier = 1.0;
        else if (count != 1)
        {
            double factor = multiplier;
            while (count != 0)
            {
                multiplier *= factor;
                count--;
            }
        }
        scaledDamage *= multiplier;
        damage = scaledDamage;
    }
    else
    {
        adjustment = damage / 10;
        switch (Battle_CountBlockingFormationColumns(attacker, target))
        {
        case 0: break;
        case 1: damage -= adjustment * 2; break;
        case 2: damage -= adjustment * 4; break;
        default: damage -= adjustment * 4; break;
        }
    }
    if (target->action == BATTLE_ACTION_DEFEND)
        damage /= 2;
    if ((u8)Battle_CheckStatusMask(target, 0x1C) == BATTLE_STATUS_MATCH_ACCEPTED)
        damage *= 2;
    if (!(Battle_GetControlFlags() & BATTLE_CONTROL_SPECIAL_ENCOUNTER)
        && attacker->id < BATTLE_CHARACTER_ID_COUNT)
    {
        adjustment = damage / 10;
        if (adjustment == 0)
            adjustment = 1;
        adjustment *= 5;
        damage += adjustment;
    }
    if (damage < 0)
        damage = 0;
    if (damage > 999)
        damage = 999;
    Battle_QueueActionStatusEffect(attacker, target);
    return damage;
}

/** @brief Calculate MEN-versus-RES skill damage.
 *  @note Consumes magic guard or damage protection before calculation. Random power, elemental response and equipment adjustments retain their signed-halfword truncation; damage is clamped to 0..999.
 *  @address 0x08044F4C
 */
u16 Battle_CalculateMagicDamage(struct BattleCombatant *attacker, struct BattleCombatant *target)
{
    u32 magicGuard = target->displayState & BATTLE_DISPLAY_MAGIC_GUARD;
    u32 random;
    u16 men;
    u16 divisor;
    u16 randomBonus;
    u16 resistance;
    u16 power;
    s16 damage;

    if (magicGuard != 0)
    {
        target->displayState &= ~BATTLE_DISPLAY_MAGIC_GUARD;
        return 0;
    }
    if (target->displayState & BATTLE_DISPLAY_DAMAGE_PROTECTED)
    {
        target->pendingDamage = magicGuard;
        return 0;
    }
    random = Battle_NextRandom();
    men = Battle_GetEffectiveCombatantStat(attacker, BATTLE_STAT_MEN);
    divisor = men / 10u;
    randomBonus = random % divisor;
    resistance = Battle_GetEffectiveCombatantStat(target, BATTLE_STAT_RES);
    power = Battle_GetActionPowerBonus(attacker, Battle_GetSelectedSkill(attacker), 0);
    damage = ((u16)Battle_GetEffectiveCombatantStat(attacker, BATTLE_STAT_MEN)
        + attacker->level * power + randomBonus - resistance) / 2;
    switch (Battle_GetElementResponse(target, Battle_GetActionElementMask(attacker)))
    {
    case BATTLE_ELEMENT_RESPONSE_NEUTRAL: break;
    case BATTLE_ELEMENT_RESPONSE_WEAK: damage *= 2; break;
    case BATTLE_ELEMENT_RESPONSE_RESISTANT: damage /= 2; break;
    }
    if (attacker->displayState & BATTLE_DISPLAY_MAGIC_POWER_BOOST)
    {
        damage *= 2;
        attacker->displayState &= ~BATTLE_DISPLAY_MAGIC_POWER_BOOST;
    }
    if (BattleItem_FindEquipmentByType(attacker, 12) >= 0)
        damage += damage / 10;
    if (damage < 0)
        damage = 0;
    if (damage > 999)
        damage = 999;
    return damage;
}

/** @brief Calculate attack-item damage with random reduction and elemental scaling.
 *  @note Items 48/49 use base damage 70 and random span 10; others use 40 and 15.
 *  randomVariation holds the span, then the negative roll. Separate cases preserve
 *  the original jump table and registers; all 0x104 bytes and relocations match.
 *  Signed damage clamps to 0..999; guard flags are ignored.
 *  @address 0x08045098
 */
u16 Battle_CalculateItemDamage(const struct BattleCombatant *actor, const struct BattleCombatant *target)
{
    s16 randomVariation;
    s16 damage;

    switch (actor->itemId)
    {
    case 0x19:
    case 0x1A:
    case 0x1B:
    case 0x1C:
    case 0x1D:
    case 0x1E:
    case 0x1F:
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23:
    case 0x24:
    case 0x25:
    case 0x26:
    case 0x27:
    case 0x28:
    case 0x29:
    case 0x2A:
    case 0x2B:
        damage = 40;
        randomVariation = 15;
        break;
    case 0x2C:
        damage = 40;
        randomVariation = 15;
        break;
    case 0x2D:
        damage = 40;
        randomVariation = 15;
        break;
    case 0x2E:
        damage = 40;
        randomVariation = 15;
        break;
    case 0x2F:
        damage = 40;
        randomVariation = 15;
        break;
    case 0x30:
        damage = 70;
        randomVariation = 10;
        break;
    case 0x31:
        damage = 70;
        randomVariation = 10;
        break;
    default:
        damage = 40;
        randomVariation = 15;
        break;
    }
    randomVariation = -(Battle_NextRandom() % randomVariation);
    if (randomVariation != 0)
        damage += randomVariation;
    switch (Battle_GetElementResponse(target, Battle_GetActionElementMask(actor)))
    {
    case BATTLE_ELEMENT_RESPONSE_WEAK:
        damage *= 2;
        break;
    case BATTLE_ELEMENT_RESPONSE_RESISTANT:
        damage /= 2;
        break;
    case BATTLE_ELEMENT_RESPONSE_NEUTRAL:
        break;
    }
    if (damage < 0)
        damage = 0;
    if (damage > 999)
        damage = 999;
    return damage;
}

/** @brief Roll and queue additional statuses from party equipment or ordinary-enemy action data.
 *  @note The byte-sized (random & 100) roll preserves register allocation. Party equipment
 *  types 1, 2 and 11 overwrite the sleep, silence and confusion choices in that order.
 *  Enemy status/threshold are defined only for attack or skill. Properties 9/12 skip
 *  queuing; per-status resistance queues zero. All 0x18C linked bytes and relocations match.
 *  @address 0x0804519C
 */
void Battle_QueueActionStatusEffect(const struct BattleCombatant *actor, struct BattleCombatant *target)
{
    u8 status;
    u8 threshold;

    if (actor->id < BATTLE_CHARACTER_ID_COUNT)
    {
        threshold = 0;
        status = BATTLE_STATUS_NORMAL;
        if (actor->action == BATTLE_ACTION_ATTACK)
        {
            if (BattleItem_FindEquipmentByType(actor, 1) >= 0)
            {
                threshold = 25;
                status = BATTLE_STATUS_ASLEEP;
            }
            if (BattleItem_FindEquipmentByType(actor, 2) >= 0)
            {
                threshold = 50;
                status = BATTLE_STATUS_SILENCED;
            }
            if (BattleItem_FindEquipmentByType(actor, 11) >= 0)
            {
                threshold = 20;
                status = BATTLE_STATUS_CONFUSED;
            }
            if ((u8)(Battle_NextRandom() & 100) < threshold)
                BattleAction_QueueTargetStatus(target, status);
        }
    }
    else if ((u8)(actor->id - 12) <= 100)
    {
        switch (actor->action)
        {
        case BATTLE_ACTION_ATTACK:
        {
            const u8 *actions = (const u8 *)gBattleEnemyActionInfo;
            s32 offset = (actor->id - 12) * sizeof(struct BattleEnemyActionInfo);

            status = actions[offset + offsetof(struct BattleEnemyActionInfo, attackStatus)];
            actions += offsetof(struct BattleEnemyActionInfo, attackStatusThreshold);
            threshold = actions[offset];
            break;
        }
        case BATTLE_ACTION_SKILL:
        {
            const u8 *actions = (const u8 *)gBattleEnemyActionInfo;
            s32 offset = (actor->id - 12) * sizeof(struct BattleEnemyActionInfo);
            const u8 *statusFields = actions + offsetof(struct BattleEnemyActionInfo, skillStatus);

            status = statusFields[offset];
            actions += offsetof(struct BattleEnemyActionInfo, skillStatusThreshold);
            threshold = actions[offset];
            break;
        }
        }
        if ((u8)(Battle_NextRandom() & 100) < threshold
            && BattleItem_FindEquipmentByProperty(target, BATTLE_ITEM_PROPERTY_BYTE1_LOW4, 9) < 0
            && BattleItem_FindEquipmentByProperty(target, BATTLE_ITEM_PROPERTY_BYTE1_LOW4, 12) < 0)
        {
            switch (status)
            {
            case BATTLE_STATUS_POISONED:
                if (BattleItem_FindEquipmentByProperty(target, BATTLE_ITEM_PROPERTY_BYTE1_LOW4, 10) >= 0)
                    status = BATTLE_STATUS_NORMAL;
                break;
            case BATTLE_STATUS_PARALYZED:
                if (BattleItem_FindEquipmentByProperty(target, BATTLE_ITEM_PROPERTY_BYTE1_LOW4, 10) >= 0
                    || BattleItem_FindEquipmentByProperty(target, BATTLE_ITEM_PROPERTY_BYTE1_LOW4, 14) >= 0)
                    status = BATTLE_STATUS_NORMAL;
                break;
            case BATTLE_STATUS_ASLEEP:
                if (BattleItem_FindEquipmentByProperty(target, BATTLE_ITEM_PROPERTY_BYTE1_LOW4, 10) >= 0)
                    status = BATTLE_STATUS_NORMAL;
                break;
            case BATTLE_STATUS_PETRIFIED:
                if (BattleItem_FindEquipmentByProperty(target, BATTLE_ITEM_PROPERTY_BYTE1_LOW4, 11) >= 0)
                    status = BATTLE_STATUS_NORMAL;
                break;
            case BATTLE_STATUS_CONFUSED:
                if (BattleItem_FindEquipmentByProperty(target, BATTLE_ITEM_PROPERTY_BYTE1_LOW4, 11) >= 0)
                    status = BATTLE_STATUS_NORMAL;
                break;
            case BATTLE_STATUS_SILENCED:
                if (BattleItem_FindEquipmentByProperty(target, BATTLE_ITEM_PROPERTY_BYTE1_LOW4, 11) >= 0)
                    status = BATTLE_STATUS_NORMAL;
                break;
            case BATTLE_STATUS_DEFEATED:
                if (BattleItem_FindEquipmentByProperty(target, BATTLE_ITEM_PROPERTY_BYTE1_LOW4, 13) >= 0)
                    status = BATTLE_STATUS_NORMAL;
                break;
            }
            BattleAction_QueueTargetStatus(target, status);
        }
    }
}

/** @brief Roll an action's hit using raw ATC, DEF and AGL values.
 *  @note Skills and targets in statuses 2..4 always hit. The normal chance clamps to 30..100 before the
 *  target's equipment subtracts 50; returns a full-width boolean.
 *  @address 0x08045328
 */
u32 Battle_RollActionHit(const struct BattleCombatant *attacker, const struct BattleCombatant *target, u8 accuracy)
{
    u32 hit;

    if ((u8)Battle_CheckStatusMask(target, 0x1C) == BATTLE_STATUS_MATCH_ACCEPTED
        || attacker->action == BATTLE_ACTION_SKILL)
        hit = 1;
    else
    {
        u8 penalty;
        s16 chance;

        if (attacker->id <= 10)
            penalty = attacker->source.party.chanceAdjustment;
        else
            penalty = 0;
        chance = (attacker->agl - target->agl) * 2
            + (attacker->atc - target->def) + (accuracy - penalty);
        if ((s16)chance > 100)
            chance = 100;
        if ((s16)chance < 30)
            chance = 30;
        if (BattleItem_FindEquipmentByProperty(target, BATTLE_ITEM_PROPERTY_BYTE1_LOW4, 8) >= 0)
            chance = (s16)chance - 50;
        {
            u16 roll = Battle_NextRandom() % 100;

            if ((s16)roll < (s16)chance)
                hit = 1;
            else
                hit = 0;
        }
    }
    return hit;
}

/** @brief Sum experience rewards from enemy slots in status eight.
 *  @note The extended-encounter flag selects the boss record's reward offsets.
 *  @address 0x080453D8
 */
u16 Battle_GetDefeatedEnemyExperience(void)
{
    struct BattleCombatant *enemies = Battle_GetEnemies();
    u16 experience = 0;
    u8 slot = 0;

    for (; slot < BATTLE_ENEMY_COUNT; slot++)
    {
        struct BattleCombatant *enemy = (struct BattleCombatant *)(
            slot * sizeof(struct BattleCombatant) + (u32)enemies);

        if (enemy->status == BATTLE_STATUS_DEFEATED)
        {
            if (Battle_GetControlFlags() & BATTLE_CONTROL_SPECIAL_ENCOUNTER)
                experience += enemy->source.boss->experienceReward;
            else
                experience += enemy->source.enemy->experienceReward;
        }
    }
    return experience;
}

/** @brief Sum defeated-enemy gold, apply the equipment bonus and award it.
 *  @note Accumulation and the returned amount retain sixteen-bit truncation.
 *  @address 0x0804542C
 */
u16 Battle_AwardDefeatedEnemyGold(void)
{
    struct BattleCombatant *enemies = Battle_GetEnemies();
    u16 gold = 0;
    u8 slot = 0;

    for (; slot < BATTLE_ENEMY_COUNT; slot++)
    {
        struct BattleCombatant *enemy = (struct BattleCombatant *)(
            slot * sizeof(struct BattleCombatant) + (u32)enemies);

        if (enemy->status == BATTLE_STATUS_DEFEATED)
        {
            if (Battle_GetControlFlags() & BATTLE_CONTROL_SPECIAL_ENCOUNTER)
                gold += enemy->source.boss->goldReward;
            else
                gold += enemy->source.enemy->goldReward;
        }
    }
    if (BattleItem_FindPartyEquipmentByProperty(BATTLE_ITEM_PROPERTY_BYTE2_LOW2, 1) >= 0)
    {
        u16 bonus = (u32)gold / 10;

        gold += bonus;
    }
    Party_AddGold(gold);
    return gold;
}

/** @brief Award experience and update eligible party members' levels.
 *  @note Returns a party-slot level-up mask; statuses seven/eight and empty slots are skipped.
 *  @address 0x080454A4
 */
u8 Battle_AwardExperience(u16 experience)
{
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 levelUpMask = 0;
    u8 slot;
    u8 eligibleCount = 0;
    u16 awardedExperience;

    for (slot = 0; slot < BATTLE_PARTY_COUNT; slot++)
    {
        if (combatants[slot].status != BATTLE_STATUS_ESCAPED && combatants[slot].status != BATTLE_STATUS_DEFEATED
            && combatants[slot].id != BATTLE_NO_COMBATANT)
            eligibleCount++;
    }
    awardedExperience = experience;
    for (slot = 0; slot < BATTLE_PARTY_COUNT; slot++)
    {
        u8 characterIndex;
        u8 *characters;
        s32 offset;
        u8 *level;
        u32 *totalExperience;
        u32 *nextExperience;
        u32 *fieldBase;

        if (combatants[slot].status == BATTLE_STATUS_ESCAPED || combatants[slot].status == BATTLE_STATUS_DEFEATED
            || combatants[slot].id == BATTLE_NO_COMBATANT)
            continue;
        characterIndex = Battle_GetPartySlotCharacterId(slot);
        if (characterIndex != 0)
            characterIndex--;
        characters = (u8 *)gPartyCharacters;
        offset = characterIndex * sizeof(struct PartyCharacter);
        level = (u8 *)(offset + (u32)characters);
        if (*level > 97)
            continue;
        fieldBase = (u32 *)(characters + offsetof(struct PartyCharacter, experience));
        totalExperience = fieldBase + characterIndex * (sizeof(struct PartyCharacter) / sizeof(u32));
        *totalExperience += awardedExperience;
        fieldBase = (u32 *)(characters + offsetof(struct PartyCharacter, nextLevelExperience));
        nextExperience = fieldBase + characterIndex * (sizeof(struct PartyCharacter) / sizeof(u32));
        if (*totalExperience >= *nextExperience)
        {
            levelUpMask |= 1 << slot;
            *nextExperience = Party_GetNextLevelExperience(
                Party_GetLevelFromExperience(*totalExperience));
            *level = Party_GetLevelFromExperience(*totalExperience);
        }
        if (*totalExperience > PARTY_MAX_EXPERIENCE)
            *totalExperience = Party_GetNextLevelExperience(97);
    }
    return levelUpMask;
}

/** @brief Read the persistent HP/MP or base-stat value for a battle party slot.
 *  @note Selectors zero/one read halfwords and two through seven read bytes; valid selectors are 0..7.
 *  @address 0x080455A0
 */
u32 Battle_GetPartySlotStoredStat(u8 slot, u8 stat)
{
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 characterIndex = gPartyCharacterSlots[combatants[slot].formationSlot];

    if (characterIndex != 0)
        characterIndex--;
    switch (stat)
    {
    case PARTY_STAT_MAX_HP: return gPartyCharacters[characterIndex].maxHp;
    case PARTY_STAT_MAX_MP: return gPartyCharacters[characterIndex].maxMp;
    case PARTY_STAT_ATC: return gPartyCharacters[characterIndex].baseAtc;
    case PARTY_STAT_DEF: return gPartyCharacters[characterIndex].baseDef;
    case PARTY_STAT_AGL: return gPartyCharacters[characterIndex].baseAgl;
    case PARTY_STAT_MEN: return gPartyCharacters[characterIndex].baseMen;
    case PARTY_STAT_RES: return gPartyCharacters[characterIndex].baseRes;
    case PARTY_STAT_NOA: return gPartyCharacters[characterIndex].baseNoa;
    }
}

/** @brief Apply a displayed stat gain to the persistent party record.
 *  @note The byte-sized gain also updates the corresponding current stat where present.
 *  @address 0x08045688
 */
void Battle_AddPartySlotStatGain(u8 slot, u8 stat, u8 gain)
{
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 characterIndex = gPartyCharacterSlots[combatants[slot].formationSlot];

    if (characterIndex != 0)
        characterIndex--;
    switch (stat)
    {
    case PARTY_STAT_MAX_HP:
        gPartyCharacters[characterIndex].maxHp += gain;
        break;
    case PARTY_STAT_MAX_MP:
        gPartyCharacters[characterIndex].maxMp += gain;
        break;
    case PARTY_STAT_ATC:
        gPartyCharacters[characterIndex].baseAtc += gain;
        gPartyCharacters[characterIndex].atc += gain;
        break;
    case PARTY_STAT_DEF:
        gPartyCharacters[characterIndex].baseDef += gain;
        gPartyCharacters[characterIndex].def += gain;
        break;
    case PARTY_STAT_AGL:
        gPartyCharacters[characterIndex].baseAgl += gain;
        gPartyCharacters[characterIndex].agl += gain;
        break;
    case PARTY_STAT_MEN:
        gPartyCharacters[characterIndex].baseMen += gain;
        gPartyCharacters[characterIndex].men += gain;
        break;
    case PARTY_STAT_RES:
        gPartyCharacters[characterIndex].baseRes += gain;
        gPartyCharacters[characterIndex].res += gain;
        break;
    case PARTY_STAT_NOA:
        gPartyCharacters[characterIndex].baseNoa += gain;
        gPartyCharacters[characterIndex].noa += gain;
        break;
    }
}

/** @brief Commit battle party HP, MP, accessories, gauge and learned skills.
 *  @note Uses gPartyMembers and stores at least one HP. Accessory B3 saves gauge zero; separate HP assignment branches preserve the original C register allocation.
 *  @address 0x080457AC
 */
void Battle_SavePartyCombatantState(void)
{
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 slot;

    for (slot = 0; slot < BATTLE_PARTY_COUNT; slot++)
    {
        u8 characterIndex = gPartyMembers[slot];

        if (characterIndex != 0xFF)
        {
            struct PartyCharacter *character;
            struct BattleCombatant *combatant;

            if (characterIndex != 0)
                characterIndex--;
            character = &gPartyCharacters[characterIndex];
            combatant = &combatants[slot];
            if (combatant->hp == 0)
                character->hp = 1;
            else
                character->hp = combatant->hp;
            character->mp = combatant->mp;
            character->equipSlot5 = combatant->equipment[4];
            character->equipSlot6 = combatant->equipment[5];
            if (combatant->equipment[4] == 0xB3 || combatant->equipment[5] == 0xB3)
                character->specialGauge = 0;
            else
                character->specialGauge = combatant->source.party.specialGauge;
            Party_BuildLearnedSkills(character->skills, character->level, gPartyMembers[slot]);
        }
    }
}

/** @brief Resolve the persistent character id while collecting level-up skills.
 *  @note Inlined reads preserve the original separate combatant-base calls.
 *  @address 0x08045860; inline expansion, no separate ROM entry
 */
static inline u8 Battle_GetPartySlotCharacterIdInline(u8 slot)
{
    struct BattleCombatant *combatants = Battle_GetCombatants();

    return gPartyCharacterSlots[combatants[slot].formationSlot];
}

/** @brief Collect skills learned while advancing a battle slot to its persistent level.
 *  @note Initializes eight output bytes to FF and reports zero-based ids. Separate inlined character-id reads and the adjusted combatant base preserve the original calls and level updates.
 *  @address 0x08045860
 */
s8 Battle_CollectNewlyLearnedSkills(u8 slot, u8 *skills)
{
    u8 characterIndex = Battle_GetPartySlotCharacterIdInline(slot);
    struct BattleCombatant *combatants = Battle_GetCombatants();
    s8 count;
    for (count = 0; count < BATTLE_SKILL_COUNT; count++)
        skills[count] = 0xFF;
    if (characterIndex != 0)
        characterIndex--;
    combatants += slot;
    count = 0;
    while (combatants->level < gPartyCharacters[characterIndex].level)
    {
        u8 level;
        u8 characterId;
        u8 skill;

        combatants->level++;
        level = combatants->level;
        characterId = Battle_GetPartySlotCharacterIdInline(slot);
        skill = Party_FindSkillLearnedAtLevel(level, characterId);
        if (skill != 0xFF && Battle_IsPartySlotMissingSkill(slot, skill) && count < BATTLE_SKILL_COUNT)
        {
            skills[count] = skill - 1;
            count++;
        }
    }
    return count;
}

/** @brief Build the character-specific list of affordable skill selections.
 *  @note Checks affordability before filtering each learned id by character. Separate cases for ids 0, 1, 6 and 7 preserve the common append block and register allocation.
 *  @address 0x08045940
 */
u8 Battle_ListUsableCharacterSkillSelections(const struct BattleCombatant *combatant, u8 *selections)
{
    u8 index;
    u8 count;

    for (index = 0; index < BATTLE_SKILL_COUNT; index++)
        selections[index] = 0;
    count = 0;
    for (index = 0; index < BATTLE_SKILL_COUNT; index++)
    {
        if (Battle_FindUsableSkillSelection(combatant, combatant->skills[index]) != 0xFF)
        {
            switch (combatant->id)
            {
            case 0:
                selections[count] = index;
                count++;
                break;
            case 1:
                selections[count] = index;
                count++;
                break;
            case 6:
                selections[count] = index;
                count++;
                break;
            case 7:
                selections[count] = index;
                count++;
                break;
            case 2:
                switch (combatant->skills[index])
                {
                case 8: case 13:
                    selections[count] = index;
                    count++;
                    break;
                }
                break;
            case 3:
                switch (combatant->skills[index])
                {
                case 14: case 15: case 16:
                    selections[count] = index;
                    count++;
                    break;
                }
                break;
            case 4:
                switch (combatant->skills[index])
                {
                case 22: case 23: case 24: case 26: case 27: case 28:
                    selections[count] = index;
                    count++;
                    break;
                }
                break;
            case 5:
                switch (combatant->skills[index])
                {
                case 30: case 31: case 33:
                    selections[count] = index;
                    count++;
                    break;
                }
                break;
            }
        }
    }
    return count;
}

/** @brief Check whether the current MP snapshot covers a learned skill's adjusted cost.
 *  @note Reductions wrap as a byte before halving; abs(cost) preserves the integer
 *  copy at 08045A56. Signed-halfword MP subtraction and all 0x64 linked bytes match.
 *  @address 0x08045A10
 */
u8 Battle_CanAffordSkillSlot(const struct BattleCombatant *combatant, u8 skillSlot)
{
    u16 mp = combatant->mp;
    u8 skillId = combatant->skills[skillSlot];
    const u8 *skills = (const u8 *)gPartySkillTable;
    s32 offset = skillId * sizeof(struct PartySkillInfo)
        + offsetof(struct PartySkillInfo, mpCost);
    u8 cost = skills[offset];
    s32 remaining;
    s32 requiredCost;

    if (BattleItem_FindEquipmentByProperty(combatant,
            BATTLE_ITEM_PROPERTY_SKILL_MP_COST, BATTLE_EQUIPMENT_MP_COST_MINUS_TWO) >= 0)
        cost -= 2;
    if (BattleItem_FindEquipmentByProperty(combatant,
            BATTLE_ITEM_PROPERTY_SKILL_MP_COST, BATTLE_EQUIPMENT_MP_COST_HALF) >= 0)
        cost /= 2;
    requiredCost = abs(cost);
    remaining = (s16)mp - requiredCost;
    if ((s16)remaining < 0)
        return 0;
    return 1;
}

/** @brief Filter a party-slot list for low HP, MP or both.
 *  @note Compacts the caller's list through a five-byte temporary. Uses strict current < (maximum / 10) *
 *  (percent / 10), with halfword product truncation; count must be at most five.
 *  @address 0x08045A74
 */
u8 Battle_FilterLowVitalSlots(const struct BattleCombatant *combatants, u8 *slots, u8 count, u8 percent, u8 vital)
{
    u8 filtered[BATTLE_PARTY_COUNT];
    u8 index;
    u8 filteredCount;

    for (index = 0; index < BATTLE_PARTY_COUNT; index++)
        filtered[index] = 0;
    percent /= 10u;
    filteredCount = 0;
    for (index = 0; index < count; index++)
    {
        u8 slot = slots[index];
        const struct BattleCombatant *combatant = &combatants[slot];
        u16 hp = combatant->hp;
        u16 mp = combatant->mp;
        u16 hpThreshold = combatant->maxHp / 10u;
        u16 mpThreshold = combatant->maxMp / 10u;

        hpThreshold *= percent;
        mpThreshold *= percent;
        switch (vital)
        {
        case BATTLE_VITAL_FILTER_HP:
            if (hp < hpThreshold)
            {
                filtered[filteredCount] = slot;
                filteredCount++;
            }
            break;
        case BATTLE_VITAL_FILTER_MP:
            if (mp < mpThreshold)
            {
                filtered[filteredCount] = slot;
                filteredCount++;
            }
            break;
        case BATTLE_VITAL_FILTER_BOTH:
            if (hp < hpThreshold && mp < mpThreshold)
            {
                filtered[filteredCount] = slot;
                filteredCount++;
            }
            break;
        }
    }
    for (index = 0; index < filteredCount; index++)
        slots[index] = filtered[index];
    return filteredCount;
}

/** @brief Subtract a selected battle skill slot's equipment-adjusted MP cost.
 *  @note Cost subtraction by two wraps as a byte before optional halving; the
 *  signed-halfword MP result is clamped to zero. Ordinary C matches all 0x64 bytes.
 *  @address 0x08045B90
 */
void Battle_ConsumeSkillSlotMp(struct BattleCombatant *combatant, u8 skillSlot)
{
    u16 mp = combatant->mp;
    u8 skillId = combatant->skills[skillSlot];
    const u8 *skills = (const u8 *)gPartySkillTable;
    s32 offset = skillId * sizeof(struct PartySkillInfo)
        + offsetof(struct PartySkillInfo, mpCost);
    u8 cost = skills[offset];
    s32 remaining;

    if (BattleItem_FindEquipmentByProperty(combatant,
            BATTLE_ITEM_PROPERTY_SKILL_MP_COST, BATTLE_EQUIPMENT_MP_COST_MINUS_TWO) >= 0)
        cost -= 2;
    if (BattleItem_FindEquipmentByProperty(combatant,
            BATTLE_ITEM_PROPERTY_SKILL_MP_COST, BATTLE_EQUIPMENT_MP_COST_HALF) >= 0)
        cost /= 2;
    remaining = (s16)mp - cost;
    mp = remaining;
    if ((s16)mp < 0)
        mp = 0;
    combatant->mp = mp;
}

/** @brief Choose the party character's special skill from id and equipped items.
 *  @note Writes source.party.specialSkill; character-specific weapon/accessory combinations override the
 *  default id. It does not spend or fill the special gauge.
 *  @address 0x08045BF4
 */
void Battle_SetPartySpecialSkill(struct BattleCombatant *combatant)
{
    switch (combatant->id)
    {
    case 0:
    case 1:
        if (combatant->equipment[0] == 0x15)
        {
            if (combatant->equipment[4] == 0xCB || combatant->equipment[5] == 0xCB)
                combatant->source.party.specialSkill = 0x37;
            else
                combatant->source.party.specialSkill = 0x30;
        }
        else
            combatant->source.party.specialSkill = 0x30;
        break;
    case 2:
        combatant->source.party.specialSkill = 0x31;
        break;
    case 3:
        if (combatant->equipment[4] == 0xBF || combatant->equipment[5] == 0xBF)
            combatant->source.party.specialSkill = 0x38;
        else
            combatant->source.party.specialSkill = 0x32;
        break;
    case 4:
        if (combatant->equipment[4] == 0xBF || combatant->equipment[5] == 0xBF)
            combatant->source.party.specialSkill = 0x39;
        else
            combatant->source.party.specialSkill = 0x33;
        break;
    case 5:
        if (combatant->equipment[0] == 0x3B)
            combatant->source.party.specialSkill = 0x3A;
        else
            combatant->source.party.specialSkill = 0x34;
        break;
    case 6:
        if (combatant->equipment[0] == 0x3C)
            combatant->source.party.specialSkill = 0x3B;
        else
            combatant->source.party.specialSkill = 0x35;
        break;
    case 7: case 8: case 9: case 10:
        combatant->source.party.specialSkill = 0xFF;
        break;
    }
}

/** @brief Build seven signed skill-target slots from a target mode and selected slot.
 *  @note Unused entries are -1; row-pair mode includes the middle formation row.
 *  @address 0x08045D00
 */
void Battle_BuildSkillTargetList(struct BattleCombatant *combatants, u8 mode, u8 selected, s8 *targets)
{
    s8 count = 0;
    u8 formation = combatants[selected].formationBits;
    s8 slot;
    u8 pairedRow;

    for (slot = 0; slot < BATTLE_ENEMY_COUNT; slot++)
        targets[slot] = -1;
    switch (mode)
    {
    case BATTLE_SKILL_TARGET_ALL_ENEMIES:
        for (slot = BATTLE_PARTY_COUNT; slot < BATTLE_COMBATANT_COUNT; slot++)
        {
            if (combatants[slot].id != BATTLE_NO_COMBATANT)
            {
                targets[count] = slot;
                count++;
            }
        }
        break;
    case BATTLE_SKILL_TARGET_ENEMY_COLUMN:
        for (slot = BATTLE_PARTY_COUNT; slot < BATTLE_COMBATANT_COUNT; slot++)
        {
            if (combatants[slot].id != BATTLE_NO_COMBATANT
                && (combatants[slot].formationBits & BATTLE_FORMATION_COLUMN_MASK)
                    == (combatants[selected].formationBits & BATTLE_FORMATION_COLUMN_MASK))
            {
                targets[count] = slot;
                count++;
            }
        }
        break;
    case BATTLE_SKILL_TARGET_ENEMY_ROW_PAIR:
        switch (formation & BATTLE_FORMATION_ROW_MASK)
        {
        case 0x10: pairedRow = 0x10; break;
        case 0x20: case 0x30: pairedRow = 0x30; break;
        }
        for (slot = BATTLE_PARTY_COUNT; slot < BATTLE_COMBATANT_COUNT; slot++)
        {
            if (combatants[slot].id != BATTLE_NO_COMBATANT)
            {
                u8 row = combatants[slot].formationBits & BATTLE_FORMATION_ROW_MASK;

                if (row == pairedRow || row == 0x20)
                {
                    targets[count] = slot;
                    count++;
                }
            }
        }
        break;
    case BATTLE_SKILL_TARGET_ENEMY:
    case BATTLE_SKILL_TARGET_PARTY:
        targets[count] = selected;
        break;
    case BATTLE_SKILL_TARGET_ALL_PARTY:
        for (slot = 0; slot < BATTLE_PARTY_COUNT; slot++)
        {
            if (combatants[slot].id != BATTLE_NO_COMBATANT)
            {
                targets[count] = slot;
                count++;
            }
        }
        break;
    case BATTLE_SKILL_TARGET_NO_SELECTION:
        break;
    }
}

/** @brief Byte view of the stat area through its six equipment slots.
 *  @note Begins at BattleCombatant.hp; the equipment field retains offset 0x21.
 *  This borrowed view allocates no additional combatant or equipment storage.
 *  @address 0x08045EB8
 */
struct BattleEquipmentStatsView
{
    u8 beforeEquipment[offsetof(struct BattleCombatant, equipment) - offsetof(struct BattleCombatant, hp)];
    u8 equipment[6];
};

/** @brief Add the six equipped items' turn-end effect bits.
 *  @note The HP-based equipment view and direct turnEndEffects writes preserve the original instructions. Property values 1, 2 and 3 add HP recovery, MP drain and gauge gain without replacing existing bits.
 *  @address 0x08045EB8
 */
void Battle_AddEquipmentTurnEndEffects(struct BattleCombatant *combatant)
{
    const struct BattleEquipmentStatsView *stats = (const void *)&combatant->hp;
    u8 index;

    for (index = 0; index < 6; index++)
    {
        switch (BattleItem_GetProperty(stats->equipment[index], BATTLE_ITEM_PROPERTY_TURN_END_EFFECT))
        {
        case BATTLE_EQUIPMENT_TURN_END_HP_RECOVERY:
            combatant->turnEndEffects |= BATTLE_TURN_END_PARTY_HP_RECOVERY;
            break;
        case BATTLE_EQUIPMENT_TURN_END_MP_DRAIN:
            combatant->turnEndEffects |= BATTLE_TURN_END_MP_DRAIN;
            break;
        case BATTLE_EQUIPMENT_TURN_END_GAUGE_GAIN:
            combatant->turnEndEffects |= BATTLE_TURN_END_GAUGE_GAIN;
            break;
        }
    }
}

/** @brief Classify an empty slot or test its numbered status against a mask.
 *  @note Returns 0 for empty, 1 for rejected and 2 for accepted; statuses above eight are rejected.
 *  @address 0x08045F10
 */
u32 Battle_CheckStatusMask(const struct BattleCombatant *combatant, u16 statusMask)
{
    u32 result = BATTLE_STATUS_MATCH_EMPTY;

    if (combatant->id != BATTLE_NO_COMBATANT)
    {
        result = BATTLE_STATUS_MATCH_REJECTED;
        switch (combatant->status)
        {
        case BATTLE_STATUS_NORMAL: if (statusMask & 0x001) result = BATTLE_STATUS_MATCH_ACCEPTED; break;
        case BATTLE_STATUS_POISONED: if (statusMask & 0x002) result = BATTLE_STATUS_MATCH_ACCEPTED; break;
        case BATTLE_STATUS_PARALYZED: if (statusMask & 0x004) result = BATTLE_STATUS_MATCH_ACCEPTED; break;
        case BATTLE_STATUS_ASLEEP: if (statusMask & 0x008) result = BATTLE_STATUS_MATCH_ACCEPTED; break;
        case BATTLE_STATUS_PETRIFIED: if (statusMask & 0x010) result = BATTLE_STATUS_MATCH_ACCEPTED; break;
        case BATTLE_STATUS_CONFUSED: if (statusMask & 0x020) result = BATTLE_STATUS_MATCH_ACCEPTED; break;
        case BATTLE_STATUS_SILENCED: if (statusMask & 0x040) result = BATTLE_STATUS_MATCH_ACCEPTED; break;
        case BATTLE_STATUS_ESCAPED: if (statusMask & 0x080) result = BATTLE_STATUS_MATCH_ACCEPTED; break;
        case BATTLE_STATUS_DEFEATED: if (statusMask & 0x100) result = BATTLE_STATUS_MATCH_ACCEPTED; break;
        }
    }
    return result;
}

/** @brief Apply the common status priority, palette and icon rules.
 *  @note The public entries differ only in the party animation selector.
 *  @address 0x08045F94, 0x08046060
 */
static inline void Battle_ApplyStatusInline(struct BattleCombatant *combatant,
    u16 status, u8 partyAnimation)
{
    if (combatant->id <= 112 || status == BATTLE_STATUS_DEFEATED)
    {
        if (combatant->status < status)
        {
            combatant->status = status;
            switch (status)
            {
            case BATTLE_STATUS_POISONED:
            case BATTLE_STATUS_PARALYZED:
            case BATTLE_STATUS_ASLEEP:
            case BATTLE_STATUS_PETRIFIED:
            case BATTLE_STATUS_CONFUSED:
            case BATTLE_STATUS_SILENCED:
            case BATTLE_STATUS_ESCAPED:
                Battle_UpdateStatusPaletteOffset(combatant, 1);
                if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
                    Battle_SetPartyAnimation(combatant, partyAnimation,
                        combatant->animation.baseTile, combatant->animation.paletteSlot, 0);
                else
                    Battle_SetEnemyAnimation(combatant, 0,
                        combatant->animation.baseTile, combatant->animation.paletteSlot, 0);
                break;
            case BATTLE_STATUS_DEFEATED:
                Battle_UpdateStatusPaletteOffset(combatant, 2);
                break;
            }
            switch (status)
            {
            case BATTLE_STATUS_POISONED:
                combatant->turnEndEffects |= BATTLE_TURN_END_HP_DAMAGE;
            case BATTLE_STATUS_PARALYZED:
            case BATTLE_STATUS_ASLEEP:
            case BATTLE_STATUS_CONFUSED:
            case BATTLE_STATUS_SILENCED:
                combatant->displayState |= BATTLE_DISPLAY_STATUS_ICON_RISING;
                combatant->statusIconPhase = 0;
                break;
            }
        }
    }
}

/** @brief Apply a higher-priority numbered status and refresh its presentation.
 *  @note Only a strictly greater status replaces the current byte. Ids above 112 accept only status 8;
 *  poison also queues turn-end HP damage. Party status animation uses selector 4.
 *  @address 0x08045F94
 */
void Battle_ApplyStatus(struct BattleCombatant *combatant, u16 status)
{
    Battle_ApplyStatusInline(combatant, status, BATTLE_COMBATANT_ANIM_STATUS);
}

/** @brief Apply the same status rule with the alternate party status animation.
 *  @note Shares the priority, boss-immunity, palette and poison rules with Battle_ApplyStatus. Its only
 *  code difference is party animation selector 10 instead of 4.
 *  @address 0x08046060
 */
void Battle_ApplyStatusWithAlternateAnimation(struct BattleCombatant *combatant, u16 status)
{
    Battle_ApplyStatusInline(combatant, status, BATTLE_COMBATANT_ANIM_ALTERNATE_STATUS);
}

/** @brief Cure the selected status or revive a fainted combatant.
 *  @note The unsigned -2 addend preserves the selector's halfword wrap and literal pool.
 *  Effect 11 excludes defeat; revival uses the first matching party slot (5 if absent)
 *  and wrapped HP + amount. Both paths clear only the poison turn-end bit and refresh
 *  the status palette; all 0xF0 linked bytes match.
 *  @address 0x0804612C
 */
void Battle_CureStatusOrRevive(struct BattleCombatant *combatant, u16 effect, u16 amount)
{
    u16 status = effect + (u16)-2;

    if (combatant->status == status || effect == BATTLE_RECOVERY_CURE_STATUS)
    {
        if (effect == BATTLE_RECOVERY_CURE_STATUS && combatant->status == BATTLE_STATUS_DEFEATED)
            return;

        if (combatant->status == BATTLE_STATUS_DEFEATED)
        {
            struct BattleCombatant *combatants = Battle_GetCombatants();
            u8 slot;

            for (slot = 0; slot < BATTLE_PARTY_COUNT; slot++)
            {
                if (combatants[slot].id == combatant->id)
                    break;
            }
            Battle_ReviveCombatant(combatant, slot, combatant->hp + amount);
        }
        else
        {
            combatant->status = BATTLE_STATUS_NORMAL;
            if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
                Battle_SetPartyAnimation(combatant, BATTLE_COMBATANT_ANIM_IDLE,
                    combatant->animation.baseTile, combatant->animation.paletteSlot, 0);
            else
                Battle_SetEnemyAnimation(combatant, 0,
                    combatant->animation.baseTile, combatant->animation.paletteSlot, 0);
        }
        if (combatant->turnEndEffects & BATTLE_TURN_END_HP_DAMAGE)
            combatant->turnEndEffects &= ~BATTLE_TURN_END_HP_DAMAGE;
        Battle_UpdateStatusPaletteOffset(combatant, 0);
    }
}

/** @brief Collect accepted slots after clearing the requested side's output list.
 *  @note Inlined into the formation filter and shared with the public listing entry.
 *  @address 0x0804621C, 0x080489E8; inline expansions
 */
static inline u8 Battle_ListCombatantsInline(struct BattleCombatant *combatants, u8 *slots, u8 side, u16 statusMask)
{
    u8 count = 0;
    u8 slot;
    u8 limit;

    if (side == BATTLE_SIDE_PARTY)
    {
        for (slot = 0; slot < BATTLE_PARTY_COUNT; slot++)
            slots[slot] = 0;
        slot = 0;
        limit = BATTLE_PARTY_COUNT;
    }
    else
    {
        for (slot = 0; slot < BATTLE_ENEMY_COUNT; slot++)
            slots[slot] = 0;
        slot = BATTLE_PARTY_COUNT;
        limit = BATTLE_COMBATANT_COUNT;
    }
    for (; slot < limit; slot++)
    {
        if ((u8)Battle_CheckStatusMask(&combatants[slot], statusMask) == BATTLE_STATUS_MATCH_ACCEPTED)
        {
            slots[count] = slot;
            count++;
        }
    }
    return count;
}

/** @brief List nonempty enemy slots sharing the requested formation bits.
 *  @note Clears seven output bytes and accepts all nine defined status values.
 *  @address 0x0804621C
 */
u8 Battle_ListEnemiesInFormation(const struct BattleCombatant *combatant, u8 *slots, u8 formationMask)
{
    u8 formation = combatant->formationBits & formationMask;
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 eligible[BATTLE_COMBATANT_COUNT];
    u8 eligibleCount;
    u8 count;
    u8 index;

    for (index = 0; index < BATTLE_ENEMY_COUNT; index++)
        slots[index] = 0;
    eligibleCount = Battle_ListCombatantsInline(combatants, eligible, BATTLE_SIDE_ENEMY, BATTLE_STATUS_MASK_ALL);
    count = 0;
    for (index = 0; index < eligibleCount; index++)
    {
        u8 slot = eligible[index];

        if ((combatants[slot].formationBits & formationMask) == formation)
        {
            slots[count] = slot;
            count++;
        }
    }
    return count;
}

/** @brief Build an action target list from its selected side and formation filter.
 *  @note Party output requires five bytes and enemy output seven. A full formation-byte filter returns the
 *  selected slot even if its status was rejected; row-pair mode includes row 2.
 *  @address 0x080462E4
 */
u8 Battle_ListActionTargetSlots(const struct BattleCombatant *combatant, u8 *slots, u16 statusMask)
{
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 side;
    u8 eligible[BATTLE_COMBATANT_COUNT];
    u8 eligibleCount;
    u8 filter;
    u8 index;
    u8 count;

    if (combatant->targetSlot <= 4)
        side = BATTLE_SIDE_PARTY;
    else
        side = BATTLE_SIDE_ENEMY;
    if (side == BATTLE_SIDE_PARTY)
    {
        for (index = 0; index < BATTLE_PARTY_COUNT; index++)
            slots[index] = 0;
    }
    else
    {
        for (index = 0; index < BATTLE_ENEMY_COUNT; index++)
            slots[index] = 0;
    }
    eligibleCount = Battle_ListCombatantsInline(combatants, eligible, side, statusMask);
    filter = Battle_GetActionFormationFilter(combatant);
    if (filter == 0)
    {
        count = eligibleCount;
        for (index = 0; index < count; index++)
            slots[index] = eligible[index];
    }
    else if ((filter & BATTLE_FORMATION_ROW_MASK) && (filter & BATTLE_FORMATION_COLUMN_MASK))
    {
        count = 0;
        for (index = 0; index < eligibleCount; index++)
        {
            u8 slot = eligible[index];

            if (combatants[slot].formationBits == filter)
            {
                slots[count] = slot;
                count++;
            }
        }
        slots[0] = combatant->targetSlot;
        count = 1;
    }
    else
    {
        count = 0;
        for (index = 0; index < eligibleCount; index++)
        {
            u8 formation = combatants[eligible[index]].formationBits
                & ((filter & BATTLE_FORMATION_COLUMN_MASK)
                    ? BATTLE_FORMATION_COLUMN_MASK : BATTLE_FORMATION_ROW_MASK);
            if (formation == filter
                || (!(filter & BATTLE_FORMATION_COLUMN_MASK) && formation == 0x20))
            {
                slots[count] = eligible[index];
                count++;
            }
        }
    }
    return count;
}

/**
 * @brief 筛选行动目标并写入调用者提供的缓冲
 * @note 动态写入 actors 的 03000730；该地址通过第二参数传入，列表项含 0x10 有效标志。
 * @address 0x08046480
 */
u8 Battle_SelectActionTargets(struct BattleCombatant *combatant, u8 *entries, u8 side)
{
    u8 targets[BATTLE_COMBATANT_COUNT];
    struct BattleCombatant *combatants;
    u8 count;
    u8 index;

    if (side == BATTLE_SIDE_PARTY)
    {
        for (index = 0; index < BATTLE_PARTY_COUNT; index++)
            entries[index] = 0;
    }
    else
    {
        for (index = 0; index < BATTLE_ENEMY_COUNT; index++)
            entries[index] = 0;
    }
    combatants = Battle_GetCombatants();
    count = Battle_ListActionTargetSlots(combatant, targets, 0x7F);
    for (index = 0; index < count; index++)
    {
        entries[index] = targets[index];
        if ((u8)Battle_RollActionHit(combatant, &combatants[targets[index]], 0x50) == 1)
        {
            if (combatants[entries[index]].status == BATTLE_STATUS_ASLEEP && Battle_NextRandom() % 100 <= 69)
                Battle_CureStatusOrRevive(&combatants[entries[index]], BATTLE_RECOVERY_CURE_SLEEP, 0);
            entries[index] |= BATTLE_TARGET_VALID;
        }
    }
    return count;
}

/** @brief List accepted combatants on the actor's side with matching formation bits.
 *  @note The actor id selects party or enemy slots; the byte mask can select a column, row or complete
 *  formation coordinate. The caller owns the output buffer.
 *  @address 0x08046558
 */
u8 Battle_ListAlliesInFormation(const struct BattleCombatant *combatant, u8 *slots, u8 formationMask, u16 statusMask)
{
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 side;
    u8 eligible[BATTLE_COMBATANT_COUNT];
    u8 eligibleCount;
    u8 count;
    u8 index;

    if (combatant->id <= 10)
        side = BATTLE_SIDE_PARTY;
    else
        side = BATTLE_SIDE_ENEMY;
    if (side == BATTLE_SIDE_PARTY)
    {
        for (index = 0; index < BATTLE_PARTY_COUNT; index++)
            slots[index] = 0;
    }
    else
    {
        for (index = 0; index < BATTLE_ENEMY_COUNT; index++)
            slots[index] = 0;
    }
    eligibleCount = Battle_ListCombatantsInline(combatants, eligible, side, statusMask);
    count = 0;
    for (index = 0; index < eligibleCount; index++)
    {
        u8 slot = eligible[index];

        if ((combatants[slot].formationBits & formationMask) == (combatant->formationBits & formationMask))
        {
            slots[count] = slot;
            count++;
        }
    }
    return count;
}

/** @brief Reset item reservations and restore the eligible party's saved tactics.
 *  @note Statuses 0, 1 and 6 are eligible. The per-slot helper reserves selected items and chooses targets;
 *  other party records are left alone.
 *  @address 0x0804666C
 */
void Battle_RestorePartyTacticsCommands(void)
{
    u8 slots[BATTLE_PARTY_COUNT];
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 count;
    u8 index;

    BattleItem_ResetReservations();
    count = Battle_ListCombatantsInline(combatants, slots, BATTLE_SIDE_PARTY, 0x43);
    for (index = 0; index < count; index++)
        Battle_RestoreTacticsCommand(&combatants[slots[index]], slots[index]);
}

/** @brief Calculate a learned slot's byte-sized MP cost for tactics restoration.
 *  @note Inlining keeps the cost table separate from the target-mode lookup.
 *  @address 0x080466F0; inline expansion, no separate ROM entry
 */
static inline u8 Battle_GetTacticsSkillCostInline(const struct BattleCombatant *combatant, u8 slot)
{
    u8 skillId = combatant->skills[slot];
    const u8 *skills = (const u8 *)gPartySkillTable;
    u32 address = skillId * sizeof(struct PartySkillInfo)
        + offsetof(struct PartySkillInfo, mpCost);
    u8 cost;

    address += (u32)skills;
    skills = (const u8 *)address;
    cost = *skills;

    if (BattleItem_FindEquipmentByProperty(combatant,
            BATTLE_ITEM_PROPERTY_SKILL_MP_COST, BATTLE_EQUIPMENT_MP_COST_MINUS_TWO) >= 0)
        cost -= 2;
    if (BattleItem_FindEquipmentByProperty(combatant,
            BATTLE_ITEM_PROPERTY_SKILL_MP_COST, BATTLE_EQUIPMENT_MP_COST_HALF) >= 0)
        cost /= 2;
    return cost;
}

/** @brief Resolve the learned slot or direct skill id to its target-mode nibble.
 *  @note The tactics path reads back skillSelection before this lookup.
 *  @address 0x080466F0; inline expansion, no separate ROM entry
 */
static inline u8 Battle_GetTacticsSkillTargetModeInline(const struct BattleCombatant *combatant, u8 selection)
{
    const u8 *skills;
    s32 offset;

    if (selection < BATTLE_SKILL_COUNT)
        selection = combatant->skills[selection];
    skills = (const u8 *)gPartySkillTable;
    offset = selection * sizeof(struct PartySkillInfo)
        + offsetof(struct PartySkillInfo, characterGroupBits);
    return skills[offset] & 0xF;
}

/** @brief Restore saved tactics, reserve selected items and choose a target.
 *  @note Missing skills/items, insufficient MP and empty E1-E4 target lists
 *  fall back to an attack without cancelling item reservations. Attack and
 *  DD-E0 retain unchecked target counts. Ordinary C matches all 0x560 bytes;
 *  skill failures use structured break exits, and no return value is defined.
 *  @address 0x080466F0
 */
void Battle_RestoreTacticsCommand(struct BattleCombatant *combatant, u8 partySlot)
{
    u8 targets[BATTLE_ENEMY_COUNT];
    u32 fallback = 0;
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 characterIndex = combatant->id;
    struct PartyCharacter *character;
    u8 count;
    u8 slot;

    if (characterIndex != 0)
        characterIndex--;
    character = &gPartyCharacters[characterIndex];
    combatant->action = character->tacticsAction;
    switch (character->tacticsAction)
    {
    case BATTLE_ACTION_ATTACK:
        count = Battle_ListCombatantsInline(combatants, targets, BATTLE_SIDE_ENEMY, 0x43);
        combatant->targetSlot = targets[Battle_NextRandom() % count];
        break;
    case BATTLE_ACTION_SKILL:
    {
        for (slot = 0; slot < BATTLE_SKILL_COUNT; slot++)
        {
            if (character->tacticsActionId - 1 == combatant->skills[slot])
                break;
        }
        if (slot < BATTLE_SKILL_COUNT)
        {
            u8 cost = Battle_GetTacticsSkillCostInline(combatant, slot);

            if ((s16)combatant->mp - cost < 0)
            {
                fallback = 1;
                break;
            }
        }
        else
        {
            fallback = 1;
            break;
        }

        combatant->skillSelection = slot;
        switch (Battle_GetTacticsSkillTargetModeInline(combatant, combatant->skillSelection))
        {
        case BATTLE_SKILL_TARGET_ENEMY:
        case BATTLE_SKILL_TARGET_ENEMY_COLUMN:
        case BATTLE_SKILL_TARGET_ENEMY_ROW_PAIR:
        case BATTLE_SKILL_TARGET_ALL_ENEMIES:
            count = Battle_ListCombatantsInline(combatants, targets, BATTLE_SIDE_ENEMY, 0x43);
            combatant->targetSlot = targets[Battle_NextRandom() % count];
            break;
        case BATTLE_SKILL_TARGET_PARTY:
        case BATTLE_SKILL_TARGET_ALL_PARTY:
            count = Battle_ListCombatantsInline(combatants, targets, BATTLE_SIDE_PARTY, 0x43);
            combatant->targetSlot = targets[Battle_NextRandom() % count];
            break;
        case BATTLE_SKILL_TARGET_NO_SELECTION:
            break;
        }

        break;
    }
    case BATTLE_ACTION_ITEM:
    {
        sBattleTacticsItemCount = BattleItem_BuildMenuList(sBattleTacticsItems);
        for (slot = 0; slot < sBattleTacticsItemCount; slot++)
        {
            if (character->tacticsActionId == sBattleTacticsItems[slot].itemId)
                break;
        }
        if (slot >= sBattleTacticsItemCount)
            fallback = 1;
        else
        {
            BattleItem_Reserve(&sBattleTacticsItems[slot], combatant, partySlot);
            switch (combatant->itemId)
            {
            case 0xDD:
            case 0xDE:
            case 0xDF:
            case 0xE0:
                count = Battle_ListCombatantsInline(combatants, targets, BATTLE_SIDE_PARTY, 0x17F);
                combatant->targetSlot = targets[Battle_NextRandom() % count];
                break;
            case 0xE1:
                count = Battle_ListCombatantsInline(combatants, targets, BATTLE_SIDE_PARTY, 0x100);
                if (count != 0)
                    combatant->targetSlot = targets[Battle_NextRandom() % count];
                else
                    fallback = 1;
                break;
            case 0xE2:
                count = Battle_ListCombatantsInline(combatants, targets, BATTLE_SIDE_PARTY, 4);
                if (count != 0)
                    combatant->targetSlot = targets[Battle_NextRandom() % count];
                else
                    fallback = 1;
                break;
            case 0xE3:
                count = Battle_ListCombatantsInline(combatants, targets, BATTLE_SIDE_PARTY, 2);
                if (count != 0)
                    combatant->targetSlot = targets[Battle_NextRandom() % count];
                else
                    fallback = 1;
                break;
            case 0xE4:
                count = Battle_ListCombatantsInline(combatants, targets, BATTLE_SIDE_PARTY, 0x17E);
                if (count != 0)
                    combatant->targetSlot = targets[Battle_NextRandom() % count];
                else
                    fallback = 1;
                break;
            case 0x19:
            case 0x1A:
            case 0x1F:
            case 0x2D:
            case 0x2E:
            case 0x30:
            case 0x31:
                count = Battle_ListCombatantsInline(combatants, targets, BATTLE_SIDE_ENEMY, 0x43);
                combatant->targetSlot = targets[Battle_NextRandom() % count];
                break;
            }
        }
        break;
    }
    case BATTLE_ACTION_DEFEND:
        Battle_SetPartyAnimation(combatant, 7, combatant->animation.baseTile,
            combatant->animation.paletteSlot, 0);
        break;
    }
    if (fallback == 1)
    {
        combatant->action = BATTLE_ACTION_ATTACK;
        count = Battle_ListCombatantsInline(combatants, targets, BATTLE_SIDE_ENEMY, 0x43);
        combatant->targetSlot = targets[Battle_NextRandom() % count];
    }
}

/** @brief Assign the escape action to command-eligible party records.
 *  @note Statuses 0, 1 and 6 are eligible. Writes action 4 without rolling escape success or selecting an
 *  attack target.
 *  @address 0x08046C50
 */
void Battle_SetPartyEscapeCommands(void)
{
    u8 slots[BATTLE_PARTY_COUNT];
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 count = Battle_ListCombatantsInline(combatants, slots, BATTLE_SIDE_PARTY, 0x43);
    u8 index;

    for (index = 0; index < count; index++)
        combatants[slots[index]].action = 4;
}

/** @brief Test the two formation columns against the melee distance limit.
 *  @note Inlining preserves the original boolean check in the opponent filter.
 *  @address 0x08046CD4; inline expansion, no separate ROM entry
 */
static inline u8 Battle_AreCombatantsInMeleeRangeInline(const struct BattleCombatant *first, const struct BattleCombatant *second)
{
    s32 distance = (first->formationBits & BATTLE_FORMATION_COLUMN_MASK)
        + (second->formationBits & BATTLE_FORMATION_COLUMN_MASK);

    if (distance <= 5)
        return 1;
    return 0;
}

/** @brief Count or list opposing combatants within the formation-column range.
 *  @note Accepts statuses 0..6 and column sums at most five. A null output pointer requests only the count;
 *  a nonnull buffer requires the opposite side's capacity.
 *  @address 0x08046CD4
 */
u8 Battle_ListOpponentsInMeleeRange(const struct BattleCombatant *combatant, u8 *slots)
{
    u8 count = 0;
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 eligible[BATTLE_ENEMY_COUNT];
    u8 eligibleCount;
    u8 index;

    if (combatant->id <= 10)
    {
        if (slots != NULL)
        {
            for (index = 0; index < BATTLE_ENEMY_COUNT; index++)
                slots[index] = 0;
        }
        eligibleCount = Battle_ListCombatantsInline(combatants, eligible, BATTLE_SIDE_ENEMY, 0x7F);
    }
    else
    {
        if (slots != NULL)
        {
            for (index = 0; index < BATTLE_PARTY_COUNT; index++)
                slots[index] = 0;
        }
        eligibleCount = Battle_ListCombatantsInline(combatants, eligible, BATTLE_SIDE_PARTY, 0x7F);
    }
    for (index = 0; index < eligibleCount; index++)
    {
        if (Battle_AreCombatantsInMeleeRangeInline(combatant, &combatants[eligible[index]]))
        {
            if (slots != NULL)
                slots[count] = eligible[index];
            count++;
        }
    }
    return count;
}

/** @brief Partition a combatant-slot range in descending order around its last slot.
 *  @note Signed byte scans compare unsigned halfword keys from the shared context.
 *  Preincrement in the left lookup preserves the original register allocation;
 *  returns the pivot's final index. All 0xF4 linked bytes match.
 *  @address 0x08046E18
 */
u8 Battle_PartitionCombatantSlots(u8 *slots, s8 first, s8 last)
{
    s8 left = first - 1;
    s8 right = last;
    u16 pivot = Battle_GetCombatantSortValue(&sBattleSortCombatants[slots[last]]);
    u8 temporary;

    for (;;)
    {
        while ((u16)Battle_GetCombatantSortValue(&sBattleSortCombatants[slots[++left]]) > pivot)
            ;

        while (left < --right
            && pivot > (u16)Battle_GetCombatantSortValue(&sBattleSortCombatants[slots[right]]))
            ;

        if (left >= right)
            break;
        temporary = slots[left];
        slots[left] = slots[right];
        slots[right] = temporary;
    }
    temporary = slots[left];
    slots[left] = slots[last];
    slots[last] = temporary;
    return left;
}

/** @brief Read the record field selected by the shared slot-sort key.
 *  @note Modifier sums wrap to halfwords. Keys 13 and 14 read formation column and row; valid selectors compile to the original C accessor.
 *  @address 0x08046F0C
 */
u32 Battle_GetCombatantSortValue(const struct BattleCombatant *combatant)
{
    switch (sBattleSortKey)
    {
    case BATTLE_SORT_LEVEL: return combatant->level;
    case BATTLE_SORT_HP: return combatant->hp;
    case BATTLE_SORT_MAX_HP: return combatant->maxHp;
    case BATTLE_SORT_MP: return combatant->mp;
    case BATTLE_SORT_MAX_MP: return combatant->maxMp;
    case BATTLE_SORT_ATC: return (u16)(combatant->atc + (u16)combatant->atcModifier);
    case BATTLE_SORT_DEF: return (u16)(combatant->def + (u16)combatant->defModifier);
    case BATTLE_SORT_AGL: return (u16)(combatant->agl + (u16)combatant->aglModifier);
    case BATTLE_SORT_MEN: return (u16)(combatant->men + (u16)combatant->menModifier);
    case BATTLE_SORT_RES: return (u16)(combatant->res + (u16)combatant->resModifier);
    case BATTLE_SORT_NOA: return combatant->noa;
    case BATTLE_SORT_HOME_X: return combatant->homeX;
    case BATTLE_SORT_HOME_Y: return combatant->homeY;
    case BATTLE_SORT_FORMATION_COLUMN: return combatant->formationBits & BATTLE_FORMATION_COLUMN_MASK;
    case BATTLE_SORT_FORMATION_ROW: return combatant->formationBits >> 4;
    }
}

/** @brief Read a battle stat with its modifier and equipment effects.
 *  @note Selectors 0..12 use BattleCombatantStat; other selectors return zero. A shared u16 adjustment preserves each division, intermediate truncation and successive equipment adjustment.
 *  @address 0x08047024
 */
u32 Battle_GetEffectiveCombatantStat(const struct BattleCombatant *combatant, u8 stat)
{
    u16 value = 0;
    u16 adjustment;

    switch (stat)
    {
    case BATTLE_STAT_LEVEL: value = combatant->level; break;
    case BATTLE_STAT_HP: value = combatant->hp; break;
    case BATTLE_STAT_MAX_HP: value = combatant->maxHp; break;
    case BATTLE_STAT_MP: value = combatant->mp; break;
    case BATTLE_STAT_MAX_MP: value = combatant->maxMp; break;
    case BATTLE_STAT_ATC:
    {
        const u16 *base = &combatant->atc;
        const u16 *modifier = (const u16 *)&combatant->atcModifier;

        value = *base + *modifier;
        if (BattleItem_FindEquipmentByProperty(combatant, BATTLE_ITEM_PROPERTY_BYTE0_LOW6, 30) >= 0)
        {
            adjustment = value / 10u;
            value += adjustment;
        }
        if (BattleItem_FindEquipmentByProperty(combatant, BATTLE_ITEM_PROPERTY_BYTE0_LOW6, 12) >= 0)
        {
            adjustment = (value / 10u) * 4;
            value -= adjustment;
        }
        if (BattleItem_FindEquipmentByProperty(combatant, BATTLE_ITEM_PROPERTY_BYTE0_LOW6, 13) >= 0)
        {
            adjustment = value / 10u;
            adjustment *= 5;
            value -= adjustment;
        }
        break;
    }
    case BATTLE_STAT_DEF:
    {
        const u16 *base = &combatant->def;
        const u16 *modifier = (const u16 *)&combatant->defModifier;

        value = *base + *modifier;
        if (BattleItem_FindEquipmentByProperty(combatant, BATTLE_ITEM_PROPERTY_BYTE0_LOW6, 30) >= 0)
        {
            adjustment = value / 10u;
            value += adjustment;
        }
        break;
    }
    case BATTLE_STAT_AGL:
    {
        const u16 *base = &combatant->agl;
        const u16 *modifier = (const u16 *)&combatant->aglModifier;

        value = *base + *modifier;
        break;
    }
    case BATTLE_STAT_MEN:
    {
        const u16 *base = &combatant->men;
        const u16 *modifier = (const u16 *)&combatant->menModifier;

        value = *base + *modifier;
        if (BattleItem_FindEquipmentByProperty(combatant, BATTLE_ITEM_PROPERTY_BYTE0_LOW6, 31) >= 0)
        {
            adjustment = value / 10u;
            value += adjustment;
        }
        break;
    }
    case BATTLE_STAT_RES:
    {
        const u16 *base = &combatant->res;
        const u16 *modifier = (const u16 *)&combatant->resModifier;

        value = *base + *modifier;
        if (BattleItem_FindEquipmentByProperty(combatant, BATTLE_ITEM_PROPERTY_BYTE0_LOW6, 31) >= 0)
        {
            adjustment = value / 10u;
            value += adjustment;
        }
        break;
    }
    case BATTLE_STAT_NOA: value = combatant->noa; break;
    case BATTLE_STAT_HOME_X: value = combatant->homeX; break;
    case BATTLE_STAT_HOME_Y: value = combatant->homeY; break;
    }
    return value;
}

/** @brief Return whether any nonempty combatant has animation graphics loading.
 *  @note Lists both sides with status mask 0x1FF and checks main and effect animation flags. Display flags do not suppress the effect-animation check; this entry now compiles from matching C.
 *  @address 0x080471AC
 */
u32 Battle_AreCombatantAnimationsLoading(void)
{
    u8 slots[BATTLE_COMBATANT_COUNT];
    u8 partySlots[BATTLE_PARTY_COUNT];
    u8 enemySlots[BATTLE_ENEMY_COUNT];
    u32 loading = 0;
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 partyCount = Battle_ListCombatantsInline(combatants, partySlots, BATTLE_SIDE_PARTY, BATTLE_STATUS_MASK_ALL);
    u8 enemyCount = Battle_ListCombatantsInline(combatants, enemySlots, BATTLE_SIDE_ENEMY, BATTLE_STATUS_MASK_ALL);
    u8 count;
    u8 index;

    for (index = 0; index < partyCount; index++)
        slots[index] = partySlots[index];
    for (index = 0; index < enemyCount; index++)
        slots[index + partyCount] = enemySlots[index];
    count = partyCount + enemyCount;
    for (index = 0; index < count; index++)
    {
        struct BattleCombatant *combatant = (struct BattleCombatant *)(
            slots[index] * sizeof(struct BattleCombatant) + (u32)combatants);

        if ((combatant->animation.flags & BATTLE_ANIM_LOADING)
            || (combatant->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            loading = 1;
            break;
        }
    }
    return loading;
}

/** @brief Read the selected action's additive damage-power contribution.
 *  @note Enemy actors and attacks return zero; skill 49 returns baseStat. Listed magic skills return 30, equipment modifies specific skill values, and unlisted selectors retain the initial value five.
 *  @address 0x080472E8
 */
u32 Battle_GetActionPowerBonus(const struct BattleCombatant *combatant, u16 skillId, u16 baseStat)
{
    u16 power = 5;

    if (combatant->id <= 10)
    {
        switch (combatant->action)
        {
        case BATTLE_ACTION_ATTACK:
            return 0;
        case BATTLE_ACTION_SKILL:
            switch (skillId)
            {
            case 15:
                power = 3;
                if (BattleItem_FindEquipmentByType(combatant, 13) >= 0)
                    power = 4;
                break;
            case 16:
                power = 5;
                if (BattleItem_FindEquipmentByType(combatant, 13) >= 0)
                    power = 7;
                break;
            case 22: return 7;
            case 23: return 3;
            case 24: return 8;
            case 26:
                power = 4;
                if (BattleItem_FindEquipmentByType(combatant, 10) >= 0)
                    power = 6;
                break;
            case 27:
                power = 6;
                if (BattleItem_FindEquipmentByType(combatant, 10) >= 0)
                    power = 9;
                break;
            case 28:
                power = 2;
                if (BattleItem_FindEquipmentByType(combatant, 10) >= 0)
                    power = 4;
                break;
            case 14:
            case 50:
                power = 9;
                if (BattleItem_FindEquipmentByType(combatant, 13) >= 0)
                    power = 13;
                break;
            case 49:
                return baseStat;
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
            case 10:
            case 11:
            case 12:
            case 13:
            case 17:
            case 18:
            case 19:
            case 20:
            case 21:
            case 25:
            case 29:
            case 30:
            case 31:
            case 32:
            case 33:
            case 34:
            case 35:
            case 36:
            case 37:
            case 44:
            case 45:
            case 46:
            case 47:
            case 51:
            case 52:
            case 53:
            case 55:
            case 59:
                return 30;
            case 0:
            case 1:
            case 2:
            case 38:
            case 39:
            case 40:
            case 41:
            case 42:
            case 43:
            case 48:
            case 54:
            case 56:
            case 57:
            case 58:
                return 0;
            }
            break;
        case BATTLE_ACTION_ITEM:
            switch (combatant->itemId)
            {
            case 25: case 26: case 31: case 45:
                return 4;
            case 46: case 48: case 49:
                return 5;
            }
            break;
        }
        return power;
    }
    return 0;
}

/** @brief Add a percentage of a base stat to its battle modifier.
 *  @note Checks the unsigned modifier before addition; DEF retains the ATC-based cap.
 *  A shared u16 limit, branch-local u32 snapshot and ordinary C casts reproduce
 *  all 0x1A0 linked bytes. Only ATC records the resulting modifier's low byte.
 *  @address 0x0804753C
 */
void Battle_AddStatModifierPercent(struct BattleCombatant *combatant, u8 stat, u8 percent)
{
    u16 limit;

    switch (stat)
    {
    case BATTLE_MODIFIER_ATC:
    {
        u16 base;
        u32 value;
        u16 *modifier;

        base = combatant->atc;
        limit = base / 10u;
        limit *= 5;
        modifier = (u16 *)&combatant->atcModifier;
        value = *modifier;
        if (value < limit)
        {
            *modifier = value + (u32)((float)base * ((float)percent / 100.0f));
            sBattleLastAtcBoostLowByte = *modifier;
        }
        break;
    }
    case BATTLE_MODIFIER_DEF:
    {
        u32 value;
        u16 *modifier;

        limit = combatant->atc / 10u;
        limit *= 5;
        modifier = (u16 *)&combatant->defModifier;
        value = *modifier;
        if (value < limit)
            *modifier = value + (u32)((float)combatant->def * ((float)percent / 100.0f));
        break;
    }
    case BATTLE_MODIFIER_AGL:
    {
        u16 base;
        u32 value;
        u16 *modifier;

        base = combatant->agl;
        limit = base / 10u;
        limit *= 5;
        modifier = (u16 *)&combatant->aglModifier;
        value = *modifier;
        if (value < limit)
            *modifier = value + (u32)((float)base * ((float)percent / 100.0f));
        break;
    }
    case BATTLE_MODIFIER_MEN:
    {
        u16 base;
        u32 value;
        u16 *modifier;

        base = combatant->men;
        limit = base / 10u;
        limit *= 5;
        modifier = (u16 *)&combatant->menModifier;
        value = *modifier;
        if (value < limit)
            *modifier = value + (u32)((float)base * ((float)percent / 100.0f));
        break;
    }
    case BATTLE_MODIFIER_RES:
    {
        u16 base;
        u32 value;
        u16 *modifier;

        base = combatant->res;
        limit = base / 10u;
        limit *= 5;
        modifier = (u16 *)&combatant->resModifier;
        value = *modifier;
        if (value < limit)
            *modifier = value + (u32)((float)base * ((float)percent / 100.0f));
        break;
    }
    }
}

/** @brief Partition one range before recursing through the public entry.
 *  @note Shared with the top-level sorter; the signed endpoints allow -1.
 *  @address 0x080476DC, 0x08048A88, 0x08048ACC; inline expansions
 */
static inline void Battle_QuicksortRangeInline(u8 *slots, s8 first, s8 last)
{
    if (first < last)
    {
        u8 pivot = Battle_PartitionCombatantSlots(slots, first, last);

        if (pivot < last)
        {
            Battle_QuicksortCombatantSlots(slots, first, pivot - 1);
            Battle_QuicksortCombatantSlots(slots, pivot + 1, last);
        }
    }
}

/** @brief Install the sort context and partition the requested slot list.
 *  @note Count is captured before Battle_GetCombatants; shared with the public sorter.
 *  @address 0x080476DC, 0x08048ACC; inline expansions
 */
static inline void Battle_SortCombatantSlotsInline(u8 *slots, u8 count, u8 sortKey)
{
    if (count > 1)
    {
        sBattleSortKey = sortKey;
        sBattleSortCombatants = Battle_GetCombatants();
        Battle_QuicksortRangeInline(slots, 0, count - 1);
    }
}

/** @brief Advance the party stat-boost effects, apply the modifier and restore draw priorities.
 *  @note Inlines slot collection and HOME_Y sorting, capturing the sort count before
 *  its getter call. Returns 1 after restoring priority 3; all 0x440 linked bytes match.
 *  @address 0x080476DC
 */
u8 Battle_UpdatePartyStatBoost(struct BattleCombatant *unused, struct BattleCombatant *target)
{
    u8 complete = 0;
    struct BattleCombatant *cursor = Battle_GetCursor();
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 index;

    switch (sBattleStatBoostPhase)
    {
    case 0:
        sBattleStatBoostPartyCount = Battle_ListCombatantsInline(combatants,
            sBattleStatBoostPartySlots, BATTLE_SIDE_PARTY, BATTLE_STATUS_MASK_ALL);
        sBattleStatBoostEnemyCount = Battle_ListCombatantsInline(combatants,
            sBattleStatBoostEnemySlots, BATTLE_SIDE_ENEMY, BATTLE_STATUS_MASK_ALL);
        Battle_SortCombatantSlotsInline(sBattleStatBoostPartySlots,
            sBattleStatBoostPartyCount, BATTLE_SORT_HOME_Y);
        for (index = 0; index < sBattleStatBoostPartyCount; index++)
        {
            struct BattleCombatant *party = (struct BattleCombatant *)(
                sBattleStatBoostPartySlots[index] * sizeof(struct BattleCombatant)
                + (u32)combatants);

            switch (party->formationBits & BATTLE_FORMATION_ROW_MASK)
            {
            case 0x10: party->animation.priority = 0; break;
            case 0x20: party->animation.priority = 1; break;
            case 0x30: party->animation.priority = 2; break;
            }
        }
        for (index = 0; index < sBattleStatBoostPartyCount; index++)
        {
            struct BattleCombatant *party = &combatants[sBattleStatBoostPartySlots[index]];

            if (target->animation.priority < party->animation.priority)
                party->animation.priority++;
        }
        sBattleStatBoostPhase = 1;
        break;
    case 1:
        Battle_SetEffectAnimation(cursor, target->homeX, target->homeY, 0x27C,
            14, (u16)(sBattleStatBoostAnimationId + 1), 0x104);
        cursor->effectAnimation.priority = target->animation.priority + 1;
        sBattleStatBoostPhase = 2;
        break;
    case 2:
        if (!(cursor->effectAnimation.flags & BATTLE_ANIM_LOADING))
            sBattleStatBoostPhase = 3;
        break;
    case 3:
        Battle_SetEffectAnimation(target, target->homeX, target->homeY, 0x344,
            14, sBattleStatBoostAnimationId, 0x105);
        target->effectAnimation.priority = 0;
        sBattleStatBoostPhase = 4;
        break;
    case 4:
        if (!(target->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(25, 1, 0);
            target->effectAnimation.flags &= 0xFEFF;
            cursor->effectAnimation.flags &= 0xFEFF;
            BattlePalette_StartObjFade(target->animation.paletteSlot, 1,
                7, 14, 28, 4, 4, -1, 2);
            sBattleStatBoostPhase = 7;
        }
        break;
    case 7:
        if (cursor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            u8 paletteSlot = target->effectAnimation.paletteSlot;

            BattlePalette_ReleaseObjBanks(paletteSlot,
                Battle_GetAnimationPaletteCount(&target->effectAnimation));
            cursor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            target->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            BattlePalette_ReturnObjFade(target->animation.paletteSlot, 1, 8);
            sBattleStatBoostFrame = 0;
            sBattleStatBoostPhase = 8;
        }
        break;
    case 8:
        if (sBattleStatBoostFrame < 8)
            sBattleStatBoostFrame++;
        else
            sBattleStatBoostPhase = 9;
        break;
    case 9:
        Battle_AddStatModifierPercent(target, sBattleStatBoostStat, sBattleStatBoostPercent);
        Battle_SetPartyAnimation(target, 0, target->animation.baseTile,
            target->animation.paletteSlot, 0);
        sBattleStatBoostPhase = 10;
        break;
    case 10:
        if (!(target->animation.flags & BATTLE_ANIM_LOADING))
            sBattleStatBoostPhase = 11;
        break;
    case 11:
        if (sBattleStatBoostFrame < 8)
            sBattleStatBoostFrame++;
        else
        {
            for (index = 0; index < sBattleStatBoostPartyCount; index++)
                combatants[sBattleStatBoostPartySlots[index]].animation.priority = 3;
            complete = 1;
        }
        break;
    }
    return complete;
}

/** @brief Advance the enemy's three-part stat-boost animation and apply its modifier.
 *  @note The middle animation completes three cycles; each finish releases OBJ banks.
 *  Returns 1 after applying the modifier in phase 11; all 0x20C linked bytes match.
 *  @address 0x08047B1C
 */
u8 Battle_UpdateEnemyStatBoost(struct BattleCombatant *combatant)
{
    u8 complete = 0;

    switch (sBattleStatBoostPhase)
    {
    case 0:
        Battle_SetAnimationById(&combatant->animation, sBattleStatBoostAnimationId,
            0x1B4, 13, 2);
        sBattleStatBoostFrame = 0;
        sBattleStatBoostPhase = 1;
        break;
    case 1:
        if (!(combatant->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(25, 1, 0);
            sBattleStatBoostPhase = 2;
        }
        break;
    case 2:
        if (combatant->animation.flags & BATTLE_ANIM_FINISHED)
        {
            u8 paletteSlot = combatant->animation.paletteSlot;
            struct BattleAnimation *animation = &combatant->animation;

            BattlePalette_ReleaseObjBanks(paletteSlot,
                Battle_GetAnimationPaletteCount(animation));
            Battle_SetAnimationById(animation, (u16)(sBattleStatBoostAnimationId + 1),
                0x1B4, 13, 0x102);
            sBattleStatBoostPhase = 3;
        }
        break;
    case 3:
        if (!(combatant->animation.flags & BATTLE_ANIM_LOADING))
        {
            combatant->animation.flags &= 0xFEFF;
            sBattleStatBoostPhase = 4;
        }
        break;
    case 4:
        if (sBattleStatBoostFrame < 3)
        {
            if (combatant->animation.flags & BATTLE_ANIM_FINISHED)
            {
                u8 paletteSlot = combatant->animation.paletteSlot;

                BattlePalette_ReleaseObjBanks(paletteSlot,
                    Battle_GetAnimationPaletteCount(&combatant->animation));
                sBattleStatBoostFrame++;
                combatant->animation.flags &= ~BATTLE_ANIM_FINISHED;
            }
        }
        else
        {
            Battle_SetAnimationById(&combatant->animation,
                (u16)(sBattleStatBoostAnimationId + 2), 0x1B4, 13, 0x102);
            sBattleStatBoostPhase = 5;
        }
        break;
    case 5:
        if (!(combatant->animation.flags & BATTLE_ANIM_LOADING))
        {
            combatant->animation.flags &= 0xFEFF;
            sBattleStatBoostPhase = 6;
        }
        break;
    case 6:
        if (combatant->animation.flags & BATTLE_ANIM_FINISHED)
        {
            u8 paletteSlot = combatant->animation.paletteSlot;

            BattlePalette_ReleaseObjBanks(paletteSlot,
                Battle_GetAnimationPaletteCount(&combatant->animation));
            combatant->animation.flags |= 0x100;
            sBattleStatBoostPhase = 11;
        }
        break;
    case 11:
        Battle_AddStatModifierPercent(combatant, sBattleStatBoostStat, sBattleStatBoostPercent);
        complete = 1;
        break;
    }
    return complete;
}

/** @brief Classify the target's response to the four low elemental bits.
 *  @note Returns neutral, weak or resistant. Weakness wins within one bit; a later higher matching bit can
 *  replace an earlier response. Boss and ordinary-enemy offsets differ.
 *  @address 0x08047D28
 */
u8 Battle_GetElementResponse(const struct BattleCombatant *combatant, u8 elementMask)
{
    u8 response = BATTLE_ELEMENT_RESPONSE_NEUTRAL;
    u16 resistance = 0;
    u16 weakness = 0;
    s16 id = combatant->id;
    u8 element;

    if (id <= 10u)
    {
        if (BattleItem_FindEquipmentByProperty(combatant, BATTLE_ITEM_PROPERTY_BYTE1_LOW4, 6) >= 0)
            resistance = 3;
        if (BattleItem_FindEquipmentByProperty(combatant, BATTLE_ITEM_PROPERTY_BYTE1_LOW4, 7) >= 0)
            resistance = 12;
    }
    else if ((u8)(id - 12) <= 100)
    {
        weakness = combatant->source.enemy->weaknessElements;
        resistance = combatant->source.enemy->resistanceElements;
    }
    else if (id > 112u)
    {
        weakness = combatant->source.boss->weaknessElements;
        resistance = combatant->source.boss->resistanceElements;
    }
    for (element = 0; element < 4; element++)
    {
        u32 selected = elementMask & (1 << element);

        if (weakness & selected)
            response = BATTLE_ELEMENT_RESPONSE_WEAK;
        else if (resistance & selected)
            response = BATTLE_ELEMENT_RESPONSE_RESISTANT;
    }
    return response;
}

/** @brief Resolve the learned slot or direct zero-based skill selection.
 *  @note The elemental dispatcher uses the same eight-slot selection rule.
 *  @address 0x08047DC8; inline expansion, no separate ROM entry
 */
static inline u8 Battle_GetSelectedSkillInline(const struct BattleCombatant *combatant)
{
    if (combatant->skillSelection < BATTLE_SKILL_COUNT)
        return combatant->skills[combatant->skillSelection];
    return combatant->skillSelection;
}

/** @brief Resolve attack, selected-skill or attack-item elemental bits.
 *  @note Weapon and equipment bits are combined for party attacks. Separate skill cases preserve register allocation; ordinary enemies read the low byte of resistanceElements and bosses return zero.
 *  @address 0x08047DC8
 */
u8 Battle_GetActionElementMask(const struct BattleCombatant *combatant)
{
    u8 elements = 0;

    if (combatant->id <= 10)
    {
        switch (combatant->action)
        {
        case BATTLE_ACTION_ATTACK:
            elements = gBattleWeaponElementMasks[combatant->equipment[0]];
            if (BattleItem_FindEquipmentByType(combatant, 14) >= 0)
                elements |= 2;
            if (BattleItem_FindEquipmentByType(combatant, 15) >= 0)
                elements |= 8;
            if (BattleItem_FindEquipmentByType(combatant, 16) >= 0)
                elements |= 4;
            if (BattleItem_FindEquipmentByType(combatant, 17) >= 0)
                elements |= 1;
            break;
        case BATTLE_ACTION_SKILL:
            switch (Battle_GetSelectedSkillInline(combatant))
            {
            case 46:
                elements = 8;
                break;
            case 48:
                elements = 8;
                break;
            case 14:
                elements = 1;
                break;
            case 15:
                elements = 1;
                break;
            case 16:
                elements = 1;
                break;
            case 50:
                elements = 1;
                break;
            case 26:
                elements = 4;
                break;
            case 27:
                elements = 4;
                break;
            case 28:
                elements = 4;
                break;
            case 45:
                elements = 4;
                break;
            case 22:
                elements = 2;
                break;
            case 23:
                elements = 2;
                break;
            case 24:
                elements = 2;
                break;
            case 44:
                elements = 2;
                break;
            }
            break;
        case BATTLE_ACTION_ITEM:
            switch (combatant->itemId)
            {
            case 25: case 45: case 48:
                elements = 4;
                break;
            case 31: case 46: case 49:
                elements = 2;
                break;
            }
            break;
        }
    }
    else if ((u8)(combatant->id - 12) <= 100)
        elements = combatant->source.enemy->resistanceElements;
    return elements;
}

/** @brief Return the encounter-specific signed enemy-level adjustment.
 *  @note The explicit encounter switch includes negative and positive byte deltas; unlisted encounter ids
 *  return zero.
 *  @address 0x08047FCC
 */
s8 Battle_GetEncounterLevelAdjustment(u16 encounterId)
{
    u8 adjustment = 0;

    switch (encounterId)
    {
    case 0:
        break;
    case 3:
        adjustment = 0xFD;
        break;
    case 1:
    case 10:
        adjustment = 0;
        break;
    case 11:
        adjustment = 0xFE;
        break;
    case 26:
        adjustment = 7;
        break;
    case 27:
        adjustment = 30;
        break;
    case 2:
    case 4: case 5: case 6: case 7: case 8: case 9:
    case 12: case 13: case 14: case 15: case 16: case 17:
    case 18: case 19: case 20: case 21: case 22: case 23:
    case 24: case 25:
    case 28: case 29: case 30: case 31: case 32: case 33:
    case 34: case 35: case 36: case 37: case 38: case 39:
    case 40: case 41: case 42: case 43: case 44: case 45:
    case 46: case 47: case 48: case 49: case 50: case 51:
    case 52: case 53: case 54: case 55: case 56: case 57:
        adjustment = 0xFF;
        break;
    }
    return adjustment;
}

/** @brief Read the first present protagonist's battle level for boss scaling.
 *  @note Includes fainted protagonists and returns 50 when neither id 0 nor 1 is present. The separate found index is read only after a successful search; keeping it separate preserves matching code.
 *  @address 0x080480EC
 */
u8 Battle_GetProtagonistBattleLevel(void)
{
    u8 level = 50;
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 slots[BATTLE_PARTY_COUNT];
    u8 count = Battle_ListCombatantsInline(combatants, slots, BATTLE_SIDE_PARTY, BATTLE_STATUS_MASK_ALL);
    u8 index;
    u8 found;

    for (index = 0; index < count; index++)
    {
        if (combatants[slots[index]].id <= 1)
        {
            found = index;
            break;
        }
    }
    if (index < count)
        level = combatants[slots[found]].level;
    return level;
}

/** @brief Count distinct occupied columns with a lower number than the target's column.
 *  @note Ignores attacker; collects target-side statuses 0..6 and keeps the shared
 *  column sort. The signed result initializes three occupancy bytes; counting uses
 *  u8, iteration uses s8, and the return retains signed-byte absolute-value handling.
 *  All 0x158 linked bytes and resolved relocations match.
 *  @address 0x080481B8
 */
s8 Battle_CountBlockingFormationColumns(const struct BattleCombatant *attacker, const struct BattleCombatant *target)
{
    u8 slots[BATTLE_ENEMY_COUNT];
    u8 occupied[3];
    u8 targetColumn;
    struct BattleCombatant *combatants;
    u8 side;
    u8 count;
    u8 blockingColumns;
    s8 index;
    s8 result = 0;

    occupied[0] = occupied[1] = occupied[2] = result;
    targetColumn = BATTLE_FORMATION_COLUMN_MASK;
    targetColumn &= target->formationBits;
    combatants = Battle_GetCombatants();
    side = BATTLE_SIDE_PARTY;
    if (target->id >= BATTLE_CHARACTER_ID_COUNT)
        side = BATTLE_SIDE_ENEMY;
    count = Battle_ListCombatantsInline(combatants, slots, side, 0x7F);
    Battle_SortCombatantSlotsInline(slots, count, BATTLE_SORT_FORMATION_COLUMN);

    blockingColumns = 0;
    for (index = 0; index < count; index++)
    {
        u8 column = combatants[slots[index]].formationBits & BATTLE_FORMATION_COLUMN_MASK;

        if (targetColumn > column && occupied[column - 1] == 0)
        {
            occupied[column - 1] = 1;
            blockingColumns++;
        }
    }
    result = blockingColumns;
    if (result < 0)
        result = -result;
    return result;
}

/** @brief Roll escape using the party and enemy effective-agility sums.
 *  @note Uses status mask 0xFF. The halfword threshold is party agility plus party adjustment bytes minus enemy agility plus 40; a strict random % 100 comparison also handles empty lists.
 *  @address 0x08048310
 */
u8 Battle_RollPartyEscape(void)
{
    u8 escaped = 0;
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 partySlots[BATTLE_PARTY_COUNT];
    u8 enemySlots[BATTLE_ENEMY_COUNT];
    u8 partyCount = Battle_ListCombatantsInline(combatants, partySlots, BATTLE_SIDE_PARTY, 0xFF);
    u8 enemyCount = Battle_ListCombatantsInline(combatants, enemySlots, BATTLE_SIDE_ENEMY, 0xFF);
    u16 partyAdjustment = 0;
    u16 partyAgility = 0;
    u16 enemyAgility;
    u16 threshold;
    u8 index;

    for (index = 0; index < partyCount; index++)
    {
        partyAgility += Battle_GetEffectiveCombatantStat(&combatants[partySlots[index]], BATTLE_STAT_AGL);
        partyAdjustment += combatants[partySlots[index]].source.party.chanceAdjustment;
    }
    enemyAgility = 0;
    for (index = 0; index < enemyCount; index++)
        enemyAgility += Battle_GetEffectiveCombatantStat(&combatants[enemySlots[index]], BATTLE_STAT_AGL);
    threshold = partyAgility + partyAdjustment - enemyAgility + 40;
    if ((u16)(Battle_NextRandom() % 100) < threshold)
        escaped = 1;
    return escaped;
}

/** @brief Roll one combatant's escape against opposing average agility.
 *  @note An empty opposing list contributes zero. Accessory C7 raises the base chance from 40 to 80; equipment property eight adds 50. The halfword threshold wraps before the strict random % 100 comparison.
 *  @address 0x08048458
 */
u8 Battle_RollCombatantEscape(const struct BattleCombatant *combatant)
{
    u8 escaped = 0;
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 slots[BATTLE_ENEMY_COUNT];
    u8 side;
    u16 baseChance;
    u8 adjustment;
    u8 count;
    u16 agility;
    u16 opponentAgility;
    u16 threshold;
    u8 index;

    if (combatant->id <= 10)
    {
        if (combatant->equipment[4] == 0xC7 || combatant->equipment[5] == 0xC7)
            baseChance = 80;
        else
            baseChance = 40;
        adjustment = combatant->source.party.chanceAdjustment;
        side = BATTLE_SIDE_ENEMY;
    }
    else
    {
        baseChance = 40;
        adjustment = 0;
        side = BATTLE_SIDE_PARTY;
    }
    count = Battle_ListCombatantsInline(combatants, slots, side, 0xFF);
    agility = Battle_GetEffectiveCombatantStat(combatant, BATTLE_STAT_AGL);
    opponentAgility = 0;
    for (index = 0; index < count; index++)
        opponentAgility += Battle_GetEffectiveCombatantStat(&combatants[slots[index]], BATTLE_STAT_AGL);
    if (count != 0)
        opponentAgility /= count;
    else
        opponentAgility = 0;
    threshold = agility + adjustment - opponentAgility + baseChance;
    if (BattleItem_FindEquipmentByProperty(combatant, BATTLE_ITEM_PROPERTY_BYTE1_LOW4, 8) >= 0)
        threshold += 50;
    if ((u16)(Battle_NextRandom() % 100) < threshold)
        escaped = 1;
    return escaped;
}

/** @brief Match an enemy's current action category with the original boss exceptions.
 *  @note Ids 12..112 read attack/skill categories; other actions use category 50.
 *  Boss ids 125/126 always match; 118/123/127 match any non-attack action, regardless
 *  of category. Other ids return zero. Separate zero-case bodies preserve register
 *  allocation; all 0xEA function bytes and resolved relocations match.
 *  @address 0x080485A4
 */
u8 Battle_MatchesEnemyActionCategory(const struct BattleCombatant *combatant, u8 category)
{
    u8 result = 0;

    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
        return 0;
    if ((u8)(combatant->id - 12) <= 100)
    {
        u8 actionCategory;

        switch (combatant->action)
        {
        case BATTLE_ACTION_ATTACK:
        {
            const u8 *actions = (const u8 *)gBattleEnemyActionInfo;
            s32 offset = (combatant->id - 12) * sizeof(struct BattleEnemyActionInfo);

            actions += offsetof(struct BattleEnemyActionInfo, attackCategory);
            actionCategory = actions[offset];
            break;
        }
        case BATTLE_ACTION_SKILL:
        {
            const u8 *actions = (const u8 *)gBattleEnemyActionInfo;
            s32 offset = (combatant->id - 12) * sizeof(struct BattleEnemyActionInfo);

            actions += offsetof(struct BattleEnemyActionInfo, skillCategory);
            actionCategory = actions[offset];
            break;
        }
        default:
            actionCategory = 50;
            break;
        }
        if (category == actionCategory)
            result = 1;
    }
    else if (combatant->id > 112)
    {
        switch (combatant->id)
        {
        case 113:
            result = 0;
            break;
        case 114:
            result = 0;
            break;
        case 115:
            result = 0;
            break;
        case 116:
            result = 0;
            break;
        case 117:
            result = 0;
            break;
        case 118:
            if (combatant->action != BATTLE_ACTION_ATTACK)
                result = 1;
            break;
        case 119:
            result = 0;
            break;
        case 120:
            result = 0;
            break;
        case 121:
            result = 0;
            break;
        case 122:
            result = 0;
            break;
        case 123:
            if (combatant->action != BATTLE_ACTION_ATTACK)
                result = 1;
            break;
        case 125: case 126:
            result = 1;
            break;
        case 127:
            if (combatant->action != BATTLE_ACTION_ATTACK)
                result = 1;
            break;
        case 128:
            result = 0;
            break;
        case 129:
            result = 0;
            break;
        }
    }
    return result;
}

/** @brief Test the signed halfword HP remaining after pending damage.
 *  @note The follow-up rule uses this inlined boolean before granting another hit.
 *  @address 0x08048690, 0x08048A68; inline expansions
 */
static inline u8 Battle_WouldPendingDamageDefeatInline(const struct BattleCombatant *combatant)
{
    const s16 *hp = (const s16 *)&combatant->hp;
    const s16 *damage = &combatant->pendingDamage;
    s32 remaining = *hp - *damage;

    if ((s16)remaining <= 0)
        return 1;
    return 0;
}

/** @brief Roll a party attack continuation within its weapon-variant hit limit.
 *  @note Equipment increases the byte chance; random % 100 must be strictly below it. A nonpositive
 *  signed-halfword HP minus pending damage rejects the continuation.
 *  @address 0x08048690
 */
u32 Battle_RollPartyAttackFollowup(const struct BattleCombatant *attacker, const struct BattleCombatant *target, u8 hitCount)
{
    u32 followup = 0;
    const struct BattleCharacterActionInfo *character = &gBattleCharacterActionInfo[attacker->id];
    u8 chance = character->followupChances[hitCount];
    const u8 *weapons;
    u8 variant;

    if (BattleItem_FindEquipmentByType(attacker, 6) >= 0)
        chance += 50;
    else if (BattleItem_FindEquipmentByType(attacker, 7) >= 0)
        chance += 30;
    else if (BattleItem_FindEquipmentByType(attacker, 5) >= 0)
        chance += 25;
    weapons = (const u8 *)gBattleWeaponActionInfo;
    variant = weapons[attacker->equipment[0] * sizeof(struct BattleWeaponActionInfo)
        + offsetof(struct BattleWeaponActionInfo, actionVariant)];
    if (hitCount < character->maxHitCounts[variant]
        && (u8)(Battle_NextRandom() % 100) < chance
        && !Battle_WouldPendingDamageDefeatInline(target))
        followup = 1;
    return followup;
}

/** @brief Resolve a learned slot or return a direct zero-based skill id.
 *  @note Selections 0..7 index the eight learned slots; every other byte is
 *  returned unchanged, including the FF sentinel. This does not test usability.
 *  @address 0x08048764
 */
u8 Battle_GetSelectedSkill(const struct BattleCombatant *combatant)
{
    u8 selection = combatant->skillSelection;

    if (selection < BATTLE_SKILL_COUNT)
        return combatant->skills[selection];
    return selection;
}

/** @brief Read the persistent level associated with a battle party slot.
 *  @note Returns the zero-extended byte in a full-width result; ids zero and one share record zero.
 *  @address 0x0804877C
 */
u32 Battle_GetPartySlotLevel(u8 slot)
{
    u8 characterIndex = Battle_GetPartySlotCharacterId(slot);

    if (characterIndex != 0)
        characterIndex--;
    return gPartyCharacters[characterIndex].level;
}

/** @brief Resolve a battle party slot through its persistent formation slot.
 *  @note Uses BattleCombatant.formationSlot to index gPartyCharacterSlots.
 *  @address 0x080487A4
 */
u8 Battle_GetPartySlotCharacterId(u8 slot)
{
    struct BattleCombatant *combatants = Battle_GetCombatants();

    return gPartyCharacterSlots[combatants[slot].formationSlot];
}

/** @brief Locate a current party member in the persistent formation slots.
 *  @note Encounters 0xA1 and 0xA7 force slot two; an unsuccessful search returns zero.
 *  @address 0x080487CC
 */
u8 Battle_GetPartyMemberFormationSlot(u8 memberIndex)
{
    u8 encounterId = Battle_GetEncounterId();
    u8 slot;

    if (encounterId == 0xA1 || encounterId == 0xA7)
        return 2;

    for (slot = 0; slot < PARTY_MEMBER_SLOT_COUNT; slot++)
    {
        if (gPartyCharacterSlots[slot] == gPartyMembers[memberIndex])
            return slot;
    }
    return 0;
}

/** @brief Calculate a battle member's base stat at its persistent level.
 *  @note Passes the resolved character id and level to Party_GetBaseStat.
 *  @address 0x08048818
 */
u32 Battle_GetPartySlotBaseStat(u8 slot, u8 stat)
{
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 characterIndex = gPartyCharacterSlots[combatants[slot].formationSlot];
    u8 characterId = characterIndex;

    if (characterIndex != 0)
        characterIndex--;
    return Party_GetBaseStat(characterId, gPartyCharacters[characterIndex].level, stat);
}

/** @brief Report whether a skill is absent from the member's persistent skill list.
 *  @note Returns one when absent and zero when found among eight entries.
 *  @address 0x08048868
 */
u8 Battle_IsPartySlotMissingSkill(u8 slot, u8 skillId)
{
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 characterIndex = gPartyCharacterSlots[combatants[slot].formationSlot];
    u8 skillSlot;

    if (characterIndex != 0)
        characterIndex--;
    for (skillSlot = 0; skillSlot < PARTY_SKILL_SLOT_COUNT; skillSlot++)
    {
        if (gPartyCharacters[characterIndex].skills[skillSlot] == skillId)
            break;
    }
    if (skillSlot < PARTY_SKILL_SLOT_COUNT)
        return 0;
    return 1;
}

/** @brief Resolve an affordable learned skill or a full-gauge special selection.
 *  @note Ids above 47 select the stored special skill once the gauge exceeds 31; FF means unavailable.
 *  @address 0x080488CC
 */
u8 Battle_FindUsableSkillSelection(const struct BattleCombatant *combatant, u8 skillId)
{
    u8 selection = BATTLE_NO_SKILL;
    u8 found = 0;

    if (skillId < PARTY_LEARNABLE_SKILL_COUNT)
    {
        u8 slot;

        for (slot = 0; slot < BATTLE_SKILL_COUNT; slot++)
        {
            if (combatant->skills[slot] != BATTLE_NO_SKILL
                && combatant->skills[slot] == skillId)
            {
                found = 1;
                break;
            }
        }
        if (found != 0 && Battle_CanAffordSkillSlot(combatant, slot))
            selection = slot;
    }
    else if (combatant->source.party.specialGauge > 31)
        selection = combatant->source.party.specialSkill;
    return selection;
}

/** @brief Get a learned skill slot's MP cost after equipment reductions.
 *  @note Skill ids are zero-based. The two-point reduction wraps as a byte
 *  before the half-cost equipment effect is applied; do not clamp it.
 *  @address 0x08048934
 */
u8 Battle_GetSkillSlotMpCost(const struct BattleCombatant *combatant, u8 skillSlot)
{
    u8 skillId = combatant->skills[skillSlot];
    const u8 *skills = (const u8 *)gPartySkillTable;
    s32 offset = skillId * sizeof(struct PartySkillInfo)
        + offsetof(struct PartySkillInfo, mpCost);
    u8 cost = skills[offset];

    if (BattleItem_FindEquipmentByProperty(combatant,
            BATTLE_ITEM_PROPERTY_SKILL_MP_COST, BATTLE_EQUIPMENT_MP_COST_MINUS_TWO) >= 0)
        cost -= 2;
    if (BattleItem_FindEquipmentByProperty(combatant,
            BATTLE_ITEM_PROPERTY_SKILL_MP_COST, BATTLE_EQUIPMENT_MP_COST_HALF) >= 0)
        cost /= 2;
    return cost;
}

/** @brief Read the effect-type nibble of a learned skill.
 *  @note Uses the selected skill id's five-byte ROM record, byte two low nibble.
 *  @address 0x08048984
 */
u8 Battle_GetSkillSlotEffectType(const struct BattleCombatant *combatant, u8 skillSlot)
{
    u8 skillId = combatant->skills[skillSlot];
    const u8 *skills = (const u8 *)gPartySkillTable;
    s32 offset = skillId * sizeof(struct PartySkillInfo)
        + offsetof(struct PartySkillInfo, effectAndFlags);

    return skills[offset] & PARTY_SKILL_EFFECT_MASK;
}

/** @brief Resolve a learned slot or direct skill id and read its target-mode nibble.
 *  @note Reads byte one of PartySkillInfo; the caller supplies a valid selection.
 *  The high character-group nibble is excluded from the result.
 *  @address 0x080489A4
 */
u8 Battle_GetSkillTargetMode(const struct BattleCombatant *combatant, u8 selection)
{
    const u8 *skills;
    s32 offset;

    if (selection < BATTLE_SKILL_COUNT)
        selection = combatant->skills[selection];
    skills = (const u8 *)gPartySkillTable;
    offset = selection * sizeof(struct PartySkillInfo)
        + offsetof(struct PartySkillInfo, characterGroupBits);
    return skills[offset] & 0xF;
}

/** @brief Limit an MP recovery amount to the missing points.
 *  @note Compares maxMp - mp as a signed value, then returns a halfword.
 *  The usual precondition is mp <= maxMp; an overfull record wraps on return.
 *  @address 0x080489C8
 */
u16 Battle_LimitMpRecovery(const struct BattleCombatant *combatant, u16 amount)
{
    s32 missing = combatant->maxMp - combatant->mp;

    if (missing < amount)
        return missing;
    return amount;
}

/**
 * @brief 按阵营范围与状态掩码枚举角色编号
 * @note 分别扫描队伍槽 0..4 或敌人槽 5..11，将符合条件的编号写入第二参数缓冲。
 * @address 0x080489E8
 */
u8 Battle_ListCombatants(struct BattleCombatant *combatants, u8 *slots, u8 side, u16 statusMask)
{
    return Battle_ListCombatantsInline(combatants, slots, side, statusMask);
}

/** @brief Test whether pending damage leaves a nonpositive signed HP value.
 *  @note The HP-minus-damage result is truncated to a signed halfword before comparison.
 *  @address 0x08048A68
 */
u8 Battle_WouldPendingDamageDefeat(const struct BattleCombatant *combatant)
{
    const s16 *hp = (const s16 *)&combatant->hp;
    const s16 *damage = &combatant->pendingDamage;
    s32 remaining = *hp - *damage;

    if ((s16)remaining <= 0)
        return 1;
    return 0;
}

/** @brief Recursively sort a combatant-slot range in descending order.
 *  @note Uses the shared context installed by Battle_SortCombatantSlots.
 *  @address 0x08048A88
 */
void Battle_QuicksortCombatantSlots(u8 *slots, s8 first, s8 last)
{
    Battle_QuicksortRangeInline(slots, first, last);
}

/** @brief Sort combatant slots by a descending record-field key.
 *  @note Uses the rules module's shared sort context; equal keys are not stable.
 *  Counts zero and one leave both the slot list and shared context unchanged.
 *  @address 0x08048ACC
 */
void Battle_SortCombatantSlots(u8 *slots, u8 count, u8 sortKey)
{
    Battle_SortCombatantSlotsInline(slots, count, sortKey);
}

/** @brief Initialize the shared stat-boost presentation parameters.
 *  @note Resets only the boost phase before storing its stat, percentage and animation id.
 *  @address 0x08048B30
 */
void Battle_BeginStatBoost(u8 stat, u8 percent, u16 animationId)
{
    sBattleStatBoostPhase = 0;
    sBattleStatBoostStat = stat;
    sBattleStatBoostPercent = percent;
    sBattleStatBoostAnimationId = animationId;
}

/** @brief Set the party gauge, honoring accessory 0xB3's full-gauge rule.
 *  @note Either accessory slot can force the gauge to 32.
 *  @address 0x08048B5C
 */
void Battle_SetSpecialGauge(struct BattleCombatant *combatant, u8 gauge)
{
    if (combatant->equipment[4] == 0xB3 || combatant->equipment[5] == 0xB3)
        combatant->source.party.specialGauge = 32;
    else
        combatant->source.party.specialGauge = gauge;
}

/** @brief Read the equipped weapon's battle-action variant.
 *  @note Unlike animation setup, this query reads only the low byte.
 *  @address 0x08048B88
 */
u8 Battle_GetWeaponActionVariant(const struct BattleCombatant *combatant)
{
    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
    {
        const u8 *weapons = (const u8 *)gBattleWeaponActionInfo;

        return weapons[combatant->equipment[0] * sizeof(struct BattleWeaponActionInfo)];
    }
    return 0;
}

/** @brief Read the weapon's additional attack-effect selector.
 *  @note Non-party ids return zero; selectors five/six trigger HP/MP recovery.
 *  @address 0x08048BAC
 */
u8 Battle_GetWeaponAttackEffect(const struct BattleCombatant *combatant)
{
    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
        return gBattleWeaponActionInfo[combatant->equipment[0]].attackEffect;
    return 0;
}

/** @brief Set the party width used to space the attack approach.
 *  @note Character five uses eight only when unarmed; enemy records are unchanged.
 *  Separate cases preserve the original eleven-entry jump table.
 *  @address 0x08048BD0
 */
void Battle_SetPartyAttackSpacingWidth(struct BattleCombatant *combatant)
{
    switch (combatant->id)
    {
    case 0: combatant->attackSpacingWidth = 16; break;
    case 1: combatant->attackSpacingWidth = 16; break;
    case 2: combatant->attackSpacingWidth = 16; break;
    case 3: combatant->attackSpacingWidth = 16; break;
    case 4: combatant->attackSpacingWidth = 16; break;
    case 5:
        if (combatant->equipment[0] == 0)
            combatant->attackSpacingWidth = 8;
        else
            combatant->attackSpacingWidth = 16;
        break;
    case 6: combatant->attackSpacingWidth = 16; break;
    case 7: combatant->attackSpacingWidth = 16; break;
    case 8: combatant->attackSpacingWidth = 16; break;
    case 9: combatant->attackSpacingWidth = 16; break;
    case 10: combatant->attackSpacingWidth = 16; break;
    }
}

/** @brief Test whether the combatant has commands that silence can block.
 *  @note Does not test the current status. Party ids 0..10 qualify; ordinary
 *  enemies qualify when either action category is magical. Other ids return zero.
 *  @address 0x08048C30
 */
u8 Battle_HasSilenceBlockedCommands(const struct BattleCombatant *combatant)
{
    u8 result = 0;

    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
    {
        switch (combatant->id)
        {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
            result = 1;
            break;
        case 8: case 9: case 10:
            result = 1;
            break;
        }
    }
    else if ((u8)(combatant->id - 12) <= 100)
    {
        const u8 *enemies = (const u8 *)gBattleEnemyActionInfo;
        s32 offset = (combatant->id - 12) * sizeof(struct BattleEnemyActionInfo);
        const u8 *attackFields = enemies + offsetof(struct BattleEnemyActionInfo, attackCategory);
        u8 attackCategory = attackFields[offset];
        u8 skillCategory;

        enemies += offsetof(struct BattleEnemyActionInfo, skillCategory);
        skillCategory = enemies[offset];

        if (attackCategory == BATTLE_ENEMY_ACTION_MAGICAL
            || skillCategory == BATTLE_ENEMY_ACTION_MAGICAL)
            result = 1;
    }
    return result;
}

/** @brief Roll recovery from a status selected by the turn-end caller.
 *  @note Uses random modulo 100 and an unclamped threshold. The party's
 *  chanceAdjustment byte is added to the side/status recovery bonuses.
 *  @address 0x08048C80
 */
u8 Battle_RollTurnEndStatusRecovery(const struct BattleCombatant *combatant)
{
    u8 result = 0;
    u8 slots[BATTLE_ENEMY_COUNT];
    u16 sideBonus;
    u16 chanceAdjustment;
    u16 statusBonus;
    u16 threshold;

    Battle_GetCombatants();
    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
    {
        sideBonus = 5;
        chanceAdjustment = combatant->source.party.chanceAdjustment;
    }
    else
    {
        sideBonus = 20;
        chanceAdjustment = 0;
    }
    switch (combatant->status)
    {
    case BATTLE_STATUS_PARALYZED: statusBonus = 5; break;
    case BATTLE_STATUS_ASLEEP: statusBonus = 20; break;
    case BATTLE_STATUS_CONFUSED: statusBonus = 10; break;
    default: statusBonus = 20; break;
    }
    threshold = sideBonus + (statusBonus + chanceAdjustment);
    if ((u16)(Battle_NextRandom() % 100) < threshold)
        result = 1;
    return result;
}

/** @brief Select the palette variant associated with party ATC/DEF modifiers.
 *  @note ATC takes precedence; item actions use the second pair. Negative
 *  modifiers qualify too: the checks are nonzero, not greater than zero.
 *  @address 0x08048CEC
 */
u8 Battle_GetStatModifierPaletteVariant(const struct BattleCombatant *combatant)
{
    u8 variant = BATTLE_MODIFIER_PALETTE_DEFAULT;

    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
    {
        if (combatant->action != BATTLE_ACTION_ITEM)
        {
            if ((u16)combatant->atcModifier != 0)
                variant = BATTLE_MODIFIER_PALETTE_ATC;
            else if ((u16)combatant->defModifier != 0)
                variant = BATTLE_MODIFIER_PALETTE_DEF;
        }
        else
        {
            if ((u16)combatant->atcModifier != 0)
                variant = BATTLE_MODIFIER_PALETTE_ITEM_ATC;
            else if ((u16)combatant->defModifier != 0)
                variant = BATTLE_MODIFIER_PALETTE_ITEM_DEF;
        }
    }
    return variant;
}

/** @brief Clear the party combatant's five signed stat modifiers.
 *  @note Used when fainting; enemy records, status and special gauge are unchanged.
 *  @address 0x08048D40
 */
void Battle_ClearPartyStatModifiers(struct BattleCombatant *combatant)
{
    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
    {
        combatant->atcModifier = 0;
        combatant->defModifier = 0;
        combatant->aglModifier = 0;
        combatant->menModifier = 0;
        combatant->resModifier = 0;
    }
}

/** @brief 将 HP 恢复量限制为角色缺少的 HP。
 *  @note 按 u32 返回，缺少的 HP 小于 amount 时仍保留原来的 u16 截断；
 *  助手机器码不变，使恢复调用方按原顺序进行半字加法。
 *  @address 0x08048D64
 */
u32 Battle_LimitHpRecovery(const struct BattleCombatant *combatant, u16 amount)
{
    s32 missing = combatant->maxHp - combatant->hp;

    if (missing < amount)
        return (u16)missing;
    return amount;
}

/** @brief Test the five-column combined formation-distance limit.
 *  @note Adds the two low formation nibbles and accepts sums at most five.
 *  Ordinary C matches all 0x1E instruction bytes. The following victory
 *  initializer supplies the original two zero bytes through function alignment.
 *  @address 0x08048D84
 */
u8 Battle_AreFormationColumnsInRange(const struct BattleCombatant *first, const struct BattleCombatant *second)
{
    s32 distance = (first->formationBits & BATTLE_FORMATION_COLUMN_MASK)
        + (second->formationBits & BATTLE_FORMATION_COLUMN_MASK);

    if (distance <= 5)
        return 1;
    return 0;
}

/** @brief Locate a result-message stream and reset its glyph cursors.
 *  @note The reward dispatcher retains the message base across the scan.
 */
static inline const u16 *BattleResult_SelectMessageInline(u8 messageIndex)
{
    u16 index = 0;
    u16 message = 0;
    const u16 *messages = D_0839B2E0;
    const u16 *selected;
    u8 *textIndex = &sBattleResultTextIndex;
    u8 *textColumn = &sBattleResultTextColumn;
    u8 *numberDigit = &sBattleResultNumberDigit;
    u8 *insertIndex = &sBattleResultInsertIndex;

    while (message < messageIndex)
    {
        if (D_0839B2E0[index] == SCRIPT_TEXT_END)
            message++;
        index++;
    }
    *textIndex = 0;
    *textColumn = 0;
    *numberDigit = 0;
    *insertIndex = 0;
    selected = (const u16 *)(index * sizeof(u16) + (u32)messages);
    Script_CacheGlyphsFromWords(selected, 0x6F1E);
    Battle_SetControlFlags(0x400);
    return selected;
}

/** @brief Victory, defeat, reward presentation and turn-end resolution.
 *  @note 27 ROM entries use matching C.
 *  @note The main battle phase remains owned by battle.
 */

/** @brief Initialize victory messages, counters and window resources.
 *  @note The original writes zero through the previously saved message pointer before replacing it.
 *  @address 0x08048DA4
 */
void BattleResult_InitVictory(void)
{
    u8 index;

    sBattleResultPartySlot = 0;
    sBattleResultTextIndex = 0;
    sBattleResultTextColumn = 0;
    sBattleResultNumberDigit = 0;
    sBattleResultInsertIndex = 0;
    *(u16 *)sBattleResultMessage = 0;
    sBattleResultNextPhase = 0;
    sBattleResultNumber = 0;
    sBattleResultLevelUpMask = 0;
    sBattleResultStatGain = 0;
    sBattleResultLearnedSkillCount = 0;
    sBattleResultLearnedSkillIndex = 0;
    sBattleResultSkillName = NULL;
    sBattleResultCombatants = NULL;
    sBattleResultPartyCount = 0;
    sBattleResultPartyIndex = 0;
    sBattleResultHighlightPhase = 0;
    sBattleResultHighlightFrame = 0;
    sBattleResultHighlightPalette = 0;
    for (index = 0; index < BATTLE_PARTY_COUNT; index++)
        sBattleResultPartySlots[index] = 0;
    for (index = 0; index < BATTLE_SKILL_COUNT; index++)
        sBattleResultLearnedSkills[index] = 0;
    Battle_ResetWindows();
    Battle_InitWindowTilemap();
    Battle_OpenWindow(0, gBattleWindowTilemap, 11, 2, 2, 1, 0, 28, 4);
    sBattleResultPartySlot = 0;
    sBattleResultPhase = 0;
    sBattleResultMode = BATTLE_RESULT_VICTORY;
    sBattleResultNumber = 0;
    sBattleResultLevelUpMask = 0;
    DmaCopy(3, gBattleMenuScrollArrowTiles, (void *)(VRAM + 0xB7C0),
        sizeof(gBattleMenuScrollArrowTiles), 16);
    DmaWait(3);
    sBattleResultLearnedSkillIndex = 0;
    sBattleResultLearnedSkillCount = 0;
    sBattleResultHighlightPhase = 0;
    sBattleResultHighlightFrame = 0;
}

/** @brief Advance the current member's level-up palette highlight.
 *  @note Uses an outward tint and a timed return to the saved OBJ colors.
 *  @address 0x08048F0C
 */
void BattleResult_UpdateLevelUpHighlight(void)
{
    switch (sBattleResultHighlightPhase)
    {
    case 0:
        break;
    case 1:
        BattlePalette_StartObjFade(sBattleResultHighlightPalette, 1, 16, 28,
            31, 4, 4, -1, 2);
        sBattleResultHighlightFrame = 0;
        Sound_PlaySfx(0x18, 0, 0);
        sBattleResultHighlightPhase = 2;
        break;
    case 2:
        if (sBattleResultHighlightFrame < 4)
            sBattleResultHighlightFrame++;
        else
        {
            BattlePalette_ReturnObjFade(sBattleResultHighlightPalette, 1, 16);
            sBattleResultHighlightFrame = 0;
            sBattleResultHighlightPhase = 3;
        }
        break;
    case 3:
        if (sBattleResultHighlightFrame < 16)
            sBattleResultHighlightFrame++;
        else
        {
            sBattleResultHighlightFrame = 0;
            sBattleResultHighlightPhase = 0;
        }
        break;
    }
}

/** @brief Advance victory animations, growth messages and rewards.
 *  @note Returns one after the result window closes; the scene owns the main battle phase.
 *  @address 0x08048FB8
 */
u8 BattleResult_UpdateVictory(void)
{
    u8 partySlots[BATTLE_PARTY_COUNT];
    u8 finished = 0;

    switch (sBattleResultPhase)
    {
    case 0:
        if ((u8)Battle_GetWindowState(0) == BATTLE_WINDOW_OPEN)
        {
            sBattleResultCombatants = Battle_GetCombatants();
            sBattleResultPhase = 1;
            sBattleResultPartyCount = Battle_ListCombatants(sBattleResultCombatants,
                sBattleResultPartySlots, BATTLE_SIDE_PARTY, 0x7F);
            sBattleResultPartyIndex = 0;
        }
        sBattleResultFrame = 0;
        break;
    case 1:
        if (sBattleResultPartyIndex < sBattleResultPartyCount)
        {
            u8 slot;
            struct BattleCombatant *combatant = &sBattleResultCombatants[slot = sBattleResultPartySlots[sBattleResultPartyIndex]];

            Battle_SetPartyAnimation(combatant, 9,
                0x1B4 + slot * 60, combatant->animation.paletteSlot, 0);
            sBattleResultPhase = 2;
        }
        else
        {
            u8 initialIndex;

            sBattleResultPhase = 15;
            initialIndex = 0;
            sBattleResultMessage = BattleResult_SelectMessage(initialIndex);
            sBattleResultNumber = Battle_GetDefeatedEnemyExperience();
            Battle_ListCombatants(Battle_GetCombatants(), partySlots, BATTLE_SIDE_PARTY, 0x7F);
            sBattleResultLevelUpMask = Battle_AwardExperience(sBattleResultNumber);
            sBattleResultPartyIndex = initialIndex;
        }
        break;
    case 2:
        if (!(sBattleResultCombatants[sBattleResultPartySlots[sBattleResultPartyIndex]].animation.flags
            & BATTLE_ANIM_LOADING))
        {
            sBattleResultPartyIndex++;
            sBattleResultPhase = 1;
        }
        break;
    case 3:
    case 13:
        sBattleResultPhase = BattleResult_AdvanceVictoryMessage(sBattleResultPhase);
        break;
    case 4:
        sBattleResultHighlightPalette = sBattleResultCombatants[sBattleResultPartySlot].animation.paletteSlot;
        sBattleResultHighlightPhase = 1;
        /* Fall through to the growth-message setup. */
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
        sBattleResultNextPhase = BattleResult_AdvanceVictoryMessage(sBattleResultPhase);
        sBattleResultMessage = BattleResult_SelectMessage(sBattleResultPhase - 3);
        sBattleResultPhase = 14;
        break;
    case 12:
        sBattleResultNextPhase = BattleResult_AdvanceVictoryMessage(sBattleResultPhase);
        sBattleResultMessage = BattleResult_SelectMessage(12);
        sBattleResultPhase = 14;
        break;
    case 14:
        if (BattleResult_DrawMessageGlyph(sBattleResultNumber, 2, 1))
        {
            sBattleResultNumber = sBattleResultStatGain;
            sBattleResultPhase = 20;
        }
        break;
    case 15:
    case 17:
        if (BattleResult_DrawMessageGlyph(sBattleResultNumber, 2, 1))
        {
            sBattleResultNextPhase = BattleResult_AdvanceVictoryMessage(sBattleResultPhase);
            sBattleResultPhase = 20;
        }
        break;
    case 16:
        sBattleResultNextPhase = BattleResult_AdvanceVictoryMessage(sBattleResultPhase);
        sBattleResultMessage = BattleResult_SelectMessage(10);
        sBattleResultPhase = 14;
        break;
    case 18:
        sBattleResultNextPhase = BattleResult_AdvanceVictoryMessage(sBattleResultPhase);
        sBattleResultMessage = BattleResult_SelectMessage(17);
        sBattleResultPhase = 14;
        break;
    case 19:
        if ((u8)Battle_GetWindowState(0) == 0)
        {
            Battle_HideWindowLayer();
            finished = 1;
        }
        break;
    case 20:
        if (Battle_GetInput() & BATTLE_INPUT_PRESS_A)
        {
            BattleResult_ClearContinueArrow(gBattleWindowTilemap);
            BattleResult_ClearMessageArea();
            sBattleResultPhase = sBattleResultNextPhase;
        }
        else
            BattleResult_DrawContinueArrow(gBattleWindowTilemap);
        break;
    case 21:
        Battle_CloseWindow(0, 4, 2);
        sBattleResultPhase = 19;
        break;
    }
    Battle_UpdateWindows();
    BattleResult_UpdateLevelUpHighlight();
    return finished;
}

/** @brief Emit one result-message glyph or one substitution character.
 *  @note The fixed tilemap view preserves address materialization; substitutions retain their token until complete.
 *  @address 0x080492C0
 */
u8 BattleResult_DrawMessageGlyph(u16 number, u8 x, u8 y)
{
    u16 *tilemap = &BATTLE_RESULT_TILEMAP[y * 32 + x];
    u8 finished = 0;
    u16 *glyphs;

    tilemap += sBattleResultTextColumn;
    if (!(Battle_GetControlFlags() & 0x400))
    {
        if (sBattleResultMessage[sBattleResultTextIndex] != SCRIPT_TEXT_END)
        {
            switch (sBattleResultMessage[sBattleResultTextIndex])
            {
            default:
            {
                u16 count = Script_GetGlyphCache(&glyphs);

                if (sBattleResultMessage[sBattleResultTextIndex] < 0xE0)
                {
                    if ((sBattleResultMessage[sBattleResultTextIndex] & 0xFF) != 0)
                    {
                        tilemap[0] = (sBattleResultMessage[sBattleResultTextIndex] & 0xFF) * 2 - 0x5000;
                        tilemap[32] = (sBattleResultMessage[sBattleResultTextIndex] & 0xFF) * 2 - 0x4FFF;
                    }
                    else
                    {
                        tilemap[0] = (sBattleResultMessage[sBattleResultTextIndex] & 0xFF) + 0xB001;
                        tilemap[32] = (sBattleResultMessage[sBattleResultTextIndex] & 0xFF) + 0xB001;
                    }
                }
                else
                {
                    u16 index;

                    for (index = 0; index < count; index++)
                    {
                        if (sBattleResultMessage[sBattleResultTextIndex] == glyphs[index])
                            break;
                    }
                    index += 0xE0;
                    sBattleResultCachedGlyph = index;
                    tilemap[0] = index * 2 - 0x5000;
                    tilemap[32] = index * 2 - 0x4FFF;
                }
                sBattleResultTextIndex++;
                break;
            }
            case 0x6E4:
                if (BattleResult_DrawNumberDigit(tilemap, number))
                    sBattleResultTextIndex++;
                break;
            case 0x6E0:
                if (BattleResult_DrawPartyNameGlyph(tilemap,
                    Battle_GetPartySlotCharacterId(sBattleResultPartySlot)))
                    sBattleResultTextIndex++;
                break;
            case 0x6E3:
                if (BattleResult_DrawSkillNameGlyph(tilemap))
                    sBattleResultTextIndex++;
                break;
            case 0x6E6:
                if (BattleResult_DrawItemNameGlyph(tilemap))
                    sBattleResultTextIndex++;
                break;
            case 0x6E5:
                if (BattleResult_DrawNumberDigit(tilemap,
                    sBattleResultItemDrops[sBattleResultItemDropIndex].count))
                    sBattleResultTextIndex++;
                break;
            case 0x6E1:
                if (BattleResult_DrawCardNameGlyph(tilemap))
                    sBattleResultTextIndex++;
                break;
            }
            sBattleResultTextColumn++;
        }
        else
            finished = 1;
    }
    return finished;
}

/** @brief Select the next growth or reward message phase.
 *  @note Item and card row pointers remain owned by battle_items.
 *  @address 0x080494F0
 */
u8 BattleResult_AdvanceVictoryMessage(u8 phase)
{
    switch (phase)
    {
    case 3:
        if ((sBattleResultLevelUpMask >> sBattleResultPartySlot) & 1)
        {
            sBattleResultNumber = Battle_GetPartySlotLevel(sBattleResultPartySlot) + 1;
            phase = 4;
        }
        else if (sBattleResultPartySlot < BATTLE_PARTY_COUNT)
            phase = 13;
        else
        {
            sBattleResultMessage = BattleResult_SelectMessageInline(11);
            sBattleResultNumber = Battle_AwardDefeatedEnemyGold();
            phase = 17;
        }
        break;
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
        phase = BattleResult_FindNextStatGain(phase);
        if (phase == 12)
            phase = 13;
        if (phase == 13)
        {
            sBattleResultLearnedSkillCount = Battle_CollectNewlyLearnedSkills(sBattleResultPartySlot,
                sBattleResultLearnedSkills);
            if (sBattleResultLearnedSkillCount > 0)
            {
                if (sBattleResultLearnedSkillCount <= 7)
                {
                    phase = 12;
                    sBattleResultLearnedSkillIndex = 0;
                }
            }
        }
        break;
    case 12:
        sBattleResultSkillName = BattleResult_GetSkillName(
            sBattleResultLearnedSkills[sBattleResultLearnedSkillIndex]);
        sBattleResultLearnedSkillIndex++;
        if (sBattleResultLearnedSkillIndex >= sBattleResultLearnedSkillCount)
            phase = 13;
        break;
    case 13:
        sBattleResultPartySlot++;
        phase = 3;
        break;
    case 15:
        sBattleResultPartySlot = 0;
        phase = 3;
        break;
    case 16:
        sBattleResultItemDropIndex++;
        if (sBattleResultItemDropIndex < (s8)(sBattleResultItemDropCount - 1))
        {
            sBattleResultInsertIndex = 0;
            phase = 16;
        }
        else
        {
            sBattleResultCardDropCount = BattleItem_AwardCardDrops(&sBattleResultCardDrops);
            if (sBattleResultCardDropCount != 0)
            {
                BattleResult_CacheCardNameGlyphs();
                sBattleResultCardDropIndex = -1;
                sBattleResultInsertIndex = 0;
                phase = 18;
            }
            else
                phase = 21;
        }
        break;
    case 17:
        sBattleResultItemDropCount = BattleItem_AwardItemDrops(&sBattleResultItemDrops);
        sBattleResultItemDropIndex = -1;
        sBattleResultInsertIndex = 0;
        if (sBattleResultItemDropCount != 0)
            phase = 16;
        else
        {
            sBattleResultCardDropCount = BattleItem_AwardCardDrops(&sBattleResultCardDrops);
            if (sBattleResultCardDropCount != 0)
            {
                BattleResult_CacheCardNameGlyphs();
                sBattleResultCardDropIndex = -1;
                sBattleResultInsertIndex = 0;
                phase = 18;
            }
            else
                phase = 21;
        }
        break;
    case 18:
        sBattleResultCardDropIndex++;
        if (sBattleResultCardDropIndex < (s8)(sBattleResultCardDropCount - 1))
        {
            sBattleResultInsertIndex = 0;
            phase = 18;
        }
        else
            phase = 21;
        break;
    }
    return phase;
}

/** @brief Emit one decimal digit as two vertically adjacent font tiles.
 *  @note The shared digit cursor suppresses leading zeroes and returns one at completion.
 *  @address 0x080497B0
 */
u8 BattleResult_DrawNumberDigit(u16 *tilemap, u16 value)
{
    u16 digits[5];
    u8 index;

    digits[0] = value / 10000;
    digits[1] = (value - digits[0] * 10000) / 1000;
    digits[2] = (value - (digits[0] * 10000 + digits[1] * 1000)) / 100;
    digits[3] = (value - (digits[0] * 10000 + digits[1] * 1000 + digits[2] * 100)) / 10;
    digits[4] = value - (digits[0] * 10000 + digits[1] * 1000
        + digits[2] * 100 + digits[3] * 10);
    for (index = 0; index < 5; index++)
    {
        if (digits[index] != 0)
            break;
    }
    if (index > 4 && value == 0)
        index = 0;
    index += sBattleResultNumberDigit;
    tilemap[0] = digits[index] * 2 - 0x4EBC;
    tilemap[32] = digits[index] * 2 - 0x4EBB;
    sBattleResultNumberDigit++;
    if (value == 0 || index > 3)
        return 1;
    return 0;
}

/** @brief Emit one glyph of the current item-drop name.
 *  @note Computes the next name offset before reading the shared insertion cursor.
 *  @address 0x080498E0
 */
u8 BattleResult_DrawItemNameGlyph(u16 *tilemap)
{
    const u8 *names = (const u8 *)gMenuItemNames;
    u8 glyph = names[sBattleResultItemDrops[sBattleResultItemDropIndex].itemId * 8
        + sBattleResultInsertIndex];

    tilemap[0] = glyph * 2 - 0x5000;
    tilemap[32] = glyph * 2 - 0x4FFF;
    sBattleResultInsertIndex++;
    {
        s32 offset = sBattleResultItemDrops[sBattleResultItemDropIndex].itemId * 8;

        if (names[offset + sBattleResultInsertIndex] != 0 && sBattleResultInsertIndex < 8)
            return 0;
        return 1;
    }
}

/** @brief Emit one glyph of the current card-drop name.
 *  @note Encounter flags select byte or halfword names; high glyphs use the cache and both paths share completion.
 *  @address 0x08049958
 */
u8 BattleResult_DrawCardNameGlyph(u16 *tilemap)
{
    u16 glyph;
    u16 *glyphs;

    if (Battle_GetControlFlags() & BATTLE_CONTROL_SPECIAL_ENCOUNTER)
    {
        const u16 *names = D_0839D348;

        glyph = names[sBattleResultInsertIndex
            + sBattleResultCardDrops[sBattleResultCardDropIndex].cardIndex * 9];
    }
    else
    {
        const u8 *names = gEnemyNames;

        glyph = names[sBattleResultInsertIndex
            + sBattleResultCardDrops[sBattleResultCardDropIndex].cardIndex * 9];
    }
    if ((Battle_GetControlFlags() & BATTLE_CONTROL_SPECIAL_ENCOUNTER) && glyph >= 0xE0)
    {
        u16 count = Script_GetGlyphCache(&glyphs);
        u16 index = 0;

        for (index = 0; index < count; index++)
        {
            if (glyph == glyphs[index])
                break;
        }
        glyph = index + 0xE0;
    }
    if (glyph != 0)
    {
        tilemap[0] = glyph * 2 - 0x5000;
        tilemap[32] = glyph * 2 - 0x4FFF;
    }
    else
    {
        tilemap[0] = 0xB001;
        tilemap[32] = 0xB001;
    }
    sBattleResultInsertIndex++;
    if (Battle_GetControlFlags() & BATTLE_CONTROL_SPECIAL_ENCOUNTER)
    {
        const u16 *names = D_0839D348;

        if (names[sBattleResultCardDrops[sBattleResultCardDropIndex].cardIndex * 9
            + sBattleResultInsertIndex] < SCRIPT_TEXT_END)
            goto incomplete;
        goto complete;
    }
    else
    {
        const u8 *names = gEnemyNames;

        if (names[sBattleResultCardDrops[sBattleResultCardDropIndex].cardIndex * 9
            + sBattleResultInsertIndex] >= 0xFF)
        {
complete:
            return 1;
        }
    }
incomplete:
    return 0;
}

/** @brief Find and apply the next positive stat increase to display.
 *  @note Stores the full halfword gain while passing its low byte to the stat writer.
 *  @address 0x08049AD8
 */
u8 BattleResult_FindNextStatGain(u8 phase)
{
    u16 remaining;
    u8 index;

    phase++;
    remaining = 13 - phase;
    for (index = 0; index < remaining; index++)
    {
        u8 stat;
        u16 oldStat = Battle_GetPartySlotStoredStat(sBattleResultPartySlot,
            stat = phase + 0xFB + index);
        u16 newStat = Battle_GetPartySlotBaseStat(sBattleResultPartySlot, stat);

        sBattleResultStatGain = newStat - oldStat;
        if ((s16)sBattleResultStatGain > 0)
        {
            Battle_AddPartySlotStatGain(sBattleResultPartySlot, stat, sBattleResultStatGain);
            break;
        }
    }
    return phase + index;
}

/** @brief Emit one glyph and advance the skill-name stream.
 *  @note Returns one when the following halfword is the 0x0F00 terminator.
 *  @address 0x08049B70
 */
u8 BattleResult_DrawSkillNameGlyph(u16 *tilemap)
{
    u16 glyph = *sBattleResultSkillName;
    u16 *glyphs;
    u16 count = Script_GetGlyphCache(&glyphs);

    if (glyph < 0xE0)
    {
        u16 character = glyph & 0xFF;

        tilemap[0] = character * 2 - 0x5000;
        tilemap[32] = character * 2 - 0x4FFF;
    }
    else
    {
        u16 index = 0;

        for (index = 0; index < count; index++)
        {
            if (glyph == glyphs[index])
                break;
        }
        glyph = index + 0xE0;
        tilemap[0] = glyph * 2 - 0x5000;
        tilemap[32] = glyph * 2 - 0x4FFF;
    }
    sBattleResultSkillName++;
    if (*sBattleResultSkillName != SCRIPT_TEXT_END)
        return 0;
    return 1;
}

/** @brief Animate and draw the sliding battle-result banner.
 *  @note Writes the remaining OAM cursor through its argument and returns completion.
 *  @address 0x08049C1C
 */
u8 BattleResult_UpdateBanner(u8 *oamCursor)
{
    u8 finished = 0;
    u8 cursor = *oamCursor;
    u8 affineCursor;

    switch (sBattleResultPhase)
    {
    case 0:
        if (!(sBattleResultBanner.flags & 0x800))
        {
            sBattleResultPhase = 1;
            if (Battle_GetControlFlags() & BATTLE_CONTROL_SPECIAL_ENCOUNTER)
                Sound_PlayBgm(0x37, 0);
            else
                Sound_PlayBgm(0x36, 0);
            Sound_FadeInBgm(10);
        }
        break;
    case 1:
        sBattleResultBanner.x = Battle_InterpolateS16(240, -120, 20, sBattleResultFrame, 2);
        if (sBattleResultFrame > 19)
        {
            sBattleResultFrame = 0;
            sBattleResultPhase = 2;
        }
        else
            sBattleResultFrame++;
        break;
    case 2:
        if (sBattleResultFrame < 50)
            sBattleResultFrame++;
        else
        {
            sBattleResultFrame = 0;
            sBattleResultPhase = 3;
        }
        break;
    case 3:
        sBattleResultBanner.x = Battle_InterpolateS16(120, -120, 8, sBattleResultFrame, 1);
        if (sBattleResultFrame < 8)
            sBattleResultFrame++;
        else
        {
            sBattleResultFrame = 0;
            sBattleResultPhase = 4;
        }
        break;
    case 4:
        finished = 1;
        break;
    }
    sBattleResultBanner.oamStart = cursor;
    *oamCursor = Battle_BuildSpriteAnimation(&sBattleResultBanner, cursor, &affineCursor);
    return finished;
}

/** @brief Upload result-banner graphics or the defeat tilemap.
 *  @note Only banner mode returns a C value; other modes preserve the original fallthrough.
 *  @address 0x08049D58
 */
u8 BattleResult_UploadGraphics(u8 oamCursor)
{
    u8 cursor = oamCursor;

    switch (sBattleResultMode)
    {
    case BATTLE_RESULT_BANNER:
        if (sBattleResultPhase != 0 && !(sBattleResultBanner.flags & 0x800))
            return Battle_UploadAnimationFrame(&sBattleResultBanner, sBattleResultBanner.oamStart);
        return cursor;
    case BATTLE_RESULT_VICTORY:
        break;
    case BATTLE_RESULT_DEFEAT:
        if (sBattleResultPhase > 1)
        {
            DmaCopy(3, gBattleWindowTilemap, BATTLE_WINDOW_TILEMAP_VRAM,
                sizeof(gBattleWindowTilemap), 16);
            DmaWait(3);
        }
        break;
    }
}

/** @brief Locate a defeat message and prepare its glyph cache.
 *  @note The defeat paths scan and select directly from the ROM message array.
 */
static inline const u16 *BattleResult_SelectDefeatMessageInline(u8 messageIndex)
{
    u16 index = 0;
    u16 message = 0;
    const u16 *selected;

    while (message < messageIndex)
    {
        if (D_0839B2E0[index] == SCRIPT_TEXT_END)
            message++;
        index++;
    }
    sBattleResultTextIndex = 0;
    sBattleResultTextColumn = 0;
    sBattleResultNumberDigit = 0;
    sBattleResultInsertIndex = 0;
    selected = (const u16 *)(index * sizeof(u16) + (u32)D_0839B2E0);
    Script_CacheGlyphsFromWords(selected, 0x6F1E);
    Battle_SetControlFlags(0x400);
    return selected;
}

/** @brief Advance the defeat presentation and retry selection.
 *  @note Shares the result phase and window resources with victory presentation.
 *  @address 0x08049DF8
 */
u8 BattleResult_UpdateDefeat(struct BattleCombatant *combatants, struct BattleCombatant *cursor)
{
    u8 result = 0;
    u16 input = Battle_GetInput();
    u8 index;

    switch (sBattleResultPhase)
    {
    case 0:
        if (!(Sound_GetFlags() & 4))
        {
            Sound_PlayBgm(0x17, 0);
            Sound_FadeInBgm(20);
            sBattleResultPhase = 1;
        }
        break;
    case 1:
        if (sBattleResultFrame < 50)
            sBattleResultFrame++;
        else
        {
            cursor->animation.flags |= BATTLE_ANIM_HIDDEN;
            for (index = 0; index < BATTLE_COMBATANT_COUNT; index++)
            {
                if (combatants[index].id != BATTLE_NO_COMBATANT)
                {
                    combatants[index].displayState |= 0x24;
                    combatants[index].animation.flags |= BATTLE_ANIM_HIDDEN;
                }
            }
            *(vu16 *)REG_DISPCNT &= 0xF1FF;
            sBattleResultFrame = 0;
            sBattleResultPhase = 2;
            Blend_StartScanlineEffect(2, -200, 50);
            Battle_InitWindowTilemap();
            sBattleResultMessage = BattleResult_SelectDefeatMessageInline(13);
            sBattleResultTextColumn = 0;
            while (!BattleResult_DrawMessageGlyph(0, 11, 8))
                ;
        }
        break;
    case 2:
        if (sBattleResultFrame < 150)
            sBattleResultFrame++;
        else
        {
            sBattleResultFrame = 0;
            sBattleResultPhase = 3;
            Battle_MoveCursor(cursor, 64, 48, 1, 0);
        }
        break;
    case 3:
        Battle_InitWindowTilemap();
        for (index = 0; index < 2; index++)
        {
            sBattleResultMessage = BattleResult_SelectDefeatMessageInline(index + 14);
            sBattleResultTextColumn = 0;
            while (!BattleResult_DrawMessageGlyph(0, 11, index * 4 + 4))
                ;
        }
        cursor->animation.flags &= 0xFDFF;
        Battle_SetCursorAnimation(cursor, 2);
        cursor->animation.flags |= 0x20;
        sBattleResultPhase = 4;
        break;
    case 4:
        if (!(cursor->displayState & BATTLE_CURSOR_MOTION_MASK))
        {
            if (input & BATTLE_INPUT_REPEAT_UP)
            {
                if (sBattleDefeatSelection == 0)
                    sBattleDefeatSelection = 1;
                else
                    sBattleDefeatSelection--;
                Battle_MoveCursor(cursor, 64, sBattleDefeatSelection * 32 + 48, 10, 2);
            }
            else if (input & BATTLE_INPUT_REPEAT_DOWN)
            {
                sBattleDefeatSelection = (sBattleDefeatSelection + 1) % 2;
                Battle_MoveCursor(cursor, 64, sBattleDefeatSelection * 32 + 48, 10, 2);
            }
            if ((input & 0xF000) == BATTLE_INPUT_PRESS_A)
                result = sBattleDefeatSelection + 1;
        }
        break;
    case 5:
        break;
    case 6:
        break;
    }
    return result;
}

/** @brief Collect periodic effects and status-recovery candidates.
 *  @note Preserves enemy-mask reads from the party list and the single-party recovery path's append behavior.
 *  @address 0x0804A148
 */
void BattleResult_CollectTurnEndEffects(void)
{
    u8 partySlots[BATTLE_PARTY_COUNT];
    u8 enemySlots[BATTLE_ENEMY_COUNT];
    u8 partyCount;
    u8 enemyCount;
    u8 index;
    struct BattleCombatant *combatants = Battle_GetCombatants();

    sBattleTurnEndTargetCount = 0;
    sBattleTurnEndEffectMask = 0;
    partyCount = Battle_ListCombatants(combatants, partySlots, BATTLE_SIDE_PARTY, 0xFF);
    enemyCount = Battle_ListCombatants(combatants, enemySlots, BATTLE_SIDE_ENEMY, 0xFF);
    for (index = 0; index < partyCount; index++)
    {
        if (combatants[partySlots[index]].turnEndEffects != 0)
        {
            sBattleTurnEndEffectMask |= combatants[partySlots[index]].turnEndEffects;
            sBattleTurnEndTargets[sBattleTurnEndTargetCount] = partySlots[index];
            sBattleTurnEndTargetCount++;
        }
    }
    for (index = 0; index < enemyCount; index++)
    {
        if (combatants[enemySlots[index]].turnEndEffects != 0)
        {
            sBattleTurnEndEffectMask |= combatants[partySlots[index]].turnEndEffects;
            sBattleTurnEndTargets[sBattleTurnEndTargetCount] = enemySlots[index];
            sBattleTurnEndTargetCount++;
        }
    }
    for (index = 0; index < partyCount; index++)
    {
        if (combatants[partySlots[index]].displayState & 0x10)
        {
            if (combatants[partySlots[index]].unknownBA < 2)
                combatants[partySlots[index]].unknownBA++;
            else
            {
                combatants[partySlots[index]].displayState &= 0xFFEF;
                combatants[partySlots[index]].unknownBA = 0;
            }
        }
    }
    partyCount = Battle_ListCombatants(combatants, partySlots, BATTLE_SIDE_PARTY, 0x2C);
    enemyCount = Battle_ListCombatants(combatants, enemySlots, BATTLE_SIDE_ENEMY, 0x2C);
    if (partyCount == 1)
    {
        sBattleStatusRecoverySlots[sBattleStatusRecoveryCount] = partySlots[0];
        sBattleStatusRecoveryCount++;
    }
    else
    {
        sBattleStatusRecoveryCount = sBattleStatusRecoveryIndex = 0;
        for (index = 0; index < partyCount; index++)
        {
            if (Battle_RollTurnEndStatusRecovery(&combatants[partySlots[index]]) == 1)
            {
                sBattleStatusRecoverySlots[sBattleStatusRecoveryCount] = partySlots[index];
                sBattleStatusRecoveryCount++;
            }
        }
    }
    for (index = 0; index < enemyCount; index++)
    {
        if (Battle_NextRandom() % 100 < 40)
        {
            sBattleStatusRecoverySlots[sBattleStatusRecoveryCount] = enemySlots[index];
            sBattleStatusRecoveryCount++;
        }
    }
    sBattleTurnEndPhase = 1;
}

/** @brief Apply queued recovery, MP drain, gauge gain, HP damage and status recovery.
 *  @note The halfword cursor wraps to a byte on increment; damage keeps signed remaining HP and unsigned prior HP.
 *  @address 0x0804A368
 */
u8 BattleResult_UpdateTurnEndEffects(struct BattleCombatant *combatants)
{
    u8 finished = 0;
    u16 index;

    switch (sBattleTurnEndPhase)
    {
    case 1:
        if (sBattleTurnEndEffectMask & BATTLE_TURN_END_HP_RECOVERY)
        {
            BattleResult_SelectTurnEndTargets(BATTLE_TURN_END_HP_RECOVERY);
            sBattleTurnEndPhase = 8;
        }
        else if (sBattleTurnEndEffectMask & BATTLE_TURN_END_PARTY_HP_RECOVERY)
        {
            BattleResult_SelectTurnEndTargets(BATTLE_TURN_END_PARTY_HP_RECOVERY);
            sBattleTurnEndPhase = 2;
        }
        else if (sBattleTurnEndEffectMask & BATTLE_TURN_END_MP_DRAIN)
        {
            BattleResult_SelectTurnEndTargets(BATTLE_TURN_END_MP_DRAIN);
            sBattleTurnEndPhase = 4;
        }
        else if (sBattleTurnEndEffectMask & BATTLE_TURN_END_GAUGE_GAIN)
        {
            BattleResult_SelectTurnEndTargets(BATTLE_TURN_END_GAUGE_GAIN);
            sBattleTurnEndPhase = 6;
        }
        else if (sBattleTurnEndEffectMask & BATTLE_TURN_END_HP_DAMAGE)
        {
            BattleResult_SelectTurnEndTargets(BATTLE_TURN_END_HP_DAMAGE);
            sBattleTurnEndPhase = 10;
        }
        else
            sBattleTurnEndPhase = 17;
        break;
    case 2:
        Battle_ResetDamageNumbers(3);
        sBattleTurnEndTargetCount = 0;
        for (index = 0; index < BATTLE_PARTY_COUNT; index = (u8)(index + 1))
        {
            struct BattleCombatant *target = &combatants[index];

            if ((u8)Battle_CheckStatusMask(target, 0x100) == BATTLE_STATUS_MATCH_REJECTED
                && (target->turnEndEffects & BATTLE_TURN_END_PARTY_HP_RECOVERY))
            {
                sBattleTurnEndTargets[sBattleTurnEndTargetCount] = index;
                target->pendingDamage = Battle_LimitHpRecovery(target, target->maxHp / 10u);
                Battle_AddDamageNumber(target);
                sBattleTurnEndTargetCount++;
            }
        }
        if (sBattleTurnEndTargetCount == 0)
            sBattleTurnEndPhase = 1;
        else
            sBattleTurnEndPhase = 3;
        break;
    case 3:
        if (Battle_UpdateDamageNumbers() == 1)
        {
            for (index = 0; index < sBattleTurnEndTargetCount; index = (u8)(index + 1))
                combatants[sBattleTurnEndTargets[index]].hp += combatants[sBattleTurnEndTargets[index]].pendingDamage;
            sBattleTurnEndEffectMask &= 0xFFFE;
            sBattleTurnEndPhase = 1;
        }
        break;
    case 8:
        Battle_ResetDamageNumbers(3);
        for (index = 0; index < sBattleTurnEndTargetCount; index = (u8)(index + 1))
        {
            combatants[sBattleTurnEndTargets[index]].pendingDamage = Battle_LimitHpRecovery(
                &combatants[sBattleTurnEndTargets[index]],
                combatants[sBattleTurnEndTargets[index]].maxHp / 10u);
            Battle_AddDamageNumber(&combatants[sBattleTurnEndTargets[index]]);
            sBattleTurnEndPhase = 9;
        }
        break;
    case 9:
        if (Battle_UpdateDamageNumbers() == 1)
        {
            for (index = 0; index < sBattleTurnEndTargetCount; index = (u8)(index + 1))
                combatants[sBattleTurnEndTargets[index]].hp += combatants[sBattleTurnEndTargets[index]].pendingDamage;
            sBattleTurnEndEffectMask &= 0xFFF7;
            sBattleTurnEndPhase = 1;
        }
        break;
    case 4:
        Battle_ResetDamageNumbers(0);
        for (index = 0; index < sBattleTurnEndTargetCount; index = (u8)(index + 1))
        {
            if (combatants[sBattleTurnEndTargets[index]].mp < 10)
                combatants[sBattleTurnEndTargets[index]].pendingDamage = combatants[sBattleTurnEndTargets[index]].mp;
            else
                combatants[sBattleTurnEndTargets[index]].pendingDamage = 10;
            Battle_AddDamageNumber(&combatants[sBattleTurnEndTargets[index]]);
            sBattleTurnEndPhase = 5;
        }
        break;
    case 5:
        if (Battle_UpdateDamageNumbers() == 1)
        {
            for (index = 0; index < sBattleTurnEndTargetCount; index = (u8)(index + 1))
                combatants[sBattleTurnEndTargets[index]].mp -= combatants[sBattleTurnEndTargets[index]].pendingDamage;
            sBattleTurnEndEffectMask &= 0xFFFD;
            sBattleTurnEndPhase = 1;
        }
        break;
    case 6:
        if (sBattleTurnEndTargetIndex < sBattleTurnEndTargetCount)
            sBattleTurnEndPhase = 7;
        else
        {
            sBattleTurnEndEffectMask &= 0xFFFB;
            sBattleTurnEndPhase = 1;
        }
        break;
    case 7:
        Battle_AddAttackGauge(&combatants[sBattleTurnEndTargets[sBattleTurnEndTargetIndex]]);
        sBattleTurnEndTargetIndex++;
        sBattleTurnEndPhase = 6;
        break;
    case 10:
        sBattleTurnEndFaintedPartyCount = 0;
        sBattleTurnEndDefeatedEnemyCount = 0;
        Battle_ResetDamageNumbers(0);
        for (index = 0; index < sBattleTurnEndTargetCount; index = (u8)(index + 1))
        {
            u32 slot = sBattleTurnEndTargets[index];
            struct BattleCombatant *target = (struct BattleCombatant *)(slot * sizeof(struct BattleCombatant) + (u32)combatants);
            s16 hp = target->hp;
            s16 damage = (s16)target->maxHp / 10;
            u16 oldHp;

            if ((s16)damage > 100)
                damage = 100;
            oldHp = hp;
            hp -= damage;
            if (hp <= 0)
            {
                if (target->id < BATTLE_CHARACTER_ID_COUNT)
                {
                    sBattleTurnEndFaintedPartySlots[sBattleTurnEndFaintedPartyCount] = slot;
                    sBattleTurnEndFaintedPartyCount++;
                }
                else
                {
                    sBattleTurnEndDefeatedEnemySlots[sBattleTurnEndDefeatedEnemyCount] = slot;
                    sBattleTurnEndDefeatedEnemyCount++;
                }
                damage = oldHp;
            }
            combatants[sBattleTurnEndTargets[index]].pendingDamage = damage;
            Battle_AddDamageNumber(&combatants[sBattleTurnEndTargets[index]]);
        }
        sBattleTurnEndPhase = 11;
        break;
    case 11:
        if (Battle_UpdateDamageNumbers() == 1)
        {
            for (index = 0; index < sBattleTurnEndTargetCount; index = (u8)(index + 1))
                combatants[sBattleTurnEndTargets[index]].hp -= combatants[sBattleTurnEndTargets[index]].pendingDamage;
            sBattleTurnEndEffectMask &= 0xFFEF;
            if (sBattleTurnEndFaintedPartyCount != 0)
                sBattleTurnEndPhase = 12;
            else if (sBattleTurnEndDefeatedEnemyCount != 0)
                sBattleTurnEndPhase = 14;
            else
                sBattleTurnEndPhase = 1;
        }
        break;
    case 12:
        Battle_ResetDefeatQueue();
        for (index = 0; index < sBattleTurnEndFaintedPartyCount; index = (u8)(index + 1))
            Battle_QueueDefeatedCombatant(&combatants[sBattleTurnEndFaintedPartySlots[index]]);
        sBattleTurnEndPhase = 13;
        break;
    case 13:
        if (Battle_ProcessDefeatQueue())
        {
            if (sBattleTurnEndDefeatedEnemyCount != 0)
                sBattleTurnEndPhase = 14;
            else
                sBattleTurnEndPhase = 1;
        }
        break;
    case 14:
        Battle_ResetDefeatQueue();
        for (index = 0; index < sBattleTurnEndDefeatedEnemyCount; index = (u8)(index + 1))
            Battle_QueueDefeatedCombatant(&combatants[sBattleTurnEndDefeatedEnemySlots[index]]);
        sBattleTurnEndPhase = 15;
        break;
    case 15:
        if (Battle_ProcessDefeatQueue())
            sBattleTurnEndPhase = 1;
        break;
    case 16:
        finished = 1;
        break;
    case 17:
        if (sBattleStatusRecoveryIndex < sBattleStatusRecoveryCount)
        {
            Battle_CureStatusOrRevive(&combatants[sBattleStatusRecoverySlots[sBattleStatusRecoveryIndex]], 11, 0);
            sBattleTurnEndPhase = 18;
        }
        else
            sBattleTurnEndPhase = 16;
        break;
    case 18:
        if (!(combatants[sBattleStatusRecoverySlots[sBattleStatusRecoveryIndex]].animation.flags
            & BATTLE_ANIM_LOADING))
        {
            sBattleStatusRecoveryIndex++;
            sBattleTurnEndPhase = 17;
        }
        break;
    }
    return finished;
}

/** @brief Rebuild the turn-end list for a selected effect mask.
 *  @note The original enemy branch appends from the party scratch list; this behavior is retained.
 *  @address 0x0804AA2C
 */
void BattleResult_SelectTurnEndTargets(u16 effectMask)
{
    u8 partySlots[BATTLE_PARTY_COUNT];
    u8 enemySlots[BATTLE_ENEMY_COUNT];
    u8 partyCount;
    u8 enemyCount;
    u8 index;
    struct BattleCombatant *combatants = Battle_GetCombatants();

    sBattleTurnEndTargetCount = 0;
    sBattleTurnEndTargetIndex = 0;
    partyCount = Battle_ListCombatants(combatants, partySlots, BATTLE_SIDE_PARTY, 0xFF);
    enemyCount = Battle_ListCombatants(combatants, enemySlots, BATTLE_SIDE_ENEMY, 0xFF);
    for (index = 0; index < partyCount; index++)
    {
        if (combatants[partySlots[index]].turnEndEffects & effectMask)
        {
            sBattleTurnEndTargets[sBattleTurnEndTargetCount] = partySlots[index];
            sBattleTurnEndTargetCount++;
        }
    }
    for (index = 0; index < enemyCount; index++)
    {
        if (combatants[enemySlots[index]].turnEndEffects & effectMask)
        {
            sBattleTurnEndTargets[sBattleTurnEndTargetCount] = partySlots[index];
            sBattleTurnEndTargetCount++;
        }
    }
}

/** @brief Start the current party slot's level-up palette highlight.
 *  @note Stores that combatant's OBJ palette bank and sets highlight phase one.
 *  @address 0x0804AB10
 */
void BattleResult_BeginLevelUpHighlight(void)
{
    sBattleResultHighlightPalette = sBattleResultCombatants[sBattleResultPartySlot].animation.paletteSlot;
    sBattleResultHighlightPhase = 1;
}

/** @brief 选择战斗结果消息，复位绘制游标并缓存字形。
 *  @note 按 SCRIPT_TEXT_END 分隔扫描消息，清零四个游标，并设置控制位 0x400。
 *  @note 编号保存为 s16，入口/循环分别转 u16/u8 比较；前两游标用局部指针归零，普通 C 匹配 0x90 字节（含字面池）。
 *  @address 0x0804AB40
 */
const u16 *BattleResult_SelectMessage(u8 messageIndex)
{
    u16 index = 0;
    u16 message = 0;
    s16 targetMessage = messageIndex;
    const u16 *messages = D_0839B2E0;
    const u16 *selected;
    u8 *textIndex = &sBattleResultTextIndex;
    u8 *textColumn = &sBattleResultTextColumn;

    if (message < (u16)targetMessage)
    {
        do
        {
            if (D_0839B2E0[index] == SCRIPT_TEXT_END)
                message++;
            index++;
        } while (message < (u8)targetMessage);
    }
    *textIndex = 0;
    *textColumn = 0;
    sBattleResultNumberDigit = 0;
    sBattleResultInsertIndex = 0;
    selected = (const u16 *)(index * sizeof(u16) + (u32)messages);
    Script_CacheGlyphsFromWords(selected, SCRIPT_GLYPH_SCAN_NAME);
    Battle_SetControlFlags(0x400);
    return selected;
}

/** @brief Clear the two-row, twenty-six-column result message area.
 *  @note The area begins at window tilemap column two, row one.
 *  @address 0x0804ABD0
 */
void BattleResult_ClearMessageArea(void)
{
    u16 *tilemap = gBattleWindowTilemap + 0x22;
    u8 column;

    for (column = 0; column < 26; column++)
    {
        tilemap[column] = 0xB001;
        tilemap[column + 32] = 0xB001;
    }
}

/** @brief Emit one glyph from the current party member's name.
 *  @note Stops after four glyphs or at the encoded terminator.
 *  @address 0x0804ABF8
 */
u8 BattleResult_DrawPartyNameGlyph(u16 *tilemap, u8 characterId)
{
    const u8 *names = (const u8 *)gScriptNames;
    s32 glyphOffset = sBattleResultInsertIndex * 2;
    s32 nameOffset;
    u16 glyph = *(const u16 *)(glyphOffset
        + (nameOffset = characterId * sizeof(struct ScriptName)) + (u32)names);

    tilemap[0] = glyph * 2 - 0x5000;
    tilemap[32] = glyph * 2 - 0x4FFF;
    sBattleResultInsertIndex++;
    if (sBattleResultInsertIndex < 4
        && *(const u16 *)(sBattleResultInsertIndex * 2 + nameOffset + (u32)names) != SCRIPT_TEXT_END)
        return 0;
    return 1;
}

/** @brief Cache the current card reward's name glyphs when requested.
 *  @note Preserves the signed card-row index and the control-flag gate.
 *  @address 0x0804AC60
 */
void BattleResult_CacheCardNameGlyphs(void)
{
    const u16 *name = D_0839D348
        + sBattleResultCardDrops[sBattleResultCardDropIndex].cardIndex * 9;
    u16 *glyphs;

    if (Battle_GetControlFlags() & BATTLE_CONTROL_SPECIAL_ENCOUNTER)
    {
        Script_CacheGlyphsFromWords(name, 0x4F1E);
        if (Script_GetGlyphCache(&glyphs) != 0)
            Battle_SetControlFlags(0x400);
    }
}

/** @brief Locate a skill-name stream and prepare its glyph cache.
 *  @note Names are sequential halfword streams separated by 0x0F00.
 *  @address 0x0804ACC0
 */
const u16 *BattleResult_GetSkillName(u8 skillIndex)
{
    u16 skill = 0;
    u16 index = 0;
    u16 target = skillIndex;
    const u16 *selected;
    u16 *glyphs;

    while (skill < target)
    {
        if (D_0839B462[index] == SCRIPT_TEXT_END)
            skill++;
        index++;
    }
    selected = D_0839B462 + index;
    Script_CacheGlyphsFromWords(selected, 0x6F1E);
    if (Script_GetGlyphCache(&glyphs) != 0)
        Battle_SetControlFlags(0x400);
    return selected;
}

/** @brief Advance and draw the two-frame continuation arrow.
 *  @note Uses tilemap halfword offset 0x5B and a sixteen-frame cycle.
 *  @address 0x0804AD24
 */
void BattleResult_DrawContinueArrow(u16 *tilemap)
{
    u16 frame;

    sBattleResultFrame = (sBattleResultFrame + 1) % 16;
    frame = sBattleResultFrame;
    frame >>= 3;
    tilemap[0x5B] = frame + 0xB1BE;
}

/** @brief Replace the continuation arrow with its blank tile.
 *  @note Writes tile value 0xB000 at halfword offset 0x5B.
 *  @address 0x0804AD54
 */
void BattleResult_ClearContinueArrow(u16 *tilemap)
{
    tilemap[0x5B] = 0xB000;
}

/** @brief Set up the standalone sliding result-banner animation.
 *  @note Loads and binds its animation resources, resets the result mode and stops BGM.
 *  @address 0x0804AD60
 */
void BattleResult_InitBanner(void)
{
    Battle_SetupAnimation(&sBattleResultBanner, 240, 80, 0x1B4, 14,
        D_08619A60, D_08619430, 0x540, 1, 0x402);
    Battle_LoadAnimationBlock(&sBattleResultBanner);
    sBattleResultBanner.flags &= 0xF7FF;
    Battle_BindAnimationTables(&sBattleResultBanner);
    sBattleResultFrame = 0;
    sBattleResultPhase = 0;
    sBattleResultMode = BATTLE_RESULT_BANNER;
    sBattleResultBanner.priority = 0;
    Sound_StopBgm();
}

/** @brief Select victory presentation and reset its subphase.
 *  @note Does not allocate windows or reinitialize the reward workspaces.
 *  @address 0x0804ADE0
 */
void BattleResult_BeginVictory(void)
{
    sBattleResultPhase = 0;
    sBattleResultMode = BATTLE_RESULT_VICTORY;
}

/** @brief Start the defeat scanline transition and reset retry selection.
 *  @note Selects result mode two and clears the shared frame and phase bytes.
 *  @address 0x0804ADF8
 */
void BattleResult_InitDefeat(void)
{
    Blend_StartScanlineEffect(2, 10, 50);
    sBattleResultPhase = 0;
    sBattleResultFrame = 0;
    sBattleResultMode = BATTLE_RESULT_DEFEAT;
    sBattleDefeatSelection = 0;
}

/* Split read-only data, in ROM address order. */

/* ROM 0x0839B2E0..0x0839BB4C; .rodata. */
const u16 D_0839B2E0[] __attribute__((aligned(1))) =
{
#include "lang/jp/battle_messages.h"
};

const u16 D_0839B462[] __attribute__((aligned(1))) =
{
#include "lang/jp/battle_skill_text.h"
};

/* ROM 0x0839CC4C..0x0839CD5C; .rodata.0039CC4C. */
const struct BattleWeaponActionInfo gBattleWeaponActionInfo[] __attribute__((section(".rodata.0039CC4C"), aligned(1))) =
{
    { 0x0000, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x03, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x01, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x02, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0002, 0x00, 0x00 },
    { 0x0001, 0x06, 0x00 },
    { 0x0001, 0x04, 0x00 },
    { 0x0001, 0x05, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x03, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x01, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0002, 0x00, 0x00 },
    { 0x0002, 0x00, 0x00 },
    { 0x0002, 0x00, 0x00 },
    { 0x0002, 0x00, 0x00 },
    { 0x0002, 0x00, 0x00 },
    { 0x0002, 0x00, 0x00 },
    { 0x0002, 0x00, 0x00 },
    { 0x0002, 0x00, 0x00 },
    { 0x0002, 0x03, 0x00 },
    { 0x0002, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x02, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0002, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0002, 0x00, 0x00 },
    { 0x0002, 0x00, 0x00 },
    { 0x0002, 0x00, 0x00 },
    { 0x0002, 0x00, 0x00 },
    { 0x0002, 0x00, 0x00 },
    { 0x0002, 0x00, 0x00 },
    { 0x0002, 0x00, 0x00 },
    { 0x0002, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
    { 0x0001, 0x00, 0x00 },
};

/* ROM 0x0839CFBA..0x0839D47C; .rodata.0039CFBA. */
const u8 gEnemyNames[] __attribute__((section(".rodata.0039CFBA"), aligned(1))) =
{
#include "lang/jp/enemy_names.h"
};

const u16 D_0839D348[] __attribute__((section(".rodata.0039CFBA"), aligned(1))) =
{
#include "lang/jp/enemy_names_extended.h"
};

/* ROM 0x0839D5BC..0x0839D8EC; .rodata.0039D5BC. */
const struct BattleEnemyActionInfo gBattleEnemyActionInfo[101] __attribute__((section(".rodata.0039D5BC"), aligned(1))) =
{
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x01, 0x00, 0x50, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x02, 0x00, 0x3C, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x03, 0x00, 0x3C, 0x00, 0x01 },
    { 0x00, 0x03, 0x00, 0x3C, 0x00, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x01, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x02, 0x00, 0x3C, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x04, 0x00, 0x50, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x03, 0x00, 0x3C, 0x00, 0x01 },
    { 0x00, 0x03, 0x00, 0x3C, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x01, 0x01 },
    { 0x00, 0x01, 0x00, 0x3C, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
    { 0x00, 0x04, 0x00, 0x50, 0x01, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x01, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x03, 0x00, 0x50, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x01, 0x00, 0x50, 0x00, 0x00 },
    { 0x00, 0x03, 0x00, 0x50, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x05, 0x00, 0x50, 0x01, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
    { 0x00, 0x02, 0x00, 0x50, 0x00, 0x00 },
    { 0x00, 0x01, 0x00, 0x50, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x06, 0x00, 0x50, 0x00, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x01, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x01, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x01, 0x00, 0x50, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x01, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x01, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x02, 0x00, 0x50, 0x01, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x01, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x02, 0x00, 0x28, 0x00, 0x01, 0x01 },
    { 0x00, 0x05, 0x00, 0x50, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x01, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x02, 0x00, 0x50, 0x01, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
    { 0x00, 0x02, 0x00, 0x3C, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x01, 0x00, 0x32, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

const u8 gBattleWeaponElementMasks[] __attribute__((section(".rodata.0039D5BC"), aligned(1))) =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* End of split read-only data. */

/* ROM 0x08619430..0x08619AA4; .rodata.00619430. */
const u16 D_08619430[] __attribute__((section(".rodata.00619430"), aligned(1))) =
{
    0xAA6A, 0x8210, 0x8273, 0x82D6, 0x8339, 0x839C, 0x83FF, 0xD7FF, 0xFFFF, 0x0421, 0x00C6, 0x016B, 0x0421,
    0x4A52, 0xD6B5, 0xE739, 0x0004, 0x00C8, 0x0007, 0x0001, 0x0012, 0x002E, 0x004A, 0x0066, 0x0082, 0x009E,
    0x00BA, 0x0000, 0x0004, 0x00E8, 0x81E8, 0x0000, 0x80E8, 0x8008, 0x0010, 0x4008, 0x81E8, 0x0018, 0x0008,
    0x4008, 0x0020, 0x0000, 0x0004, 0x00E8, 0x81E8, 0x0025, 0x80E8, 0x8008, 0x0035, 0x4008, 0x81E8, 0x003D,
    0x0008, 0x4008, 0x0045, 0x0000, 0x0004, 0x00E8, 0x81E8, 0x0049, 0x80E8, 0x8008, 0x0059, 0x4008, 0x81E8,
    0x0061, 0x0008, 0x4008, 0x0069, 0x0000, 0x0004, 0x00E8, 0x81E8, 0x006D, 0x80E8, 0x8008, 0x007D, 0x4008,
    0x81E8, 0x0085, 0x0008, 0x4008, 0x008D, 0x0000, 0x0004, 0x00E8, 0x81E8, 0x0091, 0x80E8, 0x8008, 0x00A1,
    0x4008, 0x81E8, 0x00A9, 0x0008, 0x4008, 0x00B1, 0x0000, 0x0004, 0x00E8, 0x81E8, 0x00B5, 0x80E8, 0x8008,
    0x00C5, 0x4008, 0x81E8, 0x00CD, 0x0008, 0x4008, 0x00D5, 0x0000, 0x0001, 0x00F8, 0x0000, 0x0024, 0x0001,
    0x0018, 0x0006, 0x0018, 0x0000, 0x0001, 0x0006, 0x0002, 0x0000, 0x0003, 0x0006, 0x0004, 0x0001, 0x0005,
    0x0006, 0x0006, 0x0001, 0x0007, 0x0006, 0x0008, 0x0002, 0x0009, 0x0006, 0x000A, 0x0002, 0x000B, 0x0006,
    0x000C, 0x0003, 0x000D, 0x0006, 0x000E, 0x0003, 0x000F, 0x0006, 0x0010, 0x0004, 0x0011, 0x0006, 0x0012,
    0x0004, 0x0013, 0x0006, 0x0014, 0x0005, 0x0015, 0x0006, 0x0016, 0x0005, 0x0017, 0x0006, 0x0018, 0x0004,
    0x00C8, 0x0007, 0x0001, 0x0012, 0x002E, 0x004A, 0x0066, 0x0082, 0x009E, 0x00BA, 0x0000, 0x0004, 0x00E8,
    0x81E8, 0x0000, 0x80E8, 0x8008, 0x0010, 0x4008, 0x81E8, 0x0018, 0x0008, 0x4008, 0x0020, 0x0000, 0x0004,
    0x00E8, 0x81E8, 0x0025, 0x80E8, 0x8008, 0x0035, 0x4008, 0x81E8, 0x003D, 0x0008, 0x4008, 0x0045, 0x0000,
    0x0004, 0x00E8, 0x81E8, 0x0049, 0x80E8, 0x8008, 0x0059, 0x4008, 0x81E8, 0x0061, 0x0008, 0x4008, 0x0069,
    0x0000, 0x0004, 0x00E8, 0x81E8, 0x006D, 0x80E8, 0x8008, 0x007D, 0x4008, 0x81E8, 0x0085, 0x0008, 0x4008,
    0x008D, 0x0000, 0x0004, 0x00E8, 0x81E8, 0x0091, 0x80E8, 0x8008, 0x00A1, 0x4008, 0x81E8, 0x00A9, 0x0008,
    0x4008, 0x00B1, 0x0000, 0x0004, 0x00E8, 0x81E8, 0x00B5, 0x80E8, 0x8008, 0x00C5, 0x4008, 0x81E8, 0x00CD,
    0x0008, 0x4008, 0x00D5, 0x0000, 0x0001, 0x00FC, 0x01FC, 0x0024, 0x0001, 0x001B, 0x0006, 0x0018, 0x0000,
    0x0001, 0x0006, 0x0002, 0x0000, 0x0003, 0x0006, 0x0004, 0x0001, 0x0005, 0x0006, 0x0009, 0x0001, 0x000A,
    0x0006, 0x000B, 0x0002, 0x000C, 0x0006, 0x000D, 0x0002, 0x000E, 0x0006, 0x000F, 0x0003, 0x0010, 0x0006,
    0x0011, 0x0003, 0x0012, 0x0006, 0x0013, 0x0004, 0x0014, 0x0006, 0x0015, 0x0004, 0x0016, 0x0006, 0x0017,
    0x0005, 0x0018, 0x0006, 0x0019, 0x0005, 0x001A, 0x0006, 0x001B, 0x0004, 0x00C8, 0x0007, 0x0001, 0x0012,
    0x002E, 0x004A, 0x0066, 0x0082, 0x009E, 0x00BA, 0x0000, 0x0004, 0x00E8, 0x81E8, 0x0000, 0x80E8, 0x8008,
    0x0010, 0x4008, 0x81E8, 0x0018, 0x0008, 0x4008, 0x0020, 0x0000, 0x0004, 0x00E8, 0x81E8, 0x0025, 0x80E8,
    0x8008, 0x0035, 0x4008, 0x81E8, 0x003D, 0x0008, 0x4008, 0x0045, 0x0000, 0x0004, 0x00E8, 0x81E8, 0x0049,
    0x80E8, 0x8008, 0x0059, 0x4008, 0x81E8, 0x0061, 0x0008, 0x4008, 0x0069, 0x0000, 0x0004, 0x00E8, 0x81E8,
    0x006D, 0x80E8, 0x8008, 0x007D, 0x4008, 0x81E8, 0x0085, 0x0008, 0x4008, 0x008D, 0x0000, 0x0004, 0x00E8,
    0x81E8, 0x0091, 0x80E8, 0x8008, 0x00A1, 0x4008, 0x81E8, 0x00A9, 0x0008, 0x4008, 0x00B1, 0x0000, 0x0004,
    0x00E8, 0x81E8, 0x00B5, 0x80E8, 0x8008, 0x00C5, 0x4008, 0x81E8, 0x00CD, 0x0008, 0x4008, 0x00D5, 0x0000,
    0x0001, 0x00FC, 0x01FC, 0x0024, 0x0001, 0x001B, 0x0006, 0x0018, 0x0000, 0x0001, 0x0006, 0x0002, 0x0000,
    0x0003, 0x0006, 0x0004, 0x0001, 0x0005, 0x0006, 0x0009, 0x0001, 0x000A, 0x0006, 0x000B, 0x0002, 0x000C,
    0x0006, 0x000D, 0x0002, 0x000E, 0x0006, 0x000F, 0x0003, 0x0010, 0x0006, 0x0011, 0x0003, 0x0012, 0x0006,
    0x0013, 0x0004, 0x0014, 0x0006, 0x0015, 0x0004, 0x0016, 0x0006, 0x0017, 0x0005, 0x0018, 0x0006, 0x0019,
    0x0005, 0x001A, 0x0006, 0x001B, 0x0004, 0x023C, 0x0012, 0x0001, 0x0028, 0x0044, 0x006C, 0x007C, 0x0086,
    0x00A2, 0x00BE, 0x00CE, 0x00DE, 0x00EE, 0x0110, 0x012C, 0x015A, 0x017C, 0x019E, 0x01C6, 0x01EE, 0x0210,
    0x0000, 0x0004, 0x00F2, 0x4008, 0x001F, 0x40F2, 0x81E8, 0x0023, 0x80F2, 0x000C, 0x002B, 0x40F2, 0x81EC,
    0x002D, 0x0000, 0x0006, 0x00F8, 0x000C, 0x004A, 0x40F8, 0x41EC, 0x0046, 0x80E8, 0x000C, 0x000A, 0x40E8,
    0x81EC, 0x0006, 0x80F2, 0x0004, 0x016D, 0x00F2, 0x41F4, 0x016B, 0x0000, 0x0002, 0x40F8, 0x41F0, 0x0077,
    0x40E8, 0x81F0, 0x007B, 0x0000, 0x0001, 0x00E0, 0x81F0, 0x0083, 0x0000, 0x0004, 0x80F0, 0x0004, 0x0093,
    0x00F0, 0x41F4, 0x0095, 0x80D0, 0x4004, 0x0099, 0x80D0, 0x81F4, 0x009D, 0x0000, 0x0004, 0x80F0, 0x0004,
    0x00A5, 0x00F0, 0x41F4, 0x00A7, 0x80D0, 0x4004, 0x00AB, 0x80D0, 0x81F4, 0x00AF, 0x0000, 0x0002, 0x80D0,
    0x0004, 0x00B7, 0x00D0, 0x41F4, 0x00B9, 0x0000, 0x0002, 0x80D0, 0x0004, 0x00BD, 0x00D0, 0x41F4, 0x00BF,
    0x0000, 0x0002, 0x00F2, 0x0000, 0x0000, 0x00F2, 0x01FC, 0x0001, 0x0000, 0x0005, 0x00FA, 0x01EC, 0x0002,
    0x00F2, 0x01FC, 0x0001, 0x40F2, 0x01EC, 0x0003, 0x00F2, 0x0010, 0x0005, 0x40F2, 0x0000, 0x0006, 0x0000,
    0x0004, 0x80F2, 0x0010, 0x0008, 0x00F2, 0x4000, 0x000A, 0x80F2, 0x01FC, 0x000E, 0x00F2, 0x41EC, 0x0010,
    0x0000, 0x0007, 0x00FA, 0x01F8, 0x0014, 0x40FA, 0x01E8, 0x0015, 0x80F2, 0x0010, 0x0008, 0x00F2, 0x4000,
    0x000A, 0x40FA, 0x0004, 0x0017, 0x80F2, 0x01FC, 0x0019, 0x00F2, 0x41EC, 0x001B, 0x0000, 0x0005, 0x00F8,
    0x000C, 0x0184, 0x40F8, 0x41EC, 0x0180, 0x80E8, 0x000C, 0x0144, 0x40E8, 0x81EC, 0x0140, 0x00F0, 0x41F8,
    0x016E, 0x0000, 0x0005, 0x00F8, 0x000C, 0x0035, 0x40F8, 0x41EC, 0x0036, 0x80E8, 0x000C, 0x003A, 0x40E8,
    0x81EC, 0x003C, 0x80E2, 0x41FD, 0x0044, 0x0000, 0x0006, 0x00F8, 0x000C, 0x0048, 0x40F8, 0x41EC, 0x0049,
    0x80E8, 0x000C, 0x004D, 0x40E8, 0x81EC, 0x004F, 0x00F0, 0x41F9, 0x0057, 0x80D0, 0x81F9, 0x005B, 0x0000,
    0x0006, 0x00F8, 0x000C, 0x0035, 0x40F8, 0x41EC, 0x0036, 0x80E8, 0x000C, 0x003A, 0x40E8, 0x81EC, 0x003C,
    0x00E8, 0x41F9, 0x0063, 0x80C8, 0x81F9, 0x0067, 0x0000, 0x0005, 0x00F8, 0x000C, 0x0048, 0x40F8, 0x41EC,
    0x0049, 0x80E8, 0x000C, 0x004D, 0x40E8, 0x81EC, 0x004F, 0x80C1, 0x81F9, 0x006F, 0x0000, 0x0006, 0x00F8,
    0x000C, 0x0184, 0x40F8, 0x41EC, 0x0180, 0x80E8, 0x000C, 0x0144, 0x40E8, 0x81EC, 0x0140, 0x80F2, 0x0004,
    0x01AD, 0x00F2, 0x41F4, 0x01AB, 0x0001, 0x0030, 0x0006, 0x000F, 0x0008, 0x0003, 0x0009, 0x0006, 0x000A,
    0x0009, 0x000B, 0x000B, 0x0000, 0x000D, 0x000D, 0x0010, 0x000E, 0x0013, 0x000F, 0x0016, 0x0010, 0x0019,
    0x0002, 0x001C, 0x0003, 0x0020, 0x0004, 0x0024, 0x0005, 0x0028, 0x0006, 0x002C, 0x0007, 0x0030,
};

const u16 D_08619A60[] __attribute__((section(".rodata.00619430"), aligned(1))) =
{
    0x0004, 0x0038, 0x0001, 0x0001, 0x0006, 0x0000, 0x0007, 0x40E8, 0xC1C8, 0x0000, 0x00E8, 0x8008, 0x0020,
    0x80E8, 0x8028, 0x0030, 0x4008, 0x81C8, 0x0038, 0x4008, 0x81E8, 0x0040, 0x4008, 0x8008, 0x0048, 0x0008,
    0x4028, 0x0050, 0x0001, 0x0004, 0x0006, 0x0001, 0x0000, 0x0004,
};

/* End of split read-only data. */
