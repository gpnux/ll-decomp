#ifndef GUARD_BATTLE_RULES_H
#define GUARD_BATTLE_RULES_H

#include "battle_types.h"

/** @brief Four-byte battle-action metadata indexed by equipped weapon id.
 *  @note Animation setup reads the leading halfword; the variant query reads
 *  only its low byte. The record remains in the read-only ROM resource.
 *  @address 0x0839CC4C
 */
struct BattleWeaponActionInfo
{
    u16 actionVariant;                  /* 0x00; one of three weapon styles */
    u8 attackEffect;                    /* 0x02; additional attack effect */
    u8 unknown03;
};

/** @brief Verified additional effects of weapon attacks.
 *  @note Other selector values remain unnamed; recovery is not proof of drain.
 *  @address 0x08048BAC
 */
enum BattleWeaponAttackEffect
{
    BATTLE_WEAPON_EFFECT_HP_RECOVERY = 5,
    BATTLE_WEAPON_EFFECT_MP_RECOVERY = 6,
};

/** @brief Six-byte action metadata for ordinary enemy ids 12..112.
 *  @note Status application compares (random & 100), not random modulo 100,
 *  against the threshold. The last two bytes classify attack/skill actions;
 *  category one is also consumed by the status-six command rule.
 *  @address 0x0839D5BC, 0x0804519C, 0x080485A4
 */
struct BattleEnemyActionInfo
{
    u8 attackStatus;                    /* 0x00; enum BattleStatus */
    u8 skillStatus;                     /* 0x01; enum BattleStatus */
    u8 attackStatusThreshold;
    u8 skillStatusThreshold;
    u8 attackCategory;                  /* 0x04; enum BattleEnemyActionCategory */
    u8 skillCategory;                   /* 0x05; enum BattleEnemyActionCategory */
} __attribute__((packed));

/** @brief Ordinary-enemy categories tested by protection and command rules.
 *  @note The ROM table contains only zero and one. Boss classification has
 *  separate id/action exceptions in Battle_MatchesEnemyActionCategory.
 *  @address 0x08044A40, 0x080485A4, 0x08048C30
 */
enum BattleEnemyActionCategory
{
    BATTLE_ENEMY_ACTION_PHYSICAL = 0,
    BATTLE_ENEMY_ACTION_MAGICAL = 1,
};

/** @brief Result of comparing an incoming element mask with the target.
 *  @note The response is a byte; weakness doubles damage and resistance halves it.
 *  A higher matching elemental bit can replace a lower bit's result.
 *  @address 0x08047D28, 0x08044F4C, 0x08045098
 */
enum BattleElementResponse
{
    BATTLE_ELEMENT_RESPONSE_NEUTRAL = 0,
    BATTLE_ELEMENT_RESPONSE_WEAK = 1,
    BATTLE_ELEMENT_RESPONSE_RESISTANT = 2,
};

/** @brief HP/MP conditions accepted by the low-vital party-slot filter.
 *  @note BOTH requires both values below their respective thresholds.
 *  The argument remains a byte; no enum object is stored in RAM.
 *  @address 0x08045A74
 */
enum BattleVitalFilter
{
    BATTLE_VITAL_FILTER_HP = 0,
    BATTLE_VITAL_FILTER_MP = 1,
    BATTLE_VITAL_FILTER_BOTH = 2,
};

typedef char BattleWeaponActionInfoSizeCheck[(sizeof(struct BattleWeaponActionInfo) == 4) ? 1 : -1];
typedef char BattleWeaponActionEffectCheck[(offsetof(struct BattleWeaponActionInfo, attackEffect) == 2) ? 1 : -1];
typedef char BattleEnemyActionInfoSizeCheck[(sizeof(struct BattleEnemyActionInfo) == 6) ? 1 : -1];
typedef char BattleEnemyActionThresholdCheck[(offsetof(struct BattleEnemyActionInfo, attackStatusThreshold) == 2) ? 1 : -1];
typedef char BattleEnemyActionCategoryCheck[(offsetof(struct BattleEnemyActionInfo, attackCategory) == 4) ? 1 : -1];
typedef char BattleEnemySkillCategoryCheck[(offsetof(struct BattleEnemyActionInfo, skillCategory) == 5) ? 1 : -1];

/* .rodata: defined by ROM resources, declared only by this owning interface. */
extern const struct BattleWeaponActionInfo gBattleWeaponActionInfo[]; /* 0839CC4C; extent not yet established */
extern const struct BattleEnemyActionInfo gBattleEnemyActionInfo[101]; /* 0839D5BC */
/** @brief Weapon-id byte table of outgoing attack element masks.
 *  @note Defined by the original ROM resource; extent remains unconfirmed.
 *  Equipment elements are ORed into this byte by Battle_GetActionElementMask.
 *  @address 0x0839D81A
 */
extern const u8 gBattleWeaponElementMasks[];

/** @brief Record fields accepted by Battle_SortCombatantSlots.
 *  @note The sorter orders descending; stat keys include their current modifiers.
 *  @address 0x08046F0C, 0x08048ACC
 */
enum BattleCombatantSortKey
{
    BATTLE_SORT_LEVEL = 0,
    BATTLE_SORT_HP = 1,
    BATTLE_SORT_MAX_HP = 2,
    BATTLE_SORT_MP = 3,
    BATTLE_SORT_MAX_MP = 4,
    BATTLE_SORT_ATC = 5,
    BATTLE_SORT_DEF = 6,
    BATTLE_SORT_AGL = 7,
    BATTLE_SORT_MEN = 8,
    BATTLE_SORT_RES = 9,
    BATTLE_SORT_NOA = 10,
    BATTLE_SORT_HOME_X = 11,
    BATTLE_SORT_HOME_Y = 12,
    BATTLE_SORT_FORMATION_COLUMN = 13,
    BATTLE_SORT_FORMATION_ROW = 14,
};

/** @brief Base stat and modifier pair selected by the stat-boost routines.
 *  @note The selector is a byte; the affected modifiers remain signed halfwords.
 *  @address 0x0804753C, 0x08048B30
 */
enum BattleModifierStat
{
    BATTLE_MODIFIER_ATC = 0,
    BATTLE_MODIFIER_DEF = 1,
    BATTLE_MODIFIER_AGL = 2,
    BATTLE_MODIFIER_MEN = 3,
    BATTLE_MODIFIER_RES = 4,
};

/** @brief Palette variant selected by ATC/DEF modifiers and the action kind.
 *  @note ATC takes precedence; any nonzero modifier qualifies, even negative.
 *  @address 0x08048CEC
 */
enum BattleStatModifierPaletteVariant
{
    BATTLE_MODIFIER_PALETTE_DEFAULT = 0,
    BATTLE_MODIFIER_PALETTE_ATC = 1,
    BATTLE_MODIFIER_PALETTE_DEF = 2,
    BATTLE_MODIFIER_PALETTE_ITEM_ATC = 3,
    BATTLE_MODIFIER_PALETTE_ITEM_DEF = 4,
};

/* Output buffers belong to the caller, not to the rules module. */
u8 Battle_ListEnemiesInFormation(const struct BattleCombatant *combatant, u8 *slots, u8 formationMask);
void Battle_SortCombatantSlots(u8 *slots, u8 count, u8 sortKey);

/** @brief Target-mode nibble in a skill record.
 *  @note Row-pair mode includes row 2 and either row 1 or row 3; mode 6 commits
 *  without a manual target cursor rather than building a highlight list.
 *  @address 0x08045D00, 0x080489A4
 */
enum BattleSkillTargetMode
{
    BATTLE_SKILL_TARGET_ENEMY = 0,
    BATTLE_SKILL_TARGET_ENEMY_COLUMN = 1,
    BATTLE_SKILL_TARGET_ENEMY_ROW_PAIR = 2,
    BATTLE_SKILL_TARGET_ALL_ENEMIES = 3,
    BATTLE_SKILL_TARGET_PARTY = 4,
    BATTLE_SKILL_TARGET_ALL_PARTY = 5,
    BATTLE_SKILL_TARGET_NO_SELECTION = 6,
};

/** @brief Consume a battle skill slot's MP cost after equipment reductions.
 *  @note Preserves the byte-sized cost reduction and signed-halfword clamp;
 *  the slot is unchecked and no return value is defined.
 *  @address 0x08045B90
 */
void Battle_ConsumeSkillSlotMp(struct BattleCombatant *combatant, u8 skillSlot);
/** @brief Return whether any nonempty combatant has animation graphics loading.
 *  @note Lists both sides with status mask 0x1FF and checks main and effect animation flags. Display flags do not suppress the effect-animation check; this entry now compiles from matching C.
 *  @address 0x080471AC
 */
u32 Battle_AreCombatantAnimationsLoading(void);

u8 Battle_GetSkillSlotMpCost(const struct BattleCombatant *combatant, u8 skillSlot);
u8 Battle_GetSkillTargetMode(const struct BattleCombatant *combatant, u8 selection);
void Battle_BuildSkillTargetList(struct BattleCombatant *combatants, u8 mode, u8 selected, s8 *targets);

/** @brief Dispatch the selected action to physical, magic or item damage.
 *  @note Skills 17..20 return zero; the selected calculator supplies a halfword result. Other party skill ids dispatch magic damage, while enemy actors dispatch physical damage.
 *  @address 0x0804473C
 */
u32 Battle_CalculateActionDamage(struct BattleCombatant *actor, struct BattleCombatant *target);
/** @brief Roll escape using the party and enemy effective-agility sums.
 *  @note Uses status mask 0xFF. The halfword threshold is party agility plus party adjustment bytes minus enemy agility plus 40; a strict random % 100 comparison also handles empty lists.
 *  @address 0x08048310
 */
u8 Battle_RollPartyEscape(void);
/** @brief Roll one combatant's escape against opposing average agility.
 *  @note An empty opposing list contributes zero. Accessory C7 raises the base chance from 40 to 80; equipment property eight adds 50. The halfword threshold wraps before the strict random % 100 comparison.
 *  @address 0x08048458
 */
u8 Battle_RollCombatantEscape(const struct BattleCombatant *combatant);

/* Filter the current target's side and formation; buffer holds up to seven slots. */
/** @brief Build an action target list from its selected side and formation filter.
 *  @note Party output requires five bytes and enemy output seven. A full formation-byte filter returns the
 *  selected slot even if its status was rejected; row-pair mode includes row 2.
 *  @address 0x080462E4
 */
u8 Battle_ListActionTargetSlots(const struct BattleCombatant *combatant, u8 *slots, u16 statusMask);

u8 Battle_GetSelectedSkill(const struct BattleCombatant *combatant);
u16 Battle_LimitMpRecovery(const struct BattleCombatant *combatant, u16 amount);
u32 Battle_LimitHpRecovery(const struct BattleCombatant *combatant, u16 amount);
/** @brief Apply a higher-priority numbered status and refresh its presentation.
 *  @note Only a strictly greater status replaces the current byte. Ids above 112 accept only status 8;
 *  poison also queues turn-end HP damage. Party status animation uses selector 4.
 *  @address 0x08045F94
 */
void Battle_ApplyStatus(struct BattleCombatant *combatant, u16 status);
/* Clear the selected status; selector 10 revives a fainted party member. */
/** @brief Cure the selected status or revive a fainted combatant.
 *  @note Selectors 3..9 cure status = effect - 2; selector 10 revives using wrapped HP + amount.
 *  Selector 11 excludes defeat; only the poison turn-end bit is cleared, then the
 *  status palette is refreshed. All 0xF0 linked bytes match.
 *  @address 0x0804612C
 */
void Battle_CureStatusOrRevive(struct BattleCombatant *combatant, u16 effect, u16 amount);
/** @brief Return enum BattleStatusMatch in r0; byte-sized consumers truncate explicitly. */
u32 Battle_CheckStatusMask(const struct BattleCombatant *combatant, u16 statusMask);
u8 Battle_ListCombatants(struct BattleCombatant *combatants, u8 *slots, u8 side, u16 statusMask);
/** @brief Return the encounter-specific signed enemy-level adjustment.
 *  @note The explicit encounter switch includes negative and positive byte deltas; unlisted encounter ids
 *  return zero.
 *  @address 0x08047FCC
 */
s8 Battle_GetEncounterLevelAdjustment(u16 encounterId);
/** @brief Read the first present protagonist's battle level for boss scaling.
 *  @note Includes fainted protagonists and returns 50 when neither id 0 nor 1 is present. The separate found index is read only after a successful search; keeping it separate preserves matching code.
 *  @address 0x080480EC
 */
u8 Battle_GetProtagonistBattleLevel(void);
u8 Battle_SelectActionTargets(struct BattleCombatant *combatant, u8 *entries, u8 side);
/** @brief Commit battle party HP, MP, accessories, gauge and learned skills.
 *  @note Uses gPartyMembers and stores at least one HP. Accessory B3 saves gauge zero; separate HP assignment branches preserve the original C register allocation.
 *  @address 0x080457AC
 */
void Battle_SavePartyCombatantState(void);
/** @brief Reset item reservations and restore the eligible party's saved tactics.
 *  @note Statuses 0, 1 and 6 are eligible. The per-slot helper reserves selected items and chooses targets;
 *  other party records are left alone.
 *  @address 0x0804666C
 */
void Battle_RestorePartyTacticsCommands(void);
/** @brief Assign the escape action to command-eligible party records.
 *  @note Statuses 0, 1 and 6 are eligible. Writes action 4 without rolling escape success or selecting an
 *  attack target.
 *  @address 0x08046C50
 */
void Battle_SetPartyEscapeCommands(void);

/** @brief Calculate ordinary attack damage plus the boss action's fixed bonus.
 *  @note Stores the returned halfword; the attack helper may also consume a target
 *  protection flag and clear its pending damage.
 *  @address 0x080448A8
 */
u16 Battle_CalculateBossAttackDamage(struct BattleCombatant *attacker, struct BattleCombatant *target);
/** @brief Sum experience rewards from enemy slots in status eight.
 *  @note The extended-encounter flag selects the boss record's reward offsets.
 *  @address 0x080453D8
 */
u16 Battle_GetDefeatedEnemyExperience(void);
/** @brief Sum defeated-enemy gold, apply the equipment bonus and award it.
 *  @note Accumulation and the returned amount retain sixteen-bit truncation.
 *  @address 0x0804542C
 */
u16 Battle_AwardDefeatedEnemyGold(void);
/** @brief Award experience and update eligible party members' levels.
 *  @note Returns a party-slot level-up mask; statuses seven/eight and empty slots are skipped.
 *  @address 0x080454A4
 */
u8 Battle_AwardExperience(u16 experience);
/** @brief Collect skills learned while advancing a battle slot to its persistent level.
 *  @note Initializes eight output bytes to FF and reports zero-based ids. Separate inlined character-id reads and the adjusted combatant base preserve the original calls and level updates.
 *  @address 0x08045860
 */
s8 Battle_CollectNewlyLearnedSkills(u8 slot, u8 *skills);
/** @brief Read the persistent HP/MP or base-stat value for a battle party slot.
 *  @note Selectors zero/one read halfwords and two through seven read bytes; valid selectors are 0..7.
 *  @address 0x080455A0
 */
u32 Battle_GetPartySlotStoredStat(u8 slot, u8 stat);
/** @brief Apply a displayed stat gain to the persistent party record.
 *  @note The byte-sized gain also updates the corresponding current stat where present.
 *  @address 0x08045688
 */
void Battle_AddPartySlotStatGain(u8 slot, u8 stat, u8 gain);
/** @brief Check whether a learned skill slot can be paid for with current MP.
 *  @note The slot is unchecked. Reductions wrap in a byte before halving;
 *  both the MP snapshot and the final difference use signed-halfword interpretation.
 *  @address 0x08045A10
 */
u8 Battle_CanAffordSkillSlot(const struct BattleCombatant *combatant, u8 skillSlot);
/** @brief Restore saved tactics and resolve the command's target.
 *  @note Missing skills/items, insufficient MP and empty E1-E4 target lists
 *  trigger an attack fallback. Item reservations persist; no return is defined.
 *  @address 0x080466F0
 */
void Battle_RestoreTacticsCommand(struct BattleCombatant *combatant, u8 partySlot);
/** @brief Partition a combatant-slot range in descending order around its last slot.
 *  @note Signed byte endpoints index the shared sort context; keys truncate to
 *  unsigned halfwords. Returns the pivot's final index; all 0xF4 linked bytes match.
 *  @address 0x08046E18
 */
u8 Battle_PartitionCombatantSlots(u8 *slots, s8 first, s8 last);
/** @brief Read the record field selected by the shared slot-sort key.
 *  @note Modifier sums wrap to halfwords. Keys 13 and 14 read formation column and row; valid selectors compile to the original C accessor.
 *  @address 0x08046F0C
 */
u32 Battle_GetCombatantSortValue(const struct BattleCombatant *combatant);
/** @brief Add a percentage of a base stat to its battle modifier.
 *  @note Checks the unsigned modifier against (base / 10) * 5 before adding;
 *  DEF uses ATC for this limit. Single-precision results wrap to sixteen bits.
 *  @address 0x0804753C
 */
void Battle_AddStatModifierPercent(struct BattleCombatant *combatant, u8 stat, u8 percent);
/** @brief Advance the party stat-boost effects and apply the selected modifier.
 *  @note The first argument is unused; returns 1 after restoring party draw priority 3.
 *  @address 0x080476DC
 */
u8 Battle_UpdatePartyStatBoost(struct BattleCombatant *unused, struct BattleCombatant *target);
/** @brief Advance the enemy stat-boost animation and apply the selected modifier.
 *  @note Takes only the affected combatant; returns 1 when phase 11 applies the modifier.
 *  @address 0x08047B1C
 */
u8 Battle_UpdateEnemyStatBoost(struct BattleCombatant *combatant);
/** @brief Read the persistent level associated with a battle party slot.
 *  @note Returns the zero-extended byte in a full-width result; ids zero and one share record zero.
 *  @address 0x0804877C
 */
u32 Battle_GetPartySlotLevel(u8 slot);
/** @brief Resolve a battle party slot through its persistent formation slot.
 *  @note Uses BattleCombatant.formationSlot to index gPartyCharacterSlots.
 *  @address 0x080487A4
 */
u8 Battle_GetPartySlotCharacterId(u8 slot);
/** @brief Locate a current party member in the persistent formation slots.
 *  @note Encounters 0xA1 and 0xA7 force slot two; an unsuccessful search returns zero.
 *  @address 0x080487CC
 */
u8 Battle_GetPartyMemberFormationSlot(u8 memberIndex);
/** @brief Calculate a battle member's base stat at its persistent level.
 *  @note Returns the zero-extended halfword from Party_GetBaseStat in a 32-bit result.
 *  @address 0x08048818
 */
u32 Battle_GetPartySlotBaseStat(u8 slot, u8 stat);
/** @brief Report whether a skill is absent from the member's persistent skill list.
 *  @note Returns one when absent and zero when found among eight entries.
 *  @address 0x08048868
 */
u8 Battle_IsPartySlotMissingSkill(u8 slot, u8 skillId);
/** @brief Resolve an affordable learned skill or a full-gauge special selection.
 *  @note Ids above 47 select the stored special skill once the gauge exceeds 31; FF means unavailable.
 *  @address 0x080488CC
 */
u8 Battle_FindUsableSkillSelection(const struct BattleCombatant *combatant, u8 skillId);
/** @brief Read the effect-type nibble of a learned skill.
 *  @note Uses the selected skill id's five-byte ROM record, byte two low nibble.
 *  @address 0x08048984
 */
u8 Battle_GetSkillSlotEffectType(const struct BattleCombatant *combatant, u8 skillSlot);
/** @brief Test whether pending damage leaves a nonpositive signed HP value.
 *  @note The HP-minus-damage result is truncated to a signed halfword before comparison.
 *  @address 0x08048A68
 */
u8 Battle_WouldPendingDamageDefeat(const struct BattleCombatant *combatant);
/** @brief Recursively sort a combatant-slot range in descending order.
 *  @note Uses the shared context installed by Battle_SortCombatantSlots.
 *  @address 0x08048A88
 */
void Battle_QuicksortCombatantSlots(u8 *slots, s8 first, s8 last);
/** @brief Initialize the shared stat-boost presentation parameters.
 *  @note Resets only the boost phase before storing its stat, percentage and animation id.
 *  @address 0x08048B30
 */
void Battle_BeginStatBoost(u8 stat, u8 percent, u16 animationId);
/** @brief Set the party gauge, honoring accessory 0xB3's full-gauge rule.
 *  @note Either accessory slot can force the gauge to 32.
 *  @address 0x08048B5C
 */
void Battle_SetSpecialGauge(struct BattleCombatant *combatant, u8 gauge);
/** @brief Choose the party character's special skill from id and equipped items.
 *  @note Writes source.party.specialSkill; character-specific weapon/accessory combinations override the
 *  default id. It does not spend or fill the special gauge.
 *  @address 0x08045BF4
 */
void Battle_SetPartySpecialSkill(struct BattleCombatant *combatant);
/** @brief Add the six equipped items' turn-end effect bits.
 *  @note The HP-based equipment view and direct turnEndEffects writes preserve the original instructions. Property values 1, 2 and 3 add HP recovery, MP drain and gauge gain without replacing existing bits.
 *  @address 0x08045EB8
 */
void Battle_AddEquipmentTurnEndEffects(struct BattleCombatant *combatant);

/** @brief Read the low byte of the equipped weapon's action variant.
 *  @note Returns zero for ids outside the party range 0..10. */
u8 Battle_GetWeaponActionVariant(const struct BattleCombatant *combatant);
/** @brief Read the weapon's additional attack-effect selector.
 *  @note Selectors five/six cause HP/MP recovery; enemy ids return zero. */
u8 Battle_GetWeaponAttackEffect(const struct BattleCombatant *combatant);
/** @brief Set the party attack-approach spacing width.
 *  @note Unarmed character five uses eight; other party ids use sixteen. */
void Battle_SetPartyAttackSpacingWidth(struct BattleCombatant *combatant);
/** @brief Roll the combatant's turn-end status recovery.
 *  @note The caller filters eligible statuses; the threshold is not clamped. */
u8 Battle_RollTurnEndStatusRecovery(const struct BattleCombatant *combatant);
/** @brief Select the party palette variant for ATC/DEF stat modifiers.
 *  @note Item actions use the second pair; ATC takes precedence over DEF. */
u8 Battle_GetStatModifierPaletteVariant(const struct BattleCombatant *combatant);
/** @brief Clear the five battle stat modifiers for a party combatant.
 *  @note Enemy records are unchanged; gauge and status are not modified. */
void Battle_ClearPartyStatModifiers(struct BattleCombatant *combatant);

/** @brief Test whether the combatant has commands that silence can block.
 *  @note Does not test the current status. Party ids 0..10 qualify; ordinary
 *  enemies qualify when either action category is magical. Other ids return zero.
 *  @address 0x08048C30
 */
u8 Battle_HasSilenceBlockedCommands(const struct BattleCombatant *combatant);
/** @brief Test the five-column combined formation-distance limit.
 *  @note Adds the two low formation nibbles and accepts sums at most five.
 *  @address 0x08048D84
 */
u8 Battle_AreFormationColumnsInRange(const struct BattleCombatant *first, const struct BattleCombatant *second);

/** @brief Calculate ATC-versus-DEF damage, clamp to 0..999 and queue additional status.
 *  @note Protection clears pending damage and returns zero. Signed-halfword steps
 *  and unsigned double scaling are preserved: NOA 0..2 applies four skill factors,
 *  while 255 wraps to multiplier one. Only the listed skills initialize the factor.
 *  Reusing the adjustment halfword preserves registers; all 0x50C linked bytes match.
 *  @address 0x08044A40
 */
u16 Battle_CalculatePhysicalDamage(struct BattleCombatant *attacker, struct BattleCombatant *target);

/** @brief Calculate MEN-versus-RES skill damage.
 *  @note Consumes magic guard or damage protection before calculation. Random power, elemental response and equipment adjustments retain their signed-halfword truncation; damage is clamped to 0..999.
 *  @address 0x08044F4C
 */
u16 Battle_CalculateMagicDamage(struct BattleCombatant *attacker, struct BattleCombatant *target);

/** @brief Calculate attack-item damage with random reduction and elemental scaling.
 *  @note Items 48/49 use 70 - random % 10; others use 40 - random % 15.
 *  Weakness doubles damage and resistance halves it. Signed damage clamps to 0..999;
 *  guard flags are ignored.
 *  @address 0x08045098
 */
u16 Battle_CalculateItemDamage(const struct BattleCombatant *actor, const struct BattleCombatant *target);

/** @brief Roll and queue additional statuses from party equipment or ordinary-enemy action data.
 *  @note Uses random & 100; later party equipment matches overwrite earlier ones.
 *  Enemy inputs require attack or skill. Global resistance skips queuing, while
 *  per-status resistance queues zero. All 0x18C linked bytes and relocations match.
 *  @address 0x0804519C
 */
void Battle_QueueActionStatusEffect(const struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Roll an action's hit using raw ATC, DEF and AGL values.
 *  @note Skills and targets in statuses 2..4 always hit. The normal chance clamps to 30..100 before the
 *  target's equipment subtracts 50; returns a full-width boolean.
 *  @address 0x08045328
 */
u32 Battle_RollActionHit(const struct BattleCombatant *attacker, const struct BattleCombatant *target, u8 accuracy);

/** @brief Build the character-specific list of affordable skill selections.
 *  @note Checks affordability before filtering each learned id by character. Separate cases for ids 0, 1, 6 and 7 preserve the common append block and register allocation.
 *  @address 0x08045940
 */
u8 Battle_ListUsableCharacterSkillSelections(const struct BattleCombatant *combatant, u8 *selections);

/** @brief Filter a party-slot list for low HP, MP or both.
 *  @note Compacts the caller's list through a five-byte temporary. Uses strict current < (maximum / 10) *
 *  (percent / 10), with halfword product truncation; count must be at most five.
 *  @address 0x08045A74
 */
u8 Battle_FilterLowVitalSlots(const struct BattleCombatant *combatants, u8 *slots, u8 count, u8 percent, u8 vital);

/** @brief Apply the same status rule with the alternate party status animation.
 *  @note Shares the priority, boss-immunity, palette and poison rules with Battle_ApplyStatus. Its only
 *  code difference is party animation selector 10 instead of 4.
 *  @address 0x08046060
 */
void Battle_ApplyStatusWithAlternateAnimation(struct BattleCombatant *combatant, u16 status);

/** @brief List accepted combatants on the actor's side with matching formation bits.
 *  @note The actor id selects party or enemy slots; the byte mask can select a column, row or complete
 *  formation coordinate. The caller owns the output buffer.
 *  @address 0x08046558
 */
u8 Battle_ListAlliesInFormation(const struct BattleCombatant *combatant, u8 *slots, u8 formationMask, u16 statusMask);

/** @brief Count or list opposing combatants within the formation-column range.
 *  @note Accepts statuses 0..6 and column sums at most five. A null output pointer requests only the count;
 *  a nonnull buffer requires the opposite side's capacity.
 *  @address 0x08046CD4
 */
u8 Battle_ListOpponentsInMeleeRange(const struct BattleCombatant *combatant, u8 *slots);

/** @brief Read a battle stat with its modifier and equipment effects.
 *  @note Selectors 0..12 use BattleCombatantStat; other selectors return zero. A shared u16 adjustment preserves each division, intermediate truncation and successive equipment adjustment.
 *  @address 0x08047024
 */
u32 Battle_GetEffectiveCombatantStat(const struct BattleCombatant *combatant, u8 stat);

/** @brief Read the selected action's additive damage-power contribution.
 *  @note Enemy actors and attacks return zero; skill 49 returns baseStat. Listed magic skills return 30, equipment modifies specific skill values, and unlisted selectors retain the initial value five.
 *  @address 0x080472E8
 */
u32 Battle_GetActionPowerBonus(const struct BattleCombatant *combatant, u16 skillId, u16 baseStat);

/** @brief Classify the target's response to the four low elemental bits.
 *  @note Returns neutral, weak or resistant. Weakness wins within one bit; a later higher matching bit can
 *  replace an earlier response. Boss and ordinary-enemy offsets differ.
 *  @address 0x08047D28
 */
u8 Battle_GetElementResponse(const struct BattleCombatant *combatant, u8 elementMask);

/** @brief Resolve attack, selected-skill or attack-item elemental bits.
 *  @note Weapon and equipment bits are combined for party attacks. Separate skill cases preserve register allocation; ordinary enemies read the low byte of resistanceElements and bosses return zero.
 *  @address 0x08047DC8
 */
u8 Battle_GetActionElementMask(const struct BattleCombatant *combatant);

/** @brief Count distinct occupied columns with a lower number than the target's column.
 *  @note The attacker parameter is unused. Sorts target-side slots in statuses 0..6;
 *  repeated occupants of one column count once. Preserves the signed-byte absolute
 *  return; all 0x158 linked bytes and resolved relocations match.
 *  @address 0x080481B8
 */
s8 Battle_CountBlockingFormationColumns(const struct BattleCombatant *attacker, const struct BattleCombatant *target);

/** @brief Match an enemy's current action category with the original boss exceptions.
 *  @note Ids 12..112 read attack/skill categories, using 50 for other actions.
 *  Boss ids 125/126 always match; 118/123/127 match any non-attack action, regardless
 *  of category. Other ids return zero; the 0xEA-byte C function matches the original.
 *  @address 0x080485A4
 */
u8 Battle_MatchesEnemyActionCategory(const struct BattleCombatant *combatant, u8 category);

/** @brief Roll a party attack continuation within its weapon-variant hit limit.
 *  @note Equipment increases the byte chance; random % 100 must be strictly below it. A nonpositive
 *  signed-halfword HP minus pending damage rejects the continuation.
 *  @address 0x08048690
 */
u32 Battle_RollPartyAttackFollowup(const struct BattleCombatant *attacker, const struct BattleCombatant *target, u8 hitCount);

/** @brief Result-layer mode; storage remains one byte in battle_rules.c.
 *  @note Mode chooses banner graphics, victory text or defeat/retry presentation.
 *  @address 0x03000948
 */
enum BattleResultMode
{
    BATTLE_RESULT_BANNER = 0,
    BATTLE_RESULT_VICTORY = 1,
    BATTLE_RESULT_DEFEAT = 2,
};

/** @brief Initialize victory messages, counters and window resources.
 *  @note The original writes zero through the previously saved message pointer before replacing it.
 *  @address 0x08048DA4
 */
void BattleResult_InitVictory(void);
/** @brief Advance the current member's level-up palette highlight.
 *  @note Uses an outward tint and a timed return to the saved OBJ colors.
 *  @address 0x08048F0C
 */
void BattleResult_UpdateLevelUpHighlight(void);
/** @brief Advance victory animations, growth messages and rewards.
 *  @note Returns one after the result window closes; the scene owns the main battle phase.
 *  @address 0x08048FB8
 */
u8 BattleResult_UpdateVictory(void);
/** @brief Emit one result-message glyph or one substitution character.
 *  @note Handles names and decimal numbers; a substitution token advances only after insertion completes.
 *  @address 0x080492C0
 */
u8 BattleResult_DrawMessageGlyph(u16 number, u8 x, u8 y);
/** @brief Select the next growth or reward message phase.
 *  @note Item and card row pointers remain owned by battle_items.
 *  @address 0x080494F0
 */
u8 BattleResult_AdvanceVictoryMessage(u8 phase);
/** @brief Emit one decimal digit as two vertically adjacent font tiles.
 *  @note The shared digit cursor suppresses leading zeroes and returns one at completion.
 *  @address 0x080497B0
 */
u8 BattleResult_DrawNumberDigit(u16 *tilemap, u16 value);
/** @brief Emit one glyph of the current item-drop name.
 *  @note Uses the borrowed item rows and the shared insertion cursor.
 *  @address 0x080498E0
 */
u8 BattleResult_DrawItemNameGlyph(u16 *tilemap);
/** @brief Emit one glyph of the current card-drop name.
 *  @note Encounter flags select byte or halfword names; high glyphs use the cache.
 *  @address 0x08049958
 */
u8 BattleResult_DrawCardNameGlyph(u16 *tilemap);
/** @brief Find and apply the next positive stat increase to display.
 *  @note Stores the full halfword gain while passing its low byte to the stat writer.
 *  @address 0x08049AD8
 */
u8 BattleResult_FindNextStatGain(u8 phase);
/** @brief Emit one glyph and advance the skill-name stream.
 *  @note Returns one when the following halfword is the 0x0F00 terminator.
 *  @address 0x08049B70
 */
u8 BattleResult_DrawSkillNameGlyph(u16 *tilemap);
/** @brief Animate and draw the sliding battle-result banner.
 *  @note Writes the remaining OAM cursor through its argument and returns completion.
 *  @address 0x08049C1C
 */
u8 BattleResult_UpdateBanner(u8 *oamCursor);
/** @brief Upload result-banner graphics or the defeat tilemap.
 *  @note Only banner mode returns a C value; other modes preserve the original fallthrough.
 *  @address 0x08049D58
 */
u8 BattleResult_UploadGraphics(u8 oamCursor);
/** @brief Advance the defeat presentation and retry selection.
 *  @note Shares the result phase and window resources with victory presentation.
 *  @address 0x08049DF8
 */
u8 BattleResult_UpdateDefeat(struct BattleCombatant *combatants, struct BattleCombatant *cursor);
/** @brief Collect periodic effects and status-recovery candidates.
 *  @note Preserves enemy-mask reads from the party list and the single-party recovery path's append behavior.
 *  @address 0x0804A148
 */
void BattleResult_CollectTurnEndEffects(void);
/** @brief Apply queued recovery, MP drain, gauge gain, HP damage and status recovery.
 *  @note Returns one after all effect phases finish; defeat queues are processed per side.
 *  @address 0x0804A368
 */
u8 BattleResult_UpdateTurnEndEffects(struct BattleCombatant *combatants);
/** @brief Rebuild the turn-end list for a selected effect mask.
 *  @note The original enemy branch appends from the party scratch list; this behavior is retained.
 *  @address 0x0804AA2C
 */
void BattleResult_SelectTurnEndTargets(u16 effectMask);
/** @brief Start the current party slot's level-up palette highlight.
 *  @note Stores that combatant's OBJ palette bank and sets highlight phase one.
 *  @address 0x0804AB10
 */
void BattleResult_BeginLevelUpHighlight(void);
/** @brief 选择战斗结果消息，复位绘制游标并缓存字形。
 *  @note 按 SCRIPT_TEXT_END 分隔扫描消息，清零四个游标并设置控制位 0x400；普通 C 匹配 0x90 字节。
 *  @address 0x0804AB40
 */
const u16 *BattleResult_SelectMessage(u8 messageIndex);
/** @brief Clear the two-row, twenty-six-column result message area.
 *  @note The area begins at window tilemap column two, row one.
 *  @address 0x0804ABD0
 */
void BattleResult_ClearMessageArea(void);
/** @brief Emit one glyph from the current party member's name.
 *  @note Stops after four glyphs or at the encoded terminator.
 *  @address 0x0804ABF8
 */
u8 BattleResult_DrawPartyNameGlyph(u16 *tilemap, u8 characterId);
/** @brief Cache the current card reward's name glyphs when requested.
 *  @note Preserves the signed card-row index and the control-flag gate.
 *  @address 0x0804AC60
 */
void BattleResult_CacheCardNameGlyphs(void);
/** @brief Locate a skill-name stream and prepare its glyph cache.
 *  @note Names are sequential halfword streams separated by 0x0F00.
 *  @address 0x0804ACC0
 */
const u16 *BattleResult_GetSkillName(u8 skillIndex);
/** @brief Advance and draw the two-frame continuation arrow.
 *  @note Uses tilemap halfword offset 0x5B and a sixteen-frame cycle.
 *  @address 0x0804AD24
 */
void BattleResult_DrawContinueArrow(u16 *tilemap);
/** @brief Replace the continuation arrow with its blank tile.
 *  @note Writes tile value 0xB000 at halfword offset 0x5B.
 *  @address 0x0804AD54
 */
void BattleResult_ClearContinueArrow(u16 *tilemap);
/** @brief Set up the standalone sliding result-banner animation.
 *  @note Loads and binds its animation resources, resets the result mode and stops BGM.
 *  @address 0x0804AD60
 */
void BattleResult_InitBanner(void);
/** @brief Select victory presentation and reset its subphase.
 *  @note Does not allocate windows or reinitialize the reward workspaces.
 *  @address 0x0804ADE0
 */
void BattleResult_BeginVictory(void);
/** @brief Start the defeat scanline transition and reset retry selection.
 *  @note Selects result mode two and clears the shared frame and phase bytes.
 *  @address 0x0804ADF8
 */
void BattleResult_InitDefeat(void);

/* Read-only ROM data, in address order. */
extern const u16 D_0839B2E0[];                                 /* 0x0839B2E0; Result messages separated by 0x0F00. */
extern const u16 D_0839B462[];                                 /* 0x0839B462; Skill-name streams separated by 0x0F00. */
extern const u8 gEnemyNames[];                                  /* 0x0839CFBA; Shared enemy names: nine-byte FF-terminated/padded records. */
extern const u16 D_0839D348[];                                  /* 0x0839D348; High-id enemy names: nine halfword glyphs per record. */
extern const u16 D_08619430[];                                  /* 0x08619430; Result-banner palettes. */
extern const u16 D_08619A60[];                                  /* 0x08619A60; Result-banner animation definition. */

#endif
