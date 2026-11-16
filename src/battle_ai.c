#include "battle_ai.h"
#include "battle_items.h"
#include "battle_rules.h"
#include "battle.h"
#include "core.h"

/* Private .lcomm: eleven one-byte records at 03000D38, not a shared COMMON.
 * Character ids 0 and 1 have distinct remembered slots but share one policy. */
static struct BattleAiTargetMemory sBattleAiTargetMemory[BATTLE_CHARACTER_ID_COUNT];

/** @brief Assign a remembered target, or draw current and remembered targets separately.
 *  @note The two draws on a cache miss are distinct; do not reuse the first result. */
static inline void BattleAi_AssignCachedAttack(struct BattleCombatant *combatant, u8 *targets, u8 count)
{
    if (sBattleAiTargetMemory[combatant->id].target < 0)
    {
        combatant->targetSlot = targets[(s32)Battle_NextRandom() % count];
        sBattleAiTargetMemory[combatant->id].target = targets[(s32)Battle_NextRandom() % count];
    }
    else
        combatant->targetSlot = sBattleAiTargetMemory[combatant->id].target;
}

/** @brief Shared ordinary-attack policy for character ids other than 2/5.
 *  @note Keeps the original sixteen-byte target workspace and two-draw cache miss. */
static inline void BattleAi_ChooseStandardPartyAttack(struct BattleCombatant *combatant)
{
    u8 targets[16];
    u8 count = Battle_ListCombatants(Battle_GetCombatants(), targets, BATTLE_SIDE_ENEMY, BATTLE_AI_TARGET_MASK);

    combatant->action = BATTLE_ACTION_ATTACK;
    BattleAi_AssignCachedAttack(combatant, targets, count);
}

/** @brief Shared character-2/5 attack policy using mask 017F.
 *  @note Its original workspace is twenty-four bytes; set the action before listing. */
static inline void BattleAi_ChooseExtendedPartyAttack(struct BattleCombatant *combatant)
{
    u8 targets[24];
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 count;

    combatant->action = BATTLE_ACTION_ATTACK;
    {
        s32 mask = BATTLE_AI_EXTENDED_TARGET_MASK;
        count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_ENEMY, mask);
    }
    BattleAi_AssignCachedAttack(combatant, targets, count);
}

/** @brief Build automatic commands for eligible party records and commit items.
 *  @note Character ids select separate original entry points, even when policies match.
 *  @address 0x0804C78C
 */
void BattleAi_BuildPartyCommands(void)
{
    u8 slots[16];
    struct BattleCombatant *combatants;
    u8 count;
    u8 index;

    BattleItem_ResetReservations();
    combatants = Battle_GetCombatants();
    count = Battle_ListCombatants(combatants, slots, BATTLE_SIDE_PARTY, BATTLE_AI_PARTY_COMMAND_MASK);
    for (index = 0; index < count; index++)
    {
        struct BattleCombatant *combatant = &combatants[slots[index]];

        if ((u8)Battle_CheckStatusMask(combatant, 0x20) == BATTLE_STATUS_MATCH_REJECTED)
        {
            switch (combatant->id)
            {
            case 0:
            case 1: BattleAi_ChooseCharacter01Attack(combatant, slots[index]); break;
            case 2: BattleAi_ChooseCharacter2Attack(combatant, slots[index]); break;
            case 3: BattleAi_ChooseCharacter3Attack(combatant, slots[index]); break;
            case 4: BattleAi_ChooseCharacter4Attack(combatant, slots[index]); break;
            case 5: BattleAi_ChooseCharacter5Attack(combatant, slots[index]); break;
            case 6: BattleAi_ChooseCharacter6Attack(combatant, slots[index]); break;
            case 7: BattleAi_ChooseCharacter7Attack(combatant, slots[index]); break;
            case 8: BattleAi_ChooseCharacter8Attack(combatant, slots[index]); break;
            case 9: BattleAi_ChooseCharacter9Attack(combatant, slots[index]); break;
            case 10: BattleAi_ChooseCharacter10Attack(combatant, slots[index]); break;
            }
        }
    }
    BattleItem_CommitReservations();
}

/** @brief Replace confused party commands with attacks on another combatant.
 *  @note Preserves the extra random draw before choosing each target.
 *  The local back edge keeps agbcc's zero load inside the per-slot block.
 *  @address 0x0804C890
 */
void BattleAi_BuildConfusedPartyCommands(struct BattleCombatant *combatants)
{
    u8 slot;

    BattleItem_ResetReservations();
    slot = 0;
nextSlot:
    {
        struct BattleCombatant *combatant = &combatants[slot];

        if ((u8)Battle_CheckStatusMask(combatant, 0x20) == BATTLE_STATUS_MATCH_ACCEPTED)
        {
            Battle_NextRandom();
            combatant->targetSlot = BattleAi_SelectConfusedPartyTarget(combatants, slot);
            combatant->action = BATTLE_ACTION_ATTACK;
        }
    }
    slot++;
    if (slot < BATTLE_PARTY_COUNT)
        goto nextSlot;
    BattleItem_CommitReservations();
}

/** @brief Choose a party target other than self, falling back to enemy candidates.
 *  @note The original assumes a nonempty final list; no empty-list fallback is invented.
 *  @address 0x0804C8E0
 */
u8 BattleAi_SelectConfusedPartyTarget(struct BattleCombatant *combatants, u8 selfSlot)
{
    u8 targets[8];
    u8 index;
    u8 side = BATTLE_SIDE_PARTY;
    u8 count = Battle_ListCombatants(combatants, targets, side, BATTLE_AI_TARGET_MASK);

    {
        for (index = 0; index < count; index++)
        {
            if (targets[index] == selfSlot)
            {
                u8 copyIndex = index;
                for (; copyIndex < count - 1;)
                {
                    u8 *destination = &targets[copyIndex];
                    s32 next = copyIndex + 1;
                    *destination = targets[next];
                    copyIndex = next;
                }
                count--;
                break;
            }
        }
    }
    if (count == 0)
    {
        if (side == BATTLE_SIDE_PARTY)
            side = BATTLE_SIDE_ENEMY;
        else
            side = BATTLE_SIDE_PARTY;
        count = Battle_ListCombatants(combatants, targets, side, BATTLE_AI_ALTERNATE_TARGET_MASK);
        {
            for (index = 0; index < count; index++)
            {
                if (targets[index] == selfSlot)
                {
                    u8 copyIndex = index;
                    for (; copyIndex < count - 1;)
                    {
                        u8 *destination = &targets[copyIndex];
                        s32 next = copyIndex + 1;
                        *destination = targets[next];
                        copyIndex = next;
                    }
                    count--;
                    break;
                }
            }
        }
    }
    return targets[(s32)Battle_NextRandom() % count];
}

/** @brief Force the first eligible character-9 record to attack a random enemy.
 *  @address 0x0804C9B4
 */
void BattleAi_ChooseCharacter9Command(void)
{
    u8 slots[8];
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 count = Battle_ListCombatants(combatants, slots, BATTLE_SIDE_PARTY, BATTLE_AI_TARGET_MASK);
    u8 index;

    for (index = 0; index < count; index++)
    {
        struct BattleCombatant *combatant = &combatants[slots[index]];

        if (combatant->id == 9)
        {
            combatant->action = BATTLE_ACTION_ATTACK;
            combatants = Battle_GetCombatants();
            count = Battle_ListCombatants(combatants, slots, BATTLE_SIDE_ENEMY, BATTLE_AI_TARGET_MASK);
            combatant->targetSlot = slots[(s32)Battle_NextRandom() % count];
            break;
        }
    }
}

/** @brief Choose or reuse the ordinary attack target for character ids 0/1.
 *  @address 0x0804CA2C
 */
void BattleAi_ChooseCharacter01Attack(struct BattleCombatant *combatant, u8 unusedSlot)
{
    BattleAi_ChooseStandardPartyAttack(combatant);
}

/** @brief Choose character 2's attack using the extended target mask 017F.
 *  @address 0x0804CAA0
 */
void BattleAi_ChooseCharacter2Attack(struct BattleCombatant *combatant, u8 unusedSlot)
{
    BattleAi_ChooseExtendedPartyAttack(combatant);
}

/** @brief Choose or reuse character 3's ordinary attack target.
 *  @address 0x0804CB18
 */
void BattleAi_ChooseCharacter3Attack(struct BattleCombatant *combatant, u8 unusedSlot)
{
    BattleAi_ChooseStandardPartyAttack(combatant);
}

/** @brief Choose or reuse character 4's ordinary attack target.
 *  @address 0x0804CB8C
 */
void BattleAi_ChooseCharacter4Attack(struct BattleCombatant *combatant, u8 unusedSlot)
{
    BattleAi_ChooseStandardPartyAttack(combatant);
}

/** @brief Choose character 5's attack using the extended target mask 017F.
 *  @address 0x0804CC00
 */
void BattleAi_ChooseCharacter5Attack(struct BattleCombatant *combatant, u8 unusedSlot)
{
    BattleAi_ChooseExtendedPartyAttack(combatant);
}

/** @brief Choose or reuse character 6's ordinary attack target.
 *  @address 0x0804CC78
 */
void BattleAi_ChooseCharacter6Attack(struct BattleCombatant *combatant, u8 unusedSlot)
{
    BattleAi_ChooseStandardPartyAttack(combatant);
}

/** @brief Choose or reuse character 7's ordinary attack target.
 *  @address 0x0804CCEC
 */
void BattleAi_ChooseCharacter7Attack(struct BattleCombatant *combatant, u8 unusedSlot)
{
    BattleAi_ChooseStandardPartyAttack(combatant);
}

/** @brief Choose or reuse character 8's ordinary attack target.
 *  @address 0x0804CD60
 */
void BattleAi_ChooseCharacter8Attack(struct BattleCombatant *combatant, u8 unusedSlot)
{
    BattleAi_ChooseStandardPartyAttack(combatant);
}

/** @brief Choose or reuse character 9's ordinary attack target.
 *  @address 0x0804CDD4
 */
void BattleAi_ChooseCharacter9Attack(struct BattleCombatant *combatant, u8 unusedSlot)
{
    BattleAi_ChooseStandardPartyAttack(combatant);
}

/** @brief Choose or reuse character 10's ordinary attack target.
 *  @address 0x0804CE48
 */
void BattleAi_ChooseCharacter10Attack(struct BattleCombatant *combatant, u8 unusedSlot)
{
    BattleAi_ChooseStandardPartyAttack(combatant);
}

/** @brief Invalidate all eleven remembered targets with the signed sentinel -1.
 *  @address 0x0804CEBC
 */
void BattleAi_ClearTargetMemory(void)
{
    u8 character;

    for (character = 0; character < BATTLE_CHARACTER_ID_COUNT; character++)
        sBattleAiTargetMemory[character].target = -1;
}

/** @brief Advance a byte-sized formation index with its original truncation. */
static inline u8 BattleAi_AdvanceByteIndex(u8 index)
{
    return index + 1;
}

/** @brief Choose an ordinary enemy attack/skill and resolve its targeting mode.
 *  @note Tries at most twice, falls back to defend, and handles confusion and silence.
 *  @address 0x0804CEE0
 */
void BattleAi_ChooseNormalEnemyCommand(struct BattleCombatant *combatant, struct BattleCombatant *combatants)
{
    u8 targets[8];
    const struct BattleAnimationResourceConfig *config;
    s32 retry = 1;
    u8 attempt = 0;
    s32 actionCategory;
    u8 count;
    u16 animationId;

    if (combatant->status != BATTLE_STATUS_CONFUSED)
    {
        while (attempt < 2 && retry != 0)
        {
            count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_PARTY, BATTLE_AI_TARGET_MASK);
            if ((s32)Battle_NextRandom() % 100 < 50)
                combatant->action = BATTLE_ACTION_SKILL;
            else
                combatant->action = BATTLE_ACTION_ATTACK;

            switch (combatant->action)
            {
            case BATTLE_ACTION_ATTACK:
                animationId = combatant->source.enemy->animationBaseId + 3;
                config = &gBattleAnimationConfigs[animationId];
                break;
            case BATTLE_ACTION_SKILL:
                {
                    s32 baseAnimationId = combatant->source.enemy->animationBaseId + 3;

                    animationId = baseAnimationId + combatant->source.enemy->alternateAnimationOffset;
                    config = &gBattleAnimationConfigs[animationId];
                }
                break;
            }
            switch (config->targetMode)
            {
            case BATTLE_ANIMATION_TARGET_RANDOM:
                {
                    u64 randomValue = Battle_NextRandom();
                    u8 index = (u8)randomValue % (u32)count;

                    combatant->targetSlot = targets[index];
                }
                retry = 0;
                break;
            case BATTLE_ANIMATION_TARGET_SLOT_ZERO:
                combatant->targetSlot = 0;
                retry = 0;
                break;
            case BATTLE_ANIMATION_TARGET_RANDOM_ALTERNATE:
                {
                    u8 index = (u8)Battle_NextRandom() % (u32)count;

                    combatant->targetSlot = targets[index];
                }
                retry = 0;
                break;
            case BATTLE_ANIMATION_TARGET_FORMATION:
                {
                    u8 formation;
                    u8 index;
                    u16 mode;
                    s32 stride;

                    formation = combatant->formationBits;
                    if (config->targetMode == BATTLE_ANIMATION_TARGET_RANDOM_ALTERNATE)
                        formation &= BATTLE_FORMATION_COLUMN_MASK;
                    else
                        formation &= BATTLE_FORMATION_ROW_MASK;
                    index = 0;
                    if (index >= count)
                        break;
                    mode = config->targetMode;
                    stride = sizeof(struct BattleCombatant);
                    while (index < count)
                    {
                        s32 offset = targets[index] * stride;
                        const struct BattleCombatant *candidate =
                            (const struct BattleCombatant *)(offset + (u32)combatants);
                        u8 bits = candidate->formationBits;
                        u8 masked;

                        if (mode == BATTLE_ANIMATION_TARGET_RANDOM_ALTERNATE)
                            masked = bits & BATTLE_FORMATION_COLUMN_MASK;
                        else
                            masked = bits & BATTLE_FORMATION_ROW_MASK;
                        if (formation == masked)
                        {
                            combatant->targetSlot = targets[index];
                            break;
                        }
                        index = BattleAi_AdvanceByteIndex(index);
                    }
                    if (index < count)
                    {
                        u8 randomIndex;

                        count = Battle_ListActionTargetSlots(combatant, targets, BATTLE_AI_ALTERNATE_TARGET_MASK);
                        randomIndex = (u8)Battle_NextRandom() % (u32)count;
                        combatant->targetSlot = targets[randomIndex];
                        retry = 0;
                    }
                }
                break;
            }
            attempt++;
        }

        if (retry == 1 && attempt > 1)
            combatant->action = BATTLE_ACTION_DEFEND;

        switch (combatant->action)
        {
        case BATTLE_ACTION_ATTACK:
            {
                const u8 *actions = (const u8 *)gBattleEnemyActionInfo;
                s32 offset = (combatant->id - 12) * sizeof(struct BattleEnemyActionInfo);

                actions += offsetof(struct BattleEnemyActionInfo, attackCategory);
                actionCategory = actions[offset];
            }
            break;
        case BATTLE_ACTION_SKILL:
            {
                const u8 *actions = (const u8 *)gBattleEnemyActionInfo;
                s32 offset = (combatant->id - 12) * sizeof(struct BattleEnemyActionInfo);

                actions += offsetof(struct BattleEnemyActionInfo, skillCategory);
                actionCategory = actions[offset];
            }
            break;
        }
        if (actionCategory == BATTLE_ENEMY_ACTION_MAGICAL && combatant->status == BATTLE_STATUS_SILENCED)
            combatant->action = BATTLE_ACTION_DEFEND;
    }
    else
        BattleAi_ChooseConfusedEnemyCommand(combatant);
}

/** @brief Handle confused enemies: attack another eligible record or defend.
 *  @note The enemy's confusion policy is a full word at record +1C.
 *  @address 0x0804D0F8
 */
void BattleAi_ChooseConfusedEnemyCommand(struct BattleCombatant *combatant)
{
    u8 targets[8];
    u8 count = 0;
    struct BattleCombatant *combatants;
    u8 index;

    if (combatant->source.enemy->actionSelection.confusedActionPolicy == 0)
    {
        combatant->action = BATTLE_ACTION_ATTACK;
        combatants = Battle_GetCombatants();
        count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_ENEMY, BATTLE_AI_ALTERNATE_TARGET_MASK);
        if (count <= 1)
            count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_PARTY, BATTLE_AI_ALTERNATE_TARGET_MASK);
        for (index = 0; index < count; index++)
        {
            if (combatants[targets[index]].formationBits == combatant->formationBits)
            {
                u8 copyIndex;
                for (copyIndex = index; copyIndex < count - 1;)
                {
                    u8 *destination = &targets[copyIndex];
                    s32 next = copyIndex + 1;
                    *destination = targets[next];
                    copyIndex = next;
                }
                count--;
                break;
            }
        }
        combatant->targetSlot = targets[(u8)((u8)Battle_NextRandom() % (u32)count)];
    }
    else
        combatant->action = BATTLE_ACTION_DEFEND;
}

/** @brief Boss 71: skill chance is 15 times the eligible party count out of 101.
 *  @address 0x0804D1B4
 */
void BattleAi_ChooseBoss71Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants)
{
    u8 targets[8];
    u8 count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_PARTY, BATTLE_AI_ALTERNATE_TARGET_MASK);
    const struct BattleAnimationResourceConfig *config;

    if (Battle_NextRandom() % 101 < count * 15)
        combatant->action = BATTLE_ACTION_SKILL;
    else
        combatant->action = BATTLE_ACTION_ATTACK;

    switch (combatant->action)
    {
    case BATTLE_ACTION_ATTACK:
        config = &gBattleAnimationConfigs[combatant->source.boss->attackAnimationId];
        break;
    case BATTLE_ACTION_SKILL:
        combatant->enemySkillIndex = 0;
        config = &gBattleAnimationConfigs[combatant->source.boss->skillAnimationIds[0]];
        break;
    }
    switch (config->targetMode)
    {
    case BATTLE_ANIMATION_TARGET_RANDOM:
        {
            u8 index = (u8)Battle_NextRandom() % (u32)count;
            combatant->targetSlot = targets[index];
        }
        break;
    case BATTLE_ANIMATION_TARGET_SLOT_ZERO:
        combatant->targetSlot = 0;
        break;
    }
}

/** @brief Boss 72: skill chance is 10 times the eligible party count out of 101.
 *  @address 0x0804D260
 */
void BattleAi_ChooseBoss72Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants)
{
    u8 targets[8];
    u8 count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_PARTY, BATTLE_AI_ALTERNATE_TARGET_MASK);
    const struct BattleAnimationResourceConfig *config;

    if (Battle_NextRandom() % 101 < count * 10)
        combatant->action = BATTLE_ACTION_SKILL;
    else
        combatant->action = BATTLE_ACTION_ATTACK;

    switch (combatant->action)
    {
    case BATTLE_ACTION_ATTACK:
        config = &gBattleAnimationConfigs[combatant->source.boss->attackAnimationId];
        break;
    case BATTLE_ACTION_SKILL:
        combatant->enemySkillIndex = 0;
        config = &gBattleAnimationConfigs[combatant->source.boss->skillAnimationIds[0]];
        break;
    }
    switch (config->targetMode)
    {
    case BATTLE_ANIMATION_TARGET_RANDOM:
        {
            u8 index = (u8)Battle_NextRandom() % (u32)count;
            combatant->targetSlot = targets[index];
        }
        break;
    case BATTLE_ANIMATION_TARGET_SLOT_ZERO:
        combatant->targetSlot = 0;
        break;
    }
}

/** @brief Boss 73: make the original random decision, then force ordinary attack.
 *  @note Both action writes and the consumed random draw are intentional here.
 *  @address 0x0804D310
 */
void BattleAi_ChooseBoss73Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants)
{
    u8 targets[8];
    u8 count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_PARTY, BATTLE_AI_ALTERNATE_TARGET_MASK);
    const struct BattleAnimationResourceConfig *config;

    if (Battle_NextRandom() % 101 < count * 10)
        combatant->action = BATTLE_ACTION_SKILL;
    else
        combatant->action = BATTLE_ACTION_ATTACK;
    combatant->action = BATTLE_ACTION_ATTACK;

    switch (combatant->action)
    {
    case BATTLE_ACTION_ATTACK:
        config = &gBattleAnimationConfigs[combatant->source.boss->attackAnimationId];
        break;
    case BATTLE_ACTION_SKILL:
        combatant->enemySkillIndex = 0;
        config = &gBattleAnimationConfigs[combatant->source.boss->skillAnimationIds[0]];
        break;
    }
    switch (config->targetMode)
    {
    case BATTLE_ANIMATION_TARGET_RANDOM:
        {
            u8 index = (u8)Battle_NextRandom() % (u32)count;
            combatant->targetSlot = targets[index];
        }
        break;
    case BATTLE_ANIMATION_TARGET_SLOT_ZERO:
        combatant->targetSlot = 0;
        break;
    }
}

/** @brief Boss 74: skill chance is 13 times the eligible party count out of 101.
 *  @address 0x0804D3A0
 */
void BattleAi_ChooseBoss74Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants)
{
    u8 targets[8];
    u8 count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_PARTY, BATTLE_AI_ALTERNATE_TARGET_MASK);
    const struct BattleAnimationResourceConfig *config;

    if (Battle_NextRandom() % 101 < count * 13)
        combatant->action = BATTLE_ACTION_SKILL;
    else
        combatant->action = BATTLE_ACTION_ATTACK;

    switch (combatant->action)
    {
    case BATTLE_ACTION_ATTACK:
        config = &gBattleAnimationConfigs[combatant->source.boss->attackAnimationId];
        break;
    case BATTLE_ACTION_SKILL:
        combatant->enemySkillIndex = 0;
        config = &gBattleAnimationConfigs[combatant->source.boss->skillAnimationIds[0]];
        break;
    }
    switch (config->targetMode)
    {
    case BATTLE_ANIMATION_TARGET_RANDOM:
        {
            u8 index = (u8)Battle_NextRandom() % (u32)count;
            combatant->targetSlot = targets[index];
        }
        break;
    case BATTLE_ANIMATION_TARGET_SLOT_ZERO:
        combatant->targetSlot = 0;
        break;
    }
}

/** @brief Boss 75: skill chance is 10 times the eligible party count out of 101.
 *  @address 0x0804D44C
 */
void BattleAi_ChooseBoss75Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants)
{
    u8 targets[8];
    u8 count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_PARTY, BATTLE_AI_ALTERNATE_TARGET_MASK);
    const struct BattleAnimationResourceConfig *config;

    if (Battle_NextRandom() % 101 < count * 10)
        combatant->action = BATTLE_ACTION_SKILL;
    else
        combatant->action = BATTLE_ACTION_ATTACK;

    switch (combatant->action)
    {
    case BATTLE_ACTION_ATTACK:
        config = &gBattleAnimationConfigs[combatant->source.boss->attackAnimationId];
        break;
    case BATTLE_ACTION_SKILL:
        combatant->enemySkillIndex = 0;
        config = &gBattleAnimationConfigs[combatant->source.boss->skillAnimationIds[0]];
        break;
    }
    switch (config->targetMode)
    {
    case BATTLE_ANIMATION_TARGET_RANDOM:
        {
            u8 index = (u8)Battle_NextRandom() % (u32)count;
            combatant->targetSlot = targets[index];
        }
        break;
    case BATTLE_ANIMATION_TARGET_SLOT_ZERO:
        combatant->targetSlot = 0;
        break;
    }
}

/** @brief Boss 76: count-scaled skill chance, choosing between two skill animations.
 *  @address 0x0804D4FC
 */
void BattleAi_ChooseBoss76Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants)
{
    u8 targets[8];
    u8 count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_PARTY, BATTLE_AI_ALTERNATE_TARGET_MASK);
    const struct BattleAnimationResourceConfig *config;

    if (Battle_NextRandom() % 101 < count * 10)
        combatant->action = BATTLE_ACTION_SKILL;
    else
        combatant->action = BATTLE_ACTION_ATTACK;

    {
        s32 action = combatant->action;

        switch (action)
        {
        case BATTLE_ACTION_ATTACK:
            config = &gBattleAnimationConfigs[combatant->source.boss->attackAnimationId];
            break;
        case BATTLE_ACTION_SKILL:
            combatant->enemySkillIndex = Battle_NextRandom() & action;
            config = &gBattleAnimationConfigs[combatant->source.boss->skillAnimationIds[combatant->enemySkillIndex]];
            break;
        }
    }
    switch (config->targetMode)
    {
    case BATTLE_ANIMATION_TARGET_RANDOM:
        {
            u8 index = (u8)Battle_NextRandom() % (u32)count;
            combatant->targetSlot = targets[index];
        }
        break;
    case BATTLE_ANIMATION_TARGET_SLOT_ZERO:
        combatant->targetSlot = 0;
        break;
    }
}

/** @brief Boss 77: coordinate two same-side records before selecting the skill.
 *  @note Can defend when the other formation has already selected skill 1.
 *  @address 0x0804D5B4
 */
void BattleAi_ChooseBoss77Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants)
{
    u8 targets[8];
    s32 paired = 0;
    u8 count;
    const struct BattleAnimationResourceConfig *config;

    count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_ENEMY, BATTLE_AI_ALTERNATE_TARGET_MASK);
    {
        u64 randomValue = Battle_NextRandom();

        if ((u32)randomValue % 101 < count * 10)
            combatant->action = BATTLE_ACTION_SKILL;
        else
            combatant->action = BATTLE_ACTION_ATTACK;
    }

    if (count == 2)
    {
        u8 index;

        paired = 1;
        for (index = 0; index < 2; index++)
        {
            s32 offset = targets[index] * sizeof(struct BattleCombatant);
            const struct BattleCombatant *candidate =
                (const struct BattleCombatant *)(offset + (u32)combatants);

            if (combatant->formationBits != candidate->formationBits)
            {
                if (candidate->action == BATTLE_ACTION_SKILL && candidate->enemySkillIndex == 1)
                    combatant->action = BATTLE_ACTION_DEFEND;
                break;
            }
        }
    }

    count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_PARTY, BATTLE_AI_ALTERNATE_TARGET_MASK);
    switch (combatant->action)
    {
    case BATTLE_ACTION_ATTACK:
        config = &gBattleAnimationConfigs[combatant->source.boss->attackAnimationId];
        break;
    case BATTLE_ACTION_SKILL:
        if (paired == 1 && Battle_NextRandom() % 100 < 50)
            combatant->enemySkillIndex = paired;
        else
            combatant->enemySkillIndex = 0;
        config = &gBattleAnimationConfigs[combatant->source.boss->skillAnimationIds[combatant->enemySkillIndex]];
        break;
    }
    switch (config->targetMode)
    {
    case BATTLE_ANIMATION_TARGET_RANDOM:
        {
            u8 index = (u8)Battle_NextRandom() % (u32)count;

            combatant->targetSlot = targets[index];
        }
        break;
    case BATTLE_ANIMATION_TARGET_SLOT_ZERO:
        combatant->targetSlot = 0;
        break;
    }
}

/** @brief Boss 78: retain the random decision and then force ordinary attack.
 *  @address 0x0804D708
 */
void BattleAi_ChooseBoss78Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants)
{
    u8 targets[8];
    u8 count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_PARTY, BATTLE_AI_ALTERNATE_TARGET_MASK);
    const struct BattleAnimationResourceConfig *config;

    if (Battle_NextRandom() % 101 < count * 10)
        combatant->action = BATTLE_ACTION_SKILL;
    else
        combatant->action = BATTLE_ACTION_ATTACK;
    combatant->action = BATTLE_ACTION_ATTACK;

    switch (combatant->action)
    {
    case BATTLE_ACTION_ATTACK:
        config = &gBattleAnimationConfigs[combatant->source.boss->attackAnimationId];
        break;
    case BATTLE_ACTION_SKILL:
        combatant->enemySkillIndex = 0;
        config = &gBattleAnimationConfigs[combatant->source.boss->skillAnimationIds[0]];
        break;
    }
    switch (config->targetMode)
    {
    case BATTLE_ANIMATION_TARGET_RANDOM:
        {
            u8 index = (u8)Battle_NextRandom() % (u32)count;
            combatant->targetSlot = targets[index];
        }
        break;
    case BATTLE_ANIMATION_TARGET_SLOT_ZERO:
        combatant->targetSlot = 0;
        break;
    }
}

/** @brief Boss 79: retain the initial decision and then force one of four skills.
 *  @address 0x0804D798
 */
void BattleAi_ChooseBoss79Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants)
{
    u8 targets[8];
    u8 count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_PARTY, BATTLE_AI_ALTERNATE_TARGET_MASK);
    const struct BattleAnimationResourceConfig *config;

    if (Battle_NextRandom() % 101 < count * 10)
        combatant->action = BATTLE_ACTION_SKILL;
    else
        combatant->action = BATTLE_ACTION_ATTACK;
    combatant->action = BATTLE_ACTION_SKILL;

    switch (combatant->action)
    {
    case BATTLE_ACTION_ATTACK:
        config = &gBattleAnimationConfigs[combatant->source.boss->attackAnimationId];
        break;
    case BATTLE_ACTION_SKILL:
        {
            s32 skill = Battle_NextRandom();

            skill &= 3;
            combatant->enemySkillIndex = skill;
        }
        config = &gBattleAnimationConfigs[combatant->source.boss->skillAnimationIds[combatant->enemySkillIndex]];
        break;
    }
    switch (config->targetMode)
    {
    case BATTLE_ANIMATION_TARGET_RANDOM:
        {
            u8 index = (u8)Battle_NextRandom() % (u32)count;
            combatant->targetSlot = targets[index];
        }
        break;
    case BATTLE_ANIMATION_TARGET_SLOT_ZERO:
        combatant->targetSlot = 0;
        break;
    }
}

/** @brief Boss 7A: 70 outcomes out of 101 choose one of two skill animations.
 *  @address 0x0804D840
 */
void BattleAi_ChooseBoss7ACommand(struct BattleCombatant *combatant, struct BattleCombatant *combatants)
{
    u8 targets[8];
    u8 count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_PARTY, BATTLE_AI_ALTERNATE_TARGET_MASK);
    const struct BattleAnimationResourceConfig *config;

    if (Battle_NextRandom() % 101 < 70)
        combatant->action = BATTLE_ACTION_SKILL;
    else
        combatant->action = BATTLE_ACTION_ATTACK;

    {
        s32 action = combatant->action;

        switch (action)
        {
        case BATTLE_ACTION_ATTACK:
            config = &gBattleAnimationConfigs[combatant->source.boss->attackAnimationId];
            break;
        case BATTLE_ACTION_SKILL:
            combatant->enemySkillIndex = Battle_NextRandom() & action;
            config = &gBattleAnimationConfigs[combatant->source.boss->skillAnimationIds[combatant->enemySkillIndex]];
            break;
        }
    }
    switch (config->targetMode)
    {
    case BATTLE_ANIMATION_TARGET_RANDOM:
        {
            u8 index = (u8)Battle_NextRandom() % (u32)count;
            combatant->targetSlot = targets[index];
        }
        break;
    case BATTLE_ANIMATION_TARGET_SLOT_ZERO:
        combatant->targetSlot = 0;
        break;
    }
}

/** @brief Boss 7E: trigger skill 1 once below four tenths of maximum HP.
 *  @note The threshold is (maxHp / 10) * 4, with halfword truncation; bit 0400 latches it.
 *  @address 0x0804D8F4
 */
void BattleAi_ChooseBoss7ECommand(struct BattleCombatant *combatant, struct BattleCombatant *combatants)
{
    u8 targets[8];
    u8 special = 0;
    u16 threshold = (combatant->maxHp / 10U) * 4;
    u8 count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_PARTY, BATTLE_AI_ALTERNATE_TARGET_MASK);
    const struct BattleAnimationResourceConfig *config;

    if (!(combatant->displayState & 0x400) && combatant->hp < threshold)
    {
        combatant->action = BATTLE_ACTION_SKILL;
        combatant->displayState |= 0x400;
        special = 1;
    }
    else
    {
        if (Battle_NextRandom() % 101 < 70)
            combatant->action = BATTLE_ACTION_SKILL;
        else
            combatant->action = BATTLE_ACTION_ATTACK;
    }
    switch (combatant->action)
    {
    case BATTLE_ACTION_ATTACK:
        config = &gBattleAnimationConfigs[combatant->source.boss->attackAnimationId];
        break;
    case BATTLE_ACTION_SKILL:
        if (special == 0)
            combatant->enemySkillIndex = special;
        else
            combatant->enemySkillIndex = 1;
        config = &gBattleAnimationConfigs[combatant->source.boss->skillAnimationIds[combatant->enemySkillIndex]];
        break;
    }
    switch (config->targetMode)
    {
    case BATTLE_ANIMATION_TARGET_RANDOM:
        {
            u8 index = (u8)Battle_NextRandom() % (u32)count;
            combatant->targetSlot = targets[index];
        }
        break;
    case BATTLE_ANIMATION_TARGET_SLOT_ZERO:
        combatant->targetSlot = 0;
        break;
    }
}

/** @brief Boss 7D: 70 outcomes out of 101 choose one of three skills.
 *  @address 0x0804DA04
 */
void BattleAi_ChooseBoss7DCommand(struct BattleCombatant *combatant, struct BattleCombatant *combatants)
{
    u8 targets[8];
    u8 count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_PARTY, BATTLE_AI_ALTERNATE_TARGET_MASK);
    const struct BattleAnimationResourceConfig *config;

    if (Battle_NextRandom() % 101 < 70)
        combatant->action = BATTLE_ACTION_SKILL;
    else
        combatant->action = BATTLE_ACTION_ATTACK;

    switch (combatant->action)
    {
    case BATTLE_ACTION_ATTACK:
        config = &gBattleAnimationConfigs[combatant->source.boss->attackAnimationId];
        break;
    case BATTLE_ACTION_SKILL:
        combatant->enemySkillIndex = Battle_NextRandom() % 3;
        config = &gBattleAnimationConfigs[combatant->source.boss->skillAnimationIds[combatant->enemySkillIndex]];
        break;
    }
    switch (config->targetMode)
    {
    case BATTLE_ANIMATION_TARGET_RANDOM:
        {
            u8 index = (u8)Battle_NextRandom() % (u32)count;
            combatant->targetSlot = targets[index];
        }
        break;
    case BATTLE_ANIMATION_TARGET_SLOT_ZERO:
        combatant->targetSlot = 0;
        break;
    }
}

/** @brief Bosses 7B/7C: force a skill; random variant 1 is remapped to variant 0.
 *  @address 0x0804DABC
 */
void BattleAi_ChooseBoss7B7CCommand(struct BattleCombatant *combatant, struct BattleCombatant *combatants)
{
    u8 targets[8];
    u8 count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_PARTY, BATTLE_AI_ALTERNATE_TARGET_MASK);
    const struct BattleAnimationResourceConfig *config;

    if (Battle_NextRandom() % 101 < 70)
        combatant->action = BATTLE_ACTION_SKILL;
    else
        combatant->action = BATTLE_ACTION_ATTACK;
    combatant->action = BATTLE_ACTION_SKILL;

    switch (combatant->action)
    {
    case BATTLE_ACTION_ATTACK:
        config = &gBattleAnimationConfigs[combatant->source.boss->attackAnimationId];
        break;
    case BATTLE_ACTION_SKILL:
        combatant->enemySkillIndex = Battle_NextRandom() & 3;
        if (combatant->enemySkillIndex == 1)
            combatant->enemySkillIndex = 0;
        config = &gBattleAnimationConfigs[combatant->source.boss->skillAnimationIds[combatant->enemySkillIndex]];
        break;
    }
    switch (config->targetMode)
    {
    case BATTLE_ANIMATION_TARGET_RANDOM:
        {
            u8 index = (u8)Battle_NextRandom() % (u32)count;
            combatant->targetSlot = targets[index];
        }
        break;
    case BATTLE_ANIMATION_TARGET_SLOT_ZERO:
        combatant->targetSlot = 0;
        break;
    }
}

/** @brief Boss 7F: choose among five skills, remapping variant 2 to variant 1.
 *  @address 0x0804DB64
 */
void BattleAi_ChooseBoss7FCommand(struct BattleCombatant *combatant, struct BattleCombatant *combatants)
{
    u8 targets[8];
    u8 count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_PARTY, BATTLE_AI_ALTERNATE_TARGET_MASK);
    const struct BattleAnimationResourceConfig *config;

    if (Battle_NextRandom() % 101 < 70)
        combatant->action = BATTLE_ACTION_SKILL;
    else
        combatant->action = BATTLE_ACTION_ATTACK;

    switch (combatant->action)
    {
    case BATTLE_ACTION_ATTACK:
        config = &gBattleAnimationConfigs[combatant->source.boss->attackAnimationId];
        break;
    case BATTLE_ACTION_SKILL:
        combatant->enemySkillIndex = Battle_NextRandom() % 5;
        if (combatant->enemySkillIndex == 2)
            combatant->enemySkillIndex = 1;
        config = &gBattleAnimationConfigs[combatant->source.boss->skillAnimationIds[combatant->enemySkillIndex]];
        break;
    }
    switch (config->targetMode)
    {
    case BATTLE_ANIMATION_TARGET_RANDOM:
        {
            u8 index = (u8)Battle_NextRandom() % (u32)count;
            combatant->targetSlot = targets[index];
        }
        break;
    case BATTLE_ANIMATION_TARGET_SLOT_ZERO:
        combatant->targetSlot = 0;
        break;
    }
}

/** @brief Boss 80: 60 outcomes out of 100 choose one of two skills.
 *  @address 0x0804DC24
 */
void BattleAi_ChooseBoss80Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants)
{
    u8 targets[8];
    u8 count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_PARTY, BATTLE_AI_ALTERNATE_TARGET_MASK);
    const struct BattleAnimationResourceConfig *config;

    if (Battle_NextRandom() % 100 < 60)
        combatant->action = BATTLE_ACTION_SKILL;
    else
        combatant->action = BATTLE_ACTION_ATTACK;

    {
        s32 action = combatant->action;

        switch (action)
        {
        case BATTLE_ACTION_ATTACK:
            config = &gBattleAnimationConfigs[combatant->source.boss->attackAnimationId];
            break;
        case BATTLE_ACTION_SKILL:
            combatant->enemySkillIndex = Battle_NextRandom() & action;
            config = &gBattleAnimationConfigs[combatant->source.boss->skillAnimationIds[combatant->enemySkillIndex]];
            break;
        }
    }
    switch (config->targetMode)
    {
    case BATTLE_ANIMATION_TARGET_RANDOM:
        {
            u8 index = (u8)Battle_NextRandom() % (u32)count;
            combatant->targetSlot = targets[index];
        }
        break;
    case BATTLE_ANIMATION_TARGET_SLOT_ZERO:
        combatant->targetSlot = 0;
        break;
    }
}

/** @brief Boss 81: retain the initial random decision, then force skill variant 0.
 *  @address 0x0804DCD8
 */
void BattleAi_ChooseBoss81Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants)
{
    u8 targets[8];
    u8 count = Battle_ListCombatants(combatants, targets, BATTLE_SIDE_PARTY, BATTLE_AI_ALTERNATE_TARGET_MASK);
    const struct BattleAnimationResourceConfig *config;

    if (Battle_NextRandom() % 101 < count * 10)
        combatant->action = BATTLE_ACTION_SKILL;
    else
        combatant->action = BATTLE_ACTION_ATTACK;
    combatant->action = BATTLE_ACTION_SKILL;

    switch (combatant->action)
    {
    case BATTLE_ACTION_ATTACK:
        config = &gBattleAnimationConfigs[combatant->source.boss->attackAnimationId];
        break;
    case BATTLE_ACTION_SKILL:
        combatant->enemySkillIndex = 0;
        config = &gBattleAnimationConfigs[combatant->source.boss->skillAnimationIds[0]];
        break;
    }
    switch (config->targetMode)
    {
    case BATTLE_ANIMATION_TARGET_RANDOM:
        {
            u8 index = (u8)Battle_NextRandom() % (u32)count;
            combatant->targetSlot = targets[index];
        }
        break;
    case BATTLE_ANIMATION_TARGET_SLOT_ZERO:
        combatant->targetSlot = 0;
        break;
    }
}

/** @brief Dispatch a high-id enemy through the seventeen-entry ROM policy table.
 *  @note Caller guarantees id 71..81 hexadecimal; table 7B/7C entries share a handler.
 *  @address 0x0804DD70
 */
void BattleAi_ChooseBossCommand(struct BattleCombatant *combatant, struct BattleCombatant *combatants)
{
    gBattleBossAiHandlers[combatant->id - BATTLE_AI_BOSS_FIRST_ID](combatant, combatants);
}

/* Split read-only data, in ROM address order. */

/* ROM 0x0839CE38..0x0839CE7C; .rodata. */
BattleAiBossHandler const gBattleBossAiHandlers[BATTLE_AI_BOSS_COUNT] __attribute__((aligned(1))) =
{
    (BattleAiBossHandler)BattleAi_ChooseBoss71Command, (BattleAiBossHandler)BattleAi_ChooseBoss72Command,
    (BattleAiBossHandler)BattleAi_ChooseBoss73Command, (BattleAiBossHandler)BattleAi_ChooseBoss74Command,
    (BattleAiBossHandler)BattleAi_ChooseBoss75Command, (BattleAiBossHandler)BattleAi_ChooseBoss76Command,
    (BattleAiBossHandler)BattleAi_ChooseBoss77Command, (BattleAiBossHandler)BattleAi_ChooseBoss78Command,
    (BattleAiBossHandler)BattleAi_ChooseBoss79Command, (BattleAiBossHandler)BattleAi_ChooseBoss7ACommand,
    (BattleAiBossHandler)BattleAi_ChooseBoss7B7CCommand, (BattleAiBossHandler)BattleAi_ChooseBoss7B7CCommand,
    (BattleAiBossHandler)BattleAi_ChooseBoss7DCommand, (BattleAiBossHandler)BattleAi_ChooseBoss7ECommand,
    (BattleAiBossHandler)BattleAi_ChooseBoss7FCommand, (BattleAiBossHandler)BattleAi_ChooseBoss80Command,
    (BattleAiBossHandler)BattleAi_ChooseBoss81Command,
};

/* End of split read-only data. */
