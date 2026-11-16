#ifndef PARTY_H
#define PARTY_H

#include "common.h"

#define PARTY_CHARACTER_COUNT 10
#define PARTY_MAX_MEMBERS 5
#define PARTY_MEMBER_SLOT_COUNT 6
#define PARTY_EQUIPMENT_SLOT_COUNT 6
#define PARTY_SKILL_SLOT_COUNT 8
#define PARTY_LEARNABLE_SKILL_COUNT 48
#define PARTY_ITEM_COUNT 256
#define PARTY_ATTRIBUTE_COUNT 7
#define PARTY_GROWING_STAT_COUNT 8
#define PARTY_GROWTH_CURVE_COUNT 41
#define PARTY_GROWTH_LEVEL_COUNT 100

#define PARTY_NO_MEMBER 0xFF
#define PARTY_NO_SKILL 0xFF
#define PARTY_NO_ITEM 0
#define PARTY_MAX_ITEM_COUNT 99
#define PARTY_MAX_GOLD 999999
#define PARTY_MAX_EXPERIENCE 9999999
#define PARTY_MAX_INTERNAL_LEVEL 98

/* Attribute numbers follow the growth function and the recovered character layout. */
enum PartyStat
{
    PARTY_STAT_MAX_HP = 0,
    PARTY_STAT_MAX_MP = 1,
    PARTY_STAT_ATC = 2,
    PARTY_STAT_DEF = 3,
    PARTY_STAT_AGL = 4,
    PARTY_STAT_MEN = 5,
    PARTY_STAT_RES = 6,
    PARTY_STAT_NOA = 7,
    PARTY_STAT_LUC = 8,
};

/* Each nibble of PartyItemInfo.bonusStatIds selects one equipment bonus. */
enum PartyItemBonusStat
{
    PARTY_ITEM_BONUS_ATC = 1,
    PARTY_ITEM_BONUS_DEF = 2,
    PARTY_ITEM_BONUS_AGL = 3,
    PARTY_ITEM_BONUS_MEN = 4,
    PARTY_ITEM_BONUS_RES = 5,
    PARTY_ITEM_BONUS_NOA = 6,
    PARTY_ITEM_BONUS_LUC = 7,
};

/** @brief Persistent 0x40-byte party statistics, equipment and battle presets.
 *  @note Character ids 1..10 map to indices 0..9. The saved gauge is a byte;
 *  Battle_InitPartyCombatantStats expands it into the battle record's halfword.
 *  @address 0x03004AC0, 0x080200E8, 0x080457AC
 */
struct PartyCharacter
{
    u8 level;                      /* 0x00; display level - 1 */
    u8 unknown01;                  /* 0x01 */
    u16 hp;                        /* 0x02 */
    u16 mp;                        /* 0x04 */
    u16 atc;                       /* 0x06 */
    u16 def;                       /* 0x08 */
    u16 agl;                       /* 0x0A */
    u16 men;                       /* 0x0C */
    u16 res;                       /* 0x0E */
    u8 noa;                        /* 0x10 */
    u8 luc;                        /* 0x11 */
    u16 maxHp;                     /* 0x12 */
    u16 maxMp;                     /* 0x14 */
    u8 baseAtc;                    /* 0x16 */
    u8 baseDef;                    /* 0x17 */
    u8 baseAgl;                    /* 0x18 */
    u8 baseMen;                    /* 0x19 */
    u8 baseRes;                    /* 0x1A */
    u8 baseNoa;                    /* 0x1B */
    u8 baseLuc;                    /* 0x1C */
    u8 equipAtc;                   /* 0x1D */
    u8 equipDef;                   /* 0x1E */
    u8 equipAgl;                   /* 0x1F */
    u8 equipMen;                   /* 0x20 */
    u8 equipRes;                   /* 0x21 */
    u8 equipNoa;                   /* 0x22 */
    u8 equipLuc;                   /* 0x23 */
    u8 equipSlot1;                 /* 0x24; primary equipment */
    u8 equipSlot2;                 /* 0x25 */
    u8 equipSlot3;                 /* 0x26 */
    u8 equipSlot4;                 /* 0x27 */
    u8 equipSlot5;                 /* 0x28 */
    u8 equipSlot6;                 /* 0x29 */
    u8 skills[PARTY_SKILL_SLOT_COUNT]; /* 0x2A; unused entries are 0xFF */
    u8 specialSkill;               /* 0x32; character/equipment dependent */
    u8 specialGauge;               /* 0x33; battle settlement saves the low byte */
    u8 tacticsAction;              /* 0x34; preset action: 1 skill, 2 item, 0/3 have no id here */
    u8 tacticsActionId;            /* 0x35; skill/item id for the preset action */
    u8 unknown36;                  /* 0x36 */
    u8 characterSlot;              /* 0x37; index in gPartyCharacterSlots */
    u32 experience;                /* 0x38 */
    u32 nextLevelExperience;       /* 0x3C; cumulative threshold */
};

/** @brief Verified fields in the skill record's effect-and-flags byte.
 *  @note Bit seven hides a skill from the field menu; the battle query reads
 *  only the low nibble. Other flag bits are deliberately left unnamed.
 *  @address 0x0800F128, 0x08048984
 */
enum PartySkillEffectBits
{
    PARTY_SKILL_EFFECT_MASK = 0x0F,
    PARTY_SKILL_FIELD_DISABLED = 0x80,
};

/** @brief Five-byte ROM skill record shared by field and battle menus.
 *  @note Persistent skill ids are one-based; battle skill ids are zero-based.
 *  characterGroupBits also stores the target mode in its low nibble. The field
 *  menu interprets unknown03 as HP recovery, with zero restoring full HP;
 *  its meaning for the other records has not been established.
 *  @address 0x08093418
 */
struct PartySkillInfo
{
    u8 requiredLevel;      /* 0x00; display level, or 0xFF for conditional skills */
    u8 characterGroupBits; /* 0x01; high nibble: character group; field use low nibble 5 targets all */
    u8 effectAndFlags;     /* 0x02; enum PartySkillEffectBits */
    u8 unknown03;          /* 0x03; field HP recovery amount, 0 restores full HP */
    u8 mpCost;             /* 0x04; before equipment adjustments */
} __attribute__((packed));

typedef char PartyCharacterGaugeOffsetCheck[(offsetof(struct PartyCharacter, specialGauge) == 0x33) ? 1 : -1];
typedef char PartySkillEffectOffsetCheck[(offsetof(struct PartySkillInfo, effectAndFlags) == 2) ? 1 : -1];

/* ROM record, 12 bytes. Prices are little-endian values in units of 10 gold. */
struct PartyItemInfo
{
    u16 buyPriceUnits;     /* 0x00; menus multiply by 10 */
    u16 sellPriceUnits;    /* 0x02; menus multiply by 10 */
    u8 equipmentTypeAndSet; /* 0x04; low nibble: equipment type; high: set or consumable recovery type */
    u8 unknown05;          /* 0x05 */
    u8 bonusAmounts[2];    /* 0x06; consumables use [0] as the recovery amount */
    u8 bonusStatIds;       /* 0x08; selectors 1..7 mean ATC, DEF, AGL, MEN, RES, NOA, LUC */
    u8 unknown09;          /* 0x09 */
    u8 battlePropertyIndex; /* 0x0A; index into the three-byte battle property table */
    u8 characterMask;      /* 0x0B; one bit per character for equipment checks */
} __attribute__((packed));

/* Shared COMMON objects defined in party.c and placed by config/ram.ll.ld. */
extern struct PartyCharacter gPartyCharacters[PARTY_CHARACTER_COUNT]; /* 0x03004AC0 */
extern u8 gPartyMembers[PARTY_MEMBER_SLOT_COUNT];                     /* 0x03004AA0; slot 5 is a sentinel */
extern u8 gPartyCharacterSlots[PARTY_MEMBER_SLOT_COUNT];              /* 0x03004A88 */
extern u8 gPartyItemCounts[PARTY_ITEM_COUNT];                         /* 0x03004980 */

/* Shared equipment calculation values; menu previews also use these bytes. */
extern u8 gPartyEquipmentAtcBonus; /* 0x03004A90 */
extern u8 gPartyEquipmentDefBonus; /* 0x03004AA8 */
extern u8 gPartyEquipmentAglBonus; /* 0x03004AB0 */
extern u8 gPartyEquipmentMenBonus; /* 0x03004A98 */
extern u8 gPartyEquipmentResBonus; /* 0x03004A94 */
extern u8 gPartyEquipmentNoaBonus; /* 0x03004AB4 */
extern u8 gPartyEquipmentLucBonus; /* 0x03004AB8 */
extern u8 gPartyEquipmentSetAtcBonus; /* 0x03004AAC */
extern u8 gPartyEquipmentSetDefBonus; /* 0x03004A80 */

extern const u8 gPartyStatGrowthCurveIds[PARTY_CHARACTER_COUNT + 1][PARTY_GROWING_STAT_COUNT]; /* 0x080921F0 */
extern const u32 gPartyExperienceIncrements[PARTY_GROWTH_LEVEL_COUNT]; /* 0x08092248 */
extern const u8 gPartyStatGrowthCurves[PARTY_GROWTH_CURVE_COUNT][PARTY_GROWTH_LEVEL_COUNT]; /* 0x080923D8 */
extern const u8 gPartyInitialEquipment[PARTY_CHARACTER_COUNT][PARTY_EQUIPMENT_SLOT_COUNT]; /* 0x080933DC */
extern const struct PartySkillInfo gPartySkillTable[]; /* 0x08093418; includes special skills after the first 48 */
extern const struct PartyItemInfo gPartyItemTable[PARTY_ITEM_COUNT]; /* 0x087EA580 */

/* Unless stated otherwise, level parameters are zero-based internal levels. */
u16 Party_GetBaseStat(u8 characterId, u8 level, u8 statId);
void Party_BuildLearnedSkills(u8 *skills, u8 level, u8 characterId);
u8 Party_GetSpecialSkill(u8 characterId);
void Party_InitCharacter(u8 characterId);
/* Target level is one-based; 0 copies the first character's internal level. */
void Party_RaiseCharacterLevel(u8 targetLevel, u8 characterId);
u8 Party_GetLevelFromExperience(s32 experience);
u32 Party_GetNextLevelExperience(u8 level);
u8 Party_FindSkillLearnedAtLevel(u8 level, u8 characterId);
void Party_InitCharactersAndInventory(void);
u8 Party_GetSkillMpCost(u8 skillId);

/* Calculation writes shared bonus bytes; application updates a character record. */
void Party_AccumulateItemBonuses(u8 itemId);
void Party_CalculateEquipmentBonuses(u8 characterId);
void Party_ApplyEquipmentBonuses(u8 characterId);
void Party_ClearEquipmentBonuses(void);
/* Apply only a matching set; clear bonus values before starting a calculation. */
void Party_ApplyEquipmentSetBonus(u8 item0, u8 item1, u8 item2, u8 item3);
/* Return old equipment to inventory; replacement is not consumed, specialSkill is not refreshed. */
void Party_SetEquipment(u8 characterId, u8 itemId, u8 slot);
u8 Party_HasPrimaryEquipment(void);
/* Only ids 0/1 refresh the first character; also clears a specific paired state. */
void Party_RefreshFirstCharacterSkills(u8 characterId);

void Party_RestoreCharacterHp(struct PartyCharacter *character);
void Party_RestoreCharacterMp(struct PartyCharacter *character);
void Party_RestoreHpMp(void);
void Party_RestoreCharacterHpMp(u8 characterId);
void Party_AddItem(u8 itemId, u8 amount);
void Party_RemoveItem(u8 itemId, u8 amount);
void Party_AddGold(u32 amount);
void Party_RemoveGold(u32 amount);
u8 Party_GetItemEquipmentType(u8 itemId);
u16 Party_GetItemBuyPriceUnits(u8 itemId);
u16 Party_GetItemSellPriceUnits(u8 itemId);

#endif
