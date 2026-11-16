#ifndef BATTLE_ITEMS_H
#define BATTLE_ITEMS_H

#include "battle_types.h"

#define BATTLE_TACTICS_ITEM_COUNT 16
#define BATTLE_LOOT_ENTRY_COUNT 10
#define BATTLE_ITEM_DESCRIPTION_LENGTH 24
#define BATTLE_ITEM_PROPERTY_COUNT 58
#define BATTLE_ITEM_NO_SLOT 0xFF
#define BATTLE_FIRST_RECOVERY_ITEM 221

/** @brief Select one field from the three-byte item-property record.
 *  @note Unclassified fields use their encoded position, not a guessed effect name.
 *  The selector is passed as u8; these enum values do not change RAM field widths.
 *  @address 0x0804DD90
 */
enum BattleItemProperty
{
    BATTLE_ITEM_PROPERTY_BYTE0_LOW6 = 0,
    BATTLE_ITEM_PROPERTY_TURN_END_EFFECT = 1,
    BATTLE_ITEM_PROPERTY_BYTE1_LOW4 = 2,
    BATTLE_ITEM_PROPERTY_SKILL_MP_COST = 3,
    BATTLE_ITEM_PROPERTY_BYTE1_HIGH2 = 4,
    BATTLE_ITEM_PROPERTY_BYTE2_LOW2 = 5,
    BATTLE_ITEM_PROPERTY_AUTO_REVIVE = 6,
};

/** @brief Turn-end effects encoded in property byte zero's high two bits.
 *  @note The equipment scan ORs the corresponding flags into turnEndEffects;
 *  these selectors differ from the runtime flag values at combatant +0xB8.
 *  @address 0x0839CEFC, 0x08045EB8
 */
enum BattleEquipmentTurnEndEffect
{
    BATTLE_EQUIPMENT_TURN_END_NONE = 0,
    BATTLE_EQUIPMENT_TURN_END_HP_RECOVERY = 1,
    BATTLE_EQUIPMENT_TURN_END_MP_DRAIN = 2,
    BATTLE_EQUIPMENT_TURN_END_GAUGE_GAIN = 3,
};

/** @brief MP-cost modes encoded in property byte one's bits four and five.
 *  @note Separate equipped items can supply both modes. Rules subtract two
 *  with byte wrap before halving; the original battle logic has no minimum cost.
 *  @address 0x0839CEFC, 0x08045A10, 0x08045B90, 0x08048934
 */
enum BattleEquipmentSkillMpCost
{
    BATTLE_EQUIPMENT_MP_COST_NORMAL = 0,
    BATTLE_EQUIPMENT_MP_COST_MINUS_TWO = 1,
    BATTLE_EQUIPMENT_MP_COST_HALF = 2,
};

/** @brief Item id and count shared by inventory, reservation and menu records.
 *  @note The original stride is four bytes; +2/+3 are not read or cleared by
 *  the inventory helpers. Reservation count is the count BEFORE reserving one. */
struct BattleItemStack
{
    u8 itemId;
    u8 count;
    u8 unused02[2];
};

/** @brief Card reward row returned to the result screen.
 *  @note cardIndex is a display index, not the id passed to Collection_AddCard. */
struct BattleCardDrop
{
    u8 cardIndex;
    u8 count;
    u8 unused02[2];
};

/** @brief Packed ROM properties selected by PartyItemInfo.battlePropertyIndex.
 *  @note BattleItem_GetProperty decodes three bytes without copying the table
 *  into RAM; partially understood fields retain their original packed storage.
 *  @address 0x0839CEFC
 */
struct BattleItemProperties
{
    u8 bits[3];
} __attribute__((packed));

/** @brief Two item outcomes and the card chance for enemy ids 12..112. */
struct BattleEnemyDrops
{
    u8 firstItemId;
    u8 firstItemChance;
    u8 secondItemId;
    u8 secondItemChance;
    u8 cardChance;
} __attribute__((packed));

/** @brief Two item outcomes for enemy ids starting at 113. */
struct BattleBossDrops
{
    u8 firstItemId;
    u8 firstItemChance;
    u8 secondItemId;
    u8 secondItemChance;
} __attribute__((packed));

typedef char BattleItemStackSizeCheck[(sizeof(struct BattleItemStack) == 4) ? 1 : -1];
typedef char BattleItemStackCountCheck[(offsetof(struct BattleItemStack, count) == 1) ? 1 : -1];
typedef char BattleCardDropSizeCheck[(sizeof(struct BattleCardDrop) == 4) ? 1 : -1];
typedef char BattleItemPropertiesSizeCheck[(sizeof(struct BattleItemProperties) == 3) ? 1 : -1];
typedef char BattleEnemyDropsSizeCheck[(sizeof(struct BattleEnemyDrops) == 5) ? 1 : -1];
typedef char BattleBossDropsSizeCheck[(sizeof(struct BattleBossDrops) == 4) ? 1 : -1];

/* Read-only definitions are in battle_items.c and linked into ROM. */
extern const u8 gBattleItemDescriptions[BATTLE_TACTICS_ITEM_COUNT][BATTLE_ITEM_DESCRIPTION_LENGTH]; /* 0839BB4C */
extern const struct BattleItemProperties gBattleItemProperties[BATTLE_ITEM_PROPERTY_COUNT]; /* 0839CEFC */
extern const u8 gTacticsItemIds[BATTLE_TACTICS_ITEM_COUNT]; /* 0839CFAA; shared with menu.c */
extern const struct BattleEnemyDrops gBattleEnemyDropTable[101]; /* 0839D9B8 */
extern const struct BattleBossDrops gBattleBossDropTable[]; /* 0839DBB1 */

u8 BattleItem_GetProperty(u8 itemId, u8 property);
void BattleItem_SaveInventory(void);
void BattleItem_ResetReservations(void);
u8 BattleItem_BuildMenuList(struct BattleItemStack *items);
void BattleItem_Reserve(const struct BattleItemStack *item, struct BattleCombatant *combatant, u8 partySlot);
void BattleItem_DrawNameAndCount(u16 *tilemap, u8 x, u8 y, const struct BattleItemStack *item, u8 row, u8 palette, u8 showCount);
u8 BattleItem_UpdateRecovery(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
u8 BattleItem_UpdateEquipmentEffect(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
s8 BattleItem_FindEquipmentByType(const struct BattleCombatant *combatant, u8 type);
s8 BattleItem_FindEquipmentByProperty(const struct BattleCombatant *combatant, u8 property, u8 value);
void BattleItem_QueueAutoRevive(struct BattleCombatant *combatant);
u32 BattleItem_UpdateAutoRevive(void);
u8 BattleItem_AwardItemDrops(struct BattleItemStack **items);
u8 BattleItem_AwardCardDrops(struct BattleCardDrop **cards);
void BattleItem_RestoreInventory(void);
void BattleItem_CancelReservation(u8 partySlot);
void BattleItem_CommitReservations(void);
u8 BattleItem_FindInventorySlot(u8 itemId);
void BattleItem_DrawDescription(u16 *tilemap, u8 x, u8 y, const struct BattleItemStack *item, u8 palette);
u8 BattleItem_FindTacticsIndex(u8 itemId);
void BattleItem_ResetEffect(void);
u8 BattleItem_UpdateAction(struct BattleCombatant *combatant, struct BattleCombatant *combatants);
u32 BattleItem_FindAutoReviveAccessory(const struct BattleCombatant *combatant, u8 property);
s8 BattleItem_FindPartyEquipmentByProperty(u8 property, u8 value);
u8 BattleItem_ListPartyWithEquipmentProperty(u8 *slots, u8 property, u8 value);
void BattleItem_ResetAutoRevive(void);
u8 BattleItem_GetAutoReviveCount(void);
void BattleItem_ClearItemDrops(void);

#endif
