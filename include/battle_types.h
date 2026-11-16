#ifndef GUARD_BATTLE_TYPES_H
#define GUARD_BATTLE_TYPES_H

#include "common.h"

#define BATTLE_PARTY_COUNT 5
#define BATTLE_ENEMY_COUNT 7
#define BATTLE_COMBATANT_COUNT 12
#define BATTLE_CHARACTER_ID_COUNT 11
#define BATTLE_SKILL_COUNT 8
#define BATTLE_WINDOW_COUNT 3
#define BATTLE_TILEMAP_SIZE 1024
#define BATTLE_NO_COMBATANT 0xFF
#define BATTLE_NO_SKILL 0xFF
#define BATTLE_STATUS_MASK_ALL 0x1FF /* Status numbers 0..8; still rejects empty slots. */

/** @brief Packed formation coordinates used by directional target navigation.
 *  @note Row comparisons and remembered rows retain the unshifted high nibble. */
enum BattleFormationBits
{
    BATTLE_FORMATION_COLUMN_MASK = 0x0F,
    BATTLE_FORMATION_ROW_MASK = 0xF0,
};

enum BattleSide
{
    BATTLE_SIDE_PARTY = 0,
    BATTLE_SIDE_ENEMY = 1,
};

/* Stored as a byte at BattleCombatant.action; enum storage is not used in RAM. */
enum BattleAction
{
    BATTLE_ACTION_ATTACK = 0,
    BATTLE_ACTION_SKILL = 1,
    BATTLE_ACTION_ITEM = 2,
    BATTLE_ACTION_DEFEND = 3,
    BATTLE_ACTION_ESCAPE = 4,
    BATTLE_ACTION_NONE = -1, /* Stored as FF in the signed eight-bit action field. */
};

/** @brief Numbered conditions stored in BattleCombatant.status.
 *  @note These are byte values, not masks. Skill descriptions and rendered icons
 *  identify conditions 2..6; escape and defeat have separate removal paths.
 *  The enum does not change the record's storage width or status-priority rule.
 *  @address 0x08045F94, 0x0803586C, 0x080405A4, 0x0801DC20
 */
enum BattleStatus
{
    BATTLE_STATUS_NORMAL = 0,
    BATTLE_STATUS_POISONED = 1,
    BATTLE_STATUS_PARALYZED = 2,
    BATTLE_STATUS_ASLEEP = 3,
    BATTLE_STATUS_PETRIFIED = 4,
    BATTLE_STATUS_CONFUSED = 5,
    BATTLE_STATUS_SILENCED = 6,
    BATTLE_STATUS_ESCAPED = 7,
    BATTLE_STATUS_DEFEATED = 8,
};

/** @brief Byte selectors accepted by the effective combatant-stat query.
 *  @note ATC/DEF/MEN/RES include equipment effects; sums wrap to halfwords.
 *  These selectors do not change the storage type of any stat or modifier.
 *  @address 0x08047024
 */
enum BattleCombatantStat
{
    BATTLE_STAT_LEVEL = 0,
    BATTLE_STAT_HP = 1,
    BATTLE_STAT_MAX_HP = 2,
    BATTLE_STAT_MP = 3,
    BATTLE_STAT_MAX_MP = 4,
    BATTLE_STAT_ATC = 5,
    BATTLE_STAT_DEF = 6,
    BATTLE_STAT_AGL = 7,
    BATTLE_STAT_MEN = 8,
    BATTLE_STAT_RES = 9,
    BATTLE_STAT_NOA = 10,
    BATTLE_STAT_HOME_X = 11,
    BATTLE_STAT_HOME_Y = 12,
};

enum BattleTargetEntryBits
{
    BATTLE_TARGET_SLOT_MASK = 0x0F,
    BATTLE_TARGET_VALID = 0x10,
    BATTLE_TARGET_FLAGS_MASK = 0xF0,
};

/** @brief Independent bits within BattleCombatant.displayState.
 *  @note Low state/movement subfields are not boolean flags.
 *  @address 0x080209C8, 0x08020D50, 0x08020CC4
 */
enum BattleDisplayFlags
{
    BATTLE_DISPLAY_GAUGE_DIRTY = 0x0002,
    BATTLE_DISPLAY_HIDE_STATUS_ICON = 0x0004,
    BATTLE_DISPLAY_DAMAGE_PROTECTED = 0x0010,
    BATTLE_DISPLAY_STATUS_HIGHLIGHT = 0x0080,
    BATTLE_DISPLAY_STATUS_ICON_RISING = 0x0200,
    BATTLE_DISPLAY_MAGIC_GUARD = 0x1000, /* Consumed by magic or category-one enemy damage. */
    BATTLE_DISPLAY_EFFECT_ANIMATION = 0x2000,
    BATTLE_DISPLAY_MAGIC_POWER_BOOST = 0x4000, /* Consumed when magic damage is calculated. */
};

/** @brief Animation playback/render bits, not BattleCombatant.displayState.
 *  @note FLIP_X mirrors sprite-part positions and their horizontal orientation.
 *  @address 0x0801A884
 */
enum BattleAnimationFlags
{
    BATTLE_ANIM_SEMITRANSPARENT = 0x0010, /* Force OBJ mode 1. */
    BATTLE_ANIM_FLIP_X = 0x0020,         /* Mirror part positions and horizontal orientation. */
    BATTLE_ANIM_HIDDEN = 0x0200,          /* Skip drawing and graphics upload. */
    BATTLE_ANIM_LOOP = 0x0400,            /* Do not latch the final-frame event. */
    BATTLE_ANIM_LOADING = 0x0800,
    BATTLE_ANIM_FINISHED = 0x1000,
};

enum BattleStatusMatch
{
    BATTLE_STATUS_MATCH_EMPTY = 0,
    BATTLE_STATUS_MATCH_REJECTED = 1,
    BATTLE_STATUS_MATCH_ACCEPTED = 2,
};

/** @brief Effects queued in the halfword at BattleCombatant +0xB8.
 *  @note Resolution handles these independently from the numbered status byte.
 *  HP damage is capped at 100; recovery uses one tenth of maximum HP.
 *  @address 0x0804A148, 0x0804A368
 */
enum BattleTurnEndEffect
{
    BATTLE_TURN_END_PARTY_HP_RECOVERY = 0x0001,
    BATTLE_TURN_END_MP_DRAIN = 0x0002,
    BATTLE_TURN_END_GAUGE_GAIN = 0x0004,
    BATTLE_TURN_END_HP_RECOVERY = 0x0008,
    BATTLE_TURN_END_HP_DAMAGE = 0x0010,
};

/** @brief Intrusive ascending draw-order list, also used by effect records.
 *  @note The sentinel has key FF and points to itself; equal keys insert first. */
struct BattleDrawNode
{
    u8 sortKey;                         /* 0x00 */
    u8 unknown01[3];
    struct BattleDrawNode *previous;     /* 0x04 */
    struct BattleDrawNode *next;         /* 0x08 */
};

struct BattleFrameTableHeader
{
    u16 unknown00;
    u8 paletteCount;
    u8 unknown03;
};

/** @brief Track count and playback duration shared by animation timelines.
 *  @note Every track has a separate keyframe-offset entry; duration is in frames.
 *  @address 0x0801AD0C, 0x0801B81C
 */
struct BattleTimelineHeader
{
    u16 trackCount;
    u16 frameCount;
};

/** @brief One timeline interval; endFrame is an exclusive upper bound. */
struct BattleKeyframe
{
    u16 frameIndex;
    u16 endFrame;
};

/** @brief Animation instance shared by combatants, cursor and action effects.
 *  @note Field widths follow the fieldwise copy at 0801A5EC and setup at 0801B81C. */
struct BattleAnimation
{
    const struct BattleFrameTableHeader *frames; /* 0x00 */
    const struct BattleTimelineHeader *timeline; /* 0x04 */
    const u16 *frameOffsets;              /* 0x08 */
    const u16 *timelineOffsets;           /* 0x0C */
    const u16 *definition;               /* 0x10; two relative table offsets */
    const u16 *palettes;                 /* 0x14 */
    u16 flags;                           /* 0x18; low nibble selects render/load type */
    u16 frame;                           /* 0x1A */
    u16 timelineFrame;                  /* 0x1C; playback position modulo timeline duration */
    u16 baseTile;                        /* 0x1E */
    u16 graphicsBlockCount;              /* 0x20 */
    u16 graphicsBlock;                   /* 0x22 */
    u16 initialBaseTile;                 /* 0x24 */
    u16 graphicsId;                      /* 0x26 */
    u8 unknown28;
    u8 paletteSlot;                      /* 0x29 */
    u8 priority;                         /* 0x2A */
    u8 x;                                /* 0x2B; low byte of screen coordinate */
    u8 y;                                /* 0x2C; low byte of screen coordinate */
    u8 oamStart;                         /* 0x2D; highest used OAM slot */
    u8 oamCount;                         /* 0x2E; slots descend, lower endpoint excluded */
    u8 initialPaletteSlot;               /* 0x2F */
};

/** @brief Per-character animation ids and normal-attack continuation rules.
 *  @note Eleven 0x18-byte ROM records; variant selects one of three weapon styles.
 *  Probability bytes are interpreted by their callers: 08048690 compares
 *  random % 100 strictly, while the turn dispatcher also has a % 101 rule. */
struct BattleCharacterActionInfo
{
    u8 idleAnimationIds[3];             /* 0x00 */
    u8 attackAnimationBaseIds[3];       /* 0x03; add the current hit index */
    u8 animationId06;
    u8 animationId07;
    u8 statusAnimationId;               /* 0x08 */
    u8 maxHitCounts[3];                 /* 0x09 */
    u8 followupChances[3];              /* 0x0C; indexed by completed hit count */
    u8 unknown0F;
    u16 animationId10;
    u16 defendAnimationId;              /* 0x12 */
    u16 escapeAnimationId;              /* 0x14 */
    u16 animationId16;
};

typedef char BattleCharacterActionInfoSizeCheck[(sizeof(struct BattleCharacterActionInfo) == 0x18) ? 1 : -1];
typedef char BattleCharacterActionMaxHitsCheck[(offsetof(struct BattleCharacterActionInfo, maxHitCounts) == 9) ? 1 : -1];
typedef char BattleCharacterActionFollowupsCheck[(offsetof(struct BattleCharacterActionInfo, followupChances) == 0xC) ? 1 : -1];
typedef char BattleCharacterActionDefendCheck[(offsetof(struct BattleCharacterActionInfo, defendAnimationId) == 0x12) ? 1 : -1];

struct BattleEnemyInfo;
struct BattleBossInfo;

/** @brief Party action state occupying the same four bytes as an enemy-record pointer.
 *  @note Accuracy subtracts chanceAdjustment; escape and status recovery add it.
 *  Party-stat import leaves this byte unchanged, including when persistent LUC
 *  changes. The name describes its consumers; a nonzero producer is unconfirmed.
 *  @address 0x08045328, 0x08048310, 0x08048458, 0x08048C80
 */
struct BattlePartyActionState
{
    u16 specialGauge;                   /* Combatant +0x88 */
    u8 specialSkill;                    /* Combatant +0x8A */
    u8 chanceAdjustment;               /* Combatant +0x8B; unsigned probability adjustment */
};

/** @brief Side-dependent view of BattleCombatant.source.
 *  @note Ordinary enemies and bosses use different ROM record layouts;
 *  party members store values here rather than a pointer. No storage is added.
 *  @address 0x080200E8, 0x08047D28, 0x08047DC8
 */
union BattleCombatantSource
{
    const struct BattleEnemyInfo *enemy;
    const struct BattleBossInfo *boss;
    struct BattlePartyActionState party;
};

typedef char BattlePartyActionStateSizeCheck[(sizeof(struct BattlePartyActionState) == 4) ? 1 : -1];
typedef char BattlePartySpecialSkillCheck[(offsetof(struct BattlePartyActionState, specialSkill) == 2) ? 1 : -1];
typedef char BattlePartyChanceAdjustmentCheck[(offsetof(struct BattlePartyActionState, chanceAdjustment) == 3) ? 1 : -1];
typedef char BattleCombatantSourceSizeCheck[(sizeof(union BattleCombatantSource) == 4) ? 1 : -1];

/** @brief Twelve battle records: party slots 0..4, enemy slots 5..11.
 *  @note The scene cursor reuses this layout but is not a member of the array.
 *  HP and damage are also read as signed halfwords by damage application.
 *  Unknown fields retain offsets; +88 is deliberately a union, not two objects. */
struct BattleCombatant
{
    struct BattleDrawNode drawNode;       /* 0x00 */
    struct BattleAnimation animation;     /* 0x0C */
    struct BattleAnimation effectAnimation; /* 0x3C */
    u16 hp;                              /* 0x6C */
    u16 maxHp;                           /* 0x6E */
    u16 mp;                              /* 0x70 */
    u16 maxMp;                           /* 0x72 */
    u16 atc;                             /* 0x74 */
    u16 def;                             /* 0x76 */
    u16 agl;                             /* 0x78 */
    u16 men;                             /* 0x7A */
    u16 res;                             /* 0x7C */
    s16 atcModifier;                     /* 0x7E */
    s16 defModifier;                     /* 0x80 */
    s16 aglModifier;                     /* 0x82 */
    s16 menModifier;                     /* 0x84 */
    s16 resModifier;                     /* 0x86 */
    union BattleCombatantSource source;  /* 0x88; enemy ids 113..129 use the boss view. */
    u8 unknown8C;
    u8 equipment[6];                    /* 0x8D */
    u8 unknown93[6];
    u8 skills[BATTLE_SKILL_COUNT];       /* 0x99; zero-based skill ids, FF unused */
    u8 skillSelection;                   /* 0xA1; slot 0..7 or direct skill id */
    u8 statusPaletteOffset;              /* 0xA2; HP/status palette offset, normally 0..3 */
    u8 statusHighlightPalette;            /* 0xA3; allocated BG bank while display bit 0x80 is set */
    u8 itemId;                           /* 0xA4; reserved item or usable equipment id */
    u8 unknownA5[3];
    u8 statusIconPhase;                  /* 0xA8; bobbing counter, 0..16 */
    u8 noa;                              /* 0xA9; action repetition limit, not animation hit count */
    u8 level;                            /* 0xAA; internal level */
    u8 status;                           /* 0xAB; enum BattleStatus, not a bitmask */
    u8 formationBits;                    /* 0xAC; row in high nibble, column in low nibble */
    u8 unknownAD[3];
    u16 displayState;                    /* 0xB0; animation state and flags */
    s16 pendingDamage;                   /* 0xB2; also recovery amount, or turn counter while fainted */
    u16 hitReactionStartFrame;           /* 0xB4; action timer threshold; blink strike subtracts five. */
    u16 hitEffectId;                     /* 0xB6; Battle_StartCombatantEffect selector, not a duration. */
    u16 turnEndEffects;                   /* 0xB8; enum BattleTurnEndEffect */
    u8 unknownBA;
    u8 formationSlot;                    /* 0xBB */
    s8 action : 8;                       /* 0xBC; enum BattleAction, signed byte bitfield */
    u8 targetSlot;                       /* 0xBD */
    u8 id;                               /* 0xBE; party 0..10, enemy 11..254, FF empty */
    u8 homeX;                            /* 0xBF */
    u8 homeY;                            /* 0xC0 */
    u8 verticalLift;                     /* 0xC1; rendered Y is homeY minus this byte. */
    u8 enemySkillIndex;                  /* 0xC2; index into boss skillAnimationIds */
    u8 attackSpacingWidth;               /* 0xC3; half this width contributes to horizontal attack approach */
    u8 unknownC4;                        /* 0xC4; initialized from enemy size class */
    u8 unknownC5[3];
};

/** @brief Four-byte enemy placement within a 0x20-byte encounter variant. */
struct BattleEnemyPlacement
{
    u8 enemyId;
    u8 x;
    u8 y;
    u8 formationBits;
} __attribute__((packed));

struct BattleEncounter
{
    u8 enemyCount;
    struct BattleEnemyPlacement enemies[BATTLE_ENEMY_COUNT];
    u8 unknown1D[3];
} __attribute__((packed));

/** @brief Ordinary enemy action-handler ids with the AI's overlapping word view.
 *  @note Dispatch reads separate halfwords; confused AI deliberately reads the
 *  entire word and chooses attack only when both ids together are zero.
 *  @address 0x0803F444, 0x0804D0F8
 */
union BattleEnemyActionSelection
{
    u32 confusedActionPolicy;
    struct
    {
        u16 attackHandlerId;
        u16 skillHandlerId;
    } handlers;
};

/** @brief ROM enemy statistics and animation/action selectors.
 *  @note The action-selection word has two access widths; unknown bytes stay explicit.
 *  @address 0x083989FC
 */
struct BattleEnemyInfo
{
    u16 level;
    u16 hp;
    u16 mp;
    u16 atc;
    u16 def;
    u16 agl;
    u16 men;
    u16 res;
    u16 noa;
    u16 experienceReward;               /* 0x12; accumulated for defeated enemies */
    u16 goldReward;                     /* 0x14; before equipment reward bonuses */
    u16 weaknessElements;               /* 0x16; damage response reads all sixteen bits. */
    u16 resistanceElements;             /* 0x18; outgoing ordinary-enemy element reads the low byte. */
    u16 animationBaseId;                /* 0x1A; normal attack adds 3 */
    union BattleEnemyActionSelection actionSelection; /* 0x1C; handler ids / confused AI word */
    u16 sizeClass;                       /* 0x20 */
    u8 unknown22;
    u8 attackSpacingWidth;               /* 0x23; ordinary attack approach width */
    u8 skillSpacingWidth;                /* 0x24; alternate-action approach width */
    u8 unknown25[4];
    u8 alternateAnimationOffset;        /* 0x29; added for action 1 */
    u8 unknown2A[2];
};

/** @brief High-id enemy record stored in the extended preview table.
 *  @note This is a 0x40-byte view of the same ROM records core previews use;
 *  it is not the ordinary 0x2C-byte BattleEnemyInfo layout. */
struct BattleBossInfo
{
    u16 idleAnimationId;                /* 0x00 */
    u16 attackAnimationId;              /* 0x02 */
    u16 unknown04[2];
    u16 skillAnimationIds[5];           /* 0x08; selected by combatant +C2 */
    u16 unknown12[2];
    u16 hp;                             /* 0x16 */
    u16 mp;                             /* 0x18 */
    u16 atc;                            /* 0x1A */
    u16 def;                            /* 0x1C */
    u16 agl;                            /* 0x1E */
    u16 men;                            /* 0x20 */
    u16 res;                            /* 0x22 */
    u16 noa;                            /* 0x24 */
    u16 experienceReward;               /* 0x26 */
    u16 goldReward;                     /* 0x28 */
    u16 weaknessElements;               /* 0x2A; distinct offsets from ordinary enemies. */
    u16 resistanceElements;             /* 0x2C; tested against the incoming low-four-bit mask. */
    u8 unknown2E;
    u8 attackHandlerId;                 /* 0x2F; boss action-handler table index */
    u8 skillHandlerIds[5];              /* 0x30; selected by enemySkillIndex */
    u8 unknown35[6];
    u8 unknown3B;                       /* 0x3B; copied to combatant +C3 */
    u8 unknown3C;                       /* 0x3C; copied to combatant +C4 */
    u8 unknown3D[3];
};

typedef char BattleBossInfoSizeCheck[(sizeof(struct BattleBossInfo) == 0x40) ? 1 : -1];
typedef char BattleBossAttackAnimationCheck[(offsetof(struct BattleBossInfo, attackAnimationId) == 2) ? 1 : -1];
typedef char BattleBossSkillAnimationsCheck[(offsetof(struct BattleBossInfo, skillAnimationIds) == 8) ? 1 : -1];
typedef char BattleBossHpCheck[(offsetof(struct BattleBossInfo, hp) == 0x16) ? 1 : -1];
typedef char BattleBossExperienceCheck[(offsetof(struct BattleBossInfo, experienceReward) == 0x26) ? 1 : -1];
typedef char BattleBossGoldCheck[(offsetof(struct BattleBossInfo, goldReward) == 0x28) ? 1 : -1];
typedef char BattleBossWeaknessCheck[(offsetof(struct BattleBossInfo, weaknessElements) == 0x2A) ? 1 : -1];
typedef char BattleBossResistanceCheck[(offsetof(struct BattleBossInfo, resistanceElements) == 0x2C) ? 1 : -1];
typedef char BattleEnemyExperienceCheck[(offsetof(struct BattleEnemyInfo, experienceReward) == 0x12) ? 1 : -1];
typedef char BattleEnemyGoldCheck[(offsetof(struct BattleEnemyInfo, goldReward) == 0x14) ? 1 : -1];
typedef char BattleEnemyWeaknessCheck[(offsetof(struct BattleEnemyInfo, weaknessElements) == 0x16) ? 1 : -1];
typedef char BattleEnemyResistanceCheck[(offsetof(struct BattleEnemyInfo, resistanceElements) == 0x18) ? 1 : -1];
typedef char BattleEnemyAnimationBaseCheck[(offsetof(struct BattleEnemyInfo, animationBaseId) == 0x1A) ? 1 : -1];
typedef char BattleEnemyActionSelectionSizeCheck[(sizeof(union BattleEnemyActionSelection) == 4) ? 1 : -1];
typedef char BattleEnemyConfusionPolicyCheck[(offsetof(struct BattleEnemyInfo, actionSelection) == 0x1C) ? 1 : -1];
typedef char BattleEnemyAttackHandlerCheck[(offsetof(struct BattleEnemyInfo, actionSelection.handlers.attackHandlerId) == 0x1C) ? 1 : -1];
typedef char BattleEnemySkillHandlerCheck[(offsetof(struct BattleEnemyInfo, actionSelection.handlers.skillHandlerId) == 0x1E) ? 1 : -1];
typedef char BattleBossAttackHandlerCheck[(offsetof(struct BattleBossInfo, attackHandlerId) == 0x2F) ? 1 : -1];
typedef char BattleBossSkillHandlersCheck[(offsetof(struct BattleBossInfo, skillHandlerIds) == 0x30) ? 1 : -1];
typedef char BattleEnemyAttackSpacingCheck[(offsetof(struct BattleEnemyInfo, attackSpacingWidth) == 0x23) ? 1 : -1];
typedef char BattleEnemySkillSpacingCheck[(offsetof(struct BattleEnemyInfo, skillSpacingWidth) == 0x24) ? 1 : -1];
typedef char BattleEnemyAlternateAnimationCheck[(offsetof(struct BattleEnemyInfo, alternateAnimationOffset) == 0x29) ? 1 : -1];

typedef char BattleDrawNodeSizeCheck[(sizeof(struct BattleDrawNode) == 0xC) ? 1 : -1];
typedef char BattleAnimationSizeCheck[(sizeof(struct BattleAnimation) == 0x30) ? 1 : -1];
typedef char BattleAnimationFlagsCheck[(offsetof(struct BattleAnimation, flags) == 0x18) ? 1 : -1];
typedef char BattleAnimationTimelineFrameCheck[(offsetof(struct BattleAnimation, timelineFrame) == 0x1C) ? 1 : -1];
typedef char BattleTimelineHeaderSizeCheck[(sizeof(struct BattleTimelineHeader) == 4) ? 1 : -1];
typedef char BattleAnimationPositionCheck[(offsetof(struct BattleAnimation, x) == 0x2B) ? 1 : -1];
typedef char BattleAnimationOamStartCheck[(offsetof(struct BattleAnimation, oamStart) == 0x2D) ? 1 : -1];
typedef char BattleAnimationOamCountCheck[(offsetof(struct BattleAnimation, oamCount) == 0x2E) ? 1 : -1];
typedef char BattleCombatantStatusIconPhaseCheck[(offsetof(struct BattleCombatant, statusIconPhase) == 0xA8) ? 1 : -1];
typedef char BattleCombatantReactionStartCheck[(offsetof(struct BattleCombatant, hitReactionStartFrame) == 0xB4) ? 1 : -1];
typedef char BattleCombatantHitEffectCheck[(offsetof(struct BattleCombatant, hitEffectId) == 0xB6) ? 1 : -1];
typedef char BattleCombatantVerticalLiftCheck[(offsetof(struct BattleCombatant, verticalLift) == 0xC1) ? 1 : -1];
typedef char BattleCombatantSizeCheck[(sizeof(struct BattleCombatant) == 0xC8) ? 1 : -1];
typedef char BattleCombatantAnimationCheck[(offsetof(struct BattleCombatant, animation) == 0xC) ? 1 : -1];
typedef char BattleCombatantEffectCheck[(offsetof(struct BattleCombatant, effectAnimation) == 0x3C) ? 1 : -1];
typedef char BattleCombatantHpCheck[(offsetof(struct BattleCombatant, hp) == 0x6C) ? 1 : -1];
typedef char BattleCombatantSourceCheck[(offsetof(struct BattleCombatant, source) == 0x88) ? 1 : -1];
typedef char BattleCombatantSkillsCheck[(offsetof(struct BattleCombatant, skills) == 0x99) ? 1 : -1];
typedef char BattleCombatantStatusPaletteCheck[(offsetof(struct BattleCombatant, statusPaletteOffset) == 0xA2) ? 1 : -1];
typedef char BattleCombatantHighlightPaletteCheck[(offsetof(struct BattleCombatant, statusHighlightPalette) == 0xA3) ? 1 : -1];
typedef char BattleCombatantItemCheck[(offsetof(struct BattleCombatant, itemId) == 0xA4) ? 1 : -1];
typedef char BattleCombatantStatusCheck[(offsetof(struct BattleCombatant, status) == 0xAB) ? 1 : -1];
typedef char BattleCombatantFormationCheck[(offsetof(struct BattleCombatant, formationBits) == 0xAC) ? 1 : -1];
typedef char BattleCombatantDisplayCheck[(offsetof(struct BattleCombatant, displayState) == 0xB0) ? 1 : -1];
typedef char BattleCombatantTurnEndEffectsCheck[(offsetof(struct BattleCombatant, turnEndEffects) == 0xB8) ? 1 : -1];
/* The action bitfield fills the byte between formationSlot and targetSlot. */
typedef char BattleCombatantActionBeforeCheck[(offsetof(struct BattleCombatant, formationSlot) == 0xBB) ? 1 : -1];
typedef char BattleCombatantActionAfterCheck[(offsetof(struct BattleCombatant, targetSlot) == 0xBD) ? 1 : -1];
typedef char BattleCombatantEnemySkillCheck[(offsetof(struct BattleCombatant, enemySkillIndex) == 0xC2) ? 1 : -1];
typedef char BattleCombatantAttackSpacingCheck[(offsetof(struct BattleCombatant, attackSpacingWidth) == 0xC3) ? 1 : -1];
typedef char BattleCombatantIdCheck[(offsetof(struct BattleCombatant, id) == 0xBE) ? 1 : -1];
typedef char BattleEncounterSizeCheck[(sizeof(struct BattleEncounter) == 0x20) ? 1 : -1];
typedef char BattleEnemyInfoSizeCheck[(sizeof(struct BattleEnemyInfo) == 0x2C) ? 1 : -1];

#endif
