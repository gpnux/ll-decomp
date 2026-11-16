#ifndef GUARD_BATTLE_ACTION_EFFECTS_H
#define GUARD_BATTLE_ACTION_EFFECTS_H

#include "battle_types.h"

#define BATTLE_BOSS_ACTION_HANDLER_COUNT 55
#define BATTLE_ENEMY_ACTION_HANDLER_COUNT 14
#define BATTLE_SKILL_ACTION_HANDLER_COUNT 60
#define BATTLE_ATTACK_POSITION_CONFIG_COUNT 1
#define BATTLE_ENEMY_ACTION_EFFECT_CONFIG_COUNT 6
#define BATTLE_ENEMY_ACTION_SOUND_COUNT 101
#define BATTLE_HIT_REACTION_DEFAULT_DURATION 12
#define BATTLE_HIT_REACTION_DEFAULT_SFX 0x37

/** @brief Shared phases of the extended-enemy defeat fades.
 *  @note Stored in the private removal-phase byte, independently of action phase.
 *  The two ramps sample frames 0..9 and 0..19; completion stays at phase 21.
 *  @address 0x03000889, 0x08030C08..0x08032548
 */
enum BattleBossDefeatFadePhase
{
    BATTLE_BOSS_DEFEAT_FADE_INIT = 0,
    BATTLE_BOSS_DEFEAT_FADE_BACKGROUND = 1,
    BATTLE_BOSS_DEFEAT_FADE_SPRITE = 2,
    BATTLE_BOSS_DEFEAT_FADE_STOP_SOUND = 3,
    BATTLE_BOSS_DEFEAT_FADE_COMPLETE = 21,
};

/** @brief Boss-id-specific entries in the read-only defeat callback table.
 *  @note Names use verified numeric enemy ids, not guessed enemy names. All
 *  return zero while active and one after removal; ids 123/124 share an entry.
 *  The first entry clears blending one update earlier than the other entries.
 *  @address 0x08030C08..0x08032548, 0x0839CE7C
 */
u8 BattleAction_UpdateBoss113Defeat(struct BattleCombatant *combatant);
u8 BattleAction_UpdateBoss114Defeat(struct BattleCombatant *combatant);
u8 BattleAction_UpdateBoss115Defeat(struct BattleCombatant *combatant);
u8 BattleAction_UpdateBoss116Defeat(struct BattleCombatant *combatant);
u8 BattleAction_UpdateBoss117Defeat(struct BattleCombatant *combatant);
u8 BattleAction_UpdateBoss118Defeat(struct BattleCombatant *combatant);
u8 BattleAction_UpdateBoss119Defeat(struct BattleCombatant *combatant);
u8 BattleAction_UpdateBoss120Defeat(struct BattleCombatant *combatant);
u8 BattleAction_UpdateBoss121Defeat(struct BattleCombatant *combatant);
u8 BattleAction_UpdateBoss122Defeat(struct BattleCombatant *combatant);
u8 BattleAction_UpdateBoss123And124Defeat(struct BattleCombatant *combatant);
u8 BattleAction_UpdateBoss125Defeat(struct BattleCombatant *combatant);
u8 BattleAction_UpdateBoss126Defeat(struct BattleCombatant *combatant);
u8 BattleAction_UpdateBoss127Defeat(struct BattleCombatant *combatant);
u8 BattleAction_UpdateBoss128Defeat(struct BattleCombatant *combatant);
u8 BattleAction_UpdateBoss129Defeat(struct BattleCombatant *combatant);

/** @brief Phases shared by the five probability-based status skills.
 *  @note Phase nine applies status and consumes MP every time it is called;
 *  callers must stop on result two. Stored in the existing action-phase byte.
 *  @address 0x0803586C..0x08036564, 0x03000820
 */
enum BattleStatusSkillPhase
{
    BATTLE_STATUS_SKILL_INIT = 0,
    BATTLE_STATUS_SKILL_START_CAST = 1,
    BATTLE_STATUS_SKILL_WAIT_CAST_GRAPHICS = 2,
    BATTLE_STATUS_SKILL_WAIT_CAST_FINISHED = 5,
    BATTLE_STATUS_SKILL_WAIT_IDLE_GRAPHICS = 6,
    BATTLE_STATUS_SKILL_APPLY = 9,
    BATTLE_STATUS_SKILL_WAIT_RELEASE_GRAPHICS = 18,
    BATTLE_STATUS_SKILL_WAIT_ALL_GRAPHICS = 19,
    BATTLE_STATUS_SKILL_WAIT_REACTIONS = 20,
};

/** @brief Status-number-specific skill handlers; target argument is unused.
 *  @note Handler indices 17..21 apply statuses 2/3/5/6/4 with thresholds
 *  random % 100 <= 39/39/39/39/19. Status names remain unclassified.
 *  @address 0x0803586C, 0x08035B04, 0x08035D9C, 0x08036034, 0x080362CC
 */
u8 BattleAction_UpdateStatus2Skill(struct BattleCombatant *actor, struct BattleCombatant *unusedTarget);
u8 BattleAction_UpdateStatus3Skill(struct BattleCombatant *actor, struct BattleCombatant *unusedTarget);
u8 BattleAction_UpdateStatus5Skill(struct BattleCombatant *actor, struct BattleCombatant *unusedTarget);
u8 BattleAction_UpdateStatus6Skill(struct BattleCombatant *actor, struct BattleCombatant *unusedTarget);
u8 BattleAction_UpdateStatus4Skill(struct BattleCombatant *actor, struct BattleCombatant *unusedTarget);

/** @brief Phases of the animation-only ordinary-enemy action.
 *  @note Stored in sBattleActionEffectPhase as a byte; restoring idle does not
 *  complete the action until the next update.
 *  @address 0x08042200, 0x03000820
 */
enum BattleEnemyAnimationOnlyPhase
{
    BATTLE_ENEMY_ANIMATION_INIT = 0,
    BATTLE_ENEMY_ANIMATION_WAIT = 2,
    BATTLE_ENEMY_ANIMATION_DONE = 9,
};

/** @brief Phases of the unconditional enemy-retreat presentation.
 *  @note REMOVE returns two without advancing the phase: the caller must stop
 *  updating it. Control flag 0x200 skips movement and removal entirely.
 *  @address 0x08042AB4, 0x03000820
 */
enum BattleEnemyRetreatPhase
{
    BATTLE_ENEMY_RETREAT_INIT = 0,
    BATTLE_ENEMY_RETREAT_MOVE = 2,
    BATTLE_ENEMY_RETREAT_REMOVE = 9,
    BATTLE_ENEMY_RETREAT_SKIPPED = 18,
};

/** @brief Phases of the enemy skill-animation-then-hit presentation.
 *  @note This handler returns one, unlike the animation-only and retreat handlers.
 *  The shared byte storage is retained; no enum-sized RAM object is introduced.
 *  @address 0x08043938, 0x03000820
 */
enum BattleEnemyAnimationHitPhase
{
    BATTLE_ENEMY_ANIMATION_HIT_INIT = 0,
    BATTLE_ENEMY_ANIMATION_HIT_WAIT_REACTIONS = 9,
    BATTLE_ENEMY_ANIMATION_HIT_START = 18,
    BATTLE_ENEMY_ANIMATION_HIT_WAIT_GRAPHICS = 19,
    BATTLE_ENEMY_ANIMATION_HIT_WAIT_FINISHED = 20,
    BATTLE_ENEMY_ANIMATION_HIT_WAIT_IDLE = 21,
};

/** @brief Play an enemy skill animation, restore idle and start hit reactions.
 *  @note Ordinary-enemy handler twelve; returns one only when reactions are idle.
 *  SFX A5 uses slot one; target reactions last fifty frames with hit sound 64.
 *  @address 0x08043938
 */
u8 BattleAction_UpdateEnemyAnimationHit(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget);

/** @brief Play an enemy attack animation without damage or target reactions.
 *  @note Ordinary-enemy handler four; the second dispatch argument is unused.
 *  Returns zero while active and two on the update after idle restoration.
 *  @address 0x08042200
 */
u8 BattleAction_UpdateEnemyAnimationOnly(struct BattleCombatant *combatant,
    struct BattleCombatant *unusedTarget);
/** @brief Move an enemy left for twenty updates, then remove it from battle.
 *  @note Ordinary-enemy handler seven; no escape-probability test is performed.
 *  Returns two after removal or when control flag 0x200 suppresses retreat.
 *  @address 0x08042AB4
 */
u8 BattleAction_UpdateEnemyRetreat(struct BattleCombatant *combatant,
    struct BattleCombatant *unusedTarget);

/** @brief Completion codes used by the single-combatant escape handlers.
 *  @note Do not generalize these to all actions: skill handlers can also return 3/4.
 *  @address 0x0803FF54, 0x080405A4, 0x08044680
 */
enum BattleEscapeResult
{
    BATTLE_ESCAPE_RUNNING = 0,
    BATTLE_ESCAPE_PARTY_COMPLETE = 1,
    BATTLE_ESCAPE_ENEMY_COMPLETE = 2,
};

/** @brief Skill-name window phases, stored in a byte rather than an enum object.
 *  @note The close-complete update still returns zero; the following idle update returns one.
 *  @address 0x0300086A, 0x0803F328
 */
enum BattleSkillNamePhase
{
    BATTLE_SKILL_NAME_IDLE = 0,
    BATTLE_SKILL_NAME_OPEN = 1,
    BATTLE_SKILL_NAME_WAIT_OPEN = 2,
    BATTLE_SKILL_NAME_WAIT_GLYPH_UPLOAD = 3,
    BATTLE_SKILL_NAME_HOLD = 4,
    BATTLE_SKILL_NAME_WAIT_CLOSED = 5,
};

/** @brief Action handler receiving the acting combatant and the selected target.
 *  @note Consumers use the low result byte, not a boolean; the ROM tables hold Thumb pointers.
 *  @address 0x0803F444
 */
typedef u8 (*BattleActionHandler)(struct BattleCombatant *actor, struct BattleCombatant *target);

/* .rodata: no writable copies; boss/enemy record indices and party skill ids select entries. */
extern BattleActionHandler const gBattleBossActionHandlers[BATTLE_BOSS_ACTION_HANDLER_COUNT]; /* 0839CD5C */
extern BattleActionHandler const gBattleEnemyActionHandlers[BATTLE_ENEMY_ACTION_HANDLER_COUNT]; /* 0839CEC4 */
extern BattleActionHandler const gBattleSkillActionHandlers[BATTLE_SKILL_ACTION_HANDLER_COUNT]; /* 0839D4CC */

typedef char BattleActionHandlerSizeCheck[(sizeof(BattleActionHandler) == 4) ? 1 : -1];
typedef char BattleBossActionHandlersSizeCheck[(sizeof(gBattleBossActionHandlers) == 0xDC) ? 1 : -1];
typedef char BattleEnemyActionHandlersSizeCheck[(sizeof(gBattleEnemyActionHandlers) == 0x38) ? 1 : -1];
typedef char BattleSkillActionHandlersSizeCheck[(sizeof(gBattleSkillActionHandlers) == 0xF0) ? 1 : -1];

/** @brief A sound id and its inclusive animation-timeline trigger frame.
 *  @note Read as halfwords; playback is latched once per action on SFX slot two.
 *  @address 0x080446BC
 */
struct BattleActionSoundCue
{
    u16 sfxId;
    u16 startFrame;
};

/** @brief Attack and skill sound cues indexed by ordinary enemy id minus twelve.
 *  @note Actions other than skill select the attack pair; no runtime bounds check.
 *  @address 0x0839DBF6
 */
struct BattleEnemyActionSounds
{
    struct BattleActionSoundCue attack; /* 0x00 */
    struct BattleActionSoundCue skill;  /* 0x04 */
};

typedef char BattleActionSoundCueSizeCheck[(sizeof(struct BattleActionSoundCue) == 4) ? 1 : -1];
typedef char BattleActionSoundCueFrameCheck[(offsetof(struct BattleActionSoundCue, startFrame) == 2) ? 1 : -1];
typedef char BattleEnemyActionSoundsSizeCheck[(sizeof(struct BattleEnemyActionSounds) == 8) ? 1 : -1];
typedef char BattleEnemyActionSkillSoundCheck[(offsetof(struct BattleEnemyActionSounds, skill) == 4) ? 1 : -1];

/* .rodata: defined in battle_action_effects.c at the original ROM addresses. */
extern const struct BattleEnemyActionSounds gBattleEnemyActionSounds[BATTLE_ENEMY_ACTION_SOUND_COUNT];

/** @brief Enemy-specific timing and placement for the effect at 0x080422B8.
 *  @note Six twelve-byte ROM records, searched by enemy id. Only the low bytes
 *  of the coordinate halfwords are read by this handler.
 *  @address 0x0839D47C
 */
struct BattleActionEffectConfig
{
    u16 enemyId;                         /* 0x00 */
    u16 animationId;                     /* 0x02; gBattleAnimationConfigs index */
    u16 effectStartFrame;                /* 0x04; compared with actor.animation.timelineFrame */
    u16 reactionFrame;                   /* 0x06; compared with the shared action timer */
    u16 xOffset;                         /* 0x08; low byte added to homeX */
    u16 yOffset;                         /* 0x0A; low byte added to homeY */
};

typedef char BattleActionEffectConfigSizeCheck[(sizeof(struct BattleActionEffectConfig) == 0xC) ? 1 : -1];
typedef char BattleActionEffectReactionFrameCheck[(offsetof(struct BattleActionEffectConfig, reactionFrame) == 6) ? 1 : -1];
typedef char BattleActionEffectPositionCheck[(offsetof(struct BattleActionEffectConfig, xOffset) == 8) ? 1 : -1];

/** @brief Read-only configurations for the enemy action-effect handler.
 *  @note Defined in battle_action_effects.c and linked directly into ROM.
 *  @address 0x0839D47C
 */
extern const struct BattleActionEffectConfig gBattleEnemyActionEffectConfigs[BATTLE_ENEMY_ACTION_EFFECT_CONFIG_COUNT];

/** @brief Temporary horizontal displacement during a normal attack animation.
 *  @note One eight-byte record: enemy 45, frames 24/49, X offset 24; byte seven is unclassified.
 *  The handler also sets/clears display-state bit 0x40 at the two frame boundaries.
 *  @address 0x0839D4C4, 0x08040690
 */
struct BattleAttackPositionConfig
{
    u16 combatantId; /* 0x00 */
    u16 beginFrame;  /* 0x02; animation.timelineFrame */
    u16 endFrame;    /* 0x04; animation.timelineFrame */
    u8 xOffset;     /* 0x06; added to / removed from homeX */
    u8 unknown07;
};

typedef char BattleAttackPositionConfigSizeCheck[(sizeof(struct BattleAttackPositionConfig) == 8) ? 1 : -1];
typedef char BattleAttackPositionBeginFrameCheck[(offsetof(struct BattleAttackPositionConfig, beginFrame) == 2) ? 1 : -1];
typedef char BattleAttackPositionEndFrameCheck[(offsetof(struct BattleAttackPositionConfig, endFrame) == 4) ? 1 : -1];
typedef char BattleAttackPositionOffsetCheck[(offsetof(struct BattleAttackPositionConfig, xOffset) == 6) ? 1 : -1];

extern const struct BattleAttackPositionConfig gBattleAttackPositionConfigs[BATTLE_ATTACK_POSITION_CONFIG_COUNT];

/** @brief 攻击槽阵型斩击特效循环使用的四项 X 坐标。 */
extern const u8 D_0839DF67[4];

/* Paired-boss segment durations and {x, y, deltaX, deltaY} motion records. */
extern const u8 D_0839DF6B[];
extern const s16 D_0839DF70[][4];

/* Three byte-coordinate pairs used by the boss waypoint attack. */
extern const u8 D_0839DF90[];

/** @brief Dispatch the selected party, ordinary-enemy or boss action.
 *  @note Callers enter only for attack/skill; handler indices come from skill
 *  selection or the appropriate enemy record, and results use the low byte.
 *  @address 0x0803F444
 */
u8 BattleAction_Update(struct BattleCombatant *actor, struct BattleCombatant *target);
/** @brief Draw a zero-based skill name into the action window's tilemap.
 *  @note The id is a halfword here; the window-state wrapper accepts only its low byte.
 *  @address 0x0803F21C
 */
void BattleAction_DrawSkillName(u16 *tilemap, u16 skillId);
/** @brief Open, display and close the skill-name window before action execution.
 *  @note Waits for the glyph upload request to clear before the forty-frame hold.
 *  @address 0x0803F328
 */
u8 BattleAction_UpdateSkillNameWindow(u8 skillId);
/** @brief 推进默认普通攻击的动画、连续命中和目标反应。
 *  @note 普通 C 的 0x856 字节及重定位 100% 匹配，末尾两字节零填充不计；队员默认普攻与普通敌人入口零共用。
 *  @address 0x08040690
 */
u8 BattleAction_UpdateNormalAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进角色 7 的投射攻击、目标尺寸定位和连击判定。
 *  @note 普通 C 的 0x41E 字节及重定位 100% 匹配；末尾两字节零填充不计，保留武器变体的低字节读取。
 *  @address 0x08040EE8
 */
u8 BattleAction_UpdateCharacter7ProjectileAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进角色 4 的随机投射、逐次减小的弹跳和装备追加效果。
 *  @note 普通 C 的 0x3E6 字节及重定位 100% 匹配；末尾两字节零填充不计，弹跳位移保留 0xFFEC 的无符号半字基值。
 *  @address 0x08041308
 */
u8 BattleAction_UpdateCharacter4RandomProjectileAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Placement modes accepted by the shared skill-jump helper.
 *  @note Stored/passed as a byte; normal-input Sword Dance coverage establishes mode zero.
 *  Modes one through four are recovered from the reference branch and coordinate calculations.
 *  @address 0x0803E58C
 */
enum BattleSkillJumpTargetMode
{
    BATTLE_SKILL_JUMP_TARGET_ADJACENT = 0,
    BATTLE_SKILL_JUMP_FORMATION_ROW = 1,
    BATTLE_SKILL_JUMP_TARGET_GROUP_CENTER = 2,
    BATTLE_SKILL_JUMP_ENEMY_CENTER = 3,
    BATTLE_SKILL_JUMP_TARGET_EDGE = 4,
};

/** @brief Execute Sword Dance (剣のまい), party skill handler zero.
 *  @note Normal-input GPBA replay confirms two reaction starts and MP consumption of six.
 *  @address 0x08032548
 */
u8 BattleAction_UpdateSwordDance(struct BattleCombatant *actor, struct BattleCombatant *target);
/** @brief Animate a party skill's approach or return jump, selected by shared subphase.
 *  @note Initialize subphase to zero for approach or twelve for return; twenty-three returns one.
 *  @address 0x0803E58C
 */
u8 BattleAction_UpdateSkillJump(struct BattleCombatant *actor, struct BattleCombatant *target, u8 targetMode);

/** @brief Status numbers to apply after surviving targets take action damage.
 *  @note Defined as COMMON in battle_action_effects.c; combatants consumes it.
 *  Zero means no queued status, and each byte is indexed by combatant slot.
 *  @address 0x03004F90
 */
extern u8 gBattleActionPendingStatuses[BATTLE_COMBATANT_COUNT];

/** @brief Advance the party escape presentation and failed-escape position recovery.
 *  @note Called by the scene escape phase; completion and battle control flags
 *  are consumed separately by the scene dispatcher.
 *  @address 0x080401AC
 */
s32 BattleAction_UpdatePartyEscape(void);

/** @brief Phases used by the blinking close-range strike handler.
 *  @note Stored in the module's shared byte-sized effect phase, not as an enum. */
enum BattleBlinkStrikePhase
{
    BATTLE_BLINK_STRIKE_INIT = 0,
    BATTLE_BLINK_STRIKE_FADE_OUT = 1,
    BATTLE_BLINK_STRIKE_APPROACH = 2,
    BATTLE_BLINK_STRIKE_START_ATTACK = 3,
    BATTLE_BLINK_STRIKE_WAIT_GRAPHICS = 4,
    BATTLE_BLINK_STRIKE_WAIT_ANIMATION = 5,
    BATTLE_BLINK_STRIKE_CHECK_FOLLOWUP = 6,
    BATTLE_BLINK_STRIKE_RETURN = 7,
    BATTLE_BLINK_STRIKE_FOLLOWUP_DELAY = 8,
    BATTLE_BLINK_STRIKE_WAIT_REACTIONS = 9,
};

/** @brief 推进闪烁近身攻击、追加攻击判定及返位。
 *  @note 普通 C 的 0x4CC 字节及重定位 100% 匹配；复用坐标计算临时量以保持共用写入块的寄存器分配。
 *  @address 0x080264C0
 */
u8 BattleAction_UpdateBlinkStrike(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Clear pending damage for targets selected with status mask 0x6F. */
void BattleAction_ClearTargetPendingDamage(struct BattleCombatant *actor);
/** @brief Rebuild action targets and accumulate damage for valid target entries. */
void BattleAction_AccumulateTargetDamage(struct BattleCombatant *actor);
/** @brief Advance hit effects, dodges and damage reactions for this action. */
void BattleAction_UpdateTargetReactions(struct BattleCombatant *actor);
/** @brief Start target reactions; negative durations select the 12-frame default. */
void BattleAction_BeginTargetReactions(s16 duration);
/** @brief Return one when hit effects, dodges and hit reactions are all idle.
 *  @note Does not test the main action phase; this is the reaction completion gate.
 *  @address 0x080444E8
 */
u32 BattleAction_AreTargetReactionsDone(void);
/** @brief Start target reactions with an explicit hit sound and loop flag.
 *  @note Negative durations select twelve frames; sound plays later on SFX slot zero.
 *  @address 0x08044574
 */
void BattleAction_BeginTargetReactionsWithSound(s16 duration, u16 sfxId, u8 loop);
/** @brief Clear only the removal-effect phase before a defeat/removal sequence.
 *  @note Other action and reaction state is not reset.
 *  @address 0x08044414
 */
void BattleAction_ResetRemovalEffect(void);
/** @brief Read the last attack's damage halfword.
 *  @note The value is not cleared by this getter.
 *  @address 0x08044420
 */
u16 BattleAction_GetLastAttackDamage(void);
/** @brief Read the number of party members whose escape animation completed.
 *  @note Enemy escape does not increment this counter.
 *  @address 0x08044498
 */
u8 BattleAction_GetEscapedCombatantCount(void);
/** @brief Advance one party member's escape attempt and off-screen movement.
 *  @note Returns one when finished, including the failed-escape restoration path.
 *  @address 0x0803FF54
 */
u8 BattleAction_UpdatePartyMemberEscape(struct BattleCombatant *combatant);
/** @brief Advance an enemy escape attempt and remove successful escapees.
 *  @note Returns two when finished; failed attempts also reach the completion phase.
 *  @address 0x080405A4
 */
u8 BattleAction_UpdateEnemyEscape(struct BattleCombatant *combatant);
/** @brief Dispatch a single-combatant escape by character id.
 *  @note Ids 0..10 use the party path; the result retains its low byte.
 *  @address 0x08044680
 */
u8 BattleAction_UpdateCombatantEscape(struct BattleCombatant *combatant);
/** @brief Read the current party attack hit index, or zero for an enemy.
 *  @note Reads only the low byte of the shared halfword at 03000826.
 *  @address 0x080446A4
 */
u8 BattleAction_GetAttackHitIndex(const struct BattleCombatant *combatant);
/** @brief Play the extra attack sound for five special ordinary-enemy ids.
 *  @note Uses strict timeline thresholds and the common once-per-action latch.
 *  @address 0x08044394
 */
void BattleAction_UpdateSpecialEnemyAttackSound(const struct BattleCombatant *combatant);
/** @brief Play an enemy action's table-selected sound at its trigger frame.
 *  @note Shares the once-per-action latch with the special-enemy sound helper.
 *  @address 0x080446BC
 */
void BattleAction_UpdateEnemyActionSound(const struct BattleCombatant *combatant);

/** @brief Reset the action-effect, reaction, sound and pending-status state.
 *  @note Preserves the requested starting phase and clears all twelve queued status bytes.
 *  @address 0x0804442C
 */
void BattleAction_ResetState(u8 phase);
/** @brief Reset the count of combatants that completed their escape animation.
 *  @note The escape result and other phase state remain separate objects.
 *  @address 0x0804448C
 */
void BattleAction_ResetEscapeCount(void);
/** @brief Return the per-slot queued status array.
 *  @note The shared COMMON definition remains owned by battle_action_effects.c.
 *  @address 0x080445E0
 */
u8 *BattleAction_GetPendingStatuses(void);
/** @brief Queue a status for the first action target at the combatant's formation.
 *  @note Status six additionally checks whether the combatant's commands can be affected.
 *  @address 0x080445E8
 */
void BattleAction_QueueTargetStatus(struct BattleCombatant *combatant, u8 status);

/** @brief Phases of the single-cast recovery skill family.
 *  @note Byte-sized shared action state; MP is charged only in COMPLETE.
 *  SelectedHp50Skill and SideHp60Skill compile from matching ordinary C.
 *  @address 0x08034440, 0x080345AC, 0x08034D94, 0x03000820
 */
enum BattleSimpleRecoverySkillPhase
{
    BATTLE_SIMPLE_RECOVERY_INIT = 0,
    BATTLE_SIMPLE_RECOVERY_START_CAST = 1,
    BATTLE_SIMPLE_RECOVERY_WAIT_CAST_GRAPHICS = 2,
    BATTLE_SIMPLE_RECOVERY_WAIT_CAST_FINISHED = 5,
    BATTLE_SIMPLE_RECOVERY_WAIT_IDLE_GRAPHICS = 6,
    BATTLE_SIMPLE_RECOVERY_APPLY = 8,
    BATTLE_SIMPLE_RECOVERY_COMPLETE = 9,
};

/** @brief Phases of the two-animation recovery skill family.
 *  @note Recovery uses actor->targetSlot; only the revival amount also reads
 *  the second dispatch argument. COMPLETE consumes MP and returns two on every call.
 *  @address 0x08037E14..0x08038920, 0x03000820
 */
enum BattleTwoStageRecoverySkillPhase
{
    BATTLE_TWO_STAGE_RECOVERY_INIT = 0,
    BATTLE_TWO_STAGE_RECOVERY_COMPLETE = 9,
    BATTLE_TWO_STAGE_RECOVERY_START_CAST = 18,
    BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_GRAPHICS = 19,
    BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_FINISHED = 20,
    BATTLE_TWO_STAGE_RECOVERY_WAIT_RELEASE_GRAPHICS = 21,
    BATTLE_TWO_STAGE_RECOVERY_APPLY = 22,
    BATTLE_TWO_STAGE_RECOVERY_WAIT_IDLE_GRAPHICS = 23,
};

/** @brief HP, status-cure, regeneration and revival skill interfaces.
 *  @note Names describe verified effects, not guessed original skill titles.
 *  Fixed amounts are HP points; side-list mode retains the targetSlot <= 10 rule.
 *  All return zero while active and two after consuming the selected skill's MP.
 *  @address 0x08034440, 0x080345AC, 0x08034D94, 0x08037E14..0x08038920
 */
u8 BattleAction_UpdateSelectedHp50Skill(struct BattleCombatant *actor, struct BattleCombatant *unusedTarget);
u8 BattleAction_UpdateStatusCureSkill(struct BattleCombatant *actor, struct BattleCombatant *unusedTarget);
u8 BattleAction_UpdateSideHp60Skill(struct BattleCombatant *actor, struct BattleCombatant *unusedTarget);
u8 BattleAction_UpdateSelectedHp60Skill(struct BattleCombatant *actor, struct BattleCombatant *unusedTarget);
u8 BattleAction_UpdateSideHp40Skill(struct BattleCombatant *actor, struct BattleCombatant *unusedTarget);
u8 BattleAction_UpdateSideHpRegenSkill(struct BattleCombatant *actor, struct BattleCombatant *unusedTarget);
u8 BattleAction_UpdateSelectedHp999Skill(struct BattleCombatant *actor, struct BattleCombatant *unusedTarget);
/** @brief Revive actor->targetSlot using floor(target->maxHp / 3) as the amount.
 *  @note The pointer supplying maxHp and the selected slot are distinct inputs.
 *  @address 0x08038568
 */
u8 BattleAction_UpdateReviveThirdMaxHpSkill(struct BattleCombatant *actor, struct BattleCombatant *target);
u8 BattleAction_UpdateTwoStageStatusCureSkill(struct BattleCombatant *actor, struct BattleCombatant *unusedTarget);

/** @brief Thirty-percent ATC/DEF modifier skill interfaces.
 *  @note Self variants pass actor to the enemy-style presenter; target variants
 *  use the supplied target and the party presenter. Completion consumes caster MP.
 *  @address 0x08032D74, 0x08034718, 0x08036EA4, 0x08037C40, 0x080392C0
 */
u8 BattleAction_UpdateSelfAtcBoostSkill(struct BattleCombatant *actor, struct BattleCombatant *unusedTarget);
u8 BattleAction_UpdateTargetAtcBoostSkill(struct BattleCombatant *actor, struct BattleCombatant *target);
u8 BattleAction_UpdateTwoStageAtcBoostSkill(struct BattleCombatant *actor, struct BattleCombatant *target);
u8 BattleAction_UpdateTargetDefBoostSkill(struct BattleCombatant *actor, struct BattleCombatant *target);
u8 BattleAction_UpdateAlternateSelfAtcBoostSkill(struct BattleCombatant *actor, struct BattleCombatant *unusedTarget);

/** Action-table interfaces owned by battle_action_effects.c. */
/** @brief 推进 Boss 113 的单目标投射攻击演出并排队目标伤害。
 *  @note boss[0]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x080257D8
 */
u8 BattleAction_UpdateBoss113ProjectileAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 113 的火团群体攻击、目标显隐与伤害恢复。
 *  @note 普通 C 的 0x414 字节及重定位 100% 匹配；第二参数按五项战斗记录数组访问，四个目标循环均保留半字索引。
 *  @address 0x08025994
 */
u8 BattleAction_UpdateBoss113FireBurstAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 114 的火焰吐息演出并排队所选目标伤害。
 *  @note boss[2]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08025DA8
 */
u8 BattleAction_UpdateBoss114FireBreathAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 114 的毒息动画、目标反应与中毒排队。
 *  @note 普通 C 的 0x404 字节及重定位 100% 匹配；有效目标各以 40% 概率排队中毒，保留两段吐息和循环帧更新。
 *  @address 0x080260BC
 */
u8 BattleAction_UpdateBoss114PoisonBreathAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进未被当前 Boss 配置引用的中毒攻击变体。
 *  @note boss[5]；本轮受控回放返回 1。 当前 Boss 配置未引用该槽，证据来自直接调用夹具。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802698C
 */
u8 BattleAction_UpdateBossPoisonSkillVariant(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the action selected by boss[6].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x08026D08
 */
u8 BattleAction_UpdateBoss115WhirlwindAnimationVariant(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 116 的风形移动、淡出返位与单目标受击演出。
 *  @note boss[7]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08026F88
 */
u8 BattleAction_UpdateBoss116WindAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 116 的风纹群体攻击、背景循环及混合渐变。
 *  @note 普通 C 的 0x3E0 字节及重定位 100% 匹配；按动画帧推进两段混合，再等待受击和待机恢复。
 *  @address 0x0802723C
 */
u8 BattleAction_UpdateBoss116GustAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the action selected by boss[9].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x0802761C
 */
u8 BattleAction_UpdateBoss116PoisonAttackVariant(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 117 的长舌单目标打击演出。
 *  @note boss[10]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08027760
 */
u8 BattleAction_UpdateBoss117TongueAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 117 的跃起踏击与群体受击演出。
 *  @note boss[11]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08027A20
 */
u8 BattleAction_UpdateBoss117JumpStompAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 118 的绿色光弹单目标攻击演出。
 *  @note boss[12]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08027D9C
 */
u8 BattleAction_UpdateBoss118GreenProjectileAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 118 的前冲与群体受击演出。
 *  @note boss[13]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08028098
 */
u8 BattleAction_UpdateBoss118ChargeAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 118 的横向光束与所选目标受击演出。
 *  @note boss[14]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x080282EC
 */
u8 BattleAction_UpdateBoss118BeamAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 119 的前跃扑击及返位演出。
 *  @note boss[15]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x080285A0
 */
u8 BattleAction_UpdateBoss119PounceAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 119、120 共用的火焰吐息和群体受击演出。
 *  @note boss[16]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x080287EC
 */
u8 BattleAction_UpdateSharedBossFireBreathAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance both bosses' attack preparation, paired movement, hits and return.
 *  @note Boss handler 17 saves and restores both members' positions and graphics; the second argument is unused.
 *  @note 普通 C 按原址独立链接匹配 0x790 字节，含字面池和重定位；阶段处理后统一递增八位计数。
 *  @address 0x08028AD8
 */
u8 BattleAction_UpdatePairedBossAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 120 的跳跃动作与所选目标受击演出。
 *  @note boss[18]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08029268
 */
u8 BattleAction_UpdateBoss120JumpStrikeAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进未被当前 Boss 配置引用的火焰吐息攻击变体。
 *  @note boss[19]；本轮受控回放返回 1。 当前 Boss 配置未引用该槽，证据来自直接调用夹具。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08029510
 */
u8 BattleAction_UpdateBossFireBreathVariant(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the action selected by boss[20].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x08029784
 */
u8 BattleAction_UpdateBoss120PaletteCycleAttackVariant(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进未被当前 Boss 配置引用的调色板脉冲群体攻击变体。
 *  @note boss[21]；本轮受控回放返回 1。 当前 Boss 配置未引用该槽，证据来自直接调用夹具。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x080299C8
 */
u8 BattleAction_UpdateBossPalettePulseAttackVariant(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 121 的红色能量场、三组调色板渐变和目标反应。
 *  @note 普通 C 的 0x55A 字节及重定位 100% 匹配；末尾两字节零填充不计。
 *  @address 0x08029BF8
 */
u8 BattleAction_UpdateBoss121RedEnergyAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the boss attack through its three recorded screen waypoints.
 *  @note Boss handler 23 reads byte-coordinate pairs at 0839DF90..0839DF95; the shared stage byte selects the waypoint.
 *  @address 0x0802A154
 */
u8 BattleAction_UpdateBossWaypointAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 121 的蓝色能量场、三组调色板渐变和目标反应。
 *  @note 普通 C 的 0x556 字节及重定位 100% 匹配；末尾两字节零填充不计。
 *  @address 0x0802A86C
 */
u8 BattleAction_UpdateBoss121BlueEnergyAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 121 的蓝白闪光及待机恢复，不排队目标伤害。
 *  @note boss[25]；本轮受控回放返回 2。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802ADC4
 */
u8 BattleAction_UpdateBoss121FlashAnimation(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 122、124 共用的尖刺打击、前跃与返位。
 *  @note 普通 C 的 0x518 字节及重定位 100% 匹配；保留各阶段的帧阈值、插值和标志更新顺序。
 *  @address 0x0802B0F0
 */
u8 BattleAction_UpdateBoss122And124SpikeAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 122、124 共用的风纹群体攻击演出。
 *  @note boss[27]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802B608
 */
u8 BattleAction_UpdateBoss122And124WindAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 122、124 共用的火团群体攻击演出。
 *  @note boss[28]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802B8BC
 */
u8 BattleAction_UpdateBoss122And124FireBurstAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 123 的金色火花单目标攻击演出。
 *  @note boss[29]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802BB24
 */
u8 BattleAction_UpdateBoss123SparkAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 123 的背景切换、橙色爆破和群体受击演出。
 *  @note boss[30]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802BD94
 */
u8 BattleAction_UpdateBoss123FireBlastAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 123 的环形攻击变体、背景调色板循环和淡出。
 *  @note 普通 C 的 0x3A4 字节及重定位 100% 匹配；原版 paletteSlot 未初始化且阶段 25 无处理分支，均按原样保留。
 *  @address 0x0802C0EC
 */
u8 BattleAction_UpdateBoss123RingAttackVariant(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 123 的旋风及群体受击演出。
 *  @note boss[32]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802C490
 */
u8 BattleAction_UpdateBoss123WindAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 123 的落石及群体受击演出。
 *  @note boss[33]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802C714
 */
u8 BattleAction_UpdateBoss123RockfallAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 125 的蓄电、闪电移动、目标受击及待机恢复。
 *  @note 普通 C 的 0x4A8 字节及重定位 100% 匹配；保留各阶段的帧阈值、插值和标志更新顺序。
 *  @address 0x0802C9E8
 */
u8 BattleAction_UpdateBoss125LightningAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 125 的紫色雾团群体攻击演出。
 *  @note boss[35]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802CE90
 */
u8 BattleAction_UpdateBoss125PurpleMistAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 125 的大型火焰球群体攻击演出。
 *  @note boss[36]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802D1FC
 */
u8 BattleAction_UpdateBoss125FireballAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Animate the boss attack and queue paralysis on successful targets.
 *  @note Boss handler 37 tests valid encoded target entries and queues status 2 when random % 100 <= 39; completion returns one.
 *  @address 0x0802D454
 */
u8 BattleAction_UpdateBossParalysisAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 126 的腾空、蓝色光弹与单目标受击演出。
 *  @note boss[38]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802D728
 */
u8 BattleAction_UpdateBoss126LightAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 126 的交叉光线、分块光面及群体受击演出。
 *  @note boss[39]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802DA78
 */
u8 BattleAction_UpdateBoss126CrossLightAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 播放 Boss 回复动画，结束时补充最多一半最大 HP。
 *  @note Boss 处理器 40；阶段 20 回复 min(maxHp >> 1, 缺失 HP)，阶段 9 返回 2。
 *  第二参数不使用；普通 C 的 0x1D8 字节及解析后的重定位完全匹配。
 *  @address 0x0802DE04
 */
u8 BattleAction_UpdateBossHalfMaxHpRecovery(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 126 的火团及单目标受击演出。
 *  @note boss[41]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802DFDC
 */
u8 BattleAction_UpdateBoss126FireballAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 127 的龙形突击及所选目标受击演出。
 *  @note boss[42]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802E234
 */
u8 BattleAction_UpdateBoss127DragonChargeAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 127 的单目标施法与受击演出。
 *  @note boss[43]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802E49C
 */
u8 BattleAction_UpdateBoss127SingleTargetSpellAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 127 的施法光阵、爆破动画及两次背景闪光。
 *  @note 普通 C 的 0x3FC 字节及重定位 100% 匹配；保留原有阶段、计数与动画标志更新顺序。
 *  @address 0x0802E6C8
 */
u8 BattleAction_UpdateBoss127FireBlastAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the action selected by boss[45].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x0802EAC4
 */
u8 BattleAction_UpdateBoss127ThreeEffectSpellVariant(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 127 的长施法及旋风群体攻击演出。
 *  @note boss[46]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802EDD8
 */
u8 BattleAction_UpdateBoss127WindAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 127 的长施法及落石群体攻击演出。
 *  @note boss[47]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802F100
 */
u8 BattleAction_UpdateBoss127RockfallAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进未被当前 Boss 配置引用的单目标火团攻击变体。
 *  @note boss[48]；本轮受控回放返回 1。 当前 Boss 配置未引用该槽，证据来自直接调用夹具。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802F480
 */
u8 BattleAction_UpdateBossFireballAttackVariant(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 128 的跃起及群体受击演出。
 *  @note boss[49]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802F6D8
 */
u8 BattleAction_UpdateBoss128WingLeapAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 128 的地面火焰波、五轮效果循环和淡出返位。
 *  @note 普通 C 的 0x4AC 字节及重定位 100% 匹配；保留动画帧循环与各阶段计数更新顺序。
 *  @address 0x0802F9EC
 */
u8 BattleAction_UpdateBoss128FlameWaveAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 128 的蓝色光阵、横向移动及混合淡出返位。
 *  @note 普通 C 的 0x404 字节及重定位 100% 匹配；保留原有阶段、计数与动画标志更新顺序。
 *  @address 0x0802FE98
 */
u8 BattleAction_UpdateBoss128BlueCircleAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 129 的张口打击、效果移动及往返目标纵坐标。
 *  @note 普通 C 的 0x3C8 字节及重定位 100% 匹配；保留两条待机恢复路径和标志位的半字截断。
 *  @address 0x0803029C
 */
u8 BattleAction_UpdateBoss129BiteAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Boss 129 的黑紫色冲击与群体受击演出。
 *  @note boss[53]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08030664
 */
u8 BattleAction_UpdateBoss129DarkWaveAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the action selected by boss[54].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x080309B0
 */
u8 BattleAction_UpdateBoss129DarkFaceAttackVariant(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Execute the formation attack between the center jump and shared return jump.
 *  @note All 0x21C bytes and resolved relocations match from ordinary C.
 *  Handler 1 starts target reactions and a BG fade; phase three does not advance the frame counter.
 *  Completion waits for reactions, consumes MP and returns one.
 *  @address 0x0803272C
 */
u8 BattleAction_UpdateFormationJumpSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进跳向敌方阵型、斩击及返程的技能演出，并扣除所选技能槽 MP。
 *  @note skill[2]；既有受控回放返回 1。普通 C 的 0x42A 字节及 84 项已解析重定位 100% 匹配。
 *  末尾两字节零填充不计；完整 ROM 与 baserom 一致。
 *  @note 三段动画共用 u16 局部编号；阶段 19 不递增帧计数，阶段 26 只在目标反应尚未结束时递增。
 *  接近使用跳跃模式 3，返程使用模式 0；返程坐标取队伍阵型表，阶段 9 扣除 MP 并返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08032948
 */
u8 BattleAction_UpdateFormationLeapStrikeSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 播放龙系演出并为列出的队伍成员设置一次魔法防护。
 *  @note 特效结束时清除 0x2000 并设置 0x1000，扣除 MP 后返回 2；普通 C 独立链接的 0x616 字节及重定位 100% 匹配，末尾零填充不计。
 *  @address 0x08032EA0
 */
u8 BattleAction_UpdateSideMagicGuardSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进大型红龙群体攻击演出，并扣除所选技能槽 MP。
 *  @note skill[5]；背景退场后启动命中特效 0xAC，等待目标反应结束后扣除 MP 并返回 1，第二参数未使用。
 *  @note 普通 C 的 0x4D0 字节及已解析重定位 100% 匹配，保留 8 字节未访问栈空间。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x080334B8
 */
u8 BattleAction_UpdateRedDragonSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 播放龙系演出并为选中阵营的各目标回复最多 999 HP。
 *  @note 同侧回复模式 1、HP 效果 0、数值 999；扣除 MP 后返回 2，普通 C 独立链接的 0x4A4 字节及重定位 100% 匹配。
 *  @address 0x08033988
 */
u8 BattleAction_UpdateSideHp999Skill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进灰白龙演出并驱离符合条件的敌人，扣除所选技能槽 MP 后返回 2。
 *  @note skill[7]；既有受控回放返回 2。普通 C 的 0x614 字节及 128 项已解析重定位 100% 匹配，完整 ROM 与 baserom 一致。
 *  @note 敌方列表使用状态掩码 0x7F，控制标志 0x220 会跳过驱离效果；循环索引保留 u8 截断。
 *  阶段 27 依次写入 id 0xFF、逃离状态 7 并递减敌人数；阶段 28 等待 40 帧后清除特效标志 0x100。
 *  第二参数未使用，每次调用仍更新目标反应。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08033E2C
 */
u8 BattleAction_UpdateDragonEnemyRetreatSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the action selected by skill[11].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x080348A8
 */
u8 BattleAction_UpdateAreaEnemyRetreatSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Attempt to put the enumerated enemies to sleep with the sleep song.
 *  @note Skill handler 12 lists side 1 with status mask 7 and applies status 3 when random % 100 <= 39; completion consumes MP and returns two.
 *  @address 0x08034BFC
 */
u8 BattleAction_UpdateSleepSong(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the action selected by skill[14].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x08034F00
 */
u8 BattleAction_UpdateThunderboltSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the action selected by skill[15].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x08035130
 */
u8 BattleAction_UpdateRiotSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进雷电效果位移、背景调色板脉冲与淡变，完成目标反应后恢复待机并消耗 MP。
 *  @note 普通 C 的 0x50C 字节及已解析重定位 100% 匹配；完成返回 1，第二参数未使用。
 *  @address 0x08035360
 */
u8 BattleAction_UpdateLightningBgPulseSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Fire Blast 的施法、火弹位移和目标反应，恢复待机后消耗 MP。
 *  @note 普通 C 的 0x396 字节及重定位 100% 匹配；末尾两字节零填充不计。
 *  @address 0x08036564
 */
u8 BattleAction_UpdateFireBlastSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the action selected by skill[23].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x080368FC
 */
u8 BattleAction_UpdateFlameCircleSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the action selected by skill[24].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x08036B30
 */
u8 BattleAction_UpdateInfernoSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the action selected by skill[26].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x08037078
 */
u8 BattleAction_UpdateFreezeBlowSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 在目标所在阵形列的纵向中点播放 Cold Storm，完成目标反应后消耗 MP。
 *  @note 普通 C 的 0x4E0 字节及已解析重定位 100% 匹配；排序定位效果、循环时间轴、淡变背景并向左移动，结束返回 1。
 *  @address 0x08037388
 */
u8 BattleAction_UpdateColdStormSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Blizzard 的背景渐入、调色板循环、目标反应和渐出。
 *  @note 普通 C 的 0x3D8 字节及重定位 100% 匹配；保留原版 displayState &= 0x2000 的写入。
 *  @address 0x08037868
 */
u8 BattleAction_UpdateBlizzardSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the action selected by skill[36].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x08038920
 */
u8 BattleAction_UpdateTargetEnemyRetreatSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the action selected by skill[38].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x08038C84
 */
u8 BattleAction_UpdatePowerSlashSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the action selected by skill[39].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x08038E44
 */
u8 BattleAction_UpdatePowerSweepSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the action selected by skill[40].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x08039024
 */
u8 BattleAction_UpdateSonicRiserSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the action selected by skill[42].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x080393E0
 */
u8 BattleAction_UpdateFlashArrowSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进 Rush Wind 的敌人驱离、背景调色板循环和混合渐变，结束恢复待机并消耗 MP。
 *  @note 普通 C 的 0x512 字节及已解析重定位 100% 匹配，末尾两字节零填充不计；控制标志 0x220 阻止驱离，完成返回 2，第二参数未使用。
 *  @address 0x08039724
 */
u8 BattleAction_UpdateRushWindSkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进攻击槽的阵型跳跃及斩击演出，排队目标伤害后清空攻击槽。
 *  @note skill[48]；跳跃模式 3，八轮特效位置变化后恢复背景并清空攻击槽，返回 1；普通 C 的 0x840 字节及 173 项已解析重定位 100% 匹配。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08039C38
 */
u8 BattleAction_UpdateGaugeFormationSlashes(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进攻击槽技能演出并为选中阵营回复最多 999 点 HP 和 MP。
 *  @note 技能处理器 49 使用阵营名单模式 1、HP/MP 效果 2、回复量 999；完成后清空施法者攻击槽并返回 2，不扣技能 MP，第二参数未使用。
 *  @address 0x0803A478
 */
u8 BattleAction_UpdateGaugeHpMpRecovery(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进攻击槽的头像、蓝白光束和群体攻击演出，结束清空攻击槽。
 *  @note skill[50]；目标反应全部结束后清空攻击槽并返回 1，第二参数未使用；普通 C 的 0x690 字节及 140 项已解析重定位 100% 匹配，固定基线独立构建的完整 ROM 与 baserom 一致。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0803A8D0
 */
u8 BattleAction_UpdateGaugeLightBurst(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进攻击槽技能演出并为队员和施法者启用伤害保护。
 *  @note 技能处理器 51 对状态掩码 0xFF 的队员及施法者置 displayState 的 0x10 并清除 +0xBA 计数；清空攻击槽并返回 2，第二参数未使用。
 *  @address 0x0803AF60
 */
u8 BattleAction_UpdateGaugePartyProtection(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进攻击槽的光刃演出，对被选中的普通敌人设置 9999 待伤并返回 3。
 *  @note skill[52]；id <= 112 的目标按 80% 判定写入 9999 待伤，完成返回 3，第二参数未使用；普通 C 的 0x768 字节及 134 项已解析重定位 100% 匹配。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0803B484
 */
u8 BattleAction_UpdateGaugeLethalStrike(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进攻击槽的头像、阵型跳跃和旋转斩击演出，结束清空攻击槽。
 *  @note skill[53], skill[54]；既有受控回放返回 1。普通 C 的 0x73A 字节及 149 项已解析重定位 100% 匹配。
 *  末尾两字节零填充不计；完整 ROM 与 baserom 一致。
 *  @note 阶段 34 手动循环特效时间轴尾段的四帧，保留 0xFFFB 正偏移及加法末项的帧数查询。
 *  主动画结束时统一置位 0x100，再更新目标反应。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0803BBEC
 */
u8 BattleAction_UpdateGaugeFormationSpinSlash(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进攻击槽的黑色背景和蓝绿色光阵群体攻击演出，结束清空攻击槽。
 *  @note skill[55]；恢复战斗动画、双方坐标与施法者待机后清空攻击槽并返回 1；普通 C 的 0xAE2 字节及 186 项已解析重定位 100% 匹配，末尾两字节零填充不计，固定基线独立构建的完整 ROM 与 baserom 一致。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0803C328
 */
u8 BattleAction_UpdateGaugeLightCircle(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进攻击槽技能演出并提升施法者 MEN 与一次魔法攻击威力。
 *  @note 技能处理器 56 使用 MEN 加成 30%、动画 0x39C；恢复待机后置 displayState 的 0x4000，清空攻击槽并返回 2，第二参数未使用。
 *  @address 0x0803CE0C
 */
u8 BattleAction_UpdateGaugeMagicBoost(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Apply the alternate attack-gauge magic-boost presentation to the actor.
 *  @note Skill handler 57 uses modifier selector 3 at thirty percent and animation 0x3B6; it sets display-state bit 0x4000, resets the gauge and returns two.
 *  @address 0x0803D20C
 */
u8 BattleAction_UpdateAlternateGaugeMagicBoost(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进攻击槽的持光球、腾跃和光面攻击演出，结束清空攻击槽。
 *  @note skill[58]；两次目标反应全部结束后清空攻击槽并返回 1；普通 C 的 0x8C0 字节及已解析重定位 100% 匹配，固定基线独立构建的完整 ROM 与 baserom 一致。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0803D60C
 */
u8 BattleAction_UpdateGaugeLightOrbStrike(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进攻击槽的反复跳跃斩击及返程演出，结束清空攻击槽。
 *  @note skill[59]；跳跃模式 4，返程及目标反应全部结束后清空攻击槽并返回 1；普通 C 的 0x6C0 字节及 156 项已解析重定位 100% 匹配。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0803DECC
 */
u8 BattleAction_UpdateGaugeRepeatedJumpSlashes(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Jump to the target formation's center, then descend and land.
 *  @note All 0x4E8 bytes and resolved relocations match from ordinary C.
 *  Keep signed division by two for the sorted coordinate extrema; X also includes the spacing offset.
 *  Approach lasts twenty frames and descent ten; the third argument is unused and subphase 23 returns one.
 *  @address 0x0803ED34
 */
u8 BattleAction_UpdateFormationCenterJump(struct BattleCombatant *actor,
    struct BattleCombatant *target, u8 unusedTargetMode);

/** @brief Advance the action selected by enemy[1].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x080416F0
 */
u8 BattleAction_UpdateEnemyConfiguredTargetEffectAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进敌人闪烁突进、调色板循环和淡出返位。
 *  @note 普通 C 的 0x4FC 字节及重定位 100% 匹配；保留局部插值、反应状态初始化及原有完成码。
 *  @address 0x080419E0
 */
u8 BattleAction_UpdateEnemyLungeFadeAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the action selected by enemy[3].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x08041EDC
 */
u8 BattleAction_UpdateEnemyAnimationFadeAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Play the enemy-specific effect using the timing and placement record.
 *  @note Enemy handler 5 searches the six BattleActionEffectConfig records; the coordinate halfwords are consumed as low bytes.
 *  @address 0x080422B8
 */
u8 BattleAction_UpdateConfiguredEnemyEffect(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the action selected by enemy[6].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x08042784
 */
u8 BattleAction_UpdateEnemyInterpolatedEffectAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Advance the action selected by enemy[8].
 *  @note The table role and replay are recorded in the source; a more specific semantic name remains unconfirmed. The result is a byte completion code, not a boolean.
 *  @address 0x08042B90
 */
u8 BattleAction_UpdateEnemyLiftAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进近战闪烁突进、追加攻击判定和装备特殊伤害。
 *  @note 普通 C 的 0x6E2 字节及重定位 100% 匹配；字节 ID 临时量和首次目标分支顺序保留寄存器分配及帧指针重取，完整 ROM 一致；末尾两字节零填充不计。
 *  @address 0x08042E70
 */
u8 BattleAction_UpdateEnemyMeleeLungeAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 按目标横坐标排序并在范围中心推进敌人效果与受击。
 *  @note 普通 C 的 0x3E4 字节及重定位 100% 匹配；伤害清零助手完全内联，目标槽排序及反应状态写入顺序不变。
 *  @address 0x08043554
 */
u8 BattleAction_UpdateEnemySequentialTargetEffectAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 按目标尺寸定位并移动敌人效果，结束后恢复待机。
 *  @note 普通 C 的 0x434 字节及重定位 100% 匹配；保留局部插值、反应状态初始化及原有完成码。
 *  @address 0x08043B5C
 */
u8 BattleAction_UpdateEnemyMovingTargetEffectAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief 推进普通敌人的定点效果、目标受击及原位恢复。
 *  @note 普通 C 的 0x404 字节及重定位 100% 匹配；保留目标伤害清零、背景渐变和反应状态初始化顺序。
 *  @address 0x08043F90
 */
u8 BattleAction_UpdateEnemyFixedPointEffectAttack(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Complete the empty skill-table handler immediately with result two.
 *  @note Skill handler 37 ignores both arguments and does not update action state; the skill-name window is handled by its caller.
 *  @address 0x08044728
 */
u8 BattleAction_UpdateEmptySkill(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Return zero without advancing skill-table entry 44.
 *  @note Both arguments are ignored. The original four-byte entry always returns zero; bounded replay is expected and is not action completion.
 *  @address 0x0804472C
 */
u8 BattleAction_UpdateNoOpSkill44(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Return zero without advancing skill-table entry 45.
 *  @note Both arguments are ignored. The original four-byte entry always returns zero; bounded replay is expected and is not action completion.
 *  @address 0x08044730
 */
u8 BattleAction_UpdateNoOpSkill45(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Return zero without advancing skill-table entry 46.
 *  @note Both arguments are ignored. The original four-byte entry always returns zero; bounded replay is expected and is not action completion.
 *  @address 0x08044734
 */
u8 BattleAction_UpdateNoOpSkill46(struct BattleCombatant *actor, struct BattleCombatant *target);

/** @brief Return zero without advancing skill-table entry 47.
 *  @note Both arguments are ignored. The original four-byte entry always returns zero; bounded replay is expected and is not action completion.
 *  @address 0x08044738
 */
u8 BattleAction_UpdateNoOpSkill47(struct BattleCombatant *actor, struct BattleCombatant *target);

#endif
