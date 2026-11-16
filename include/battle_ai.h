#ifndef GUARD_BATTLE_AI_H
#define GUARD_BATTLE_AI_H

#include "battle_types.h"

#define BATTLE_AI_BOSS_FIRST_ID 0x71
#define BATTLE_AI_BOSS_COUNT 17

/** @brief Status masks passed to the rules-layer list builder.
 *  @note These are masks, not status numbers or booleans. */
enum BattleAiTargetMask
{
    BATTLE_AI_PARTY_COMMAND_MASK = 0x43,
    BATTLE_AI_ALTERNATE_TARGET_MASK = 0x6F,
    BATTLE_AI_TARGET_MASK = 0x7F,
    BATTLE_AI_EXTENDED_TARGET_MASK = 0x17F,
};

/** @brief One remembered party target; negative values mean no remembered slot.
 *  @note The signed eight-bit packed field preserves agbcc's byte accesses,
 *  including the original read/OR/write when reset to FF. Storage is private. */
struct BattleAiTargetMemory
{
    s8 target : 8;
} __attribute__((packed));

typedef char BattleAiTargetMemorySizeCheck[(sizeof(struct BattleAiTargetMemory) == 1) ? 1 : -1];
typedef void (*BattleAiBossHandler)(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
extern BattleAiBossHandler const gBattleBossAiHandlers[BATTLE_AI_BOSS_COUNT]; /* 0839CE38 */

void BattleAi_BuildPartyCommands(void);
void BattleAi_BuildConfusedPartyCommands(struct BattleCombatant *combatants);
u8 BattleAi_SelectConfusedPartyTarget(struct BattleCombatant *combatants, u8 selfSlot);
void BattleAi_ChooseCharacter9Command(void);
void BattleAi_ChooseCharacter01Attack(struct BattleCombatant *combatant, u8 unusedSlot);
void BattleAi_ChooseCharacter2Attack(struct BattleCombatant *combatant, u8 unusedSlot);
void BattleAi_ChooseCharacter3Attack(struct BattleCombatant *combatant, u8 unusedSlot);
void BattleAi_ChooseCharacter4Attack(struct BattleCombatant *combatant, u8 unusedSlot);
void BattleAi_ChooseCharacter5Attack(struct BattleCombatant *combatant, u8 unusedSlot);
void BattleAi_ChooseCharacter6Attack(struct BattleCombatant *combatant, u8 unusedSlot);
void BattleAi_ChooseCharacter7Attack(struct BattleCombatant *combatant, u8 unusedSlot);
void BattleAi_ChooseCharacter8Attack(struct BattleCombatant *combatant, u8 unusedSlot);
void BattleAi_ChooseCharacter9Attack(struct BattleCombatant *combatant, u8 unusedSlot);
void BattleAi_ChooseCharacter10Attack(struct BattleCombatant *combatant, u8 unusedSlot);
void BattleAi_ClearTargetMemory(void);
void BattleAi_ChooseNormalEnemyCommand(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
void BattleAi_ChooseConfusedEnemyCommand(struct BattleCombatant *combatant);

/* Boss suffixes are hexadecimal enemy ids, not inferred creature names. */
void BattleAi_ChooseBoss71Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
void BattleAi_ChooseBoss72Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
void BattleAi_ChooseBoss73Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
void BattleAi_ChooseBoss74Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
void BattleAi_ChooseBoss75Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
void BattleAi_ChooseBoss76Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
void BattleAi_ChooseBoss77Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
void BattleAi_ChooseBoss78Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
void BattleAi_ChooseBoss79Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
void BattleAi_ChooseBoss7ACommand(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
void BattleAi_ChooseBoss7ECommand(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
void BattleAi_ChooseBoss7DCommand(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
void BattleAi_ChooseBoss7B7CCommand(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
void BattleAi_ChooseBoss7FCommand(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
void BattleAi_ChooseBoss80Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
void BattleAi_ChooseBoss81Command(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
void BattleAi_ChooseBossCommand(struct BattleCombatant *combatant, struct BattleCombatant *combatants);

#endif
