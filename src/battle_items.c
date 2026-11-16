#include "battle_items.h"
#include "battle_action_effects.h"
#include "battle.h"
#include "battle_palette.h"
#include "battle_rules.h"
#include "menu.h"
#include "party.h"
#include "sound.h"

/* Private .lcomm objects in original address order. No explicit zero initializers:
 * agbcc would put those in .data rather than the original zero-fill storage.
 * Menu/rules receive pointers to these records; they do not own another copy. */
static struct BattleItemStack sBattleSavedItems[BATTLE_TACTICS_ITEM_COUNT]; /* 03000D48 */
static struct BattleItemStack sBattleAvailableItems[BATTLE_TACTICS_ITEM_COUNT]; /* 03000D88 */
static struct BattleItemStack sBattleItemReservations[BATTLE_PARTY_COUNT]; /* 03000DC8 */
static u8 sBattleAvailableItemCount; /* 03000DDC */
static u8 sBattleSavedItemCount; /* 03000DDD */
static u8 sBattleItemEffectPhase; /* 03000DDE */
static u16 sBattleItemLoopStartFrame; /* 03000DE0 */
static u16 sBattleItemLoopEndFrame; /* 03000DE2; exclusive */
static u8 sBattleItemLoopCount; /* 03000DE4 */
static u8 sBattleItemLoopIteration; /* 03000DE5 */
static u16 sBattleItemSavedBaseTile; /* 03000DE6; combatant animation +1E */
static u8 sBattleItemSavedPaletteSlot; /* 03000DE8; combatant animation +29 */
static struct BattleCombatant *sBattleAutoReviveQueue[BATTLE_PARTY_COUNT]; /* 03000DF0 */
static u8 sBattleAutoReviveCount; /* 03000E04 */
static u8 sBattleAutoRevivePhase; /* 03000E05 */
static struct BattleItemStack sBattleItemDrops[BATTLE_LOOT_ENTRY_COUNT]; /* 03000E08 */
static u8 sBattleItemDropCount; /* 03000E30 */
static struct BattleCardDrop sBattleCardDrops[BATTLE_LOOT_ENTRY_COUNT]; /* 03000E38 */
static u8 sBattleCardDropCount; /* 03000E60 */

/** @brief Decode one field of an item's three-byte battle-property record.
 *  @note PartyItemInfo +0A selects the record. Unknown selectors return zero;
 *  selector 6 reads bit 3 of byte 2 (the auto-revive accessory flag). */
u8 BattleItem_GetProperty(u8 itemId, u8 property)
{
    u8 value = 0;
    const u8 *items = (const u8 *)gPartyItemTable;
    s32 offset = itemId * sizeof(struct PartyItemInfo)
        + offsetof(struct PartyItemInfo, battlePropertyIndex);
    const struct BattleItemProperties *properties = &gBattleItemProperties[items[offset]];

    switch (property)
    {
    case BATTLE_ITEM_PROPERTY_BYTE0_LOW6:
        value = properties->bits[0] & 0x3F;
        break;
    case BATTLE_ITEM_PROPERTY_TURN_END_EFFECT:
        value = properties->bits[0] >> 6;
        break;
    case BATTLE_ITEM_PROPERTY_BYTE1_LOW4:
        value = properties->bits[1] & 0xF;
        break;
    case BATTLE_ITEM_PROPERTY_SKILL_MP_COST:
        value = (properties->bits[1] >> 4) & 3;
        break;
    case BATTLE_ITEM_PROPERTY_BYTE1_HIGH2:
        value = properties->bits[1] >> 6;
        break;
    case BATTLE_ITEM_PROPERTY_BYTE2_LOW2:
        value = properties->bits[2] & 3;
        break;
    case BATTLE_ITEM_PROPERTY_AUTO_REVIVE:
        value = (properties->bits[2] >> 3) & 1;
        break;
    }
    return value;
}

/** @brief Snapshot the owned items in the sixteen-item battle whitelist.
 *  @note Only id/count bytes are cleared; the two unused bytes of each row remain. */
void BattleItem_SaveInventory(void)
{
    u8 index;

    for (index = 0; index < BATTLE_TACTICS_ITEM_COUNT; index++)
    {
        sBattleSavedItems[index].itemId = 0;
        sBattleSavedItems[index].count = 0;
    }
    sBattleSavedItemCount = 0;
    for (index = 0; index < BATTLE_TACTICS_ITEM_COUNT; index++)
    {
        if (gPartyItemCounts[gTacticsItemIds[index]] != 0)
        {
            sBattleSavedItems[sBattleSavedItemCount].itemId = gTacticsItemIds[index];
            sBattleSavedItems[sBattleSavedItemCount].count = gPartyItemCounts[gTacticsItemIds[index]];
            sBattleSavedItemCount++;
        }
    }
}

/** @brief Clear five reservations and rebuild the command-selection inventory.
 *  @note This is a working copy; it does not consume the party inventory. */
void BattleItem_ResetReservations(void)
{
    u8 index;

    for (index = 0; index < BATTLE_PARTY_COUNT; index++)
    {
        sBattleItemReservations[index].itemId = 0;
        sBattleItemReservations[index].count = 0;
    }
    for (index = 0; index < BATTLE_TACTICS_ITEM_COUNT; index++)
    {
        sBattleAvailableItems[index].itemId = 0;
        sBattleAvailableItems[index].count = 0;
    }
    sBattleAvailableItemCount = 0;
    for (index = 0; index < BATTLE_TACTICS_ITEM_COUNT; index++)
    {
        if (gPartyItemCounts[gTacticsItemIds[index]] != 0)
        {
            sBattleAvailableItems[sBattleAvailableItemCount].itemId = gTacticsItemIds[index];
            sBattleAvailableItems[sBattleAvailableItemCount].count = gPartyItemCounts[gTacticsItemIds[index]];
            sBattleAvailableItemCount++;
        }
    }
}

/** @brief Fill a caller-owned sixteen-row menu buffer with nonempty working stacks.
 *  @note Returns the number of rows written; unused id/count fields are zero. */
u8 BattleItem_BuildMenuList(struct BattleItemStack *items)
{
    u8 index;
    u8 count;

    for (index = 0; index < BATTLE_TACTICS_ITEM_COUNT; index++)
    {
        items[index].itemId = 0;
        items[index].count = 0;
    }
    count = 0;
    for (index = 0; index < sBattleAvailableItemCount; index++)
    {
        if (sBattleAvailableItems[index].count != 0)
        {
            items[count].itemId = sBattleAvailableItems[index].itemId;
            items[count].count = sBattleAvailableItems[index].count;
            count++;
        }
    }
    return count;
}

/** @brief Reserve one working item and set the combatant's item command.
 *  @note Save the pre-reservation count for undo; caller supplies an available row
 *  and a party slot 0..4. No inventory bounds check is added to the original. */
void BattleItem_Reserve(const struct BattleItemStack *item, struct BattleCombatant *combatant, u8 partySlot)
{
    u8 index;

    sBattleItemReservations[partySlot].itemId = item->itemId;
    sBattleItemReservations[partySlot].count = item->count;
    combatant->itemId = item->itemId;
    combatant->action = BATTLE_ACTION_ITEM;
    for (index = 0; index < sBattleAvailableItemCount; index++)
    {
        if (sBattleAvailableItems[index].itemId == item->itemId)
        {
            sBattleAvailableItems[index].count--;
            break;
        }
    }
}

/** @brief Draw the eight-glyph item name and optional two-digit count.
 *  @note Each glyph occupies two vertical tiles; row adds two tile rows to y. */
void BattleItem_DrawNameAndCount(u16 *tilemap, u8 x, u8 y,
    const struct BattleItemStack *item, u8 row, u8 palette, u8 showCount)
{
    u16 *destination = &tilemap[(y + row * 2) * 32 + x];
    const u8 *name = gMenuItemNames[item->itemId];
    u8 index = 0;
    u8 digits[2];

    for (; index < 8; index++)
    {
        if (name[index] != 0)
        {
            destination[index] = (palette << 12) + name[index] * 2;
            destination[index + 32] = (palette << 12) + (name[index] * 2 + 1);
        }
        else
        {
            destination[index] = (palette << 12) + 1;
            destination[index + 32] = (palette << 12) + 1;
        }
    }
    if (showCount == 1)
    {
        digits[0] = item->count / 10;
        digits[1] = item->count - digits[0] * 10;
        for (index = 0; index < 2; index++)
        {
            if (index == 0 && digits[0] == 0)
            {
                destination[index + 10] = (palette << 12) + 1;
                destination[index + 42] = (palette << 12) + 1;
            }
            else
            {
                destination[index + 10] = (palette << 12) + (digits[index] + 0xA2) * 2;
                destination[index + 42] = (palette << 12) + ((digits[index] + 0xA2) * 2 + 1);
            }
        }
    }
}

/** @brief Advance the recovery/status-item animation and application sequence.
 *  @note Uses combatant +A4; valid item ids are 221..228. Returns 1 on completion. */
u8 BattleItem_UpdateRecovery(struct BattleCombatant *combatant,
    struct BattleCombatant *combatants)
{
    u8 completed = 0;
    u8 effect;
    u16 amount;

    switch (combatant->itemId)
    {
    case 221:
        effect = 0;
        amount = 30;
        break;
    case 222:
        effect = 0;
        amount = 150;
        break;
    case 223:
        effect = 1;
        amount = 50;
        break;
    case 224:
        effect = 1;
        amount = 999;
        break;
    case 225:
        effect = 10;
        amount = 1;
        break;
    case 226:
        effect = 4;
        break;
    case 227:
        effect = 3;
        break;
    case 228:
        effect = 11;
        break;
    }
    switch (sBattleItemEffectPhase)
    {
    case 0:
        Battle_ResetEffectPhase();
        sBattleItemEffectPhase = 1;
        sBattleItemSavedBaseTile = combatant->animation.baseTile;
        sBattleItemSavedPaletteSlot = combatant->animation.paletteSlot;
        break;
    case 1:
        Battle_SetPartyAnimation(combatant, 6, 436, 13, 0);
        sBattleItemEffectPhase = 2;
        Sound_PlaySfx(23, 0, 0);
        break;
    case 2:
        if (combatant->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(combatant->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&combatant->animation));
            combatant->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetPartyAnimation(combatant, 0, sBattleItemSavedBaseTile,
                sBattleItemSavedPaletteSlot, 0);
            combatant->animation.flags |= 0x100;
            sBattleItemEffectPhase = 3;
        }
        break;
    case 3:
        if (!(combatant->animation.flags & BATTLE_ANIM_LOADING))
        {
            combatant->animation.flags &= ~0x100;
            sBattleItemEffectPhase = 4;
        }
        break;
    case 4:
        if ((u8)Battle_UpdateTargetRecoveryEffect(combatant, combatants, 0, effect, amount) == 1)
            sBattleItemEffectPhase = 13;
        break;
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
        break;
    case 13:
        completed = 1;
        sBattleItemEffectPhase = 0;
        break;
    }
    return completed;
}

/** @brief Advance the usable-equipment item's animation and target effect.
 *  @note Shares the recovery phase byte; completed paths return 1 or 2. */
u8 BattleItem_UpdateEquipmentEffect(struct BattleCombatant *combatant,
    struct BattleCombatant *combatants)
{
    u8 completed = 0;

    switch (sBattleItemEffectPhase)
    {
    case 0:
        sBattleItemSavedBaseTile = combatant->animation.baseTile;
        sBattleItemSavedPaletteSlot = combatant->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(combatant);
        BattleAction_AccumulateTargetDamage(combatant);
        switch (combatant->itemId)
        {
        case 25:
            combatant->hitEffectId = 167;
            sBattleItemLoopCount = 3;
            break;
        case 26:
            combatant->hitEffectId = 168;
            sBattleItemLoopCount = 3;
            break;
        case 31:
            combatant->hitEffectId = 173;
            sBattleItemLoopCount = 3;
            break;
        case 45:
            combatant->hitEffectId = 169;
            sBattleItemLoopCount = 3;
            Battle_SetBlend(0x410, 16, 8);
            break;
        case 46:
            combatant->hitEffectId = 170;
            sBattleItemLoopCount = 3;
            break;
        case 47:
            combatant->hitEffectId = 2;
            sBattleItemLoopCount = 3;
            break;
        case 48:
            combatant->hitEffectId = 171;
            sBattleItemLoopCount = 3;
            break;
        case 49:
            combatant->hitEffectId = 172;
            sBattleItemLoopCount = 3;
            break;
        }
        sBattleItemEffectPhase = 1;
        break;
    case 1:
        Battle_SetPartyAnimation(combatant, 6, 436, 13, 0);
        sBattleItemEffectPhase = 2;
        Sound_PlaySfx(23, 0, 0);
        break;
    case 2:
        if (combatant->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(combatant->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&combatant->animation));
            combatant->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetPartyAnimation(combatant, 0, sBattleItemSavedBaseTile,
                sBattleItemSavedPaletteSlot, 0);
            combatant->animation.flags |= 0x100;
            if (Battle_GetActionTargets()[0] & BATTLE_TARGET_VALID)
                sBattleItemEffectPhase = 5;
            else
                sBattleItemEffectPhase = 10;
        }
        break;
    case 3:
    case 4:
        break;
    case 5:
        if (!(combatant->animation.flags & BATTLE_ANIM_LOADING))
            sBattleItemEffectPhase = 6;
        break;
    case 6:
        if (Sound_IsSfxPlaying(0) == 0)
        {
            BattleAction_BeginTargetReactions(20);
            if (combatant->itemId != 47)
                sBattleItemEffectPhase = 7;
            else
                sBattleItemEffectPhase = 11;
        }
        break;
    case 7:
        if (!(combatants[combatant->targetSlot].effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            combatant->animation.flags &= ~0x100;
            if (combatant->itemId != 48)
                sBattleItemEffectPhase = 8;
            else
                sBattleItemEffectPhase = 10;
        }
        break;
    case 8:
        {
            const struct BattleKeyframe *keyframes = Battle_GetTrackKeyframe(
                &combatants[combatant->targetSlot].effectAnimation, 0,
                combatants[combatant->targetSlot].effectAnimation.timelineFrame);
            u8 index;

            if (keyframes->frameIndex == 0)
            {
                sBattleItemLoopStartFrame = keyframes->endFrame;
                for (index = 2; keyframes[index].frameIndex != 0 && index < 5; index++)
                    ;
                sBattleItemLoopEndFrame = keyframes[index - 1].endFrame;
                sBattleItemLoopIteration = 0;
                combatants[combatant->targetSlot].effectAnimation.flags |= 0x100;
                sBattleItemEffectPhase = 9;
            }
        }
        break;
    case 9:
        if (sBattleItemLoopIteration < sBattleItemLoopCount)
        {
            combatants[combatant->targetSlot].effectAnimation.timelineFrame =
                (combatants[combatant->targetSlot].effectAnimation.timelineFrame + 1)
                % (sBattleItemLoopEndFrame - sBattleItemLoopStartFrame)
                + sBattleItemLoopStartFrame;
            if (combatants[combatant->targetSlot].effectAnimation.timelineFrame == sBattleItemLoopStartFrame)
                sBattleItemLoopIteration++;
        }
        else
        {
            combatants[combatant->targetSlot].effectAnimation.flags &= ~0x100;
            sBattleItemEffectPhase = 10;
        }
        break;
    case 10:
        if (BattleAction_AreTargetReactionsDone() == 1)
            sBattleItemEffectPhase = 13;
        break;
    case 11:
        if (BattleAction_AreTargetReactionsDone() == 1)
            sBattleItemEffectPhase = 12;
        break;
    case 12:
        if ((s32)Battle_NextRandom() % 100 < 50)
            Battle_ApplyStatus(&combatants[combatant->targetSlot], 3);
        completed = 1;
        sBattleItemEffectPhase = 0;
        break;
    case 13:
        completed = 2;
        sBattleItemEffectPhase = 0;
        break;
    }
    BattleAction_UpdateTargetReactions(combatant);
    return completed;
}

/** @brief Find the first of six equipment slots with matching item-table +05.
 *  @note Returns -1 for enemies or no match. The empty-equipment test also reads
 *  the adjacent +93/+94 bytes; their meanings remain unclassified. */
s8 BattleItem_FindEquipmentByType(const struct BattleCombatant *combatant, u8 type)
{
    u8 found = BATTLE_ITEM_NO_SLOT;
    const u8 *equipment;
    u8 index;

    if (combatant->id >= BATTLE_CHARACTER_ID_COUNT)
        return found;
    equipment = (const u8 *)combatant + offsetof(struct BattleCombatant, equipment);
    if (equipment[0] == 0 && equipment[1] == 0 && equipment[2] == 0
        && equipment[3] == 0 && equipment[4] == 0 && equipment[5] == 0
        && equipment[6] == 0 && equipment[7] == 0)
        return found;
    index = 0;
    for (; index < PARTY_EQUIPMENT_SLOT_COUNT; index++)
    {
        const u8 *items = (const u8 *)gPartyItemTable;
        s32 offset = equipment[index] * sizeof(struct PartyItemInfo)
            + offsetof(struct PartyItemInfo, unknown05);

        if (items[offset] == type)
        {
            found = index;
            break;
        }
    }
    return found;
}

/** @brief Find the first equipment slot whose decoded property equals value.
 *  @note Searches party ids 0..10 and six slots, including zero item ids.
 *  The empty-equipment test also reads +93/+94; -1 means no match. */
s8 BattleItem_FindEquipmentByProperty(const struct BattleCombatant *combatant, u8 property, u8 value)
{
    u8 found = BATTLE_ITEM_NO_SLOT;
    const u8 *equipment;
    u8 index;

    if (combatant->id >= BATTLE_CHARACTER_ID_COUNT)
        return found;
    equipment = (const u8 *)combatant + offsetof(struct BattleCombatant, equipment);
    if (equipment[0] == 0 && equipment[1] == 0 && equipment[2] == 0
        && equipment[3] == 0 && equipment[4] == 0 && equipment[5] == 0
        && equipment[6] == 0 && equipment[7] == 0)
        return found;
    for (index = 0; index < PARTY_EQUIPMENT_SLOT_COUNT; index++)
    {
        if (BattleItem_GetProperty(equipment[index], property) == value)
        {
            found = index;
            break;
        }
    }
    return found;
}

/** @brief Find an auto-revive accessory in the last two equipment slots.
 *  @note Returns 1/2 rather than a zero-based equipment index; only selector 6
 *  is implemented. Shared by the public query and the consuming queue operation. */
static inline u32 BattleItem_FindAutoReviveAccessoryInline(const struct BattleCombatant *combatant, u8 property)
{
    u8 found = 0;
    u8 first = combatant->equipment[4];
    u8 second = combatant->equipment[5];

    if (first == 0 && second == 0)
        return 0;
    switch (property)
    {
    case BATTLE_ITEM_PROPERTY_BYTE0_LOW6:
    case BATTLE_ITEM_PROPERTY_TURN_END_EFFECT:
    case BATTLE_ITEM_PROPERTY_BYTE1_LOW4:
    case BATTLE_ITEM_PROPERTY_SKILL_MP_COST:
    case BATTLE_ITEM_PROPERTY_BYTE1_HIGH2:
    case BATTLE_ITEM_PROPERTY_BYTE2_LOW2:
        break;
    case BATTLE_ITEM_PROPERTY_AUTO_REVIVE:
        if (BattleItem_GetProperty(first, BATTLE_ITEM_PROPERTY_AUTO_REVIVE) != 0)
            found = 1;
        else if (BattleItem_GetProperty(second, BATTLE_ITEM_PROPERTY_AUTO_REVIVE) != 0)
            found = 2;
        break;
    }
    return found;
}

/** @brief Consume a qualifying accessory and queue its wearer for auto-revival.
 *  @note Checks equipment slots 4/5, clears the first matching slot and appends
 *  the combatant pointer. The five-entry limit is enforced by the caller. */
void BattleItem_QueueAutoRevive(struct BattleCombatant *combatant)
{
    u8 accessory = BattleItem_FindAutoReviveAccessoryInline(combatant, BATTLE_ITEM_PROPERTY_AUTO_REVIVE);

    if (accessory != 0)
    {
        combatant->equipment[accessory + 3] = 0;
        sBattleAutoReviveQueue[sBattleAutoReviveCount] = combatant;
        sBattleAutoReviveCount++;
    }
}

/** @brief Advance the queued auto-revival animation and revive every wearer.
 *  @note Returns a full-width 0/1 result. The queue must be nonempty;
 *  its first animation is copied to other rows. */
u32 BattleItem_UpdateAutoRevive(void)
{
    u8 completed = 0;
    u8 index;

    switch (sBattleAutoRevivePhase)
    {
    case 0:
        Battle_SetEffectAnimation(sBattleAutoReviveQueue[0],
            sBattleAutoReviveQueue[0]->homeX, sBattleAutoReviveQueue[0]->homeY,
            746, 14, 166, 0x104);
        sBattleAutoReviveQueue[0]->effectAnimation.priority = 3;
        sBattleAutoRevivePhase = 1;
        break;
    case 1:
        if (!(sBattleAutoReviveQueue[0]->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            sBattleAutoReviveQueue[0]->effectAnimation.flags &= ~0x100;
            for (index = 1; index < sBattleAutoReviveCount; index++)
                sBattleAutoReviveQueue[index]->effectAnimation =
                    sBattleAutoReviveQueue[0]->effectAnimation;
            sBattleAutoRevivePhase = 2;
        }
        break;
    case 2:
        if (sBattleAutoReviveQueue[0]->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(sBattleAutoReviveQueue[0]->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&sBattleAutoReviveQueue[0]->effectAnimation));
            for (index = 0; index < sBattleAutoReviveCount; index++)
                sBattleAutoReviveQueue[index]->displayState &= ~0x2000;
            sBattleAutoRevivePhase = 4;
        }
        break;
    case 4:
        for (index = 0; index < sBattleAutoReviveCount; index++)
            Battle_CureStatusOrRevive(sBattleAutoReviveQueue[index], 10, 1);
        completed = 1;
        break;
    }
    return completed;
}

static inline s8 BattleItem_FindPartyEquipmentByPropertyInline(u8 property, u8 value)
{
    u8 found = BATTLE_ITEM_NO_SLOT;
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 slots[BATTLE_PARTY_COUNT];
    u8 count = Battle_ListCombatants(combatants, slots, BATTLE_SIDE_PARTY, BATTLE_STATUS_MASK_ALL);
    u8 index;

    for (index = 0; index < count; index++)
    {
        u8 equipmentIndex = BattleItem_FindEquipmentByProperty(&combatants[slots[index]], property, value);

        if ((s8)equipmentIndex >= 0)
        {
            found = equipmentIndex;
            break;
        }
    }
    return found;
}

/** @brief Roll item rewards, merge equal ids, add them to inventory and return rows.
 *  @note The output points into private storage. Enemy status 7 skips rewards. */
u8 BattleItem_AwardItemDrops(struct BattleItemStack **items)
{
    struct BattleCombatant *combatants;
    const struct BattleEncounter *encounter;
    u8 equipmentIndex;
    u8 bonus;
    u8 enemyIndex;
    u8 tableIndex;
    u8 awarded;
    u8 itemId;
    u8 dropIndex;

    sBattleItemDropCount = 0;
    combatants = Battle_GetCombatants();
    encounter = Battle_GetEncounter();
    equipmentIndex = BattleItem_FindPartyEquipmentByPropertyInline(BATTLE_ITEM_PROPERTY_BYTE2_LOW2, 4);
    if ((s8)equipmentIndex >= 0)
        bonus = 15;
    else
        bonus = 0;
    for (enemyIndex = 0; enemyIndex < encounter->enemyCount; enemyIndex++)
    {
        awarded = 0;
        itemId = 0;
        if (combatants[enemyIndex + BATTLE_PARTY_COUNT].status == 7)
            continue;
        if (encounter->enemies[enemyIndex].enemyId <= 112)
        {
            tableIndex = encounter->enemies[enemyIndex].enemyId - 12;
            if ((s32)Battle_NextRandom() % 100 < 60)
            {
                if ((s32)Battle_NextRandom() % 100 < gBattleEnemyDropTable[tableIndex].firstItemChance)
                {
                    itemId = gBattleEnemyDropTable[tableIndex].firstItemId + bonus;
                    awarded = 1;
                }
            }
            else
            {
                if ((s32)Battle_NextRandom() % 100 < gBattleEnemyDropTable[tableIndex].secondItemChance)
                {
                    itemId = gBattleEnemyDropTable[tableIndex].secondItemId + bonus;
                    awarded = 1;
                }
            }
        }
        else
        {
            tableIndex = encounter->enemies[enemyIndex].enemyId - 113;
            if ((s32)Battle_NextRandom() % 100 < 60)
            {
                if ((s32)Battle_NextRandom() % 100 < gBattleBossDropTable[tableIndex].firstItemChance)
                {
                    itemId = gBattleBossDropTable[tableIndex].firstItemId + bonus;
                    awarded = 1;
                }
            }
            else
            {
                if ((s32)Battle_NextRandom() % 100 < gBattleBossDropTable[tableIndex].secondItemChance)
                {
                    itemId = gBattleBossDropTable[tableIndex].secondItemId + bonus;
                    awarded = 1;
                }
            }
        }
        if (awarded == 1 && itemId != 0)
        {
            awarded = 0;
            for (dropIndex = 0; dropIndex < sBattleItemDropCount; dropIndex++)
            {
                if (itemId == sBattleItemDrops[dropIndex].itemId)
                {
                    awarded = 1;
                    break;
                }
            }
            if (awarded == 1)
                sBattleItemDrops[dropIndex].count++;
            else
            {
                sBattleItemDrops[sBattleItemDropCount].itemId = itemId;
                sBattleItemDrops[sBattleItemDropCount].count = 1;
                sBattleItemDropCount++;
            }
        }
    }
    for (enemyIndex = 0; enemyIndex < sBattleItemDropCount; enemyIndex++)
        Party_AddItem(sBattleItemDrops[enemyIndex].itemId, sBattleItemDrops[enemyIndex].count);
    *items = sBattleItemDrops;
    return sBattleItemDropCount;
}

/** @brief Roll card rewards, update the collection and return display rows.
 *  @note Display indices and collection ids differ for ordinary/special encounters. */
u8 BattleItem_AwardCardDrops(struct BattleCardDrop **cards)
{
    u8 mode = 0;
    u8 enemyIndex;
    struct BattleCombatant *combatants;
    const struct BattleEncounter *encounter;
    struct BattleCombatant *lastEnemy;
    u8 equipmentIndex;
    u8 awarded;
    u8 cardIndex;
    u8 dropIndex;

    for (enemyIndex = 0; enemyIndex < BATTLE_LOOT_ENTRY_COUNT; enemyIndex++)
    {
        sBattleCardDrops[enemyIndex].cardIndex = 0;
        sBattleCardDrops[enemyIndex].count = 0;
    }
    sBattleCardDropCount = 0;
    combatants = Battle_GetCombatants();
    encounter = Battle_GetEncounter();
    lastEnemy = Battle_GetLastDefeatedEnemy();
    equipmentIndex = BattleItem_FindPartyEquipmentByPropertyInline(BATTLE_ITEM_PROPERTY_BYTE2_LOW2, 2);
    if ((s8)equipmentIndex >= 0)
        mode = 1;
    equipmentIndex = BattleItem_FindPartyEquipmentByPropertyInline(BATTLE_ITEM_PROPERTY_BYTE2_LOW2, 3);
    if ((s8)equipmentIndex >= 0)
        mode = 2;
    for (enemyIndex = 0; enemyIndex < encounter->enemyCount; enemyIndex++)
    {
        awarded = 0;
        if (combatants[enemyIndex + BATTLE_PARTY_COUNT].status == 7)
            continue;
        if (encounter->enemies[enemyIndex].enemyId <= 112)
        {
            cardIndex = encounter->enemies[enemyIndex].enemyId - 12;
            if (combatants[enemyIndex + BATTLE_PARTY_COUNT].formationBits == lastEnemy->formationBits)
            {
                switch (mode)
                {
                case 0:
                    if ((s32)Battle_NextRandom() % 100 < gBattleEnemyDropTable[cardIndex].cardChance)
                        awarded = 1;
                    break;
                case 1:
                    if ((s32)Battle_NextRandom() % 100 < 50)
                        awarded = 1;
                    break;
                case 2:
                    awarded = 1;
                    break;
                }
            }
            else if ((s32)Battle_NextRandom() % 100 < gBattleEnemyDropTable[cardIndex].cardChance)
                awarded = 1;
        }
        else
        {
            cardIndex = encounter->enemies[enemyIndex].enemyId - 113;
            if (cardIndex > 11)
                cardIndex--;
            if (combatants[enemyIndex + BATTLE_PARTY_COUNT].id != 121)
                awarded = 1;
        }
        if (awarded == 1)
        {
            awarded = 0;
            for (dropIndex = 0; dropIndex < sBattleCardDropCount; dropIndex++)
            {
                if (cardIndex == sBattleCardDrops[dropIndex].cardIndex)
                {
                    awarded = 1;
                    break;
                }
            }
            if (awarded == 1)
                sBattleCardDrops[dropIndex].count++;
            else
            {
                sBattleCardDrops[sBattleCardDropCount].cardIndex = cardIndex;
                sBattleCardDrops[sBattleCardDropCount].count = 1;
                sBattleCardDropCount++;
            }
        }
    }
    for (enemyIndex = 0; enemyIndex < sBattleCardDropCount; enemyIndex++)
    {
        if (Battle_GetControlFlags() & 0x20)
        {
            Collection_AddCard(sBattleCardDrops[enemyIndex].cardIndex + 160);
            if (sBattleCardDrops[enemyIndex].cardIndex > 10)
                sBattleCardDrops[enemyIndex].cardIndex++;
        }
        else
            Collection_AddCard(sBattleCardDrops[enemyIndex].cardIndex + 59);
    }
    *cards = sBattleCardDrops;
    return sBattleCardDropCount;
}

/** @brief Restore the saved whitelist counts at the scene's rollback point.
 *  @note Writes only items present in the snapshot, not the entire inventory. */
void BattleItem_RestoreInventory(void)
{
    u8 index;

    for (index = 0; index < sBattleSavedItemCount; index++)
        gPartyItemCounts[sBattleSavedItems[index].itemId] = sBattleSavedItems[index].count;
}

/** @brief Undo a party slot's reservation using its saved pre-reservation count.
 *  @note Does not clear the reservation or the combatant command; menu owns both. */
void BattleItem_CancelReservation(u8 partySlot)
{
    struct BattleItemStack *reservations = sBattleItemReservations;
    u8 index;

    if (reservations[partySlot].itemId != 0)
    {
        for (index = 0; index < sBattleAvailableItemCount; index++)
        {
            struct BattleItemStack *available = sBattleAvailableItems;
            struct BattleItemStack *item = &reservations[partySlot];

            if (available[index].itemId == item->itemId)
                available[index].count = item->count;
        }
    }
}

/** @brief Commit reserved consumable counts when command selection completes.
 *  @note Item ids below 221 are usable equipment and are not consumed here. */
void BattleItem_CommitReservations(void)
{
    u8 index;

    for (index = 0; index < sBattleAvailableItemCount; index++)
    {
        if (sBattleAvailableItems[index].itemId >= BATTLE_FIRST_RECOVERY_ITEM)
            gPartyItemCounts[sBattleAvailableItems[index].itemId] = sBattleAvailableItems[index].count;
    }
}

/** @brief Find a working inventory row by id, or return FF when absent.
 *  @note A zero-count row still exists and can be found after reservations. */
u8 BattleItem_FindInventorySlot(u8 itemId)
{
    u8 found = BATTLE_ITEM_NO_SLOT;
    u8 index;

    for (index = 0; index < sBattleAvailableItemCount; index++)
    {
        if (sBattleAvailableItems[index].itemId == itemId)
        {
            found = index;
            break;
        }
    }
    return found;
}

/** @brief Draw the selected whitelist item's 24-glyph description.
 *  @note Caller supplies a whitelisted item; glyphs 0/FF use blank tile 1.
 *  Each glyph fills two vertically adjacent tile rows. */
void BattleItem_DrawDescription(u16 *tilemap, u8 x, u8 y, const struct BattleItemStack *item, u8 palette)
{
    u16 *destination = &tilemap[y * 32 + x];
    const u8 *description = gBattleItemDescriptions[BattleItem_FindTacticsIndex(item->itemId)];
    u16 index = 0;
    s32 paletteBits = palette << 12;

    for (; index < BATTLE_ITEM_DESCRIPTION_LENGTH; index++)
    {
        if (description[index] != 0 && description[index] != 0xFF)
        {
            destination[index] = paletteBits + description[index] * 2;
            destination[index + 32] = paletteBits + (description[index] * 2 + 1);
        }
        else
        {
            destination[index] = paletteBits + 1;
            destination[index + 32] = paletteBits + 1;
        }
    }
}

/** @brief Return the item's whitelist index, or 16 if it is not listed. */
u8 BattleItem_FindTacticsIndex(u8 itemId)
{
    u8 index;

    for (index = 0; index < BATTLE_TACTICS_ITEM_COUNT; index++)
    {
        if (itemId == gTacticsItemIds[index])
            break;
    }
    return index;
}

/** @brief Restart the shared recovery/equipment item effect sequence. */
void BattleItem_ResetEffect(void)
{
    sBattleItemEffectPhase = 0;
}

/** @brief Dispatch the current party item action; enemy records return 2.
 *  @note Ids below 221 use the equipment path; the return code is passed through. */
u8 BattleItem_UpdateAction(struct BattleCombatant *combatant, struct BattleCombatant *combatants)
{
    if (combatant->id >= BATTLE_CHARACTER_ID_COUNT)
        return 2;
    if (combatant->itemId >= BATTLE_FIRST_RECOVERY_ITEM)
        return BattleItem_UpdateRecovery(combatant, combatants);
    return BattleItem_UpdateEquipmentEffect(combatant, combatants);
}

/** @brief Return 1/2 for the first auto-revive accessory in equipment slot 4/5.
 *  @note Only property selector 6 is supported; zero means no qualifying slot. */
u32 BattleItem_FindAutoReviveAccessory(const struct BattleCombatant *combatant, u8 property)
{
    return BattleItem_FindAutoReviveAccessoryInline(combatant, property);
}

/** @brief Return the first matching equipment slot across eligible party records.
 *  @note The result is an equipment index, not a combatant index; -1 means absent. */
s8 BattleItem_FindPartyEquipmentByProperty(u8 property, u8 value)
{
    u8 found = BATTLE_ITEM_NO_SLOT;
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 slots[BATTLE_PARTY_COUNT];
    u8 count = Battle_ListCombatants(combatants, slots, BATTLE_SIDE_PARTY, BATTLE_STATUS_MASK_ALL);
    u8 index;

    for (index = 0; index < count; index++)
    {
        u8 equipmentIndex = BattleItem_FindEquipmentByProperty(&combatants[slots[index]], property, value);

        if ((s8)equipmentIndex >= 0)
        {
            found = equipmentIndex;
            break;
        }
    }
    return found;
}

/** @brief List eligible party slots with an equipment-property match.
 *  @note The caller provides five bytes; the returned value is the number filled. */
u8 BattleItem_ListPartyWithEquipmentProperty(u8 *slots, u8 property, u8 value)
{
    u8 index;
    struct BattleCombatant *combatants;
    u8 partySlots[BATTLE_PARTY_COUNT];
    u8 partyCount;
    u8 count;

    for (index = 0; index < BATTLE_PARTY_COUNT; index++)
        slots[index] = 0;
    combatants = Battle_GetCombatants();
    partyCount = Battle_ListCombatants(combatants, partySlots, BATTLE_SIDE_PARTY, BATTLE_STATUS_MASK_ALL);
    count = 0;
    for (index = 0; index < partyCount; index++)
    {
        if (BattleItem_FindEquipmentByProperty(&combatants[partySlots[index]], property, value) >= 0)
        {
            slots[count] = partySlots[index];
            count++;
        }
    }
    return count;
}

/** @brief Clear the five auto-revival pointers and restart queue processing. */
void BattleItem_ResetAutoRevive(void)
{
    u8 index;

    for (index = 0; index < BATTLE_PARTY_COUNT; index++)
        sBattleAutoReviveQueue[index] = NULL;
    sBattleAutoReviveCount = 0;
    sBattleAutoRevivePhase = 0;
}

/** @brief Return the number of wearers queued by consumed revival accessories. */
u8 BattleItem_GetAutoReviveCount(void)
{
    return sBattleAutoReviveCount;
}

/** @brief Clear ten item-reward rows and their count, retaining unused row bytes. */
void BattleItem_ClearItemDrops(void)
{
    u8 index;

    for (index = 0; index < BATTLE_LOOT_ENTRY_COUNT; index++)
    {
        sBattleItemDrops[index].itemId = 0;
        sBattleItemDrops[index].count = 0;
    }
    sBattleItemDropCount = 0;
}

/* Split read-only data, in ROM address order. */

/* ROM 0x0839BB4C..0x0839BCCC; .rodata. */
const u8 gBattleItemDescriptions[BATTLE_TACTICS_ITEM_COUNT][BATTLE_ITEM_DESCRIPTION_LENGTH] __attribute__((aligned(1))) =
{
#include "lang/jp/battle_item_descriptions.h"
};

/* ROM 0x0839CEFC..0x0839CFBA; .rodata.0039CEFC. */
const struct BattleItemProperties gBattleItemProperties[BATTLE_ITEM_PROPERTY_COUNT] __attribute__((section(".rodata.0039CEFC"), aligned(1))) =
{
    { { 0x00, 0x00, 0x00 } },
    { { 0x01, 0x00, 0x00 } },
    { { 0x02, 0x00, 0x00 } },
    { { 0x03, 0x00, 0x00 } },
    { { 0x04, 0x00, 0x00 } },
    { { 0x05, 0x00, 0x00 } },
    { { 0x07, 0x00, 0x00 } },
    { { 0x08, 0x00, 0x00 } },
    { { 0x09, 0x00, 0x00 } },
    { { 0x0A, 0x00, 0x00 } },
    { { 0x0B, 0x00, 0x00 } },
    { { 0x0C, 0x00, 0x00 } },
    { { 0x0D, 0x00, 0x00 } },
    { { 0x0E, 0x00, 0x00 } },
    { { 0x0F, 0x00, 0x00 } },
    { { 0x10, 0x00, 0x00 } },
    { { 0x11, 0x00, 0x00 } },
    { { 0x12, 0x00, 0x00 } },
    { { 0x13, 0x00, 0x00 } },
    { { 0x14, 0x00, 0x00 } },
    { { 0x15, 0x00, 0x00 } },
    { { 0x16, 0x0A, 0x00 } },
    { { 0x17, 0x00, 0x00 } },
    { { 0x18, 0x0B, 0x00 } },
    { { 0x19, 0x00, 0x00 } },
    { { 0x1A, 0x00, 0x00 } },
    { { 0x1B, 0x0C, 0x00 } },
    { { 0x1C, 0x00, 0x00 } },
    { { 0x1D, 0x00, 0x00 } },
    { { 0x1E, 0x00, 0x00 } },
    { { 0x1F, 0x00, 0x00 } },
    { { 0x20, 0x00, 0x00 } },
    { { 0x21, 0x00, 0x00 } },
    { { 0x40, 0x00, 0x00 } },
    { { 0x80, 0x00, 0x00 } },
    { { 0xC0, 0x00, 0x00 } },
    { { 0x00, 0x01, 0x00 } },
    { { 0x00, 0x02, 0x00 } },
    { { 0x00, 0x08, 0x00 } },
    { { 0x12, 0x08, 0x00 } },
    { { 0x00, 0x04, 0x00 } },
    { { 0x00, 0x05, 0x00 } },
    { { 0x00, 0x06, 0x00 } },
    { { 0x00, 0x07, 0x00 } },
    { { 0x00, 0x08, 0x00 } },
    { { 0x00, 0x09, 0x00 } },
    { { 0x00, 0x0D, 0x00 } },
    { { 0x00, 0x0E, 0x00 } },
    { { 0x00, 0x0F, 0x00 } },
    { { 0x00, 0x10, 0x00 } },
    { { 0x00, 0x20, 0x00 } },
    { { 0x00, 0x40, 0x00 } },
    { { 0x00, 0x80, 0x00 } },
    { { 0x00, 0x00, 0x01 } },
    { { 0x00, 0x00, 0x02 } },
    { { 0x00, 0x00, 0x03 } },
    { { 0x00, 0x00, 0x04 } },
    { { 0x00, 0x00, 0x08 } },
};

const u8 gTacticsItemIds[BATTLE_TACTICS_ITEM_COUNT] __attribute__((section(".rodata.0039CEFC"), aligned(1))) =
{ 0xDD, 0xDE, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0x19, 0x1A, 0x1F, 0x2D, 0x2E, 0x2F, 0x30, 0x31 };

/* ROM 0x0839D9B8..0x0839DBF5; .rodata.0039D9B8. */
const struct BattleEnemyDrops gBattleEnemyDropTable[101] __attribute__((section(".rodata.0039D9B8"), aligned(1))) =
{
    { 0xDD, 0x04, 0x00, 0x00, 0x0F },
    { 0xDD, 0x04, 0x00, 0x00, 0x0F },
    { 0xDD, 0x04, 0x00, 0x00, 0x0F },
    { 0xDD, 0x04, 0x00, 0x00, 0x32 },
    { 0xDD, 0x04, 0x00, 0x00, 0x32 },
    { 0xE3, 0x04, 0x00, 0x00, 0x0F },
    { 0xDE, 0x04, 0xDD, 0x02, 0x0F },
    { 0xE3, 0x04, 0x00, 0x00, 0x0F },
    { 0xDD, 0x04, 0x00, 0x00, 0x0F },
    { 0xDE, 0x02, 0x00, 0x00, 0x0F },
    { 0x00, 0x00, 0x00, 0x00, 0x0F },
    { 0x00, 0x00, 0x00, 0x00, 0x0F },
    { 0xA7, 0x0A, 0x00, 0x00, 0x0F },
    { 0xDD, 0x04, 0x00, 0x00, 0x0F },
    { 0xDD, 0x04, 0x00, 0x00, 0x0F },
    { 0xDE, 0x04, 0x00, 0x00, 0x0F },
    { 0xDD, 0x04, 0x00, 0x00, 0x0F },
    { 0x00, 0x00, 0x00, 0x00, 0x0F },
    { 0xDD, 0x04, 0x00, 0x00, 0x0F },
    { 0xDE, 0x04, 0x00, 0x00, 0x0F },
    { 0xE4, 0x04, 0x00, 0x00, 0x0F },
    { 0xDD, 0x04, 0x00, 0x00, 0x0F },
    { 0xDD, 0x20, 0x00, 0x00, 0x0F },
    { 0xDD, 0x20, 0x00, 0x00, 0x0F },
    { 0xDD, 0x20, 0xDE, 0x10, 0x0F },
    { 0xDD, 0x18, 0x00, 0x00, 0x0F },
    { 0x00, 0x00, 0x00, 0x00, 0x0F },
    { 0x00, 0x00, 0x00, 0x00, 0x0F },
    { 0x00, 0x00, 0x00, 0x00, 0x0F },
    { 0x00, 0x00, 0x00, 0x00, 0x0F },
    { 0xDF, 0x02, 0x00, 0x00, 0x0F },
    { 0xDD, 0x04, 0x00, 0x00, 0x0F },
    { 0xDF, 0x02, 0x00, 0x00, 0x0F },
    { 0xDE, 0x04, 0xDF, 0x02, 0x0F },
    { 0x00, 0x00, 0x00, 0x00, 0x32 },
    { 0xDE, 0x04, 0x00, 0x00, 0x0F },
    { 0xDE, 0x04, 0x00, 0x00, 0x0F },
    { 0xDE, 0x08, 0x00, 0x00, 0x0F },
    { 0xDE, 0x08, 0x00, 0x00, 0x0F },
    { 0xDF, 0x18, 0xDE, 0x08, 0x0F },
    { 0xDE, 0x12, 0xC2, 0x02, 0x0F },
    { 0xE4, 0x08, 0x00, 0x00, 0x0F },
    { 0xDF, 0x0C, 0x00, 0x00, 0x0F },
    { 0xDF, 0x08, 0x00, 0x00, 0x0F },
    { 0xDD, 0x04, 0x00, 0x00, 0x0F },
    { 0xDF, 0x10, 0xC3, 0x02, 0x0F },
    { 0xDF, 0x10, 0x00, 0x00, 0x0F },
    { 0xDE, 0x01, 0xC3, 0x02, 0x0F },
    { 0xE3, 0x01, 0x00, 0x00, 0x0F },
    { 0xDD, 0x01, 0x00, 0x00, 0x0F },
    { 0xB1, 0x02, 0x00, 0x00, 0x0F },
    { 0xDD, 0x01, 0x00, 0x00, 0x0F },
    { 0xDD, 0x01, 0x00, 0x00, 0x0F },
    { 0x00, 0x00, 0x00, 0x00, 0x0F },
    { 0x00, 0x00, 0xC4, 0x02, 0x0F },
    { 0x00, 0x00, 0x00, 0x00, 0x32 },
    { 0x00, 0x00, 0x00, 0x00, 0x0F },
    { 0x00, 0x00, 0x00, 0x00, 0x0F },
    { 0x26, 0x0A, 0x00, 0x00, 0x0F },
    { 0x00, 0x00, 0x00, 0x00, 0x0F },
    { 0xDD, 0x01, 0x00, 0x00, 0x0F },
    { 0xDD, 0x01, 0x00, 0x00, 0x0F },
    { 0xDE, 0x01, 0x00, 0x00, 0x0F },
    { 0xDE, 0x02, 0x00, 0x00, 0x0F },
    { 0xDD, 0x18, 0xE1, 0x20, 0x0F },
    { 0xDD, 0x18, 0xDE, 0x08, 0x0F },
    { 0xDF, 0x18, 0xDE, 0x10, 0x0F },
    { 0xDD, 0x18, 0xDE, 0x08, 0x0F },
    { 0x00, 0x00, 0x00, 0x00, 0x32 },
    { 0x00, 0x00, 0x00, 0x00, 0x32 },
    { 0xDE, 0x02, 0x00, 0x00, 0x0F },
    { 0xDE, 0x02, 0x00, 0x00, 0x0F },
    { 0xE4, 0x02, 0x00, 0x00, 0x0F },
    { 0xDE, 0x02, 0x00, 0x00, 0x0F },
    { 0xDF, 0x02, 0xE1, 0x02, 0x0F },
    { 0xDF, 0x02, 0xC5, 0x02, 0x0F },
    { 0xDE, 0x04, 0x00, 0x00, 0x0F },
    { 0xDE, 0x02, 0x00, 0x00, 0x0F },
    { 0xE1, 0x02, 0x00, 0x00, 0x0F },
    { 0x00, 0x00, 0x00, 0x00, 0x0F },
    { 0xDE, 0x02, 0x00, 0x00, 0x0F },
    { 0xDF, 0x64, 0x00, 0x00, 0x0F },
    { 0xDE, 0x02, 0x00, 0x00, 0x0F },
    { 0xE1, 0x04, 0x64, 0x01, 0x0F },
    { 0xE4, 0x04, 0xDF, 0x02, 0x0F },
    { 0xDE, 0x04, 0x00, 0x00, 0x0F },
    { 0xDD, 0x01, 0x00, 0x00, 0x0F },
    { 0xDF, 0x02, 0xE1, 0x02, 0x0F },
    { 0xDE, 0x04, 0xDF, 0x08, 0x0F },
    { 0xE1, 0x0C, 0xA4, 0x02, 0x0F },
    { 0xE4, 0x08, 0xB4, 0x02, 0x0F },
    { 0xDF, 0x18, 0xB5, 0x02, 0x0F },
    { 0xE1, 0x0C, 0xA4, 0x02, 0x0F },
    { 0xDD, 0x02, 0x00, 0x00, 0x0F },
    { 0xDD, 0x02, 0x00, 0x00, 0x0F },
    { 0xE4, 0x01, 0x00, 0x00, 0x0F },
    { 0xDD, 0x01, 0x00, 0x00, 0x0F },
    { 0xDE, 0x04, 0x00, 0x00, 0x0F },
    { 0xDE, 0x02, 0x00, 0x00, 0x0F },
    { 0xDE, 0x01, 0x00, 0x00, 0x0F },
    { 0xDE, 0x04, 0x00, 0x00, 0x0F },
};

const struct BattleBossDrops gBattleBossDropTable[] __attribute__((section(".rodata.0039D9B8"), aligned(1))) =
{
    { 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00 },
    { 0x5D, 0x0A, 0x5D, 0x0A },
    { 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00 },
    { 0xE0, 0x64, 0xE0, 0x64 },
    { 0xE0, 0x64, 0xE0, 0x64 },
    { 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00 },
};

/* End of split read-only data. */
