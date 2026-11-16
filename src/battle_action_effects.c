#include "include_asm.h"
#include "gba.h"
#include "battle_action_effects.h"
#include "battle.h"
#include "battle_rules.h"
#include "battle_palette.h"
#include "battle_items.h"
#include "core.h"
#include "script.h"
#include "sound.h"

/* Private .lcomm objects in original address order; no initialized RAM data.
 * Array capacities follow the target lists and adjacent objects. Unused bytes
 * between them are natural assembler alignment, not invented state fields. */
static u8 sBattleActionEffectPhase; /* 03000820 */
static u16 sBattleActionSavedBaseTile; /* 03000822 */
static u8 sBattleActionSavedPaletteSlot; /* 03000824 */
static u8 sBattleActionFrame; /* 03000825 */
static u16 sBattleActionHitIndex; /* 03000826; animation selection uses the low byte. */
static u8 sBattleActionSavedX; /* 03000828 */
static u8 sBattleActionSavedY; /* 03000829 */
static u8 sBattleEffectCombatantSlots[BATTLE_COMBATANT_COUNT]; /* 03000830 */
static u8 sBattleEffectCombatantCount; /* 0300083C */
static u8 sBattleActionEffectTargetCount; /* 0300083D */
static u8 *sBattleActionTargetEntries; /* 03000840; borrowed from battle. */
static u8 sBattleHitEffectPhase; /* 03000844 */
static u8 sBattleDodgePhase; /* 03000845 */
static u8 sBattleDodgeSavedX[BATTLE_COMBATANT_COUNT]; /* 03000848; target-list order. */
static u8 sBattleDodgeFrame; /* 03000854 */
static u8 sBattleHitEffectTargetIndex; /* 03000855; single-target path stores an encoded entry. */
static u8 sBattleHitReactionPhase; /* 03000856 */
static u8 sBattleHitReactionFrame; /* 03000857 */
static u8 sBattleActionValidTargetCount; /* 03000858 */
static s16 sBattleHitReactionDuration; /* 0300085A */
static u8 sBattleCompletedHitEffectCount; /* 0300085C */
static const struct BattleActionEffectConfig *sBattleActionEffectConfig; /* 03000860 */
static u8 sBattleEscapeFailed; /* 03000864 */
static u8 sBattleEscapedCombatantCount; /* 03000865 */
static u8 sBattleAttackConfigIndex; /* 03000866 */
static u8 sBattleEffectBlendEva; /* 03000867 */
static u8 sBattleEffectBlendEvb; /* 03000868 */
static u8 sBattleHitReactionTargetIndex; /* 03000869 */
static u8 sBattleSkillNamePhase; /* 0300086A */
static u8 sBattleActionSubphase; /* 0300086B */
static u16 sBattleActionAnimationId; /* 0300086C */
static u8 sBattleActionScratch86E; /* 0300086E; position or target-list index. */
static u8 sBattleActionScratch86F; /* 0300086F; position scratch. */
static u8 sBattleEscapeSavedX[BATTLE_PARTY_COUNT]; /* 03000870; five observed party slots. */
static u8 sBattleInstantDefeatSlots[BATTLE_ENEMY_COUNT]; /* 03000878 */
static u8 sBattleInstantDefeatCount; /* 0300087F */
static u8 sBattleActionSavedTargetX; /* 03000880 */
static u8 sBattleActionSavedTargetY; /* 03000881 */
static u16 sBattleLastAttackDamage; /* 03000882 */
static u8 sBattleAttackSfxPlayed; /* 03000884 */
static u16 sBattleHitSfxId; /* 03000886 */
static u8 sBattleHitSfxLoop; /* 03000888 */
static u8 sBattleRemovalEffectPhase; /* 03000889 */
static u8 sBattleAttackSoundFrame; /* 0300088A */
static u8 sBattlePairEnemyCount; /* 0300088B */
static u8 sBattlePairActorIndex; /* 0300088C */
static u8 sBattlePairEnemySlots[BATTLE_ENEMY_COUNT]; /* 03000890 */
static u8 sBattlePairSavedX[2]; /* 03000898 */
static u8 sBattlePairSavedY[2]; /* 0300089A */
static u8 sBattlePairSavedPaletteSlot[2]; /* 0300089C; two observed pair members. */
static u16 sBattlePairSavedBaseTile[2]; /* 030008A0 */
static u8 sBattlePairMotionStage; /* 030008A4 */
static u8 sBattleActionStage8A5; /* 030008A5; coordinate-table stage. */

/* .comm: shared with combatant damage/status application. */
/** @brief Per-slot status numbers queued by the current action.
 *  @note Reset before each action; a surviving target consumes its nonzero entry.
 *  @address 0x03004F90
 */
u8 gBattleActionPendingStatuses[BATTLE_COMBATANT_COUNT];

/*
 * 080257D8..0804473C：普通攻击、技能和敌人行动的具体演出与效果。
 * 已恢复的语义、接口及回放证据见 docs/BATTLE-ACTION-EFFECTS.md。
 * 03000820..030008A5 的字面基址形成集中工作状态；171 个入口不宜按体积任意再切。
 * 派发既有直接调用，也有 ROM 函数指针表，不能用 BL 数量代表全部入口关系。
 * 职责、运行覆盖和合并结论见 docs/BATTLE-MODULES.md（2026-11-11）。
 * 通用状态、目标反应初始化、状态排队、逃跑分发与音效助手使用 C；具体演出入口保留 INCLUDE_ASM。
 */

/** @brief 推进 Boss 113 的单目标投射攻击演出并排队目标伤害。
 *  @note boss[0]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x080257D8
 */
u8 BattleAction_UpdateBoss113ProjectileAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionEffectPhase = 3;
        break;
    case 3:
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        Battle_SetBossAnimation(actor, 1, 0x1B4, 13, 0);
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = 4;
        break;
    case 4:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 5;
        break;
    case 5:
        if (sBattleActionFrame == actor->hitReactionStartFrame - 10)
            Sound_PlaySfx(0x31, 1, 0);
        if (sBattleActionFrame == actor->hitReactionStartFrame - 5)
            BattleAction_BeginTargetReactions(20);
        else if ((u8)(sBattleActionFrame - 100) <= 5)
        {
            sBattleActionSavedX = Battle_InterpolateS16(
                gBattlePartyHomeX[target->formationSlot], 5, 5,
                sBattleActionFrame - 100, 2);
            target->homeX = sBattleActionSavedX;
        }
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_RestoreIdleAnimation(actor, actor->homeX, actor->homeY,
                sBattleActionSavedBaseTile, sBattleActionSavedPaletteSlot);
            sBattleActionEffectPhase = 6;
            target->homeX = gBattlePartyHomeX[target->formationSlot];
        }
        sBattleActionFrame++;
        break;
    case 6:
        sBattleActionEffectPhase = 7;
        sBattleActionFrame = 0;
        break;
    case 7:
        if (sBattleActionFrame > 24)
            sBattleActionEffectPhase = 9;
        sBattleActionFrame++;
        break;
    case 9:
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 113 的火团群体攻击、目标显隐与伤害恢复。
 *  @note 普通 C 的 0x414 字节及重定位 100% 匹配；第二参数按五项战斗记录数组访问，四个目标循环均保留半字索引。
 *  @address 0x08025994
 */
u8 BattleAction_UpdateBoss113FireBurstAttack(struct BattleCombatant *actor,
    struct BattleCombatant *targets)
{
    extern const u8 D_08393B19[];
    extern const u8 D_08393B20[];
    u8 result = 0;
    u16 index;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionEffectPhase = 3;
        Battle_SetupAnimation(&actor->effectAnimation, 120, 120, 0x2EA, 10,
            (const u16 *)gBattleAnimationConfigs[actor->source.boss->skillAnimationIds[0] + 1].animationData,
            gBattleAnimationConfigs[actor->source.boss->skillAnimationIds[0] + 1].palettes,
            gBattleAnimationConfigs[actor->source.boss->skillAnimationIds[0] + 1].graphicsId,
            gBattleAnimationConfigs[actor->source.boss->skillAnimationIds[0] + 1].graphicsBlockCount, 4);
        actor->displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
        actor->effectAnimation.flags |= 0x100;
        actor->effectAnimation.priority = 2;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        break;
    case 1:
    case 2:
        break;
    case 3:
    {
        u16 loading = actor->effectAnimation.flags & BATTLE_ANIM_LOADING;

        if (!loading)
        {
            sBattleActionSavedBaseTile = actor->animation.baseTile;
            sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
            Battle_SetBossAnimation(actor, 5, 0x1B4, 7, 0);
            actor->animation.priority = 3;
            actor->animation.flags |= 0x100;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 4;
        }
        break;
    }
    case 4:
        if (actor->animation.graphicsBlock == actor->animation.graphicsBlockCount - 1)
        {
            actor->displayState |= 0x40;
            actor->animation.x += 24;
        }
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            actor->animation.flags &= ~0x100;
            actor->effectAnimation.flags &= ~0x100;
            sBattleActionEffectPhase = 5;
        }
        break;
    case 5:
        if (actor->effectAnimation.timelineFrame == 42)
            Sound_PlaySfx(0x5C, 1, 0);
        if (actor->animation.timelineFrame == 85)
        {
            for (index = 0; index < BATTLE_PARTY_COUNT; index++)
            {
                if ((u8)Battle_CheckStatusMask(&targets[index], 1))
                {
                    targets[index].displayState |= BATTLE_DISPLAY_HIDE_STATUS_ICON;
                    targets[index].animation.flags |= BATTLE_ANIM_HIDDEN;
                }
            }
            Sound_PlaySfx(0x64, 1, 0);
        }
        else if (actor->animation.timelineFrame == 214)
        {
            for (index = 0; index < BATTLE_PARTY_COUNT; index++)
            {
                if ((u8)Battle_CheckStatusMask(&targets[index], 1))
                {
                    targets[index].displayState &= ~BATTLE_DISPLAY_HIDE_STATUS_ICON;
                    targets[index].animation.flags &= ~BATTLE_ANIM_HIDDEN;
                }
            }
            actor->animation.priority = 0;
            actor->effectAnimation.priority = 0;
            Sound_PlaySfx(0x50, 1, 0);
            BattleAction_BeginTargetReactions(40);
        }
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 6;
            Battle_RestoreIdleAnimation(actor, actor->homeX, actor->homeY,
                sBattleActionSavedBaseTile, sBattleActionSavedPaletteSlot);
            actor->displayState &= ~0x40;
        }
        break;
    case 6:
        Battle_AddAttackGauge(actor);
        sBattleActionEffectPhase = 7;
        for (index = 0; index < BATTLE_PARTY_COUNT; index++)
        {
            if ((u8)Battle_CheckStatusMask(&targets[index], 0x110) == 1)
                targets[index].pendingDamage = Battle_CalculateActionDamage(actor, &targets[index]);
        }
        break;
    case 7:
        for (index = 0; index < BATTLE_PARTY_COUNT; index++)
        {
            if ((u8)Battle_CheckStatusMask(&targets[index], 0x110) == 1)
            {
                targets[index].animation.flags &= ~BATTLE_ANIM_FINISHED;
                if (targets[index].id < BATTLE_CHARACTER_ID_COUNT)
                    Battle_RestoreIdleAnimation(&targets[index],
                        gBattlePartyHomeX[targets[index].formationSlot],
                        gBattlePartyHomeY[targets[index].formationSlot],
                        targets[index].animation.baseTile, targets[index].animation.paletteSlot);
                else
                    Battle_RestoreIdleAnimation(&targets[index],
                        D_08393B19[targets[index].formationSlot],
                        D_08393B20[targets[index].formationSlot],
                        targets[index].animation.baseTile, targets[index].animation.paletteSlot);
            }
        }
        sBattleActionEffectPhase = 9;
        break;
    case 8:
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            result = 1;
            actor->animation.priority = 3;
        }
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 114 的火焰吐息演出并排队所选目标伤害。
 *  @note boss[2]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08025DA8
 */
u8 BattleAction_UpdateBoss114FireBreathAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionEffectPhase = 3;
        break;
    case 3:
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        Battle_SetBossAnimation(actor, 1, 0x1B4, 11, 0);
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 4:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (actor->animation.timelineFrame > 64)
            sBattleActionEffectPhase = 20;
        break;
    case 20:
        Battle_SetupAnimation(&actor->effectAnimation, 94, 56, 0x2EA, 13,
            (const u16 *)gBattleAnimationConfigs[0x2BE].animationData,
            gBattleAnimationConfigs[0x2BE].palettes,
            gBattleAnimationConfigs[0x2BE].graphicsId,
            gBattleAnimationConfigs[0x2BE].graphicsBlockCount, 4);
        actor->displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
        actor->effectAnimation.priority = 2;
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 21;
        break;
    case 21:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x20, 1, 0);
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (actor->animation.timelineFrame > 109)
        {
            Sound_PlaySfx(0x64, 2, 0);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame <= 9)
        {
            actor->effectAnimation.x = Battle_InterpolateS16(118,
                target->homeX - 153, 10, sBattleActionFrame, 1);
            actor->effectAnimation.y = Battle_InterpolateS16(90,
                target->homeY - 90, 10, sBattleActionFrame, 1);
            sBattleActionFrame++;
        }
        else
        {
            actor->hitEffectId = 0x2BF;
            BattleAction_BeginTargetReactions(40);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 5;
        }
        break;
    case 5:
        if (!(target->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            target->effectAnimation.x = target->homeX - 35;
            target->effectAnimation.y = target->homeY;
            sBattleActionEffectPhase = 6;
        }
        break;
    case 6:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    if (actor->animation.flags & BATTLE_ANIM_FINISHED)
    {
        actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
        Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
            sBattleActionSavedPaletteSlot, 0);
    }
    return result;
}

/** @brief 推进 Boss 114 的毒息动画、目标反应与中毒排队。
 *  @note 普通 C 的 0x404 字节及重定位 100% 匹配；有效目标各以 40% 概率排队中毒，保留两段吐息和循环帧更新。
 *  @address 0x080260BC
 */
u8 BattleAction_UpdateBoss114PoisonBreathAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = 3;
        break;
    case 3:
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        Battle_SetBossAnimation(actor, 5, 0x1B4, 11, 0);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 4:
    case 5:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (actor->animation.timelineFrame > 70)
            sBattleActionEffectPhase = 20;
        break;
    case 20:
        Battle_SetupAnimation(&actor->effectAnimation, 128, 119, 0x2EA, 13,
            (const u16 *)gBattleAnimationConfigs[0x2C1].animationData,
            gBattleAnimationConfigs[0x2C1].palettes,
            gBattleAnimationConfigs[0x2C1].graphicsId,
            gBattleAnimationConfigs[0x2C1].graphicsBlockCount, 4);
        actor->displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
        actor->effectAnimation.priority = 2;
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 21;
        break;
    case 21:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x55, 1, 0);
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (actor->effectAnimation.timelineFrame == 9)
        {
            actor->effectAnimation.flags |= 0x100;
            actor->animation.flags |= 0x100;
            sBattleActionFrame = 0;
            actor->effectAnimation.timelineFrame++;
        }
        else if (actor->effectAnimation.timelineFrame > 9)
        {
            actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 8 + 10;
            sBattleActionFrame++;
            if (sBattleActionFrame == 16)
                actor->animation.flags &= ~0x100;
            else if (sBattleActionFrame > 23)
            {
                Sound_StopSfx(1);
                actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
                Battle_SetupAnimation(&actor->effectAnimation, 116, 128, 0x2EA, 14,
                    (const u16 *)gBattleAnimationConfigs[0x2C2].animationData,
                    gBattleAnimationConfigs[0x2C2].palettes,
                    gBattleAnimationConfigs[0x2C2].graphicsId,
                    gBattleAnimationConfigs[0x2C2].graphicsBlockCount, 4);
                actor->effectAnimation.flags |= 0x2000;
                actor->displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
                actor->effectAnimation.priority = 2;
                sBattleActionEffectPhase = 23;
            }
        }
        break;
    case 23:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            actor->hitEffectId = 1;
            BattleAction_BeginTargetReactions(40);
            sBattleActionEffectPhase = 6;
        }
        break;
    case 6:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            actor->animation.flags |= 0x100;
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            actor->animation.flags &= ~0x100;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            u8 index;

            Battle_GetCombatants();
            for (index = 0; index < sBattleActionEffectTargetCount; index++)
            {
                if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK)
                    == BATTLE_TARGET_VALID && Battle_NextRandom() % 100 <= 39)
                    gBattleActionPendingStatuses[sBattleActionTargetEntries[index]
                        & BATTLE_TARGET_SLOT_MASK] = BATTLE_STATUS_POISONED;
            }
            result = 1;
        }
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    if (actor->animation.flags & BATTLE_ANIM_FINISHED)
    {
        actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
        Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
            sBattleActionSavedPaletteSlot, 0);
    }
    return result;
}

/** @brief 推进闪烁近身攻击、追加攻击判定及返位。
 *  @note 普通 C 的 0x4CC 字节及重定位 100% 匹配；复用坐标计算临时量以保持共用写入块的寄存器分配。
 *  @address 0x080264C0
 */
u8 BattleAction_UpdateBlinkStrike(struct BattleCombatant *actor, struct BattleCombatant *target)
{
    u8 done = 0;
    /* Preserve the ROM's address calculation even for ids outside this 11-row
     * character table; moving the party-id guard earlier changes its accesses. */
    const struct BattleCharacterActionInfo *info =
        (const struct BattleCharacterActionInfo *)(actor->id
            * sizeof(struct BattleCharacterActionInfo) + (u32)gBattleCharacterActionInfo);

    switch (sBattleActionEffectPhase)
    {
    case BATTLE_BLINK_STRIKE_INIT:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionFrame = 0;
        actor->animation.flags &= ~BATTLE_ANIM_LOOP;
        BattleAction_ClearTargetPendingDamage(actor);
        sBattleActionEffectPhase = BATTLE_BLINK_STRIKE_FADE_OUT;
        break;
    case BATTLE_BLINK_STRIKE_FADE_OUT:
        if (actor->animation.flags & BATTLE_ANIM_HIDDEN)
            actor->animation.flags &= ~BATTLE_ANIM_HIDDEN;
        else
            actor->animation.flags |= BATTLE_ANIM_HIDDEN;
        if (sBattleActionFrame <= 4)
        {
            sBattleActionFrame++;
            if (actor->id < BATTLE_CHARACTER_ID_COUNT)
            {
                actor->homeX = Battle_InterpolateS16(sBattleActionSavedX, -10,
                    5, sBattleActionFrame, 2);
                actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                    (target->homeY - sBattleActionSavedY) / 10, 5, sBattleActionFrame, 2);
            }
            else
            {
                actor->homeX = Battle_InterpolateS16(sBattleActionSavedX, 10,
                    5, sBattleActionFrame, 2);
                actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                    (target->homeY - sBattleActionSavedY) / 10, 5, sBattleActionFrame, 2);
            }
        }
        else
        {
            sBattleActionFrame = 0;
            if (actor->id < BATTLE_CHARACTER_ID_COUNT)
            {
                actor->homeX = target->homeX
                    + ((actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1)) + 10;
                actor->homeY = target->homeY + 1;
            }
            else
            {
                u32 work;
                u8 coordinate;
                const u8 *targetX = &target->homeX;

                work = (actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1);
                coordinate = *targetX - work - 10;
                work = (u32)actor + offsetof(struct BattleCombatant, homeX);
                *(u8 *)work = coordinate;
                coordinate = target->homeY + 1;
                work++;
                *(u8 *)work = coordinate;
            }
            sBattleActionEffectPhase = BATTLE_BLINK_STRIKE_APPROACH;
        }
        break;
    case BATTLE_BLINK_STRIKE_APPROACH:
        if (actor->animation.flags & BATTLE_ANIM_HIDDEN)
            actor->animation.flags &= ~BATTLE_ANIM_HIDDEN;
        else
            actor->animation.flags |= BATTLE_ANIM_HIDDEN;
        if (sBattleActionFrame <= 4)
        {
            sBattleActionFrame++;
            if (actor->id < BATTLE_CHARACTER_ID_COUNT)
                actor->homeX = Battle_InterpolateS16(target->homeX
                    + ((actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1)) + 10,
                    -10, 5, sBattleActionFrame, 2);
            else
                actor->homeX = Battle_InterpolateS16(target->homeX
                    - ((actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1)) - 10,
                    10, 5, sBattleActionFrame, 2);
        }
        else
        {
            if (actor->animation.flags & BATTLE_ANIM_HIDDEN)
                actor->animation.flags &= ~BATTLE_ANIM_HIDDEN;
            sBattleActionEffectPhase = BATTLE_BLINK_STRIKE_START_ATTACK;
        }
        break;
    case BATTLE_BLINK_STRIKE_START_ATTACK:
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        if (actor->id < BATTLE_CHARACTER_ID_COUNT)
        {
            Battle_SetPartyAnimation(actor, BATTLE_COMBATANT_ANIM_ATTACK,
                0x1B4, 13, (u8)sBattleActionHitIndex);
            sBattleActionHitIndex++;
        }
        /* The original calls the boss selector even after the party branch. */
        Battle_SetBossAnimation(actor, BATTLE_COMBATANT_ANIM_ATTACK, 0x1B4, 13, 0);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = BATTLE_BLINK_STRIKE_WAIT_GRAPHICS;
        break;
    case BATTLE_BLINK_STRIKE_WAIT_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x50, 1, 0);
            sBattleActionEffectPhase = BATTLE_BLINK_STRIKE_WAIT_ANIMATION;
        }
        break;
    case BATTLE_BLINK_STRIKE_WAIT_ANIMATION:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_RestoreIdleAnimation(actor, actor->homeX, actor->homeY,
                sBattleActionSavedBaseTile, sBattleActionSavedPaletteSlot);
            sBattleActionEffectPhase = BATTLE_BLINK_STRIKE_CHECK_FOLLOWUP;
        }
        sBattleActionFrame++;
        if (sBattleActionFrame == actor->hitReactionStartFrame - 5)
            BattleAction_BeginTargetReactions(20);
        break;
    case BATTLE_BLINK_STRIKE_CHECK_FOLLOWUP:
        if (sBattleActionValidTargetCount)
        {
            if (sBattleActionHitIndex < info->maxHitCounts[1]
                && (u8)(Battle_NextRandom() % 101) <= info->followupChances[sBattleActionHitIndex]
                && actor->id < BATTLE_CHARACTER_ID_COUNT)
            {
                Battle_SeedRandom(Battle_NextRandom());
                Battle_AddAttackGauge(actor);
                sBattleActionEffectPhase = BATTLE_BLINK_STRIKE_FOLLOWUP_DELAY;
            }
            else
            {
                Battle_AddAttackGauge(actor);
                sBattleActionEffectPhase = BATTLE_BLINK_STRIKE_RETURN;
            }
        }
        else
            sBattleActionEffectPhase = BATTLE_BLINK_STRIKE_RETURN;
        sBattleActionFrame = 0;
        break;
    case BATTLE_BLINK_STRIKE_FOLLOWUP_DELAY:
        if (sBattleActionFrame > 14)
            sBattleActionEffectPhase = BATTLE_BLINK_STRIKE_START_ATTACK;
        sBattleActionFrame++;
        break;
    case BATTLE_BLINK_STRIKE_RETURN:
        if (sBattleActionFrame > 24)
            sBattleActionEffectPhase = BATTLE_BLINK_STRIKE_WAIT_REACTIONS;
        else if (sBattleActionFrame == 8)
            actor->animation.flags |= BATTLE_ANIM_SEMITRANSPARENT;
        else if (sBattleActionFrame == 10)
        {
            actor->homeX = sBattleActionSavedX;
            actor->homeY = sBattleActionSavedY;
        }
        else if (sBattleActionFrame == 13)
            actor->animation.flags &= ~BATTLE_ANIM_SEMITRANSPARENT;
        sBattleActionFrame++;
        break;
    case BATTLE_BLINK_STRIKE_WAIT_REACTIONS:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            done = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return done;
}

/** @brief 推进未被当前 Boss 配置引用的中毒攻击变体。
 *  @note boss[5]；本轮受控回放返回 1。 当前 Boss 配置未引用该槽，证据来自直接调用夹具。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802698C
 */
u8 BattleAction_UpdateBossPoisonSkillVariant(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = 3;
        break;
    case 3:
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        Battle_SetBossAnimation(actor, 5, 0x1B4, 13, 0);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 4;
        break;
    case 4:
        if (actor->animation.timelineFrame > 70)
            sBattleActionEffectPhase = 2;
        break;
    case 2:
        Battle_SetupAnimation(&actor->effectAnimation, 128, 119, 0x2EA, 14,
            (const u16 *)gBattleAnimationConfigs[0x2C1].animationData,
            gBattleAnimationConfigs[0x2C1].palettes,
            gBattleAnimationConfigs[0x2C1].graphicsId,
            gBattleAnimationConfigs[0x2C1].graphicsBlockCount, 4);
        actor->displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
        actor->effectAnimation.priority = 2;
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 1;
        break;
    case 1:
        if (actor->effectAnimation.timelineFrame == 9)
        {
            actor->effectAnimation.flags |= 0x100;
            actor->animation.flags |= 0x100;
            sBattleActionFrame = 0;
            actor->effectAnimation.timelineFrame++;
        }
        else if (actor->effectAnimation.timelineFrame > 9)
        {
            actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 8 + 10;
            sBattleActionFrame++;
            if (sBattleActionFrame == 16)
                actor->animation.flags &= ~0x100;
            else if (sBattleActionFrame > 23)
            {
                actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
                Battle_SetupAnimation(&actor->effectAnimation, 116, 128, 0x2EA, 14,
                    (const u16 *)gBattleAnimationConfigs[0x2C2].animationData,
                    gBattleAnimationConfigs[0x2C2].palettes,
                    gBattleAnimationConfigs[0x2C2].graphicsId,
                    gBattleAnimationConfigs[0x2C2].graphicsBlockCount, 4);
                actor->effectAnimation.flags |= 0x2000;
                actor->displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
                actor->effectAnimation.priority = 2;
                sBattleActionEffectPhase = 5;
            }
        }
        break;
    case 5:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            actor->hitEffectId = 1;
            BattleAction_BeginTargetReactions(40);
            sBattleActionEffectPhase = 6;
        }
        break;
    case 6:
        if (sBattleActionValidTargetCount)
            sBattleActionEffectPhase = 9;
        else
            sBattleActionEffectPhase = 7;
        break;
    case 7:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            u8 index;

            Battle_GetCombatants();
            for (index = 0; index < sBattleActionEffectTargetCount; index++)
            {
                if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK)
                    == BATTLE_TARGET_VALID && Battle_NextRandom() % 100 <= 39)
                    gBattleActionPendingStatuses[sBattleActionTargetEntries[index]
                        & BATTLE_TARGET_SLOT_MASK] = BATTLE_STATUS_POISONED;
            }
            result = 1;
        }
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    if (actor->animation.flags & BATTLE_ANIM_FINISHED)
    {
        actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
        Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
            sBattleActionSavedPaletteSlot, 0);
    }
    return result;
}

/** @brief Advance the action selected by boss[6].
 *  @note Seeded GPBA replay stayed at phase 3 with result zero through 450 dispatch updates.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x08026D08
 *  静态收尾：设置并更新白紫旋风及 Boss 动画；复位后返回 2，不执行通常目标伤害结算。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdateBoss115WhirlwindAnimationVariant(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        Battle_SetupAnimation(&actor->effectAnimation, 10, 19, 0, 14,
            (const u16 *)gBattleAnimationConfigs[0x2C8].animationData,
            gBattleAnimationConfigs[0x2C8].palettes,
            gBattleAnimationConfigs[0x2C8].graphicsId,
            gBattleAnimationConfigs[0x2C8].graphicsBlockCount, 6);
        actor->displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
        actor->effectAnimation.priority = 0;
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 3;
        break;
    case 3:
        break;
    case 4:
        if (actor->effectAnimation.timelineFrame > 35)
        {
            Battle_SetBossAnimation(actor, 5, 0x1B4, 13, 1);
            actor->animation.flags |= BATTLE_ANIM_HIDDEN | BATTLE_ANIM_SEMITRANSPARENT;
            *(vu16 *)REG_BLDCNT = 0x400;
            *(vu16 *)REG_BLDALPHA = 0xC0F;
            actor->animation.priority = 3;
            actor->effectAnimation.flags |= 0x100;
            sBattleActionEffectPhase = 2;
        }
        break;
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            actor->effectAnimation.flags &= ~0x100;
            actor->animation.flags &= ~BATTLE_ANIM_HIDDEN;
            sBattleActionEffectPhase = 5;
            sBattleActionFrame = 0;
        }
        break;
    case 5:
        if (sBattleActionFrame <= 4)
        {
            *(vu16 *)REG_BLDALPHA = Battle_InterpolateS16(3, 12, 5,
                sBattleActionFrame, 2) + 0xC00;
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionEffectPhase = 1;
            sBattleActionFrame = 0;
        }
        break;
    case 1:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            if (actor->animation.flags & 0x100)
                actor->animation.timelineFrame = (actor->animation.timelineFrame + 1) % 5 + 135;
            else
                actor->animation.flags |= 0x100;
            if (sBattleActionFrame <= 14)
            {
                *(vu16 *)REG_BLDALPHA = Battle_InterpolateS16(15, -12, 15,
                    sBattleActionFrame, 1) + 0xC00;
                sBattleActionFrame++;
            }
        }
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 6:
    case 7:
    case 8:
        break;
    case 9:
        actor->animation.priority = 3;
        result = 2;
        break;
    }
    return result;
}

/** @brief 推进 Boss 116 的风形移动、淡出返位与单目标受击演出。
 *  @note boss[7]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08026F88
 */
u8 BattleAction_UpdateBoss116WindAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 1, 0x1B4, 13, 0);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x57, 1, 0);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (actor->animation.timelineFrame > 110)
        {
            Battle_SetEffectAnimation(actor, 7, 15, 0, 14, 0x2CD, 7);
            sBattleEffectBlendEva = 0;
            Battle_SetBlend(0x442, 0, 12);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (actor->animation.timelineFrame <= 130)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 7, 20,
                actor->animation.timelineFrame - 111, 2);
            Battle_SetBlend(0x442, sBattleEffectBlendEva, 12);
        }
        else
            sBattleActionEffectPhase = 21;
        break;
    case 21:
        if (actor->animation.timelineFrame <= 149)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(7, -7, 19,
                actor->animation.timelineFrame - 131, 2);
            Battle_SetBlend(0x442, sBattleEffectBlendEva, 12);
        }
        else
        {
            actor->animation.flags |= 0x100;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            *(vu16 *)REG_DISPCNT &= ~0x200;
            Battle_SetBlend(0, sBattleEffectBlendEva, 12);
            BattleAction_BeginTargetReactions(45);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        target->effectAnimation.flags |= 0x2000;
        sBattleActionEffectPhase = 23;
        break;
    case 23:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            actor->animation.flags &= ~0x100;
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        result = 1;
        break;
    }
    sBattleActionFrame++;
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 116 的风纹群体攻击、背景循环及混合渐变。
 *  @note 普通 C 的 0x3E0 字节及重定位 100% 匹配；按动画帧推进两段混合，再等待受击和待机恢复。
 *  @address 0x0802723C
 */
u8 BattleAction_UpdateBoss116GustAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 1, 0x1B4, 13, 0);
        actor->hitEffectId = 1;
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x57, 1, 0);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (actor->animation.timelineFrame > 110)
        {
            Battle_SetEffectAnimation(actor, 7, 15, 0, 14, 0x2CD, 7);
            sBattleEffectBlendEva = 0;
            Battle_SetBlend(0x442, 0, 12);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (actor->animation.timelineFrame <= 130)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 7, 20,
                actor->animation.timelineFrame - 111, 2);
            Battle_SetBlend(0x442, sBattleEffectBlendEva, 12);
        }
        else
            sBattleActionEffectPhase = 21;
        break;
    case 21:
        if (actor->animation.timelineFrame <= 149)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(7, -7, 19,
                actor->animation.timelineFrame - 131, 2);
            Battle_SetBlend(0x442, sBattleEffectBlendEva, 12);
        }
        else
        {
            actor->animation.flags |= 0x100;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        actor->animation.flags |= 0x100;
        actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
        Battle_ResetBackgroundLoader();
        sBattleActionEffectPhase = 23;
        break;
    case 23:
        if ((u8)Battle_UpdateBackgroundLoader(13, 3, 14, 2))
        {
            Sound_PlaySfx(0x55, 1, 0);
            Battle_SetBlend(0x1447, 9, 15);
            BattlePalette_StartBgCycle(14, 1, 1, -3, 12);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
    {
        u16 loading = actor->animation.flags & BATTLE_ANIM_LOADING;

        if (!loading)
        {
            BattleAction_BeginTargetReactions(60);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 25;
        }
        break;
    }
    case 25:
        if (sBattleActionFrame <= 59)
            sBattleActionFrame++;
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (sBattleActionFrame <= 29)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(9, -9, 30, sBattleActionFrame, 2);
            Battle_SetBlend(0x1447, sBattleEffectBlendEva, 15);
            sBattleActionFrame++;
        }
        else
        {
            Sound_StopSfx(1);
            BattlePalette_StopBgCycle(14, 1);
            Battle_SetBlend(0, 0, 0);
            *(vu16 *)REG_DISPCNT &= ~0x200;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        actor->animation.flags &= ~0x100;
        sBattleActionEffectPhase = 28;
        break;
    case 28:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            actor->animation.flags &= ~0x100;
            sBattleActionEffectPhase = 29;
        }
        break;
    case 29:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 30;
        }
        break;
    case 30:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Advance the action selected by boss[9].
 *  @note Seeded GPBA replay stayed at phase 1 with result zero through 450 dispatch updates.
 *  The fixture called this entry directly; it does not establish a normal dispatcher selection.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x0802761C
 *  静态收尾：准备 Boss 技能动画；可执行完成阶段累积伤害，并以战斗随机数百分比小于 40 排队中毒。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdateBoss116PoisonAttackVariant(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 13, 0);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 1;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            u8 index;

            Battle_GetCombatants();
            for (index = 0; index < sBattleActionEffectTargetCount; index++)
            {
                if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK)
                    == BATTLE_TARGET_VALID && Battle_NextRandom() % 100 <= 39)
                    gBattleActionPendingStatuses[sBattleActionTargetEntries[index]
                        & BATTLE_TARGET_SLOT_MASK] = BATTLE_STATUS_POISONED;
            }
            result = 1;
        }
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    if (actor->animation.flags & BATTLE_ANIM_FINISHED)
    {
        actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
        Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
            sBattleActionSavedPaletteSlot, 0);
    }
    return result;
}

/** @brief 推进 Boss 117 的长舌单目标打击演出。
 *  @note boss[10]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08027760
 */
u8 BattleAction_UpdateBoss117TongueAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        Sound_PlaySfx(0x28, 1, 0);
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (sBattleActionFrame <= 49)
        {
            actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                target->homeX - 150 - sBattleActionSavedX, 50, sBattleActionFrame, 2);
            actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                target->homeY + 15 - sBattleActionSavedY, 50, sBattleActionFrame, 2);
        }
        else
        {
            Sound_StopSfx(1);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        Battle_SetBossAnimation(actor, 1, 0x1B4, 11, 0);
        sBattleActionEffectPhase = 20;
        break;
    case 20:
        if (actor->animation.graphicsBlock >= actor->animation.graphicsBlockCount - 1)
        {
            actor->homeX += 52;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 22;
        break;
    case 22:
        if (actor->animation.timelineFrame > 77)
        {
            BattleAction_BeginTargetReactions(20);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->animation.flags |= 0x100;
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            actor->animation.flags |= 0x100;
            actor->homeX = sBattleActionSavedX;
            actor->homeY = sBattleActionSavedY;
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            actor->homeX = sBattleActionSavedX;
            actor->homeY = sBattleActionSavedY;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        result = 1;
        break;
    }
    sBattleActionFrame++;
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 117 的跃起踏击与群体受击演出。
 *  @note boss[11]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08027A20
 */
u8 BattleAction_UpdateBoss117JumpStompAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 11, 0);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (actor->animation.timelineFrame == 57)
        {
            Sound_PlaySfx(0x1D, 2, 0);
            actor->animation.flags |= 0x100;
            sBattleActionEffectPhase = 20;
            sBattleActionFrame = 0;
        }
        break;
    case 20:
        if (sBattleActionFrame == 13)
            actor->animation.flags |= BATTLE_ANIM_HIDDEN;
        if (sBattleActionFrame <= 19)
            actor->homeY = Battle_InterpolateS16(sBattleActionSavedY, -100, 20, sBattleActionFrame, 2);
        else
        {
            actor->homeX = 180;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (sBattleActionFrame == 5)
            actor->animation.flags &= ~BATTLE_ANIM_HIDDEN;
        if (sBattleActionFrame <= 19)
            actor->homeY = Battle_InterpolateS16(sBattleActionSavedY - 100, 100, 20, sBattleActionFrame, 1);
        else
        {
            sBattleActionFrame = 0;
            BattleAction_BeginTargetReactions(20);
            actor->animation.flags &= ~0x100;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (actor->animation.timelineFrame == 100)
        {
            sBattleActionFrame = 0;
            actor->animation.flags |= 0x100;
            Sound_PlaySfx(0x1D, 2, 0);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame == 13)
            actor->animation.flags |= BATTLE_ANIM_HIDDEN;
        if (sBattleActionFrame <= 19)
            actor->homeY = Battle_InterpolateS16(sBattleActionSavedY, -100, 20, sBattleActionFrame, 2);
        else
        {
            actor->homeX = sBattleActionSavedX + 10;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame == 5)
            actor->animation.flags &= ~BATTLE_ANIM_HIDDEN;
        if (sBattleActionFrame <= 19)
            actor->homeY = Battle_InterpolateS16(sBattleActionSavedY - 100, 100, 20, sBattleActionFrame, 1);
        else
        {
            sBattleActionFrame = 0;
            actor->animation.flags &= ~0x100;
            actor->homeX = sBattleActionSavedX;
            actor->homeY = sBattleActionSavedY;
            Sound_PlaySfx(0x43, 2, 0);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            actor->homeX = sBattleActionSavedX;
            actor->homeY = sBattleActionSavedY;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    sBattleActionFrame++;
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 118 的绿色光弹单目标攻击演出。
 *  @note boss[12]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08027D9C
 */
u8 BattleAction_UpdateBoss118GreenProjectileAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 1, 0x1B4, 12, 0);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (actor->animation.timelineFrame == 32)
        {
            Battle_SetEffectAnimation(actor, actor->homeX + 10, actor->homeY - 20,
                0x300, 14, 0x2DA, 5);
            actor->animation.flags |= 0x100;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x31, 1, 0);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (sBattleActionFrame <= 9)
        {
            actor->effectAnimation.x = Battle_InterpolateS16(actor->homeX + 10,
                target->homeX - (actor->homeX + 10), 10, sBattleActionFrame, 2);
            actor->effectAnimation.y = Battle_InterpolateS16(actor->homeY - 20,
                target->homeY + 12 - actor->homeY, 10, sBattleActionFrame, 2);
        }
        else
        {
            sBattleActionFrame = 0;
            BattleAction_BeginTargetReactions(20);
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame <= 9)
        {
            actor->effectAnimation.x = Battle_InterpolateS16(target->homeX,
                actor->homeX + 10 - target->homeX, 10, sBattleActionFrame, 1);
            actor->effectAnimation.y = Battle_InterpolateS16(target->homeY - 8,
                actor->homeY - 12 - target->homeY, 10, sBattleActionFrame, 1);
        }
        else
        {
            sBattleActionFrame = 0;
            actor->animation.flags &= ~0x100;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            actor->animation.flags &= ~0x100;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        result = 1;
        break;
    }
    sBattleActionFrame++;
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 118 的前冲与群体受击演出。
 *  @note boss[13]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08028098
 */
u8 BattleAction_UpdateBoss118ChargeAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 12, 0);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (actor->animation.graphicsBlock >= actor->animation.graphicsBlockCount - 1)
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 20;
        break;
    case 20:
        if (actor->animation.timelineFrame > 103)
        {
            Battle_SetEffectAnimation(actor, actor->homeX + 64, actor->homeY - 32,
                0x300, 14, 0x2DC, 5);
            actor->effectAnimation.priority = 0;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x19, 1, 0);
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (actor->effectAnimation.timelineFrame == 44)
            Sound_PlaySfx(0x5A, 1, 0);
        sBattleActionFrame++;
        if (sBattleActionFrame == 110)
            BattleAction_BeginTargetReactions(55);
        else if (sBattleActionFrame == 115)
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    if (actor->animation.flags & BATTLE_ANIM_FINISHED)
    {
        actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
        Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
            sBattleActionSavedPaletteSlot, 0);
    }
    if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
    {
        actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
        actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
    }
    return result;
}

/** @brief 推进 Boss 118 的横向光束与所选目标受击演出。
 *  @note boss[14]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x080282EC
 */
u8 BattleAction_UpdateBoss118BeamAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (sBattleActionFrame <= 9)
        {
            actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                target->homeY + 16 - sBattleActionSavedY, 10, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
            sBattleActionEffectPhase = 20;
        break;
    case 20:
        Battle_SetBossAnimation(actor, 5, 0x1B4, 12, 1);
        sBattleActionEffectPhase = 21;
        break;
    case 21:
        if (actor->animation.graphicsBlock >= actor->animation.graphicsBlockCount - 1)
        {
            actor->homeX += 71;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (actor->animation.timelineFrame == 25)
            Sound_PlaySfx(0x19, 1, 0);
        if (actor->animation.timelineFrame == 107)
            Sound_PlaySfx(0x5A, 1, 0);
        if (actor->animation.timelineFrame == 209)
        {
            BattleAction_BeginTargetReactions(30);
            sBattleActionEffectPhase = 23;
        }
        sBattleActionFrame++;
        break;
    case 23:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags |= 0x100;
            sBattleActionEffectPhase = 24;
        }
        sBattleActionFrame++;
        break;
    case 24:
        if (!(target->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            actor->animation.flags &= ~(0x100 | BATTLE_ANIM_FINISHED);
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            actor->homeX -= 71;
            sBattleActionEffectPhase = 25;
            sBattleActionFrame = 0;
        }
        break;
    case 25:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            if (sBattleActionFrame <= 9)
            {
                actor->homeY = Battle_InterpolateS16(target->homeY + 16,
                    sBattleActionSavedY - (target->homeY + 16), 10, sBattleActionFrame, 2);
                sBattleActionFrame++;
            }
            else
            {
                actor->homeY = sBattleActionSavedY;
                sBattleActionEffectPhase = 9;
            }
        }
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 119 的前跃扑击及返位演出。
 *  @note boss[15]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x080285A0
 */
u8 BattleAction_UpdateBoss119PounceAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 1, 0x1B4, 13, 0);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            BattlePalette_StartObjCycle(13, 1, 3, -11, 5);
            Sound_PlaySfx(0x31, 1, 0);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (actor->animation.timelineFrame <= 46)
        {
            actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                target->homeX - 48 - sBattleActionSavedX,
                47, actor->animation.timelineFrame, 2);
            actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                target->homeY - sBattleActionSavedY,
                47, actor->animation.timelineFrame, 2);
        }
        else
            sBattleActionEffectPhase = 20;
        break;
    case 20:
        if (actor->animation.timelineFrame > 79)
        {
            BattleAction_BeginTargetReactions(20);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_StopObjCycle(13, 1);
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            actor->homeX = sBattleActionSavedX;
            actor->homeY = sBattleActionSavedY;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 23;
        break;
    case 23:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        result = 1;
        break;
    }
    sBattleActionFrame++;
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 119、120 共用的火焰吐息和群体受击演出。
 *  @note boss[16]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x080287EC
 */
u8 BattleAction_UpdateSharedBossFireBreathAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 13, 0);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            BattlePalette_StartObjCycle(actor->animation.paletteSlot, 1, 3, -11, 5);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (actor->animation.timelineFrame == 50)
            Sound_PlaySfx(0x64, 1, 0);
        if (actor->animation.timelineFrame > 79)
        {
            BattlePalette_StopObjCycle(actor->animation.paletteSlot, 1);
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            BattlePalette_StartObjCycle(actor->animation.paletteSlot, 1, 3, -11, 5);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        Battle_SetEffectAnimation(actor, 180, 120, 0x1E6, 13, 0x34A, 2);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 22;
        break;
    case 22:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x64, 1, 0);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame > 0)
        {
            BattleAction_BeginTargetReactions(20);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            BattlePalette_StartObjCycle(actor->animation.paletteSlot, 1, 3, -11, 5);
            result = 1;
        }
        break;
    }
    sBattleActionFrame++;
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Advance both bosses' attack preparation, paired movement, hits and return.
 *  @note Boss handler 17 saves both members' positions and graphics; the second argument is unused.
 *  @note 普通 C 按原址独立链接后，0x790 字节（含重定位）完全匹配；字节地址按成员步长 8、阶段步长 16 分步移位相加。
 *  两个运动阶段的插值周期分别为 15、7 帧；阶段处理后统一递增八位帧计数，再更新目标反应。
 *  @note Seeded GPBA replay returned 1 after 293 dispatch updates (293 entry hits).
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x08028AD8
 */
u8 BattleAction_UpdatePairedBossAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;
    struct BattleCombatant *combatants = Battle_GetCombatants();

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattlePairEnemyCount = Battle_ListCombatants(combatants, sBattlePairEnemySlots,
            BATTLE_SIDE_ENEMY, 0x6F);
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 1;
        sBattlePairActorIndex = 0;
        sBattlePairMotionStage = 0;
        break;
    case 1:
        if (!Sound_IsSfxPlaying(1))
        {
            sBattlePairSavedX[sBattlePairActorIndex] =
                combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].homeX;
            sBattlePairSavedY[sBattlePairActorIndex] =
                combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].homeY;
            sBattlePairSavedBaseTile[sBattlePairActorIndex] =
                combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].animation.baseTile;
            sBattlePairSavedPaletteSlot[sBattlePairActorIndex] =
                combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].animation.paletteSlot;
            Battle_SetBossAnimation(&combatants[sBattlePairEnemySlots[sBattlePairActorIndex]],
                5, 0x27C, 13, 1);
            if (sBattlePairActorIndex == 0)
                sBattleActionEffectPhase = 3;
            else
                sBattleActionEffectPhase = 2;
        }
        break;
    case 3:
        if (combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].animation.timelineFrame > 23)
        {
            struct BattleCombatant *member = &combatants[sBattlePairEnemySlots[sBattlePairActorIndex]];

            Battle_SetupAnimation(&member->animation, member->animation.x, member->animation.y,
                0x1B4, 14, (const u16 *)gBattleAnimationConfigs[0x2E3].animationData,
                gBattleAnimationConfigs[0x2E3].palettes,
                gBattleAnimationConfigs[0x2E3].graphicsId,
                gBattleAnimationConfigs[0x2E3].graphicsBlockCount, 0x401);
            sBattleActionEffectPhase = 4;
        }
        break;
    case 4:
        if (!(combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].animation.flags
            & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x31, 1, 0);
            sBattleActionSavedPaletteSlot =
                combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].animation.paletteSlot;
            BattlePalette_StartObjCycle(
                combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].animation.paletteSlot,
                1, 3, -11, 5);
            sBattlePairActorIndex++;
            sBattleActionEffectPhase = 1;
        }
        break;
    case 2:
        if (combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].animation.timelineFrame > 23)
        {
            Sound_PlaySfx(0x31, 1, 0);
            Sound_PlaySfx(0x36, 2, 0);
            combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].animation =
                combatants[sBattlePairEnemySlots[0]].animation;
            sBattleActionEffectPhase = 5;
            sBattleActionFrame = 0;
            sBattlePairActorIndex = 0;
        }
        break;
    case 5:
    {
        const u8 *motionData;
        u32 motionAddress;
        const s16 *motion;

        if (sBattleActionFrame < D_0839DF6B[sBattlePairMotionStage])
        {
            motionData = (const u8 *)D_0839DF70;
            motionAddress = sBattlePairActorIndex;
            motionAddress <<= 3;
            motionAddress += (u32)motionData;
            motion = (const s16 *)motionAddress;
            combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].homeX =
                Battle_InterpolateS16(motion[0], motion[2],
                    D_0839DF6B[sBattlePairMotionStage], sBattleActionFrame, 1);
            motionAddress = sBattlePairActorIndex;
            motionAddress <<= 3;
            motionAddress += (u32)motionData;
            motion = (const s16 *)motionAddress;
            combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].homeY =
                Battle_InterpolateS16(motion[1], motion[3],
                    D_0839DF6B[sBattlePairMotionStage], sBattleActionFrame, 1);
        }
        else if (sBattlePairActorIndex == 0)
        {
            sBattlePairActorIndex++;
            sBattleActionFrame = 0;
            Sound_PlaySfx(0x36, 2, 0);
        }
        else
        {
            sBattlePairMotionStage++;
            sBattleActionFrame = 0;
            sBattlePairActorIndex = 0;
            sBattleActionEffectPhase = 6;
            Sound_PlaySfx(0x65, 2, 0);
        }
        break;
    }
    case 6:
    {
        const u8 *motionData;
        u32 motionAddress;
        const s16 *motion;

        if (sBattleActionFrame < D_0839DF6B[sBattlePairMotionStage])
        {
            motionData = (const u8 *)D_0839DF70;
            motionAddress = sBattlePairActorIndex;
            motionAddress <<= 3;
            motionAddress += sBattlePairMotionStage << 4;
            motionAddress += (u32)motionData;
            motion = (const s16 *)motionAddress;
            combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].homeX =
                Battle_InterpolateS16(motion[0], motion[2],
                    D_0839DF6B[sBattlePairMotionStage], sBattleActionFrame, 2);
            motionAddress = sBattlePairActorIndex;
            motionAddress <<= 3;
            motionAddress += sBattlePairMotionStage << 4;
            motionAddress += (u32)motionData;
            motion = (const s16 *)motionAddress;
            combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].homeY =
                Battle_InterpolateS16(motion[1], motion[3],
                    D_0839DF6B[sBattlePairMotionStage], sBattleActionFrame, 2);
        }
        else
        {
            BattleAction_BeginTargetReactions(50);
            BattlePalette_StartBgFade(0, 3, 16, 28, 31, 4, 4, -1, 2);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 10;
        }
        break;
    }
    case 7:
    case 8:
        break;
    case 10:
        if (sBattleActionFrame > 3)
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 11;
        }
        break;
    case 11:
        if (sBattleHitEffectPhase > 5)
        {
            BattlePalette_ReturnBgFade(0, 3, 16);
            sBattleActionEffectPhase = 12;
            sBattleActionFrame = 0;
        }
        break;
    case 12:
        if (sBattleActionFrame > 15)
        {
            sBattleActionFrame = 0;
            sBattleActionSavedX =
                combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].homeX;
            sBattleActionSavedY =
                combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].homeY;
            sBattleActionEffectPhase = 13;
        }
        break;
    case 13:
        if (sBattleActionFrame < D_0839DF6B[sBattlePairMotionStage])
        {
            combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].homeX =
                Battle_InterpolateS16(sBattleActionSavedX,
                    sBattlePairSavedX[sBattlePairActorIndex] - sBattleActionSavedX,
                    D_0839DF6B[sBattlePairMotionStage], sBattleActionFrame, 2);
            combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].homeY =
                Battle_InterpolateS16(sBattleActionSavedY,
                    sBattlePairSavedY[sBattlePairActorIndex] - sBattleActionSavedY,
                    D_0839DF6B[sBattlePairMotionStage], sBattleActionFrame, 2);
        }
        else
        {
            sBattleActionFrame = 0;
            Battle_SetBossAnimation(&combatants[sBattlePairEnemySlots[sBattlePairActorIndex]],
                5, 0x27C, 13, 1);
            sBattleActionEffectPhase = 14;
        }
        break;
    case 14:
        if (!(combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].animation.flags
            & BATTLE_ANIM_LOADING))
        {
            combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].animation.timelineFrame = 24;
            sBattleActionEffectPhase = 15;
        }
        break;
    case 15:
        if (combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].animation.flags
            & BATTLE_ANIM_FINISHED)
        {
            Battle_SetBossAnimation(&combatants[sBattlePairEnemySlots[sBattlePairActorIndex]],
                0, sBattlePairSavedBaseTile[sBattlePairActorIndex],
                sBattlePairSavedPaletteSlot[sBattlePairActorIndex], 0);
            sBattleActionEffectPhase = 16;
        }
        break;
    case 16:
        if (!(combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].animation.flags
            & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x43, 2, 0);
            combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].homeX =
                sBattlePairSavedX[sBattlePairActorIndex];
            combatants[sBattlePairEnemySlots[sBattlePairActorIndex]].homeY =
                sBattlePairSavedY[sBattlePairActorIndex];
            sBattleActionEffectPhase = 17;
        }
        break;
    case 17:
        if (sBattleHitEffectPhase == 0 && sBattleDodgePhase == 0
            && sBattleHitReactionPhase == 0)
        {
            if (sBattlePairActorIndex == 0)
            {
                sBattlePairActorIndex++;
                sBattleActionFrame = 0;
                sBattleActionEffectPhase = 6;
                Sound_PlaySfx(0x65, 2, 0);
            }
            else
            {
                sBattleActionEffectPhase = 9;
                BattlePalette_StopObjCycle(sBattleActionSavedPaletteSlot, 1);
            }
        }
        break;
    case 9:
        if (sBattleHitEffectPhase == 0 && sBattleDodgePhase == 0
            && sBattleHitReactionPhase == 0)
            result = 1;
        break;
    }
    sBattleActionFrame++;
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 120 的跳跃动作与所选目标受击演出。
 *  @note boss[18]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08029268
 */
u8 BattleAction_UpdateBoss120JumpStrikeAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionEffectPhase = 1;
        break;
    case 1:
        target->pendingDamage = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        Battle_SetBossAnimation(actor, 1, 0x1B4, 13, 0);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 2;
        break;
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x31, 1, 0);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->animation.flags |= 0x100;
            Battle_SetEffectAnimation(actor, actor->homeX, actor->homeY - 16,
                0x1F4, 14, 0x8D, 0x25);
            sBattleActionFrame = 0;
            Sound_PlaySfx(0x34, 1, 0);
            sBattleActionEffectPhase = 3;
        }
        break;
    case 3:
        if (sBattleActionFrame <= 9)
        {
            actor->effectAnimation.x = Battle_InterpolateS16(actor->homeX,
                target->homeX + (Battle_GetCombatantDimension(target, 0) >> 1) - actor->homeX,
                10, sBattleActionFrame, 2);
            actor->effectAnimation.y = Battle_InterpolateS16(actor->homeY - 16,
                target->homeY - (Battle_GetCombatantDimension(target, 1) >> 1) - (actor->homeY - 16),
                10, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            BattleAction_BeginTargetReactions(20);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 6;
        }
        break;
    case 6:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_SetAnimationById(&actor->animation, 0x8C, 0x1B4, 13, 0x22);
            sBattleActionEffectPhase = 8;
        }
        break;
    case 8:
        sBattleActionEffectPhase = 7;
        break;
    case 7:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_RestoreIdleAnimation(actor, sBattleActionSavedX, sBattleActionSavedY,
                sBattleActionSavedBaseTile, sBattleActionSavedPaletteSlot);
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进未被当前 Boss 配置引用的火焰吐息攻击变体。
 *  @note boss[19]；本轮受控回放返回 1。 当前 Boss 配置未引用该槽，证据来自直接调用夹具。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08029510
 */
u8 BattleAction_UpdateBossFireBreathVariant(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 13, 0);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 1;
        break;
    case 1:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            BattlePalette_StartObjCycle(actor->animation.paletteSlot, 1, 3, -11, 5);
            sBattleActionEffectPhase = 3;
        }
        break;
    case 3:
        if (actor->animation.timelineFrame > 79)
        {
            BattlePalette_StopObjCycle(actor->animation.paletteSlot, 1);
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 4;
        }
        break;
    case 4:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            BattlePalette_StartObjCycle(actor->animation.paletteSlot, 1, 3, -11, 5);
            sBattleActionEffectPhase = 2;
        }
        break;
    case 2:
        Battle_SetEffectAnimation(actor, 180, 120, 0x1E6, 13, 0x34A, 2);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 5;
        break;
    case 5:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 6;
        }
        break;
    case 6:
        if (sBattleActionFrame > 0)
        {
            BattleAction_BeginTargetReactions(20);
            sBattleActionEffectPhase = 7;
        }
        break;
    case 7:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            BattlePalette_StartObjCycle(actor->animation.paletteSlot, 1, 3, -11, 5);
            result = 1;
        }
        break;
    }
    sBattleActionFrame++;
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Advance the action selected by boss[20].
 *  @note Seeded GPBA replay stayed at phase 1 with result zero through 450 dispatch updates.
 *  The fixture called this entry directly; it does not establish a normal dispatcher selection.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x08029784
 *  静态收尾：暂停并恢复 OBJ 调色板循环，启动目标效果动画与反应，恢复待机后返回 1。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdateBoss120PaletteCycleAttackVariant(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 13, 1);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 1;
        break;
    case 1:
        break;
    case 3:
        if (actor->animation.timelineFrame > 79)
        {
            BattlePalette_StopObjCycle(actor->animation.paletteSlot, 1);
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 4;
        }
        break;
    case 4:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            BattlePalette_StartObjCycle(actor->animation.paletteSlot, 1, 3, -11, 5);
            sBattleActionEffectPhase = 2;
        }
        break;
    case 2:
        Battle_SetEffectAnimation(actor, 180, 120, 0x1E6, 13, 0x34A, 2);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 5;
        break;
    case 5:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 6;
        }
        break;
    case 6:
        if (sBattleActionFrame > 0)
        {
            BattleAction_BeginTargetReactions(20);
            sBattleActionEffectPhase = 7;
        }
        break;
    case 7:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            BattlePalette_StartObjCycle(actor->animation.paletteSlot, 1, 3, -11, 5);
            result = 1;
        }
        break;
    }
    sBattleActionFrame++;
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进未被当前 Boss 配置引用的调色板脉冲群体攻击变体。
 *  @note boss[21]；本轮受控回放返回 1。 当前 Boss 配置未引用该槽，证据来自直接调用夹具。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x080299C8
 */
u8 BattleAction_UpdateBossPalettePulseAttackVariant(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 1, 0x1B4, 13, 0);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 1;
        break;
    case 1:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            BattlePalette_StartObjCycle(actor->animation.paletteSlot, 1, 3, -11, 5);
            sBattleActionEffectPhase = 2;
        }
        break;
    case 2:
        if (actor->animation.timelineFrame <= 46)
        {
            actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                target->homeX - 48 - sBattleActionSavedX,
                47, actor->animation.timelineFrame, 2);
            actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                target->homeY - sBattleActionSavedY,
                47, actor->animation.timelineFrame, 2);
        }
        else
            sBattleActionEffectPhase = 3;
        break;
    case 3:
        if (actor->animation.timelineFrame > 79)
        {
            BattleAction_BeginTargetReactions(20);
            sBattleActionEffectPhase = 4;
        }
        break;
    case 4:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_StopObjCycle(actor->animation.paletteSlot, 1);
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            actor->homeX = sBattleActionSavedX;
            actor->homeY = sBattleActionSavedY;
            sBattleActionEffectPhase = 5;
        }
        break;
    case 5:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            BattlePalette_StartObjCycle(actor->animation.paletteSlot, 1, 3, -11, 5);
            sBattleActionEffectPhase = 6;
        }
        break;
    case 6:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        result = 1;
        break;
    }
    sBattleActionFrame++;
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 121 的红色能量场、三组调色板渐变和目标反应。
 *  @note 普通 C 的 0x55A 字节及重定位 100% 匹配；末尾两字节零填充不计。
 *  @address 0x08029BF8
 */
u8 BattleAction_UpdateBoss121RedEnergyAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        actor->hitEffectId = 0xAC;
        actor->hitReactionStartFrame = 0;
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->animation.flags |= BATTLE_ANIM_SEMITRANSPARENT;
        Battle_SetBlend(0x410, 16, 0);
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(16, -16, 10, sBattleActionFrame, 2);
            Battle_SetBlend(0x410, sBattleEffectBlendEva, 16 - sBattleEffectBlendEva);
            sBattleActionFrame++;
        }
        else
        {
            actor->animation.flags |= BATTLE_ANIM_HIDDEN;
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        Battle_ResetBackgroundLoader();
        Battle_SetBgScroll(1, -35, -5);
        sBattleEffectBlendEva = 0;
        Battle_SetBlend(0x1747, 0, 16);
        sBattleActionEffectPhase = 20;
        break;
    case 20:
        if ((u8)Battle_UpdateBackgroundLoader(15, 3, 14, 1))
        {
            sBattleActionFrame = 0;
            BattlePalette_StartBgFade(0, 3, 7, 14, 28, 4, 4, -1, 2);
            Sound_PlaySfx(0x20, 0, 0);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (sBattleActionFrame <= 19)
        {
            if (sBattleActionFrame == 4)
                BattlePalette_ReturnBgFade(0, 3, 16);
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 20, sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 10, sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame <= 19)
            sBattleActionFrame++;
        else
        {
            Battle_SetEffectAnimation(actor, 120, 105, 0x1B4, 14, 0x2E7, 0x2404);
            actor->effectAnimation.priority = 0;
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x64, 0, 0);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (actor->effectAnimation.timelineFrame > 29)
        {
            actor->effectAnimation.flags |= 0x100;
            actor->effectAnimation.timelineFrame++;
            sBattleActionEffectPhase = 28;
        }
        break;
    case 26:
    case 27:
        break;
    case 28:
        actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 6 + 30;
        BattlePalette_StartBgFade(0, 3, 31, 20, 14, 4, 4, -1, 2);
        BattlePalette_StartBgFade(14, 1, 31, 20, 14, 4, 4, -1, 2);
        BattlePalette_StartObjFade(1, 5, 31, 20, 14, 4, 4, -1, 2);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 29;
        break;
    case 29:
        if (sBattleActionFrame <= 19)
            sBattleActionFrame++;
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 30;
        }
        actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 6 + 30;
        break;
    case 30:
        actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 6 + 30;
        if (sBattleActionFrame <= 7)
            sBattleActionFrame++;
        else
        {
            Battle_SetBgRequests(0x20);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            BattlePalette_ReturnBgFade(0, 3, 32);
            BattlePalette_ReturnBgFade(14, 1, 32);
            BattlePalette_ReturnObjFade(1, 5, 32);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 31;
        }
        break;
    case 31:
        if (sBattleActionFrame <= 31)
            sBattleActionFrame++;
        else
        {
            actor->animation.flags &= ~BATTLE_ANIM_HIDDEN;
            Battle_SetBlend(0x410, 0, 16);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 32;
        }
        break;
    case 32:
        if (sBattleActionFrame <= 31)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 32, sBattleActionFrame, 2);
            Battle_SetBlend(0x410, sBattleEffectBlendEva, 16 - sBattleEffectBlendEva);
            sBattleActionFrame++;
        }
        else
        {
            actor->animation.flags &= ~BATTLE_ANIM_SEMITRANSPARENT;
            Battle_SetBlend(0, 0, 0);
            BattleAction_BeginTargetReactions(20);
            sBattleActionEffectPhase = 33;
        }
        break;
    case 33:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            sBattleActionEffectPhase = 9;
        break;
    case 34:
    case 35:
    case 36:
    case 37:
        break;
    case 9:
        Battle_SetBgScroll(1, 0, 0);
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Advance the boss attack through its three recorded screen waypoints.
 *  @note Boss handler 23 reads byte-coordinate pairs at 0839DF90..0839DF95; the shared stage byte selects the waypoint.
 *  @note 普通 C 的 0x718 字节及解析后的 135 项代码重定位完全匹配。
 *  保留 0..36 的空分支、背景/OBJ 调色板操作与各阶段独立的八位帧计数。
 *  阶段 33 在重置计数后仍递增；阶段 34 先判反应时间再判动画结束。
 *  三个坐标点依次为 (225,80)、(140,80)、(180,120)；第二参数未使用。
 *  @note Seeded GPBA replay returned 1 after 413 dispatch updates (413 entry hits).
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x0802A154
 */
u8 BattleAction_UpdateBossWaypointAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        actor->hitEffectId = 1;
        actor->hitReactionStartFrame = 0;
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->animation.flags |= BATTLE_ANIM_SEMITRANSPARENT;
        Battle_SetBlend(0x410, 16, 0);
        sBattleActionStage8A5 = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(16, -16, 10,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x410, sBattleEffectBlendEva, 16 - sBattleEffectBlendEva);
            sBattleActionFrame++;
        }
        else
        {
            actor->animation.flags |= BATTLE_ANIM_HIDDEN;
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        Battle_ResetBackgroundLoader();
        Battle_SetBgScroll(1, -35, -5);
        sBattleEffectBlendEva = 0;
        Battle_SetBlend(0x1747, 0, 16);
        sBattleActionEffectPhase = 20;
        break;
    case 20:
        if (Battle_UpdateBackgroundLoader(15, 3, 14, 2))
        {
            sBattleActionFrame = 0;
            BattlePalette_StartBgFade(0, 3, 7, 14, 28, 4, 4, -1, 2);
            Sound_PlaySfx(0x20, 0, 0);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (sBattleActionFrame <= 19)
        {
            if (sBattleActionFrame == 4)
                BattlePalette_ReturnBgFade(0, 3, 16);
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 10,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        BattlePalette_StartBgFade(0, 3, 7, 14, 28, 4, 4, -1, 2);
        BattlePalette_StartBgFade(14, 1, 7, 14, 28, 4, 4, -1, 2);
        BattlePalette_StartObjFade(1, 5, 7, 14, 28, 4, 4, -1, 2);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 24;
        break;
    case 24:
        if (sBattleActionFrame <= 7)
            sBattleActionFrame++;
        else
        {
            Battle_SetEffectAnimation(actor, 115, 115, 0x1B4, 14, 0x2ED, 5);
            actor->effectAnimation.priority = 2;
            BattlePalette_ReturnBgFade(0, 3, 32);
            BattlePalette_ReturnBgFade(14, 1, 32);
            BattlePalette_ReturnObjFade(1, 5, 32);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
    {
        u16 loading = actor->effectAnimation.flags & BATTLE_ANIM_LOADING;

        if (loading == 0)
        {
            Sound_PlaySfx(0x68, 1, 0);
            sBattleActionFrame = loading;
            sBattleActionEffectPhase = 26;
        }
        break;
    }
    case 26:
        if (sBattleActionFrame <= 31)
            sBattleActionFrame++;
        else
        {
            BattlePalette_StartBgPulse(0, 3, 7, 14, 28, 4, 4, 2);
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        if (!Sound_IsSfxPlaying(1))
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            BattlePalette_StopBgPulse(0, 3);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 28;
        }
        break;
    case 28:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(0, 16, 10,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, 16 - sBattleEffectBlendEvb, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Battle_SetBgRequests(0x20);
            sBattleActionEffectPhase = 29;
        }
        break;
    case 29:
        actor->animation.flags &= ~BATTLE_ANIM_HIDDEN;
        Battle_SetBlend(0x410, 0, 16);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 30;
        break;
    case 30:
        if (sBattleActionFrame <= 31)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 32,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x410, sBattleEffectBlendEva, 16 - sBattleEffectBlendEva);
            sBattleActionFrame++;
        }
        else
        {
            actor->animation.flags &= ~BATTLE_ANIM_SEMITRANSPARENT;
            Battle_SetBlend(0, 0, 0);
            Battle_SetEffectAnimation(actor, D_0839DF90[sBattleActionStage8A5 * 2],
                (D_0839DF90 + 1)[sBattleActionStage8A5 * 2], 0x1B4, 13, 0x2EE, 2);
            actor->effectAnimation.priority = 0;
            sBattleActionEffectPhase = 31;
        }
        break;
    case 31:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 32;
        break;
    case 32:
        if (actor->effectAnimation.timelineFrame > 12)
        {
            BattlePalette_StartBgFade(0, 3, 16, 28, 31, 4, 4, -1, 2);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 33;
        }
        break;
    case 33:
        if (sBattleActionFrame > 3)
        {
            BattlePalette_ReturnBgFade(0, 3, 16);
            sBattleActionFrame = 0;
            Sound_PlaySfx(0x68, 1, 0);
            if (sBattleActionStage8A5 == 0)
                sBattleActionEffectPhase = 34;
            else
                sBattleActionEffectPhase = 35;
        }
        sBattleActionFrame++;
        break;
    case 34:
        if (sBattleActionFrame > 15)
        {
            BattleAction_BeginTargetReactions(90);
            sBattleActionEffectPhase = 35;
        }
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 35;
        }
        sBattleActionFrame++;
        break;
    case 35:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 36;
        }
        break;
    case 36:
        if (sBattleActionStage8A5 <= 1)
        {
            sBattleActionStage8A5++;
            actor->effectAnimation.timelineFrame = 0;
            actor->displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
            actor->effectAnimation.x = D_0839DF90[sBattleActionStage8A5 * 2];
            actor->effectAnimation.y = (D_0839DF90 + 1)[sBattleActionStage8A5 * 2];
            sBattleActionEffectPhase = 32;
        }
        else if (sBattleHitEffectPhase == 0 && sBattleDodgePhase == 0
            && sBattleHitReactionPhase == 0)
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        if (sBattleHitEffectPhase == 0 && sBattleDodgePhase == 0
            && sBattleHitReactionPhase == 0)
        {
            Battle_SetBgScroll(1, 0, 0);
            result = 1;
        }
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 121 的蓝色能量场、三组调色板渐变和目标反应。
 *  @note 普通 C 的 0x556 字节及重定位 100% 匹配；末尾两字节零填充不计。
 *  @address 0x0802A86C
 */
u8 BattleAction_UpdateBoss121BlueEnergyAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        actor->hitEffectId = 0xA9;
        actor->hitReactionStartFrame = 0;
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->animation.flags |= BATTLE_ANIM_SEMITRANSPARENT;
        Battle_SetBlend(0x410, 16, 0);
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(16, -16, 10, sBattleActionFrame, 2);
            Battle_SetBlend(0x410, sBattleEffectBlendEva, 16 - sBattleEffectBlendEva);
            sBattleActionFrame++;
        }
        else
        {
            actor->animation.flags |= BATTLE_ANIM_HIDDEN;
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        Battle_ResetBackgroundLoader();
        Battle_SetBgScroll(1, -35, -5);
        sBattleEffectBlendEva = 0;
        Battle_SetBlend(0x1747, 0, 16);
        sBattleActionEffectPhase = 20;
        break;
    case 20:
        if ((u8)Battle_UpdateBackgroundLoader(15, 3, 14, 1))
        {
            sBattleActionFrame = 0;
            BattlePalette_StartBgFade(0, 3, 7, 14, 28, 4, 4, -1, 2);
            Sound_PlaySfx(0x20, 0, 0);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (sBattleActionFrame <= 19)
        {
            if (sBattleActionFrame == 4)
                BattlePalette_ReturnBgFade(0, 3, 16);
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 20, sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 10, sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame <= 19)
            sBattleActionFrame++;
        else
        {
            Battle_SetEffectAnimation(actor, 120, 105, 0x1B4, 14, 0x2E8, 0x2404);
            actor->effectAnimation.priority = 0;
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x64, 0, 0);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (actor->effectAnimation.timelineFrame > 29)
        {
            actor->effectAnimation.flags |= 0x100;
            actor->effectAnimation.timelineFrame++;
            sBattleActionEffectPhase = 28;
        }
        break;
    case 26:
    case 27:
        break;
    case 28:
        actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 6 + 30;
        BattlePalette_StartBgFade(0, 3, 14, 20, 31, 4, 4, -1, 2);
        BattlePalette_StartBgFade(14, 1, 14, 20, 31, 4, 4, -1, 2);
        BattlePalette_StartObjFade(1, 5, 14, 20, 31, 4, 4, -1, 2);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 29;
        break;
    case 29:
        if (sBattleActionFrame <= 19)
            sBattleActionFrame++;
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 30;
        }
        actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 6 + 30;
        break;
    case 30:
        actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 6 + 30;
        if (sBattleActionFrame <= 7)
            sBattleActionFrame++;
        else
        {
            Battle_SetBgRequests(0x20);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            BattlePalette_ReturnBgFade(0, 3, 32);
            BattlePalette_ReturnBgFade(14, 1, 32);
            BattlePalette_ReturnObjFade(1, 5, 32);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 31;
        }
        break;
    case 31:
        if (sBattleActionFrame <= 31)
            sBattleActionFrame++;
        else
        {
            actor->animation.flags &= ~BATTLE_ANIM_HIDDEN;
            Battle_SetBlend(0x410, 0, 16);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 32;
        }
        break;
    case 32:
        if (sBattleActionFrame <= 31)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 32, sBattleActionFrame, 2);
            Battle_SetBlend(0x410, sBattleEffectBlendEva, 16 - sBattleEffectBlendEva);
            sBattleActionFrame++;
        }
        else
        {
            actor->animation.flags &= ~BATTLE_ANIM_SEMITRANSPARENT;
            Battle_SetBlend(0, 0, 0);
            BattleAction_BeginTargetReactions(20);
            sBattleActionEffectPhase = 33;
        }
        break;
    case 33:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            sBattleActionEffectPhase = 9;
        break;
    case 34:
    case 35:
    case 36:
    case 37:
        break;
    case 9:
        Battle_SetBgScroll(1, 0, 0);
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 121 的蓝白闪光及待机恢复，不排队目标伤害。
 *  @note boss[25]；本轮受控回放返回 2。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802ADC4
 */
u8 BattleAction_UpdateBoss121FlashAnimation(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            actor->animation.flags &= ~0x100;
            Battle_ResetBackgroundLoader();
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if ((u8)Battle_UpdateBackgroundLoader(14, 3, 14, 1))
        {
            Sound_PlaySfx(0x5A, 1, 0);
            sBattleEffectBlendEva = 0;
            Battle_SetBlend(0x1C42, 0, 16);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        sBattleActionFrame = 0;
        BattlePalette_StartBgCycle(14, 1, 1, 1, 15);
        sBattleActionEffectPhase = 21;
        break;
    case 21:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 12, 20, sBattleActionFrame, 2);
            Battle_SetBlend(0x1C42, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleEffectBlendEva = 12;
            Battle_SetBlend(0x1C42, 12, 16);
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (!Sound_IsSfxPlaying(1))
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(12, -12, 20, sBattleActionFrame, 2);
            Battle_SetBlend(0x1C42, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Sound_StopSfx(1);
            Battle_SetBlend(0, 0, 0);
            actor->displayState &= BATTLE_DISPLAY_EFFECT_ANIMATION;
            BattlePalette_StopBgCycle(14, 1);
            *(vu16 *)REG_DISPCNT &= ~0x200;
            BattlePalette_StartObjFade(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation),
                7, 14, 28, 4, 4, -1, 2);
            sBattleActionEffectPhase = 24;
            Sound_PlaySfx(0x58, 1, 0);
        }
        break;
    case 24:
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            BattlePalette_ReturnObjFade(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation), 32);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (sBattleActionFrame <= 31)
            sBattleActionFrame++;
        else
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        result = 2;
        break;
    }
    sBattleActionFrame++;
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 122、124 共用的尖刺打击、前跃与返位。
 *  @note 普通 C 的 0x518 字节及重定位 100% 匹配；保留各阶段的帧阈值、插值和标志更新顺序。
 *  @address 0x0802B0F0
 */
u8 BattleAction_UpdateBoss122And124SpikeAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0;
        actor->hitReactionStartFrame = 0;
        Battle_SetBossAnimation(actor, 1, 0x1B4, 11, 0);
        actor->animation.flags |= 0x100;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Battle_SetEffectAnimation(actor, actor->homeX - 6, actor->homeY + 10,
                0x27C, 13, 0x2F4, 4);
            actor->effectAnimation.flags |= 0x100;
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x5A, 1, 0);
            actor->animation.flags &= ~0x100;
            actor->effectAnimation.flags &= ~0x100;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (actor->animation.timelineFrame > 90)
        {
            Sound_PlaySfx(0x31, 1, 0);
            actor->animation.flags |= 0x100;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame <= 29)
        {
            actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                target->homeX - 8 - sBattleActionSavedX, 30, sBattleActionFrame, 0);
            actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                target->homeY - sBattleActionSavedY, 30, sBattleActionFrame, 0);
            actor->verticalLift = Battle_InterpolateS16(0, 70, 15, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            actor->verticalLift = 0;
            actor->animation.flags &= ~0x100;
            actor->homeY = target->homeY + 2;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (actor->animation.timelineFrame > 96)
        {
            Battle_SetEffectAnimation(actor, actor->homeX - 8, actor->homeY,
                0x27C, 13, 0x2F5, 5);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            BattleAction_BeginTargetReactions(40);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
            sBattleActionEffectPhase = 26;
        }
        if (actor->animation.timelineFrame > 155)
            actor->animation.flags |= 0x100;
        break;
    case 26:
        if (actor->animation.timelineFrame > 155)
        {
            actor->animation.flags &= ~0x100;
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        if (actor->animation.timelineFrame <= 165)
        {
            actor->animation.flags |= 0x100;
            sBattleActionFrame = 0;
            sBattleActionScratch86E = actor->homeX;
            sBattleActionScratch86F = actor->homeY;
            sBattleActionEffectPhase = 28;
            Sound_PlaySfx(0x31, 1, 0);
        }
        break;
    case 28:
        if (sBattleActionFrame <= 29)
        {
            actor->homeX = Battle_InterpolateS16(sBattleActionScratch86E,
                sBattleActionSavedX - sBattleActionScratch86E, 30, sBattleActionFrame, 0);
            actor->homeY = Battle_InterpolateS16(sBattleActionScratch86F,
                sBattleActionSavedY - sBattleActionScratch86F, 30, sBattleActionFrame, 0);
            actor->verticalLift = Battle_InterpolateS16(0, 70, 15, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            actor->verticalLift = 0;
            actor->animation.flags &= ~0x100;
            actor->homeY = sBattleActionSavedY;
            actor->homeX = sBattleActionSavedX;
            sBattleActionEffectPhase = 29;
            Sound_PlaySfx(0x31, 1, 0);
        }
        break;
    case 29:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 30;
        }
        break;
    case 30:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 122、124 共用的风纹群体攻击演出。
 *  @note boss[27]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802B608
 */
u8 BattleAction_UpdateBoss122And124WindAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 12, 0);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 2;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags |= 0x100;
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_ResetBackgroundLoader();
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if ((u8)Battle_UpdateBackgroundLoader(13, 3, 14, 2))
        {
            Sound_PlaySfx(0x55, 1, 0);
            Battle_SetBlend(0x1447, 9, 15);
            BattlePalette_StartBgCycle(14, 1, 1, -3, 12);
            BattleAction_BeginTargetReactions(60);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (sBattleActionFrame <= 59)
            sBattleActionFrame++;
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame <= 29)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(9, -9, 30,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1447, sBattleEffectBlendEva, 15);
            sBattleActionFrame++;
        }
        else
        {
            Sound_StopSfx(1);
            BattlePalette_StopBgCycle(14, 1);
            Battle_SetBlend(0, 0, 0);
            *(vu16 *)REG_DISPCNT &= ~0x200;
            sBattleActionEffectPhase = 23;
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
        }
        break;
    case 23:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 24;
        break;
    case 24:
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 9;
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 122、124 共用的火团群体攻击演出。
 *  @note boss[28]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802B8BC
 */
u8 BattleAction_UpdateBoss122And124FireBurstAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 11, 1);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 2;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (actor->animation.timelineFrame > 70)
        {
            Battle_SetEffectAnimation(actor, 185, 120, 0x2E0, 13, 0x2F9, 5);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x36, 1, 1);
            BattleAction_BeginTargetReactions(90);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (!(u8)Battle_AreCombatantAnimationsLoading())
            sBattleActionEffectPhase = 22;
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->animation.flags |= 0x100;
        }
        break;
    case 22:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Sound_StopSfx(1);
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    sBattleActionFrame++;
    return result;
}

/** @brief 推进 Boss 123 的金色火花单目标攻击演出。
 *  @note boss[29]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802BB24
 */
u8 BattleAction_UpdateBoss123SparkAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0;
        actor->hitReactionStartFrame = 0;
        Battle_SetBossAnimation(actor, 1, 0x1B4, 10, 0);
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x5A, 1, 0);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetEffectAnimation(actor, target->homeX + 4, target->homeY + 4,
                0x27C, 13, 0x301, 5);
            actor->effectAnimation.priority = 0;
            actor->animation.flags |= 0x100;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x9A, 1, 0);
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 22;
        break;
    case 22:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            BattleAction_BeginTargetReactions(30);
            sBattleActionFrame = 0;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            sBattleActionEffectPhase = 9;
        else
            sBattleActionFrame++;
        break;
    case 9:
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 123 的背景切换、橙色爆破和群体受击演出。
 *  @note boss[30]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802BD94
 */
u8 BattleAction_UpdateBoss123FireBlastAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 13, 0);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 1;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x5A, 1, 0);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (actor->animation.timelineFrame == 110)
            Sound_PlaySfx(0x68, 1, 0);
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Sound_StopSfx(1);
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 21;
        break;
    case 21:
        Battle_SetEffectAnimation(actor, 130, 125, 0x1B4, 10, 0x30E, 5);
        actor->effectAnimation.priority = 0;
        sBattleActionEffectPhase = 22;
        break;
    case 22:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x34, 1, 0);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (actor->effectAnimation.timelineFrame > 14)
        {
            BattlePalette_StartBgFade(0, 3, 16, 28, 31, 4, 4, -1, 2);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            BattlePalette_ReturnBgFade(0, 3, 16);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (actor->effectAnimation.timelineFrame == 40)
            Sound_PlaySfx(0x5A, 1, 0);
        if (actor->effectAnimation.timelineFrame > 81)
        {
            Sound_PlaySfx(0x4E, 2, 1);
            BattlePalette_StartBgFade(0, 3, 16, 28, 31, 4, 4, -1, 2);
            sBattleActionFrame = 0;
            BattleAction_BeginTargetReactions(30);
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            BattlePalette_ReturnBgFade(0, 3, 16);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            Sound_StopSfx(2);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 123 的环形攻击变体、背景调色板循环和淡出。
 *  @note 普通 C 的 0x3A4 字节及重定位 100% 匹配；原版 paletteSlot 未初始化且阶段 25 无处理分支，均按原样保留。
 *  @address 0x0802C0EC
 */
u8 BattleAction_UpdateBoss123RingAttackVariant(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;
    u8 paletteSlot;

    Battle_GetCombatants();
    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 13, 0);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 2;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x5A, 1, 0);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (actor->animation.timelineFrame == 110)
            Sound_PlaySfx(0x68, 1, 0);
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Sound_StopSfx(1);
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile, paletteSlot, 0);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Battle_ResetBackgroundLoader();
            Battle_SetBlend(0x1F47, 15, 8);
            Sound_PlaySfx(0x64, 1, 0);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if ((u8)Battle_UpdateBackgroundLoader(12, 3, 15, 2))
        {
            BattlePalette_StartBgCycle(15, 1, 3, 1, 6);
            sBattleActionFrame = 0;
            Battle_SetEffectAnimation(actor, 125, 145, 0x27C, 13, 0x33B, 5);
            actor->effectAnimation.priority = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 23;
        break;
    case 23:
        if (sBattleActionFrame <= 49)
        {
            actor->animation.timelineFrame = (actor->animation.timelineFrame + 1)
                % Battle_GetAnimationFrameCount(&actor->animation) + 20;
            actor->homeX = Battle_InterpolateS16(sBattleActionScratch86E,
                240 - sBattleActionScratch86E, 50, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            actor->verticalLift = actor->homeY;
            actor->homeX = sBattleActionSavedX;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            BattleAction_BeginTargetReactions(40);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame <= 39)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(15, -15, 40, sBattleActionFrame, 2);
            sBattleEffectBlendEvb = Battle_InterpolateS16(8, 8, 40, sBattleActionFrame, 2);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            Battle_SetBlend(0, 0, 0);
            BattlePalette_StopBgCycle(15, 1);
            *(vu16 *)REG_DISPCNT &= ~0x200;
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile, paletteSlot, 0);
            actor->animation.flags |= 0x100;
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
    case 26:
    case 27:
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 123 的旋风及群体受击演出。
 *  @note boss[32]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802C490
 */
u8 BattleAction_UpdateBoss123WindAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 13, 0);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 2;
        actor->hitReactionStartFrame = 20;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x5A, 1, 0);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (actor->animation.timelineFrame == 110)
            Sound_PlaySfx(0x68, 1, 0);
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Sound_StopSfx(1);
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 21;
        break;
    case 21:
        Battle_SetEffectAnimation(actor, 186, 125, 0x1B4, 10, 0x312, 5);
        actor->effectAnimation.priority = 0;
        sBattleActionEffectPhase = 22;
        break;
    case 22:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x55, 1, 0);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (actor->effectAnimation.timelineFrame > 19)
        {
            BattleAction_BeginTargetReactions(30);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            Sound_StopSfx(1);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
        }
        sBattleActionFrame++;
        break;
    case 25:
    case 26:
    case 27:
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        else
            sBattleActionFrame++;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 123 的落石及群体受击演出。
 *  @note boss[33]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802C714
 */
u8 BattleAction_UpdateBoss123RockfallAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 13, 0);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 2;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x5A, 1, 0);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (actor->animation.timelineFrame == 110)
            Sound_PlaySfx(0x68, 1, 0);
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Sound_StopSfx(1);
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 21;
        break;
    case 21:
        Battle_SetEffectAnimation(actor, 130, 125, 0x1B4, 10, 0x313, 5);
        actor->effectAnimation.priority = 0;
        sBattleActionEffectPhase = 22;
        break;
    case 22:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x4E, 1, 1);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (actor->effectAnimation.timelineFrame > 24)
        {
            BattlePalette_StartBgFade(0, 3, 16, 28, 31, 4, 4, -1, 2);
            sBattleActionFrame = 0;
            BattleAction_BeginTargetReactions(50);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame > 3)
        {
            sBattleActionEffectPhase = 25;
            BattlePalette_ReturnBgFade(0, 3, 16);
        }
        else
            sBattleActionFrame++;
        break;
    case 25:
        if (actor->effectAnimation.timelineFrame == 91)
            Sound_StopSfx(1);
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
        }
        sBattleActionFrame++;
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 125 的蓄电、闪电移动、目标受击及待机恢复。
 *  @note 普通 C 的 0x4A8 字节及重定位 100% 匹配；保留各阶段的帧阈值、插值和标志更新顺序。
 *  @address 0x0802C9E8
 */
u8 BattleAction_UpdateBoss125LightningAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        Battle_SetBossAnimation(actor, 1, 0x1B4, 10, 0);
        actor->hitEffectId = 0;
        actor->hitReactionStartFrame = 15;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (actor->animation.timelineFrame > 23)
        {
            Sound_PlaySfx(0x66, 1, 1);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (actor->animation.timelineFrame > 71)
        {
            Sound_StopSfx(1);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (actor->animation.timelineFrame > 82)
            sBattleActionEffectPhase = 22;
        break;
    case 22:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->animation.flags |= 0x100;
        }
        sBattleActionScratch86E = actor->homeX + 46;
        sBattleActionScratch86F = actor->homeY - 46;
        Battle_SetEffectAnimation(actor, sBattleActionScratch86E,
            sBattleActionScratch86F, 0x27C, 12, 0x31A, 5);
        sBattleActionEffectPhase = 23;
        break;
    case 23:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->animation.flags |= 0x100;
        }
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            actor->effectAnimation.flags |= 0x100;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->animation.flags |= 0x100;
        }
        if (sBattleActionFrame <= 9)
        {
            actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 20;
            actor->effectAnimation.x = Battle_InterpolateS16(sBattleActionScratch86E,
                target->homeX - sBattleActionScratch86E, 10, sBattleActionFrame, 0);
            actor->effectAnimation.y = Battle_InterpolateS16(sBattleActionScratch86F,
                target->homeY + 4 - sBattleActionScratch86F, 10, sBattleActionFrame, 0);
            sBattleActionFrame++;
        }
        else
        {
            Sound_PlaySfx(0x67, 1, 0);
            actor->effectAnimation.flags &= ~0x100;
            actor->effectAnimation.timelineFrame = 20;
            actor->effectAnimation.x = target->homeX + 4;
            actor->effectAnimation.y = target->homeY + 4;
            sBattleActionFrame = 0;
            BattleAction_BeginTargetReactions(20);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->animation.flags |= 0x100;
        }
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 26;
        }
        sBattleActionFrame++;
        break;
    case 26:
        Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
            sBattleActionSavedPaletteSlot, 0);
        sBattleActionEffectPhase = 27;
        sBattleActionFrame++;
        break;
    case 27:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        sBattleActionFrame++;
        break;
    case 28:
    case 29:
        break;
    case 30:
        if (sBattleActionFrame <= 29)
        {
            actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                target->homeX - 8 - sBattleActionSavedX, 30, sBattleActionFrame, 0);
            actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                target->homeY - sBattleActionSavedY, 30, sBattleActionFrame, 0);
            actor->verticalLift = Battle_InterpolateS16(0, 70, 15, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            actor->verticalLift = 0;
            actor->animation.flags &= ~0x100;
            actor->homeY = target->homeY + 2;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 9:
        sBattleActionFrame++;
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 125 的紫色雾团群体攻击演出。
 *  @note boss[35]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802CE90
 */
u8 BattleAction_UpdateBoss125PurpleMistAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 10, 0);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 2;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (actor->animation.timelineFrame > 55)
        {
            actor->animation.flags |= 0x100;
            Battle_SetEffectAnimation(actor, actor->homeX + 70, actor->homeY - 32,
                0x27C, 12, 0x31C, 5);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x55, 1, 0);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            Sound_StopSfx(1);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            actor->animation.flags &= ~0x100;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Battle_SetEffectAnimation(actor, 116, 128, 0x2EA, 14, 0x2C2, 5);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            actor->effectAnimation.flags |= 0x2000;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            actor->hitEffectId = 1;
            BattleAction_BeginTargetReactions(40);
            sBattleActionEffectPhase = 9;
        }
        break;
    case 26:
        if (sBattleActionFrame <= 24)
        {
            sBattleActionFrame++;
            sBattleEffectBlendEva = Battle_InterpolateS16(7, -7, 25,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1C47, sBattleEffectBlendEva, 15);
        }
        else
        {
            BattlePalette_StopBgCycle(14, 1);
            Battle_SetBlend(0, 0, 0);
            *(vu16 *)REG_DISPCNT &= ~0x200;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 125 的大型火焰球群体攻击演出。
 *  @note boss[36]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802D1FC
 */
u8 BattleAction_UpdateBoss125FireballAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 10, 0);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 2;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (actor->animation.timelineFrame > 55)
        {
            actor->animation.flags |= 0x100;
            Battle_SetEffectAnimation(actor, actor->homeX + 60, actor->homeY - 24,
                0x27C, 12, 0x320, 5);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x64, 1, 0);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            Sound_StopSfx(1);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            actor->animation.flags &= ~0x100;
            BattleAction_BeginTargetReactions(40);
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Animate the boss attack and queue paralysis on successful targets.
 *  @note Boss handler 37 tests valid encoded target entries and queues status 2 when random % 100 <= 39; completion returns one.
 *  @note 普通 C 的 0x2D4 字节及解析后的 63 项代码重定位完全匹配。
 *  阶段为 0→18→19→20→21→22→23→24→25→9；保留 0..25 的空分支。
 *  阶段 20 单独递增八位帧计数；阶段 9 以 u8 索引遍历并在随机调用后重读目标条目。
 *  每次调用均更新目标反应，完成返回 1。
 *  @note Seeded GPBA replay returned 1 after 224 dispatch updates (224 entry hits).
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x0802D454
 */
u8 BattleAction_UpdateBossParalysisAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 10, 2);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 0x31D;
        actor->hitReactionStartFrame = 12;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (actor->animation.timelineFrame > 39)
        {
            Sound_PlaySfx(0x9A, 1, 0);
            BattlePalette_StartBgFade(0, 3, 16, 28, 31, 4, 4, -1, 2);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            BattlePalette_ReturnBgFade(0, 3, 16);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (actor->animation.timelineFrame > 87)
        {
            actor->animation.flags |= 0x100;
            sBattleActionFrame = 0;
            BattleAction_BeginTargetReactions(50);
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (!(target->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x69, 1, 0);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleHitEffectPhase == 0 && sBattleDodgePhase == 0
            && sBattleHitReactionPhase == 0)
        {
            actor->animation.flags &= ~0x100;
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 9:
    {
        u8 index;

        Battle_GetCombatants();
        for (index = 0; index < sBattleActionEffectTargetCount; index++)
        {
            if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK)
                == BATTLE_TARGET_VALID && Battle_NextRandom() % 100 <= 39)
                gBattleActionPendingStatuses[sBattleActionTargetEntries[index]
                    & BATTLE_TARGET_SLOT_MASK] = BATTLE_STATUS_PARALYZED;
        }
        result = 1;
        break;
    }
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 126 的腾空、蓝色光弹与单目标受击演出。
 *  @note boss[38]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802D728
 */
u8 BattleAction_UpdateBoss126LightAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0;
        actor->hitReactionStartFrame = 0;
        Battle_SetBossAnimation(actor, 1, 0x1B4, 13, 0);
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (actor->animation.timelineFrame == 12)
            Sound_PlaySfx(0x5A, 1, 0);
        if (actor->animation.timelineFrame > 121)
        {
            Sound_PlaySfx(0x31, 1, 0);
            sBattleActionScratch86E = actor->homeX + 29;
            sBattleActionScratch86F = actor->homeY - 47;
            Battle_SetEffectAnimation(actor, sBattleActionScratch86E, sBattleActionScratch86F,
                0x27C, 14, 0x328, 5);
            actor->effectAnimation.flags |= 0x100;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags |= 0x100;
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
        }
        if (sBattleActionFrame <= 9)
        {
            actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 9;
            actor->effectAnimation.x = Battle_InterpolateS16(sBattleActionScratch86E,
                target->homeX - sBattleActionScratch86E, 30, sBattleActionFrame, 0);
            actor->effectAnimation.y = Battle_InterpolateS16(sBattleActionScratch86F,
                target->homeY - sBattleActionScratch86F, 30, sBattleActionFrame, 0);
            sBattleActionFrame++;
        }
        else
        {
            actor->effectAnimation.timelineFrame = 10;
            actor->effectAnimation.flags &= ~0x100;
            actor->effectAnimation.x = target->homeX;
            actor->effectAnimation.y = target->homeY;
            BattleAction_BeginTargetReactions(60);
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            actor->animation.flags |= 0x100;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            sBattleActionEffectPhase = 9;
            actor->animation.flags &= ~0x100;
        }
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 126 的交叉光线、分块光面及群体受击演出。
 *  @note boss[39]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802DA78
 */
u8 BattleAction_UpdateBoss126CrossLightAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, sBattleActionSavedBaseTile, 13, 0);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 10;
        actor->hitEffectId = 1;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (actor->animation.timelineFrame > 6)
        {
            Battle_SetEffectAnimation(actor, actor->homeX + 60, actor->homeY,
                0x1B4, 14, 0x329, 5);
            actor->animation.flags |= 0x100;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            sBattleActionEffectPhase = 21;
            Sound_PlaySfx(0x5B, 1, 0);
            actor->animation.flags &= ~0x100;
        }
        break;
    case 21:
        if (actor->animation.timelineFrame > 95)
        {
            Sound_PlaySfx(0x5A, 1, 0);
            BattleAction_BeginTargetReactions(60);
            sBattleActionFrame = 0;
            actor->animation.flags |= 0x100;
            actor->effectAnimation.flags |= 0x100;
            BattlePalette_StartBgFade(0, 3, 16, 28, 31, 4, 4, -1, 2);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        actor->animation.timelineFrame = (actor->animation.timelineFrame + 1) % 31 + 96;
        actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 16 + 90;
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            sBattleActionFrame = 0;
            BattlePalette_ReturnBgFade(0, 3, 32);
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        actor->animation.timelineFrame = (actor->animation.timelineFrame + 1) % 31 + 97;
        actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 16 + 90;
        if (sBattleActionFrame <= 29)
            sBattleActionFrame++;
        else
        {
            actor->effectAnimation.timelineFrame = 107;
            actor->effectAnimation.flags &= ~0x100;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        actor->animation.timelineFrame = (actor->animation.timelineFrame + 1) % 31 + 97;
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            actor->animation.flags &= ~0x100;
            actor->animation.timelineFrame = 128;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            actor->animation.flags |= 0x100;
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            sBattleActionEffectPhase = 9;
            actor->animation.flags &= ~0x100;
        }
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 播放 Boss 回复动画，结束时补充最多一半最大 HP。
 *  @note 普通 C 的 0x1D8 字节及解析后的重定位与原 ROM 完全匹配。
 *  常规阶段为 0→18→20→21→9；保留阶段 19 及 0..23 的空分支。
 *  阶段 20 用 u32 保存受缺失 HP 限制的回复量，再半字写回；恢复原图块和调色板，
 *  置动画标志 0x100，阶段 21 等待加载结束后清除此位，阶段 9 返回 2。
 *  每次调用均更新目标反应并递增八位帧计数；第二参数不使用。
 *  @note Seeded GPBA replay returned 2 after 262 dispatch updates (262 entry hits).
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x0802DE04
 */
u8 BattleAction_UpdateBossHalfMaxHpRecovery(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 12, 1);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 2;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 20;
        break;
    case 19:
        if (actor->animation.timelineFrame > 102)
        {
            Battle_SetEffectAnimation(actor, 120, 80, 0, 14, 0x32A, 4);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            u32 recovery = Battle_LimitHpRecovery(actor, actor->maxHp >> 1);

            actor->hp += recovery;
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            actor->animation.flags |= 0x100;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            sBattleActionEffectPhase = 9;
            actor->animation.flags &= ~0x100;
        }
        break;
    case 22:
    case 23:
        break;
    case 9:
        result = 2;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    sBattleActionFrame++;
    return result;
}

/** @brief 推进 Boss 126 的火团及单目标受击演出。
 *  @note boss[41]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802DFDC
 */
u8 BattleAction_UpdateBoss126FireballAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 12, 1);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 2;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (actor->animation.timelineFrame > 70)
        {
            Battle_SetEffectAnimation(actor, 185, 120, 0x2E0, 14, 0x2F9, 5);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            BattleAction_BeginTargetReactions(90);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (!(u8)Battle_AreCombatantAnimationsLoading())
            sBattleActionEffectPhase = 22;
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->animation.flags |= 0x100;
        }
        break;
    case 22:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    sBattleActionFrame++;
    return result;
}

/** @brief 推进 Boss 127 的龙形突击及所选目标受击演出。
 *  @note boss[42]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802E234
 */
u8 BattleAction_UpdateBoss127DragonChargeAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0;
        actor->hitReactionStartFrame = 0;
        Battle_SetBossAnimation(actor, 1, 0x1B4, 10, 0);
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (actor->animation.timelineFrame > 39)
        {
            Sound_PlaySfx(0x63, 1, 0);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (actor->animation.timelineFrame > 76)
        {
            sBattleActionFrame = 0;
            Sound_PlaySfx(0x36, 1, 0);
            actor->animation.flags |= 0x100;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (sBattleActionFrame <= 19)
        {
            if (sBattleActionFrame == 10)
                BattleAction_BeginTargetReactions(20);
            actor->animation.timelineFrame = (actor->animation.timelineFrame + 1) % 21 + 77;
            actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                240 - sBattleActionSavedX, 20, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            actor->animation.flags &= ~0x100;
            actor->animation.timelineFrame = 100;
            actor->homeX = sBattleActionSavedX;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 127 的单目标施法与受击演出。
 *  @note boss[43]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802E49C
 */
u8 BattleAction_UpdateBoss127SingleTargetSpellAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 12, 0);
        actor->animation.flags |= 0x100;
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 2;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Battle_SetEffectAnimation(actor, target->homeX, target->homeY,
                0x27C, 14, 0x30C, 5);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x5E, 1, 0);
            actor->animation.flags &= ~0x100;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (actor->animation.timelineFrame > 131)
        {
            Sound_PlaySfx(0x58, 1, 0);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 127 的施法光阵、爆破动画及两次背景闪光。
 *  @note 普通 C 的 0x3FC 字节及重定位 100% 匹配；保留原有阶段、计数与动画标志更新顺序。
 *  @address 0x0802E6C8
 */
u8 BattleAction_UpdateBoss127FireBlastAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 13, 1);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 2;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 28;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 18:
    case 19:
    case 20:
        break;
    case 28:
        if (actor->animation.timelineFrame > 169)
        {
            actor->animation.flags |= 0x100;
            Battle_SetEffectAnimation(actor, actor->homeX + 4, actor->homeY - 16,
                0x27C, 10, 0x315, 4);
            sBattleActionEffectPhase = 29;
        }
        break;
    case 29:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x59, 1, 0);
            actor->animation.flags &= ~0x100;
            sBattleActionEffectPhase = 30;
        }
        break;
    case 30:
        if (actor->effectAnimation.timelineFrame == 200)
            Sound_PlaySfx(0x5A, 1, 0);
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
        }
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 31;
        }
        break;
    case 31:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 21;
        break;
    case 21:
        Battle_SetEffectAnimation(actor, 130, 125, 0x1B4, 10, 0x30E, 5);
        actor->effectAnimation.priority = 0;
        sBattleActionEffectPhase = 22;
        break;
    case 22:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x34, 1, 0);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (actor->effectAnimation.timelineFrame > 14)
        {
            BattlePalette_StartBgFade(0, 3, 16, 28, 31, 4, 4, -1, 2);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            BattlePalette_ReturnBgFade(0, 3, 16);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (actor->effectAnimation.timelineFrame == 40)
            Sound_PlaySfx(0x5A, 1, 0);
        if (actor->effectAnimation.timelineFrame > 81)
        {
            Sound_PlaySfx(0x4E, 2, 1);
            BattlePalette_StartBgFade(0, 3, 16, 28, 31, 4, 4, -1, 2);
            sBattleActionFrame = 0;
            BattleAction_BeginTargetReactions(30);
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            BattlePalette_ReturnBgFade(0, 3, 16);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            Sound_StopSfx(2);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Advance the action selected by boss[45].
 *  @note Seeded GPBA replay stayed at phase 30 with result zero through 450 dispatch updates.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x0802EAC4
 *  静态收尾：使用三组效果动画资源推进 Boss 施法与目标反应；当前正常阶段链最终写入未处理阶段 25。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdateBoss127ThreeEffectSpellVariant(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;
    u8 paletteSlot;

    Battle_GetCombatants();
    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 13, 1);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 2;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 28;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 18:
    case 19:
    case 20:
        break;
    case 28:
        if (actor->animation.timelineFrame > 169)
        {
            actor->animation.flags |= 0x100;
            Battle_SetEffectAnimation(actor, actor->homeX + 4, actor->homeY - 16,
                0x27C, 10, 0x314, 4);
            sBattleActionEffectPhase = 29;
        }
        break;
    case 29:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x59, 1, 0);
            actor->animation.flags &= ~0x100;
            sBattleActionEffectPhase = 30;
        }
        break;
    case 30:
        if (actor->effectAnimation.timelineFrame == 200)
            Sound_PlaySfx(0x5A, 1, 0);
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
        }
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                paletteSlot, 0);
            sBattleActionEffectPhase = 31;
        }
        break;
    case 31:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 21;
        break;
    case 21:
        Battle_SetEffectAnimation(actor, 180, 135, 0x1B4, 10, 0x30F, 4);
        actor->effectAnimation.priority = 2;
        sBattleActionEffectPhase = 22;
        break;
    case 22:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (actor->effectAnimation.timelineFrame
            >= (u16)Battle_GetAnimationFrameCount(&actor->effectAnimation) - 32)
            sBattleActionEffectPhase = 24;
        break;
    case 24:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetEffectAnimation(actor, 120, 80, 0x1B4, 10, 0x311, 4);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
    case 26:
    case 27:
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 127 的长施法及旋风群体攻击演出。
 *  @note boss[46]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802EDD8
 */
u8 BattleAction_UpdateBoss127WindAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 13, 1);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 2;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 28;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 18:
    case 19:
    case 20:
        break;
    case 28:
        if (actor->animation.timelineFrame > 169)
        {
            actor->animation.flags |= 0x100;
            Battle_SetEffectAnimation(actor, actor->homeX + 4, actor->homeY - 16,
                0x27C, 10, 0x316, 4);
            sBattleActionEffectPhase = 29;
        }
        break;
    case 29:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x59, 1, 0);
            actor->animation.flags &= ~0x100;
            sBattleActionEffectPhase = 30;
        }
        break;
    case 30:
        if (actor->effectAnimation.timelineFrame == 200)
            Sound_PlaySfx(0x5A, 1, 0);
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
        }
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 31;
        }
        break;
    case 31:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 21;
        break;
    case 21:
        Battle_SetEffectAnimation(actor, 186, 125, 0x1B4, 10, 0x312, 5);
        actor->effectAnimation.priority = 0;
        sBattleActionEffectPhase = 22;
        break;
    case 22:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x55, 1, 0);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (actor->effectAnimation.timelineFrame > 19)
        {
            BattleAction_BeginTargetReactions(30);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            Sound_StopSfx(1);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
        }
        sBattleActionFrame++;
        break;
    case 25:
    case 26:
    case 27:
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        else
            sBattleActionFrame++;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 127 的长施法及落石群体攻击演出。
 *  @note boss[47]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802F100
 */
u8 BattleAction_UpdateBoss127RockfallAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 13, 1);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 2;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 28;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 18:
    case 19:
    case 20:
        break;
    case 28:
        if (actor->animation.timelineFrame > 169)
        {
            actor->animation.flags |= 0x100;
            Battle_SetEffectAnimation(actor, actor->homeX + 4, actor->homeY - 16,
                0x27C, 10, 0x317, 4);
            sBattleActionEffectPhase = 29;
        }
        break;
    case 29:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x59, 1, 0);
            actor->animation.flags &= ~0x100;
            sBattleActionEffectPhase = 30;
        }
        break;
    case 30:
        if (actor->effectAnimation.timelineFrame == 200)
            Sound_PlaySfx(0x5A, 1, 0);
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
        }
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 31;
        }
        break;
    case 31:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 21;
        break;
    case 21:
        Battle_SetEffectAnimation(actor, 130, 125, 0x1B4, 10, 0x313, 5);
        actor->effectAnimation.priority = 0;
        sBattleActionEffectPhase = 22;
        break;
    case 22:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x4E, 1, 1);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (actor->effectAnimation.timelineFrame > 24)
        {
            BattlePalette_StartBgFade(0, 3, 16, 28, 31, 4, 4, -1, 2);
            sBattleActionFrame = 0;
            BattleAction_BeginTargetReactions(50);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame > 3)
        {
            sBattleActionEffectPhase = 25;
            BattlePalette_ReturnBgFade(0, 3, 16);
        }
        else
            sBattleActionFrame++;
        break;
    case 25:
        if (actor->effectAnimation.timelineFrame == 91)
            Sound_StopSfx(1);
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
        }
        sBattleActionFrame++;
        break;
    case 26:
    case 27:
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进未被当前 Boss 配置引用的单目标火团攻击变体。
 *  @note boss[48]；本轮受控回放返回 1。 当前 Boss 配置未引用该槽，证据来自直接调用夹具。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802F480
 */
u8 BattleAction_UpdateBossFireballAttackVariant(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 12, 1);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 2;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (actor->animation.timelineFrame > 70)
        {
            Battle_SetEffectAnimation(actor, 185, 120, 0x2E0, 14, 0x2F9, 5);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            BattleAction_BeginTargetReactions(90);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (!(u8)Battle_AreCombatantAnimationsLoading())
            sBattleActionEffectPhase = 22;
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->animation.flags |= 0x100;
        }
        break;
    case 22:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    sBattleActionFrame++;
    return result;
}

/** @brief 推进 Boss 128 的跃起及群体受击演出。
 *  @note boss[49]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0802F6D8
 */
u8 BattleAction_UpdateBoss128WingLeapAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        Battle_SetBossAnimation(actor, 1, 0x1B4, 13, 0);
        actor->hitEffectId = 0x339;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x59, 0, 0);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (actor->animation.timelineFrame > 33)
        {
            actor->animation.flags |= 0x100;
            BattlePalette_StartObjFade(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation),
                16, 28, 31, 4, 4, -1, 2);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            BattlePalette_ReturnObjFade(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation), 16);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 21;
            Sound_PlaySfx(0x64, 1, 0);
        }
        break;
    case 21:
        if (sBattleActionFrame <= 15)
        {
            actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                240 - sBattleActionSavedX, 16, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            actor->verticalLift = actor->homeY;
            actor->homeX = sBattleActionSavedX;
            BattleAction_BeginTargetReactions(40);
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            actor->animation.flags |= 0x100;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            actor->animation.flags &= ~0x100;
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (!Sound_IsSfxPlaying(1))
            sBattleActionEffectPhase = 25;
        break;
    case 25:
        if (sBattleActionFrame <= 29)
        {
            actor->verticalLift = Battle_InterpolateS16(actor->homeY,
                -actor->homeY, 30, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 26:
    case 27:
        break;
    case 9:
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 128 的地面火焰波、五轮效果循环和淡出返位。
 *  @note 普通 C 的 0x4AC 字节及重定位 100% 匹配；保留动画帧循环与各阶段计数更新顺序。
 *  @address 0x0802F9EC
 */
u8 BattleAction_UpdateBoss128FlameWaveAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 13, 0);
        actor->animation.flags |= 0x100;
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 2;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            sBattleActionScratch86E = actor->homeX;
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (sBattleActionFrame <= 9)
        {
            actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                30 - sBattleActionSavedX, 10, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame <= 19)
        {
            actor->animation.timelineFrame = (actor->animation.timelineFrame + 1)
                % Battle_GetAnimationFrameCount(&actor->animation) + 20;
            actor->homeX = Battle_InterpolateS16(sBattleActionScratch86E,
                240 - sBattleActionScratch86E, 20, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            actor->verticalLift = actor->homeY;
            actor->homeX = sBattleActionSavedX;
            Battle_SetEffectAnimation(actor, 15, 20, 0, 14, 0x33A, 7);
            actor->effectAnimation.priority = 2;
            Battle_SetBlend(0x1F47, 10, 12);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
    {
        u16 loading = actor->effectAnimation.flags & BATTLE_ANIM_LOADING;

        if (!loading)
        {
            Sound_PlaySfx(0x64, 1, 1);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    }
    case 22:
        if (actor->effectAnimation.timelineFrame > 27)
        {
            sBattleActionScratch86F = 0;
            sBattleActionFrame = 0;
            BattleAction_BeginTargetReactions(40);
            BattlePalette_StartBgFade(0, 3, 16, 28, 31, 4, 4, -1, 2);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 13 + 28;
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            BattlePalette_ReturnBgFade(0, 3, 32);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionScratch86F <= 4)
        {
            actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 13 + 28;
            if (actor->effectAnimation.timelineFrame == 28)
                sBattleActionScratch86F++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (sBattleActionFrame <= 9)
        {
            actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 13 + 28;
            sBattleEffectBlendEva = Battle_InterpolateS16(10, -10, 10, sBattleActionFrame, 2);
            sBattleEffectBlendEvb = Battle_InterpolateS16(12, 4, 10, sBattleActionFrame, 2);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            Sound_StopSfx(1);
            Battle_SetBlend(0, 0, 0);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            *(vu16 *)REG_DISPCNT &= ~0x200;
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            actor->animation.flags |= 0x100;
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            actor->animation.flags &= ~0x100;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        if (sBattleActionFrame <= 29)
        {
            actor->verticalLift = Battle_InterpolateS16(actor->homeY,
                -actor->homeY, 30, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 128 的蓝色光阵、横向移动及混合淡出返位。
 *  @note 普通 C 的 0x404 字节及重定位 100% 匹配；保留原有阶段、计数与动画标志更新顺序。
 *  @address 0x0802FE98
 */
u8 BattleAction_UpdateBoss128BlueCircleAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 12, 0);
        actor->animation.flags |= 0x100;
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 2;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (sBattleActionFrame <= 9)
        {
            actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                30 - sBattleActionSavedX, 10, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionScratch86E = actor->homeX;
            Battle_ResetBackgroundLoader();
            Battle_SetBlend(0x1F47, 15, 8);
            Sound_PlaySfx(0x64, 1, 0);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if ((u8)Battle_UpdateBackgroundLoader(12, 3, 14, 2))
        {
            BattlePalette_StartBgCycle(14, 1, 3, 1, 6);
            sBattleActionFrame = 0;
            Battle_SetEffectAnimation(actor, 125, 145, 0x27C, 13, 0x33B, 5);
            actor->effectAnimation.priority = 0;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 22;
        break;
    case 22:
        if (sBattleActionFrame <= 49)
        {
            actor->animation.timelineFrame = (actor->animation.timelineFrame + 1)
                % Battle_GetAnimationFrameCount(&actor->animation) + 20;
            actor->homeX = Battle_InterpolateS16(sBattleActionScratch86E,
                240 - sBattleActionScratch86E, 50, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            actor->verticalLift = actor->homeY;
            actor->homeX = sBattleActionSavedX;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            BattleAction_BeginTargetReactions(40);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame <= 39)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(15, -15, 40, sBattleActionFrame, 2);
            sBattleEffectBlendEvb = Battle_InterpolateS16(8, 8, 40, sBattleActionFrame, 2);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            Battle_SetBlend(0, 0, 0);
            BattlePalette_StopBgCycle(14, 1);
            *(vu16 *)REG_DISPCNT &= ~0x200;
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile, sBattleActionSavedPaletteSlot, 0);
            actor->animation.flags |= 0x100;
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            actor->animation.flags &= ~0x100;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (sBattleActionFrame <= 29)
        {
            actor->verticalLift = Battle_InterpolateS16(actor->homeY,
                -actor->homeY, 30, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 26:
    case 27:
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 129 的张口打击、效果移动及往返目标纵坐标。
 *  @note 普通 C 的 0x3C8 字节及重定位 100% 匹配；保留两条待机恢复路径和标志位的半字截断。
 *  @address 0x0803029C
 */
u8 BattleAction_UpdateBoss129BiteAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0;
        actor->hitReactionStartFrame = 0;
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (sBattleActionFrame <= 19)
        {
            actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                target->homeY - sBattleActionSavedY, 20, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            actor->homeY = target->homeY;
            Battle_SetBossAnimation(actor, 1, 0x1B4, 13, 0);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 20;
        break;
    case 20:
        if (actor->animation.timelineFrame > 7)
        {
            sBattleActionScratch86E = actor->homeX + 4;
            sBattleActionScratch86F = actor->homeY + 4;
            Battle_SetEffectAnimation(actor, sBattleActionScratch86E,
                sBattleActionScratch86F, 0x27C, 14, 0x332, 5);
            actor->animation.flags |= 0x100;
            actor->effectAnimation.flags |= 0x100;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            actor->animation.flags &= ~0x100;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 31;
        if (actor->animation.timelineFrame > 46)
        {
            sBattleActionFrame = 0;
            actor->effectAnimation.timelineFrame = 32;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame <= 24)
        {
            if (sBattleActionFrame == 15)
                BattleAction_BeginTargetReactions(30);
            actor->effectAnimation.x = Battle_InterpolateS16(sBattleActionScratch86E,
                250 - sBattleActionScratch86E, 25, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 24;
        }
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->animation.flags |= 0x100;
        }
        break;
    case 24:
    {
        u16 paused = actor->animation.flags & 0x100;

        if (paused)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 25;
        }
        else if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 25;
        }
        break;
    }
    case 25:
    {
        u16 loading = actor->animation.flags & BATTLE_ANIM_LOADING;

        if (!loading)
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 26;
        }
        break;
    }
    case 26:
        if (sBattleActionFrame <= 19)
        {
            actor->homeY = Battle_InterpolateS16(target->homeY,
                sBattleActionSavedY - target->homeY, 20, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            actor->homeY = sBattleActionSavedY;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 27:
    case 28:
    case 29:
    case 30:
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Boss 129 的黑紫色冲击与群体受击演出。
 *  @note boss[53]；本轮受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08030664
 */
u8 BattleAction_UpdateBoss129DarkWaveAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0;
        actor->hitReactionStartFrame = 0;
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        Battle_SetBossAnimation(actor, 1, 0x1B4, 13, 0);
        sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 20;
        break;
    case 20:
        if (actor->animation.timelineFrame > 7)
        {
            sBattleActionScratch86E = actor->homeX + 4;
            sBattleActionScratch86F = actor->homeY + 4;
            Battle_SetEffectAnimation(actor, sBattleActionScratch86E, sBattleActionScratch86F,
                0x27C, 14, 0x334, 5);
            actor->animation.flags |= 0x100;
            actor->effectAnimation.flags |= 0x100;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            actor->animation.flags &= ~0x100;
            actor->effectAnimation.flags &= ~0x100;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 31;
        if (actor->animation.timelineFrame > 46)
        {
            sBattleActionFrame = 0;
            actor->effectAnimation.timelineFrame = 40;
            actor->effectAnimation.flags |= 0x100;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame <= 24)
        {
            if (sBattleActionFrame == 15)
                BattleAction_BeginTargetReactions(30);
            actor->effectAnimation.x = Battle_InterpolateS16(sBattleActionScratch86E,
                250 - sBattleActionScratch86E, 25, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 24;
        }
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->animation.flags |= 0x100;
        }
        break;
    case 24:
        if (actor->animation.flags & 0x100)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 25;
        }
        else if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 9;
        }
        break;
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Advance the action selected by boss[54].
 *  @note Seeded GPBA replay stayed at phase 19 with result zero through 450 dispatch updates.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x080309B0
 *  静态收尾：准备伤害与黑色面孔动画，等待 timelineFrame 超过 70 再启动效果；当前动画范围 0..40。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdateBoss129DarkFaceAttackVariant(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_ClearTargetPendingDamage(actor);
        Battle_SetBossAnimation(actor, 5, 0x1B4, 12, 1);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionFrame = 0;
        actor->hitEffectId = 2;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (actor->animation.timelineFrame > 70)
        {
            Battle_SetEffectAnimation(actor, 185, 120, 0x2E0, 14, 0x2F9, 5);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            BattleAction_BeginTargetReactions(90);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (!(u8)Battle_AreCombatantAnimationsLoading())
            sBattleActionEffectPhase = 22;
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->animation.flags |= 0x100;
        }
        break;
    case 22:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetBossAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    sBattleActionFrame++;
    return result;
}

/** @brief Compile-time cleanup ordering for the three defeat-fade variants.
 *  @note No enum object is allocated. Ordering differs in the reference even
 *  where the final state is identical; callers pass constants for full inlining.
 *  @address 0x08030C08, 0x08030D9C, 0x08031714
 */
enum BattleBossDefeatCleanupOrder
{
    BATTLE_BOSS_DEFEAT_CLEANUP_EARLY,
    BATTLE_BOSS_DEFEAT_REMOVE_THEN_CLEAR_ALPHA,
    BATTLE_BOSS_DEFEAT_CLEAR_ALPHA_THEN_REMOVE,
};

/** @brief Apply the shared two-stage boss defeat fade without merging ROM entries.
 *  @note Only the cleanup order varies. The hidden bit remains set, status and
 *  enemy count are untouched, and the completing call clears the combatant id.
 *  Word-sized blendControl and halfword alphaDelta preserve the reference's
 *  argument preparation when this helper is fully inlined by the current agbcc.
 *  @address 0x08030C08..0x08032548
 */
static inline u8 BattleAction_UpdateBossDefeatFadeInline(struct BattleCombatant *combatant,
    u8 cleanupOrder)
{
    u8 result = 0;

    switch (sBattleRemovalEffectPhase)
    {
    case BATTLE_BOSS_DEFEAT_FADE_INIT:
        sBattleActionFrame = 0;
        sBattleEffectBlendEva = 16;
        sBattleEffectBlendEvb = 0;
        combatant->animation.flags |= BATTLE_ANIM_SEMITRANSPARENT;
        Battle_SetBlend(0x710, 16, 0);
        Sound_PlaySfx(0x5A, 0, 0);
        sBattleRemovalEffectPhase = BATTLE_BOSS_DEFEAT_FADE_BACKGROUND;
        break;
    case BATTLE_BOSS_DEFEAT_FADE_BACKGROUND:
        if (sBattleActionFrame < 10)
        {
            u32 blendControl;

            sBattleEffectBlendEvb = Battle_InterpolateS16(0, 16, 10, sBattleActionFrame, 2);
            blendControl = 0x710;
            Battle_SetBlend(blendControl, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleRemovalEffectPhase = BATTLE_BOSS_DEFEAT_FADE_SPRITE;
        }
        break;
    case BATTLE_BOSS_DEFEAT_FADE_SPRITE:
        if (sBattleActionFrame < 20)
        {
            s16 alphaDelta = -16;
            u32 blendControl;

            sBattleEffectBlendEva = Battle_InterpolateS16(16, alphaDelta, 20, sBattleActionFrame, 2);
            blendControl = 0x710;
            Battle_SetBlend(blendControl, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            combatant->animation.flags |= BATTLE_ANIM_HIDDEN;
            sBattleRemovalEffectPhase = BATTLE_BOSS_DEFEAT_FADE_STOP_SOUND;
        }
        break;
    case BATTLE_BOSS_DEFEAT_FADE_STOP_SOUND:
        Sound_StopSfx(0);
        if (cleanupOrder == BATTLE_BOSS_DEFEAT_CLEANUP_EARLY)
        {
            Battle_SetBlend(0, 0, 0);
            combatant->animation.flags &= ~BATTLE_ANIM_SEMITRANSPARENT;
        }
        sBattleRemovalEffectPhase = BATTLE_BOSS_DEFEAT_FADE_COMPLETE;
        break;
    case BATTLE_BOSS_DEFEAT_FADE_COMPLETE:
        if (cleanupOrder != BATTLE_BOSS_DEFEAT_CLEANUP_EARLY)
            Battle_SetBlend(0, 0, 0);
        if (cleanupOrder == BATTLE_BOSS_DEFEAT_REMOVE_THEN_CLEAR_ALPHA)
        {
            combatant->id = BATTLE_NO_COMBATANT;
            combatant->animation.flags &= ~BATTLE_ANIM_SEMITRANSPARENT;
        }
        else
        {
            if (cleanupOrder == BATTLE_BOSS_DEFEAT_CLEAR_ALPHA_THEN_REMOVE)
                combatant->animation.flags &= ~BATTLE_ANIM_SEMITRANSPARENT;
            combatant->id = BATTLE_NO_COMBATANT;
        }
        result = 1;
        break;
    }
    return result;
}

/** @brief Fade out boss id 113.
 *  @note Blending and alpha mode clear in phase three, before the completion update.
 *  @address 0x08030C08
 */
u8 BattleAction_UpdateBoss113Defeat(struct BattleCombatant *combatant)
{
    return BattleAction_UpdateBossDefeatFadeInline(combatant, BATTLE_BOSS_DEFEAT_CLEANUP_EARLY);
}

/** @brief Fade out boss id 114.
 *  @note Completion clears the id before alpha mode.
 *  @address 0x08030D9C
 */
u8 BattleAction_UpdateBoss114Defeat(struct BattleCombatant *combatant)
{
    return BattleAction_UpdateBossDefeatFadeInline(combatant, BATTLE_BOSS_DEFEAT_REMOVE_THEN_CLEAR_ALPHA);
}

/** @brief Fade out boss id 115.
 *  @note Completion clears the id before alpha mode.
 *  @address 0x08030F30
 */
u8 BattleAction_UpdateBoss115Defeat(struct BattleCombatant *combatant)
{
    return BattleAction_UpdateBossDefeatFadeInline(combatant, BATTLE_BOSS_DEFEAT_REMOVE_THEN_CLEAR_ALPHA);
}

/** @brief Fade out boss id 116.
 *  @note Completion clears the id before alpha mode.
 *  @address 0x080310C4
 */
u8 BattleAction_UpdateBoss116Defeat(struct BattleCombatant *combatant)
{
    return BattleAction_UpdateBossDefeatFadeInline(combatant, BATTLE_BOSS_DEFEAT_REMOVE_THEN_CLEAR_ALPHA);
}

/** @brief Fade out boss id 117.
 *  @note Completion clears the id before alpha mode.
 *  @address 0x08031258
 */
u8 BattleAction_UpdateBoss117Defeat(struct BattleCombatant *combatant)
{
    return BattleAction_UpdateBossDefeatFadeInline(combatant, BATTLE_BOSS_DEFEAT_REMOVE_THEN_CLEAR_ALPHA);
}

/** @brief Fade out boss id 118.
 *  @note Completion clears the id before alpha mode.
 *  @address 0x080313EC
 */
u8 BattleAction_UpdateBoss118Defeat(struct BattleCombatant *combatant)
{
    return BattleAction_UpdateBossDefeatFadeInline(combatant, BATTLE_BOSS_DEFEAT_REMOVE_THEN_CLEAR_ALPHA);
}

/** @brief Fade out boss id 119.
 *  @note Completion clears the id before alpha mode.
 *  @address 0x08031580
 */
u8 BattleAction_UpdateBoss119Defeat(struct BattleCombatant *combatant)
{
    return BattleAction_UpdateBossDefeatFadeInline(combatant, BATTLE_BOSS_DEFEAT_REMOVE_THEN_CLEAR_ALPHA);
}

/** @brief Fade out boss id 120.
 *  @note Completion clears alpha mode before the id.
 *  @address 0x08031714
 */
u8 BattleAction_UpdateBoss120Defeat(struct BattleCombatant *combatant)
{
    return BattleAction_UpdateBossDefeatFadeInline(combatant, BATTLE_BOSS_DEFEAT_CLEAR_ALPHA_THEN_REMOVE);
}

/** @brief Fade out boss id 121.
 *  @note Completion clears alpha mode before the id.
 *  @address 0x080318A8
 */
u8 BattleAction_UpdateBoss121Defeat(struct BattleCombatant *combatant)
{
    return BattleAction_UpdateBossDefeatFadeInline(combatant, BATTLE_BOSS_DEFEAT_CLEAR_ALPHA_THEN_REMOVE);
}

/** @brief Fade out boss id 122.
 *  @note Completion clears alpha mode before the id.
 *  @address 0x08031A3C
 */
u8 BattleAction_UpdateBoss122Defeat(struct BattleCombatant *combatant)
{
    return BattleAction_UpdateBossDefeatFadeInline(combatant, BATTLE_BOSS_DEFEAT_CLEAR_ALPHA_THEN_REMOVE);
}

/** @brief Fade out boss ids 123 and 124 through their shared callback.
 *  @note Both ROM table entries point here; completion clears alpha mode first.
 *  @address 0x08031BD0
 */
u8 BattleAction_UpdateBoss123And124Defeat(struct BattleCombatant *combatant)
{
    return BattleAction_UpdateBossDefeatFadeInline(combatant, BATTLE_BOSS_DEFEAT_CLEAR_ALPHA_THEN_REMOVE);
}

/** @brief Fade out boss id 125.
 *  @note Completion clears alpha mode before the id.
 *  @address 0x08031D64
 */
u8 BattleAction_UpdateBoss125Defeat(struct BattleCombatant *combatant)
{
    return BattleAction_UpdateBossDefeatFadeInline(combatant, BATTLE_BOSS_DEFEAT_CLEAR_ALPHA_THEN_REMOVE);
}

/** @brief Fade out boss id 126.
 *  @note Completion clears alpha mode before the id.
 *  @address 0x08031EF8
 */
u8 BattleAction_UpdateBoss126Defeat(struct BattleCombatant *combatant)
{
    return BattleAction_UpdateBossDefeatFadeInline(combatant, BATTLE_BOSS_DEFEAT_CLEAR_ALPHA_THEN_REMOVE);
}

/** @brief Fade out boss id 127.
 *  @note Completion clears alpha mode before the id.
 *  @address 0x0803208C
 */
u8 BattleAction_UpdateBoss127Defeat(struct BattleCombatant *combatant)
{
    return BattleAction_UpdateBossDefeatFadeInline(combatant, BATTLE_BOSS_DEFEAT_CLEAR_ALPHA_THEN_REMOVE);
}

/** @brief Fade out boss id 128.
 *  @note Completion clears alpha mode before the id.
 *  @address 0x08032220
 */
u8 BattleAction_UpdateBoss128Defeat(struct BattleCombatant *combatant)
{
    return BattleAction_UpdateBossDefeatFadeInline(combatant, BATTLE_BOSS_DEFEAT_CLEAR_ALPHA_THEN_REMOVE);
}

/** @brief Fade out boss id 129.
 *  @note Completion clears alpha mode before the id.
 *  @address 0x080323B4
 */
u8 BattleAction_UpdateBoss129Defeat(struct BattleCombatant *combatant)
{
    return BattleAction_UpdateBossDefeatFadeInline(combatant, BATTLE_BOSS_DEFEAT_CLEAR_ALPHA_THEN_REMOVE);
}

/** @brief 执行剑舞：接近目标、播放两段命中反应并跳回原位。
 *  @note 第 33、144 帧分别启动 94、40 帧的目标反应；第 124 帧停止循环音效，所有反应结束后扣除 MP 并返回 1。
 *  @note 动画参数保留 u32 局部量；普通 C 的 0x1E4 字节（含字面池）及已解析重定位 100% 匹配。
 *  GPBA 正常输入回放观察到 335 次调用、两次反应启动和 MP 10 -> 4；往返跳跃均使用目标模式 0。
 *  @address 0x08032548
 */
u8 BattleAction_UpdateSwordDance(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 1;
        sBattleActionSubphase = 0;
        break;
    case 1:
        if ((u8)BattleAction_UpdateSkillJump(actor, target,
            BATTLE_SKILL_JUMP_TARGET_ADJACENT) == 1)
        {
            u32 animationId;
            struct BattleAnimation *animation;
            u32 baseTile;

            if (actor->id == 0)
                animationId = 0x359;
            else
                animationId = 0x368;
            animation = &actor->animation;
            baseTile = 0x1B4;
            Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
            if (target->id <= 10)
                actor->animation.flags |= BATTLE_ANIM_FLIP_X;
            sBattleActionEffectPhase = 2;
        }
        break;
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 3;
        break;
    case 3:
        if (actor->animation.timelineFrame == 0x21)
        {
            Sound_PlaySfx(0x31, 1, 1);
            BattleAction_BeginTargetReactions(0x5E);
        }
        else if (actor->animation.timelineFrame == 0x7C)
            Sound_StopSfx(1);
        else if (actor->animation.timelineFrame == 0x90)
        {
            BattleAction_BeginTargetReactions(0x28);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            sBattleActionSubphase = 12;
            sBattleActionEffectPhase = 8;
        }
        break;
    case 8:
        if ((u8)BattleAction_UpdateSkillJump(actor, target,
            BATTLE_SKILL_JUMP_TARGET_ADJACENT) == 1)
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        if (sBattleHitEffectPhase == 0 && sBattleDodgePhase == 0
            && sBattleHitReactionPhase == 0)
        {
            Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
            result = 1;
        }
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Execute the formation attack between the center jump and shared return jump.
 *  @note All 0x21C bytes and resolved relocations match from ordinary C.
 *  Handler 1 starts target reactions and a BG fade; phase three does not advance the frame counter.
 *  Completion waits for reactions, consumes MP and returns one.
 *  @note Seeded GPBA replay returned 1 after 284 dispatch updates (233 entry hits).
 *  Dispatch update counts include the skill-name window.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x0803272C
 */
u8 BattleAction_UpdateFormationJumpSkill(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;
    u16 loading;
    u16 animationId;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = 1;
        sBattleActionSubphase = 0;
        break;
    case 1:
        if ((u8)BattleAction_UpdateFormationCenterJump(actor, target, 0) == 1)
        {
            animationId = actor->id == 0 ? 0x35C : 0x36B;
            Battle_SetAnimationById(&actor->animation, animationId, 0x1B4, 13, 2);
            actor->hitEffectId = 0x35E;
            actor->hitReactionStartFrame = 0;
            if (target->id <= 10)
                actor->animation.flags |= BATTLE_ANIM_FLIP_X;
            sBattleActionEffectPhase = 2;
        }
        break;
    case 2:
        loading = actor->animation.flags & BATTLE_ANIM_LOADING;
        if (loading == 0)
        {
            Sound_PlaySfx(0x4F, 1, 0);
            BattleAction_BeginTargetReactions(40);
            BattlePalette_StartBgFade(0, 3, 7, 14, 28, 4, 4, -1, 2);
            sBattleActionFrame = loading;
            sBattleActionEffectPhase = 3;
        }
        break;
    case 3:
        if (sBattleActionFrame <= 3)
        {
            BattlePalette_ReturnBgFade(0, 3, 16);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            sBattleActionSubphase = 12;
            sBattleActionEffectPhase = 8;
        }
        break;
    case 8:
        if ((u8)BattleAction_UpdateSkillJump(actor, target, 0) == 1)
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        if (sBattleHitEffectPhase == 0 && sBattleDodgePhase == 0
            && sBattleHitReactionPhase == 0)
        {
            Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
            result = 1;
        }
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进跳向敌方阵型、斩击及返程的技能演出，并扣除所选技能槽 MP。
 *  @note skill[2]；既有受控回放返回 1。普通 C 的 0x42A 字节及 84 项已解析重定位 100% 匹配。
 *  末尾两字节零填充不计；完整 ROM 与 baserom 一致。
 *  @note 三段动画共用 u16 局部编号；阶段 19 不递增帧计数，阶段 26 只在目标反应尚未结束时递增。
 *  接近使用跳跃模式 3，返程使用模式 0；返程坐标取队伍阵型表，阶段 9 扣除 MP 并返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08032948
 */
u8 BattleAction_UpdateFormationLeapStrikeSkill(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;
    u16 animationId;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 1;
        sBattleActionSubphase = 0;
        break;
    case 1:
        if ((u8)BattleAction_UpdateSkillJump(actor, target,
            BATTLE_SKILL_JUMP_ENEMY_CENTER) == 1)
        {
            struct BattleAnimation *animation;
            u32 baseTile;

            animationId = actor->id == 0 ? 0x35F : 0x36E;
            animation = &actor->animation;
            baseTile = 0x1B4;
            Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
            if (target->id <= 10)
                actor->animation.flags |= BATTLE_ANIM_FLIP_X;
            sBattleActionEffectPhase = 2;
        }
        break;
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 18;
        break;
    case 18:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags |= 0x100;
            sBattleActionFrame = 0;
            Sound_PlaySfx(0x31, 1, 0);
            BattlePalette_StartBgFade(0, 3, 7, 14, 28, 4, 4, -1, 2);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (sBattleActionFrame <= 3)
        {
            BattlePalette_ReturnBgFade(0, 3, 16);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
    {
        struct BattleAnimation *animation;
        u32 baseTile;

        animationId = actor->id == 0 ? 0x360 : 0x36F;
        animation = &actor->animation;
        baseTile = 0x1B4;
        Battle_SetAnimationById(animation, animationId, baseTile, 13, 0x402);
        sBattleActionEffectPhase = 21;
        break;
    }
    case 21:
    {
        u16 loading = actor->animation.flags & BATTLE_ANIM_LOADING;

        if (!loading)
        {
            sBattleActionFrame = loading;
            BattlePalette_StartBgFade(0, 3, 7, 14, 28, 4, 4, -1, 2);
            sBattleActionEffectPhase = 22;
            Sound_PlaySfx(0x31, 1, 1);
        }
        break;
    }
    case 22:
        if (sBattleActionFrame <= 19)
        {
            if (sBattleActionFrame == 4)
                BattlePalette_ReturnBgFade(0, 3, 16);
            actor->homeX = Battle_InterpolateS16(sBattleActionScratch86E,
                -112, 20, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            Sound_StopSfx(1);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
    {
        struct BattleAnimation *animation;
        u32 baseTile;

        animationId = actor->id == 0 ? 0x361 : 0x370;
        animation = &actor->animation;
        baseTile = 0x1B4;
        Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
        sBattleActionEffectPhase = 24;
        break;
    }
    case 24:
    {
        u16 loading = actor->animation.flags & BATTLE_ANIM_LOADING;

        if (!loading)
        {
            BattleAction_BeginTargetReactions(20);
            sBattleActionFrame = loading;
            sBattleActionEffectPhase = 25;
        }
        break;
    }
    case 25:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags |= 0x100;
            sBattleActionFrame = 0;
            sBattleActionScratch86E = actor->homeX;
            sBattleActionSavedX = gBattlePartyHomeX[actor->formationSlot];
            sBattleActionSavedY = gBattlePartyHomeY[actor->formationSlot];
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            sBattleActionSubphase = 12;
            sBattleActionEffectPhase = 27;
        }
        else
            sBattleActionFrame++;
        break;
    case 27:
        if ((u8)BattleAction_UpdateSkillJump(actor, target,
            BATTLE_SKILL_JUMP_TARGET_ADJACENT) == 1)
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 为施法者播放 30% ATC 增益演出，恢复待机后扣除 MP。
 *  @note 增益演出只接收 actor；id 为零时动画基址为 0x362，否则为 0x371。
 *  阶段依次为 0、19、20、6、9；阶段 9 每次调用均扣除所选技能 MP 并返回 2。
 *  @note 普通 C 的 0x12A 字节及已解析重定位 100% 匹配，末尾两字节零填充不计；完整 ROM 与 baserom 一致。
 *  @address 0x08032D74
 */
u8 BattleAction_UpdateSelfAtcBoostSkill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
    {
        u16 animationId;

        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        animationId = actor->id == 0 ? 0x362 : 0x371;
        Battle_BeginStatBoost(BATTLE_MODIFIER_ATC, 30, animationId);
        sBattleActionEffectPhase = 19;
        break;
    }
    case 19:
        if (Battle_UpdateEnemyStatBoost(actor) == 1)
            sBattleActionEffectPhase = 20;
        break;
    case 20:
        Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
            sBattleActionSavedPaletteSlot, 0);
        sBattleActionEffectPhase = 6;
        break;
    case 6:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    return result;
}

/** @brief 播放龙系施法与背景演出，为列出的队伍成员设置一次魔法防护。
 *  @note 动画 0x374、0x375、0x376 后播放背景 0；复制首目标的 0x37E 特效，结束时清除特效位并设置魔法防护位，阶段 9 扣除 MP 并返回 2。
 *  @note 两轮循环共用目标指针，清位和置位分步写入；按原地址独立链接，普通 C 的 0x616 字节及已解析重定位 100% 匹配，末尾两字节零填充不计。
 *  @address 0x08032EA0
 */
u8 BattleAction_UpdateSideMagicGuardSkill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;
    struct BattleCombatant *combatant;

    struct BattleCombatant *combatants = Battle_GetCombatants();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionFrame = 0;
        sBattleEffectCombatantCount = Battle_ListCombatants(combatants,
            sBattleEffectCombatantSlots, BATTLE_SIDE_PARTY, 0x7F);
        sBattleActionEffectPhase = 38;
        break;
    case 38:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x374;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
        sBattleActionEffectPhase = 39;
        break;
    }
    case 39:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x31, 0, 0);
            sBattleActionEffectPhase = 40;
        }
        break;
    case 40:
        if (actor->animation.timelineFrame == 0x3A)
            Sound_PlaySfx(0x58, 0, 0);
        if (actor->animation.timelineFrame == 0x87)
            Sound_PlaySfx(0x31, 0, 0);
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            struct BattleAnimation *animation = &actor->animation;
            u32 animationId = 0x375;
            u32 baseTile = 0x1B4;

            Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
            sBattleActionEffectPhase = 41;
        }
        break;
    case 41:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 42;
        break;
    case 42:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            struct BattleAnimation *animation = &actor->animation;
            u32 animationId = 0x376;
            u32 baseTile = 0x1B4;

            Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
            sBattleActionEffectPhase = 43;
        }
        break;
    case 43:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 44;
        break;
    case 44:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 45;
        }
        break;
    case 45:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 46;
        break;
    case 46:
        Battle_ResetBackgroundLoader();
        sBattleEffectBlendEva = 0;
        Battle_SetBlend(0x1747, 0, 16);
        sBattleActionEffectPhase = 18;
        break;
    case 18:
        if (Battle_UpdateBackgroundLoader(0, 3, 14, 1))
        {
            sBattleActionFrame = 0;
            BattlePalette_StartBgFade(0, 3, 7, 14, 28, 4, 4, -1, 2);
            Sound_PlaySfx(0x20, 0, 0);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (sBattleActionFrame < 20)
        {
            if (sBattleActionFrame == 4)
                BattlePalette_ReturnBgFade(0, 3, 16);
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame < 10)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 10,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (sBattleActionFrame < 70)
            sBattleActionFrame++;
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame < 10)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(0, 16, 10,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame < 20)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            struct BattleCombatant *first;

            sBattleActionFrame = 0;
            Battle_SetBgRequests(0x20);
            first = &combatants[sBattleEffectCombatantSlots[0]];
            Battle_SetEffectAnimation(first, first->homeX, first->homeY,
                0x1B4, 14, 0x37E, 4);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (!(combatants[sBattleEffectCombatantSlots[0]].effectAnimation.flags
            & BATTLE_ANIM_LOADING))
        {
            u8 index;

            for (index = 1; index < sBattleEffectCombatantCount; index++)
            {
                combatant = &combatants[sBattleEffectCombatantSlots[index]];

                combatant->effectAnimation = combatants[sBattleEffectCombatantSlots[0]].effectAnimation;
                combatant->displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
                combatant->effectAnimation.x = combatant->homeX;
                combatant->effectAnimation.y = combatant->homeY;
            }
            Sound_PlaySfx(0x58, 0, 0);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (combatants[sBattleEffectCombatantSlots[0]].effectAnimation.flags
            & BATTLE_ANIM_FINISHED)
        {
            u8 index;

            BattlePalette_ReleaseObjBanks(actor->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->effectAnimation));
            for (index = 0; index < sBattleEffectCombatantCount; index++)
            {
                combatant = &combatants[sBattleEffectCombatantSlots[index]];

                combatant->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
                combatant->displayState |= BATTLE_DISPLAY_MAGIC_GUARD;
            }
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        Battle_SetBlend(0, 0, 0);
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进大型红龙群体攻击演出，并扣除所选技能槽 MP。
 *  @note skill[5]；动画 0x374、0x375、0x376 后播放背景 1，背景退场时设置命中特效 0xAC 并启动 40 帧目标反应，反应结束后扣除 MP 并返回 1；第二参数未使用。
 *  @note 普通 C 的 0x4D0 字节及已解析重定位 100% 匹配；普通字节数组保留参考栈帧中未访问的 8 字节空间。
 *  既有受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x080334B8
 */
u8 BattleAction_UpdateRedDragonSkill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;
    u8 reservedStack[8];

    Battle_GetCombatants();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = 38;
        break;
    case 38:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x374;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
        sBattleActionEffectPhase = 39;
        break;
    }
    case 39:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x31, 0, 0);
            sBattleActionEffectPhase = 40;
        }
        break;
    case 40:
        if (actor->animation.timelineFrame == 0x3A)
            Sound_PlaySfx(0x58, 0, 0);
        if (actor->animation.timelineFrame == 0x87)
            Sound_PlaySfx(0x31, 0, 0);
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            struct BattleAnimation *animation = &actor->animation;
            u32 animationId = 0x375;
            u32 baseTile = 0x1B4;

            Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
            sBattleActionEffectPhase = 41;
        }
        break;
    case 41:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 42;
        break;
    case 42:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            struct BattleAnimation *animation = &actor->animation;
            u32 animationId = 0x376;
            u32 baseTile = 0x1B4;

            Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
            sBattleActionEffectPhase = 43;
        }
        break;
    case 43:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 44;
        break;
    case 44:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 45;
        }
        break;
    case 45:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 46;
        break;
    case 46:
        Battle_ResetBackgroundLoader();
        sBattleEffectBlendEva = 0;
        Battle_SetBlend(0x1747, 0, 16);
        sBattleActionEffectPhase = 18;
        break;
    case 18:
        if (Battle_UpdateBackgroundLoader(1, 3, 14, 1))
        {
            sBattleActionFrame = 0;
            BattlePalette_StartBgFade(0, 3, 28, 14, 7, 4, 4, -1, 2);
            Sound_PlaySfx(0x20, 0, 0);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (sBattleActionFrame < 20)
        {
            if (sBattleActionFrame == 4)
                BattlePalette_ReturnBgFade(0, 3, 16);
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame < 10)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 10,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (sBattleActionFrame < 70)
            sBattleActionFrame++;
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame < 10)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(0, 16, 10,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame < 20)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Battle_SetBgRequests(0x20);
            actor->hitEffectId = 0xAC;
            actor->hitReactionStartFrame = 0;
            BattleAction_BeginTargetReactions(40);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            sBattleActionEffectPhase = 9;
        else
            sBattleActionFrame++;
        break;
    case 9:
        Battle_SetBlend(0, 0, 0);
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 播放龙系施法与背景演出，为选中阵营的各目标回复最多 999 HP。
 *  @note 背景 2 退场后调用同侧回复模式 1、HP 效果 0、数值 999；阶段 9 扣除 MP 并返回 2，每帧继续更新目标反应。
 *  @note 普通字节数组保留参考栈帧中未访问的 8 字节局部空间；按原地址独立链接，普通 C 的 0x4A4 字节及已解析重定位 100% 匹配。
 *  @address 0x08033988
 */
u8 BattleAction_UpdateSideHp999Skill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;
    u8 reservedStack[8];

    Battle_GetCombatants();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        Battle_ResetEffectPhase();
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 38;
        break;
    case 38:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x374;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
        sBattleActionEffectPhase = 39;
        break;
    }
    case 39:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x31, 0, 0);
            sBattleActionEffectPhase = 40;
        }
        break;
    case 40:
        if (actor->animation.timelineFrame == 0x3A)
            Sound_PlaySfx(0x58, 0, 0);
        if (actor->animation.timelineFrame == 0x87)
            Sound_PlaySfx(0x31, 0, 0);
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            struct BattleAnimation *animation = &actor->animation;
            u32 animationId = 0x375;
            u32 baseTile = 0x1B4;

            Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
            sBattleActionEffectPhase = 41;
        }
        break;
    case 41:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 42;
        break;
    case 42:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            struct BattleAnimation *animation = &actor->animation;
            u32 animationId = 0x376;
            u32 baseTile = 0x1B4;

            Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
            sBattleActionEffectPhase = 43;
        }
        break;
    case 43:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 44;
        break;
    case 44:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 45;
        }
        break;
    case 45:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 46;
        break;
    case 46:
        Battle_ResetBackgroundLoader();
        sBattleEffectBlendEva = 0;
        Battle_SetBlend(0x1747, 0, 16);
        sBattleActionEffectPhase = 18;
        break;
    case 18:
        if (Battle_UpdateBackgroundLoader(2, 3, 14, 1))
        {
            sBattleActionFrame = 0;
            BattlePalette_StartBgFade(0, 3, 7, 14, 28, 4, 4, -1, 2);
            Sound_PlaySfx(0x20, 0, 0);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (sBattleActionFrame < 20)
        {
            if (sBattleActionFrame == 4)
                BattlePalette_ReturnBgFade(0, 3, 16);
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame < 10)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 10,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (sBattleActionFrame < 70)
            sBattleActionFrame++;
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame < 10)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(0, 16, 10,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame < 20)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Battle_SetBgRequests(0x20);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if ((u8)Battle_UpdateTargetRecoveryEffect(actor, Battle_GetCombatants(),
            BATTLE_RECOVERY_SIDE_TARGETS, BATTLE_RECOVERY_HP, 999) == 1)
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        Battle_SetBlend(0, 0, 0);
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进灰白龙演出并驱离符合条件的敌人，扣除所选技能槽 MP 后返回 2。
 *  @note skill[7]；既有受控回放返回 2。普通 C 的 0x614 字节及 128 项已解析重定位 100% 匹配，完整 ROM 与 baserom 一致。
 *  @note 敌方列表使用状态掩码 0x7F，控制标志 0x220 会跳过驱离效果；循环索引保留 u8 截断。
 *  阶段 27 依次写入 id 0xFF、逃离状态 7 并递减敌人数；阶段 28 等待 40 帧后清除特效标志 0x100。
 *  第二参数未使用，每次调用仍更新目标反应。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08033E2C
 */
u8 BattleAction_UpdateDragonEnemyRetreatSkill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;
    struct BattleCombatant *combatants = Battle_GetCombatants();

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionFrame = 0;
        sBattleEffectCombatantCount = Battle_ListCombatants(combatants,
            sBattleEffectCombatantSlots, BATTLE_SIDE_ENEMY, 0x7F);
        sBattleActionEffectPhase = 38;
        break;
    case 38:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x374;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
        sBattleActionEffectPhase = 39;
        break;
    }
    case 39:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x31, 0, 0);
            sBattleActionEffectPhase = 40;
        }
        break;
    case 40:
        if (actor->animation.timelineFrame == 0x3A)
            Sound_PlaySfx(0x58, 0, 0);
        if (actor->animation.timelineFrame == 0x87)
            Sound_PlaySfx(0x31, 0, 0);
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            struct BattleAnimation *animation = &actor->animation;
            u32 animationId = 0x375;
            u32 baseTile = 0x1B4;

            Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
            sBattleActionEffectPhase = 41;
        }
        break;
    case 41:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 42;
        break;
    case 42:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            struct BattleAnimation *animation = &actor->animation;
            u32 animationId = 0x376;
            u32 baseTile = 0x1B4;

            Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
            sBattleActionEffectPhase = 43;
        }
        break;
    case 43:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 44;
        break;
    case 44:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 45;
        }
        break;
    case 45:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 46;
        break;
    case 46:
        Battle_ResetBackgroundLoader();
        sBattleEffectBlendEva = 0;
        Battle_SetBlend(0x1747, 0, 16);
        sBattleActionEffectPhase = 18;
        break;
    case 18:
        if (Battle_UpdateBackgroundLoader(3, 3, 14, 1))
        {
            sBattleActionFrame = 0;
            BattlePalette_StartBgFade(0, 3, 7, 14, 28, 4, 4, -1, 2);
            Sound_PlaySfx(0x20, 0, 0);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (sBattleActionFrame < 20)
        {
            if (sBattleActionFrame == 4)
                BattlePalette_ReturnBgFade(0, 3, 16);
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame < 10)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 10,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (sBattleActionFrame < 70)
            sBattleActionFrame++;
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame < 10)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(0, 16, 10,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame < 20)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1747, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Battle_SetBgRequests(0x20);
            if (Battle_GetControlFlags() & 0x220)
                sBattleActionEffectPhase = 9;
            else
                sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        Battle_SetEffectAnimation(actor, 60, 115, 0x1B4, 14, 0x37F, 0x14);
        actor->effectAnimation.priority = 0;
        Battle_SetBlend(0x410, 0, 7);
        sBattleActionEffectPhase = 25;
        break;
    case 25:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 0, 0);
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            Battle_SetEffectAnimation(actor, 60, 115, 0x1B4, 14, 0x380, 0x114);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            u8 index;

            for (index = 0; index < sBattleEffectCombatantCount; index++)
            {
                struct BattleCombatant *combatant = combatants + sBattleEffectCombatantSlots[index];

                combatant->id = BATTLE_NO_COMBATANT;
                combatant->status = BATTLE_STATUS_ESCAPED;
                Battle_DecrementEnemyCount();
            }
            sBattleActionEffectPhase = 28;
        }
        break;
    case 28:
        if (sBattleActionFrame <= 39)
            sBattleActionFrame++;
        else
        {
            actor->effectAnimation.flags &= ~0x100;
            sBattleActionEffectPhase = 29;
        }
        break;
    case 29:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->effectAnimation));
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        Battle_SetBlend(0, 0, 0);
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 播放施法动画并为施法者选中的目标回复最多 50 点 HP。
 *  @note 动画 0x386 完成后释放调色板并恢复待机；阶段 8 执行回复，阶段 9 扣除 MP 并返回 2。
 *  @note 施法参数保留 u32 局部量，回复结果按 u8 比较；普通 C 的 0x16C 字节（含字面池）及已解析重定位 100% 匹配。
 *  @address 0x08034440
 */
u8 BattleAction_UpdateSelectedHp50Skill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case BATTLE_SIMPLE_RECOVERY_INIT:
        Battle_ResetEffectPhase();
        sBattleActionEffectPhase = BATTLE_SIMPLE_RECOVERY_START_CAST;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        break;
    case BATTLE_SIMPLE_RECOVERY_START_CAST:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x386;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
        sBattleActionEffectPhase = BATTLE_SIMPLE_RECOVERY_WAIT_CAST_GRAPHICS;
        break;
    }
    case BATTLE_SIMPLE_RECOVERY_WAIT_CAST_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x3E, 1, 0);
            sBattleActionEffectPhase = BATTLE_SIMPLE_RECOVERY_WAIT_CAST_FINISHED;
        }
        break;
    case BATTLE_SIMPLE_RECOVERY_WAIT_CAST_FINISHED:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = BATTLE_SIMPLE_RECOVERY_WAIT_IDLE_GRAPHICS;
        }
        break;
    case BATTLE_SIMPLE_RECOVERY_WAIT_IDLE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_SIMPLE_RECOVERY_APPLY;
        break;
    case BATTLE_SIMPLE_RECOVERY_APPLY:
        if ((u8)Battle_UpdateTargetRecoveryEffect(actor, Battle_GetCombatants(),
            BATTLE_RECOVERY_SELECTED_TARGET, BATTLE_RECOVERY_HP, 50) == 1)
            sBattleActionEffectPhase = BATTLE_SIMPLE_RECOVERY_COMPLETE;
        break;
    case BATTLE_SIMPLE_RECOVERY_COMPLETE:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    return result;
}

/** @brief 播放施法动画并解除选中目标的非战败异常状态。
 *  @note 动画 0x386 完成后恢复待机；恢复选择器 11 的参数 30 不用于回复 HP，也不复活状态 8，阶段 9 扣除 MP 并返回 2。
 *  @note 施法参数保留 u32 局部量，恢复结果按 u8 比较；普通 C 的 0x16C 字节及已解析重定位 100% 匹配。
 *  @address 0x080345AC
 */
u8 BattleAction_UpdateStatusCureSkill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case BATTLE_SIMPLE_RECOVERY_INIT:
        Battle_ResetEffectPhase();
        sBattleActionEffectPhase = BATTLE_SIMPLE_RECOVERY_START_CAST;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        break;
    case BATTLE_SIMPLE_RECOVERY_START_CAST:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x386;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
        sBattleActionEffectPhase = BATTLE_SIMPLE_RECOVERY_WAIT_CAST_GRAPHICS;
        break;
    }
    case BATTLE_SIMPLE_RECOVERY_WAIT_CAST_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x3E, 1, 0);
            sBattleActionEffectPhase = BATTLE_SIMPLE_RECOVERY_WAIT_CAST_FINISHED;
        }
        break;
    case BATTLE_SIMPLE_RECOVERY_WAIT_CAST_FINISHED:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = BATTLE_SIMPLE_RECOVERY_WAIT_IDLE_GRAPHICS;
        }
        break;
    case BATTLE_SIMPLE_RECOVERY_WAIT_IDLE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_SIMPLE_RECOVERY_APPLY;
        break;
    case BATTLE_SIMPLE_RECOVERY_APPLY:
        if ((u8)Battle_UpdateTargetRecoveryEffect(actor, Battle_GetCombatants(),
            BATTLE_RECOVERY_SELECTED_TARGET, BATTLE_RECOVERY_CURE_STATUS, 30) == 1)
            sBattleActionEffectPhase = BATTLE_SIMPLE_RECOVERY_COMPLETE;
        break;
    case BATTLE_SIMPLE_RECOVERY_COMPLETE:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    return result;
}

/** @brief 播放施法动画并为传入目标增加 30% ATC 修正。
 *  @note 动画 0x386 完成后恢复施法者待机；阶段 18 播放目标加成效果 0x3AD，阶段 9 扣除 MP 并返回 2。
 *  @note 施法参数使用 u32 局部量，加成结果按 u8 比较；普通 C 的 0x190 字节及已解析重定位 100% 匹配。
 *  @address 0x08034718
 */
u8 BattleAction_UpdateTargetAtcBoostSkill(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        Battle_BeginStatBoost(BATTLE_MODIFIER_ATC, 30, 0x3AD);
        sBattleActionEffectPhase = 1;
        break;
    case 1:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x386;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
        sBattleActionEffectPhase = 2;
        break;
    }
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x3E, 1, 0);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 6;
        }
        break;
    case 6:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 18;
        break;
    case 18:
        if ((u8)Battle_UpdatePartyStatBoost(actor, target) == 1)
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    return result;
}

/** @brief Advance the action selected by skill[11].
 *  @note Seeded GPBA replay returned 2 after 424 dispatch updates (373 entry hits).
 *  Dispatch update counts include the skill-name window.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x080348A8
 *  静态收尾：列出敌方单位并播放区域驱离效果；普通遭遇中写状态 7、清空 id、递减敌人数，消耗 MP 后返回 2。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdateAreaEnemyRetreatSkill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;
    struct BattleCombatant *combatants = Battle_GetCombatants();

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionEffectPhase = 1;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleEffectCombatantCount = Battle_ListCombatants(combatants,
            sBattleEffectCombatantSlots, BATTLE_SIDE_ENEMY, 0x7F);
        break;
    case 1:
        Battle_SetAnimationById(&actor->animation, 0x386, 0x1B4, 13, 2);
        sBattleActionEffectPhase = 2;
        break;
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x3E, 1, 0);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 6;
        }
        break;
    case 6:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            if (Battle_GetControlFlags() & 0x220)
                sBattleActionEffectPhase = 9;
            else
                sBattleActionEffectPhase = 24;
        }
        break;
    case 3:
    case 4:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 18:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
        break;
    case 24:
        Battle_SetEffectAnimation(actor, 60, 115, 0x1B4, 14, 0x37F, 0x14);
        actor->effectAnimation.priority = 0;
        Battle_SetBlend(0x410, 0, 7);
        sBattleActionEffectPhase = 25;
        break;
    case 25:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 0, 0);
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->effectAnimation));
            Battle_SetEffectAnimation(actor, 60, 115, 0x1B4, 14, 0x380, 0x114);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            u8 index;

            for (index = 0; index < sBattleEffectCombatantCount; index++)
            {
                struct BattleCombatant *combatant = combatants + sBattleEffectCombatantSlots[index];

                combatant->id = BATTLE_NO_COMBATANT;
                combatant->status = BATTLE_STATUS_ESCAPED;
                Battle_DecrementEnemyCount();
            }
            sBattleActionEffectPhase = 28;
        }
        break;
    case 28:
        if (sBattleActionFrame <= 39)
            sBattleActionFrame++;
        else
        {
            actor->effectAnimation.flags &= ~0x100;
            sBattleActionEffectPhase = 29;
        }
        break;
    case 29:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->effectAnimation));
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    return result;
}

/** @brief 播放催眠曲，对正常、中毒或麻痹的敌人逐个尝试施加睡眠。
 *  @note 敌方列表使用状态掩码 7；每个目标在 random % 100 <= 39 时施加状态 3，阶段 9 扣除 MP 并返回 2。
 *  @note 目标列表最多七个槽位，计数和索引保留 u8 截断；普通 C 的 0x198 字节及已解析重定位 100% 匹配。
 *  GPBA 固定种子回放在 257 次派发更新（含技能名窗口）、206 次入口调用后返回 2。
 *  @address 0x08034BFC
 */
u8 BattleAction_UpdateSleepSong(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;
    struct BattleCombatant *combatants = Battle_GetCombatants();

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionEffectPhase = 1;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        break;
    case 1:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x386;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
        sBattleActionEffectPhase = 2;
        break;
    }
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x3E, 1, 0);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 6;
        }
        break;
    case 6:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 8;
        break;
    case 8:
    {
        u8 slots[BATTLE_ENEMY_COUNT];
        u8 count = Battle_ListCombatants(combatants, slots, BATTLE_SIDE_ENEMY, 7);
        u8 index;

        for (index = 0; index < count; index++)
        {
            if (Battle_NextRandom() % 100 <= 39)
                Battle_ApplyStatus(&combatants[slots[index]], BATTLE_STATUS_ASLEEP);
        }
        sBattleActionEffectPhase = 9;
        break;
    }
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    return result;
}

/** @brief 播放施法动画并为选中阵营的各目标回复最多 60 点 HP。
 *  @note 动画 0x386 完成后恢复待机；回复模式 1 保留助手的 targetSlot <= 10 阵营判定，阶段 9 扣除 MP 并返回 2。
 *  @note 施法参数使用 u32 局部量，回复结果按 u8 比较；普通 C 的 0x16C 字节及已解析重定位 100% 匹配。
 *  @address 0x08034D94
 */
u8 BattleAction_UpdateSideHp60Skill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case BATTLE_SIMPLE_RECOVERY_INIT:
        Battle_ResetEffectPhase();
        sBattleActionEffectPhase = BATTLE_SIMPLE_RECOVERY_START_CAST;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        break;
    case BATTLE_SIMPLE_RECOVERY_START_CAST:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x386;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
        sBattleActionEffectPhase = BATTLE_SIMPLE_RECOVERY_WAIT_CAST_GRAPHICS;
        break;
    }
    case BATTLE_SIMPLE_RECOVERY_WAIT_CAST_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x3E, 1, 0);
            sBattleActionEffectPhase = BATTLE_SIMPLE_RECOVERY_WAIT_CAST_FINISHED;
        }
        break;
    case BATTLE_SIMPLE_RECOVERY_WAIT_CAST_FINISHED:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = BATTLE_SIMPLE_RECOVERY_WAIT_IDLE_GRAPHICS;
        }
        break;
    case BATTLE_SIMPLE_RECOVERY_WAIT_IDLE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_SIMPLE_RECOVERY_APPLY;
        break;
    case BATTLE_SIMPLE_RECOVERY_APPLY:
        if ((u8)Battle_UpdateTargetRecoveryEffect(actor, Battle_GetCombatants(),
            BATTLE_RECOVERY_SIDE_TARGETS, BATTLE_RECOVERY_HP, 60) == 1)
            sBattleActionEffectPhase = BATTLE_SIMPLE_RECOVERY_COMPLETE;
        break;
    case BATTLE_SIMPLE_RECOVERY_COMPLETE:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    return result;
}

/** @brief Advance the action selected by skill[14].
 *  @note Seeded GPBA replay returned 1 after 303 dispatch updates (252 entry hits).
 *  Dispatch update counts include the skill-name window.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x08034F00
 *  静态收尾：推进 Thunderbolt 的施法、命中动画和目标反应，消耗选中技能槽 MP。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdateThunderboltSkill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = 1;
        actor->hitEffectId = 0x38B;
        actor->hitReactionStartFrame = 35;
        sBattleActionFrame = 0;
        break;
    case 1:
        Battle_SetAnimationById(&actor->animation, 0x387, 0x1B4, 13, 2);
        sBattleActionEffectPhase = 2;
        break;
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 1, 0);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 3:
    case 4:
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetAnimationById(&actor->animation, 0x388, 0x1B4, 13, 2);
            sBattleActionEffectPhase = 18;
        }
        break;
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            BattleAction_BeginTargetReactions(50);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (!(u8)Battle_AreCombatantAnimationsLoading())
        {
            Sound_PlaySfx(0x66, 1, 0);
            sBattleActionEffectPhase = 20;
        }
        sBattleActionFrame++;
        break;
    case 20:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 6;
        }
        sBattleActionFrame++;
        break;
    case 6:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 7:
    case 8:
        break;
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Advance the action selected by skill[15].
 *  @note Seeded GPBA replay returned 1 after 288 dispatch updates (237 entry hits).
 *  Dispatch update counts include the skill-name window.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x08035130
 *  静态收尾：推进 Riot 的施法、命中动画和目标反应，消耗选中技能槽 MP。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdateRiotSkill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = 1;
        actor->hitEffectId = 0x38C;
        actor->hitReactionStartFrame = 20;
        sBattleActionFrame = 0;
        break;
    case 1:
        Battle_SetAnimationById(&actor->animation, 0x387, 0x1B4, 13, 2);
        sBattleActionEffectPhase = 2;
        break;
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 1, 0);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 3:
    case 4:
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetAnimationById(&actor->animation, 0x388, 0x1B4, 13, 2);
            sBattleActionEffectPhase = 18;
        }
        break;
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            BattleAction_BeginTargetReactions(50);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (!(u8)Battle_AreCombatantAnimationsLoading())
        {
            Sound_PlaySfx(0x66, 1, 0);
            sBattleActionEffectPhase = 20;
        }
        sBattleActionFrame++;
        break;
    case 20:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 6;
        }
        sBattleActionFrame++;
        break;
    case 6:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 7:
    case 8:
        break;
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进雷电效果位移、背景调色板脉冲与淡变，完成目标反应后恢复待机并消耗 MP。
 *  @note 施法动画 0x387/0x388 使用调色板 13；效果 0x38D..0x390 使用调色板 14，完成返回 1，第二参数未使用。
 *  @note 普通 C 的 0x50C 字节及已解析重定位 100% 匹配；施法参数使用 u32 局部量，阶段 22 的加载标志保存为 u16，保留 38 项阶段表。
 *  @address 0x08035360
 */
u8 BattleAction_UpdateLightningBgPulseSkill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = 1;
        actor->hitEffectId = 0x391;
        actor->hitReactionStartFrame = 0;
        sBattleActionFrame = 0;
        break;
    case 1:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x387;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
        sBattleActionEffectPhase = 2;
        break;
    }
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 1, 0);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 3:
    case 4:
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            {
                u32 animationId = 0x388;
                u32 baseTile = 0x1B4;

                Battle_SetAnimationById(&actor->animation, animationId, baseTile, 13, 2);
            }
            sBattleActionEffectPhase = 18;
        }
        break;
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        Battle_SetEffectAnimation(actor, 80, 100, 0x27C, 14, 0x38D, 5);
        actor->effectAnimation.priority = 0;
        sBattleActionEffectPhase = 20;
        break;
    case 20:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 21;
        break;
    case 21:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->effectAnimation));
            Battle_SetEffectAnimation(actor, 80, 100, 0x27C, 14, 0x38E, 0x405);
            actor->effectAnimation.priority = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
    {
        u16 loading = actor->effectAnimation.flags & BATTLE_ANIM_LOADING;

        if (!loading)
        {
            Sound_PlaySfx(0x66, 1, 1);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    }
    case 23:
        if (sBattleActionFrame <= 29)
        {
            actor->effectAnimation.y = Battle_InterpolateS16(100, -130, 30, sBattleActionFrame, 1);
            sBattleActionFrame++;
        }
        else
            sBattleActionEffectPhase = 24;
        break;
    case 24:
        Battle_SetEffectAnimation(actor, 120, 80, 0x27C, 14, 0x390, 0x405);
        actor->effectAnimation.priority = 0;
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 25;
        break;
    case 25:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x69, 1, 0);
            BattlePalette_StartBgPulse(0, 3, 15, 15, 2, 4, 4, 2);
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (sBattleActionFrame <= 59)
            sBattleActionFrame++;
        else
        {
            BattlePalette_StopBgPulse(0, 3);
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        Battle_SetEffectAnimation(actor, 80, 120, 0x27C, 14, 0x38F, 5);
        actor->effectAnimation.priority = 0;
        sBattleActionEffectPhase = 28;
        Sound_StopSfx(1);
        break;
    case 28:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x4F, 1, 0);
            sBattleActionEffectPhase = 29;
        }
        break;
    case 29:
        if (actor->effectAnimation.timelineFrame > 14)
        {
            sBattleActionFrame = 0;
            BattleAction_BeginTargetReactions(40);
            BattlePalette_StartBgFade(0, 3, 15, 15, 2, 4, 4, -1, 2);
            sBattleActionEffectPhase = 30;
        }
        break;
    case 30:
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            BattlePalette_ReturnBgFade(0, 3, 16);
            sBattleActionFrame++;
            sBattleActionEffectPhase = 31;
        }
        break;
    case 31:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->effectAnimation));
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 32;
        }
        sBattleActionFrame++;
        break;
    case 32:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 33;
        }
        break;
    case 33:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 6:
    case 7:
    case 8:
    case 34:
    case 35:
    case 36:
    case 37:
        break;
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Execute skill-handler seventeen with a forty-percent status-two attempt.
 *  @note Only valid targets draw RNG values; entries are reread after RNG.
 *  Keep these five bodies separate: inlining their shared byte-returning body
 *  adds a return truncation under current agbcc. Result two ends the action.
 *  @address 0x0803586C
 */
u8 BattleAction_UpdateStatus2Skill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case BATTLE_STATUS_SKILL_INIT:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = BATTLE_STATUS_SKILL_START_CAST;
        actor->hitEffectId = 0x392;
        actor->hitReactionStartFrame = 35;
        sBattleActionFrame = 0;
        break;
    case BATTLE_STATUS_SKILL_START_CAST:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x387;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 12, 2);
        sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_CAST_GRAPHICS;
        break;
    }
    case BATTLE_STATUS_SKILL_WAIT_CAST_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 1, 0);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_CAST_FINISHED;
        }
        break;
    case BATTLE_STATUS_SKILL_WAIT_CAST_FINISHED:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            {
                u32 animationId = 0x388;
                u32 baseTile = 0x1B4;

                Battle_SetAnimationById(&actor->animation, animationId, baseTile, 12, 2);
            }
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_RELEASE_GRAPHICS;
        }
        break;
    case BATTLE_STATUS_SKILL_WAIT_RELEASE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            BattleAction_BeginTargetReactions(50);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_ALL_GRAPHICS;
        }
        break;
    case BATTLE_STATUS_SKILL_WAIT_ALL_GRAPHICS:
        if ((u8)Battle_AreCombatantAnimationsLoading() == 0)
        {
            Sound_PlaySfx(0x66, 1, 0);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_REACTIONS;
        }
        sBattleActionFrame++;
        break;
    case BATTLE_STATUS_SKILL_WAIT_REACTIONS:
        if (sBattleHitEffectPhase == 0 && sBattleDodgePhase == 0 && sBattleHitReactionPhase == 0)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_IDLE_GRAPHICS;
        }
        sBattleActionFrame++;
        break;
    case BATTLE_STATUS_SKILL_WAIT_IDLE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_APPLY;
        break;
    case BATTLE_STATUS_SKILL_APPLY:
    {
        struct BattleCombatant *combatants = Battle_GetCombatants();
        u8 index = 0;

        for (; index < sBattleActionEffectTargetCount; index++)
        {
            if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK) == BATTLE_TARGET_VALID
                && Battle_NextRandom() % 100 <= 39)
            {
                Battle_ApplyStatus(&combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK], 2);
            }
        }
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Execute skill-handler eighteen, with a forty-percent status-three attempt.
 *  @note Uses hit-effect 0x393 and the common fifty-frame target reactions.
 *  @address 0x08035B04
 */
u8 BattleAction_UpdateStatus3Skill(struct BattleCombatant *actor, struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case BATTLE_STATUS_SKILL_INIT:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = BATTLE_STATUS_SKILL_START_CAST;
        actor->hitEffectId = 0x393;
        actor->hitReactionStartFrame = 35;
        sBattleActionFrame = 0;
        break;
    case BATTLE_STATUS_SKILL_START_CAST:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x387;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 12, 2);
        sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_CAST_GRAPHICS;
        break;
    }
    case BATTLE_STATUS_SKILL_WAIT_CAST_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 1, 0);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_CAST_FINISHED;
        }
        break;
    case BATTLE_STATUS_SKILL_WAIT_CAST_FINISHED:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            {
                u32 animationId = 0x388;
                u32 baseTile = 0x1B4;

                Battle_SetAnimationById(&actor->animation, animationId, baseTile, 12, 2);
            }
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_RELEASE_GRAPHICS;
        }
        break;
    case BATTLE_STATUS_SKILL_WAIT_RELEASE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            BattleAction_BeginTargetReactions(50);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_ALL_GRAPHICS;
        }
        break;
    case BATTLE_STATUS_SKILL_WAIT_ALL_GRAPHICS:
        if ((u8)Battle_AreCombatantAnimationsLoading() == 0)
        {
            Sound_PlaySfx(0x66, 1, 0);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_REACTIONS;
        }
        sBattleActionFrame++;
        break;
    case BATTLE_STATUS_SKILL_WAIT_REACTIONS:
        if (sBattleHitEffectPhase == 0 && sBattleDodgePhase == 0 && sBattleHitReactionPhase == 0)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_IDLE_GRAPHICS;
        }
        sBattleActionFrame++;
        break;
    case BATTLE_STATUS_SKILL_WAIT_IDLE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_APPLY;
        break;
    case BATTLE_STATUS_SKILL_APPLY:
    {
        struct BattleCombatant *combatants = Battle_GetCombatants();
        u8 index = 0;

        for (; index < sBattleActionEffectTargetCount; index++)
        {
            if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK) == BATTLE_TARGET_VALID
                && Battle_NextRandom() % 100 <= 39)
            {
                Battle_ApplyStatus(&combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK], 3);
            }
        }
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Execute skill-handler nineteen, with a forty-percent status-five attempt.
 *  @note Uses hit-effect 0x394; the nonsequential status number is intentional.
 *  @address 0x08035D9C
 */
u8 BattleAction_UpdateStatus5Skill(struct BattleCombatant *actor, struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case BATTLE_STATUS_SKILL_INIT:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = BATTLE_STATUS_SKILL_START_CAST;
        actor->hitEffectId = 0x394;
        actor->hitReactionStartFrame = 35;
        sBattleActionFrame = 0;
        break;
    case BATTLE_STATUS_SKILL_START_CAST:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x387;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 12, 2);
        sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_CAST_GRAPHICS;
        break;
    }
    case BATTLE_STATUS_SKILL_WAIT_CAST_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 1, 0);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_CAST_FINISHED;
        }
        break;
    case BATTLE_STATUS_SKILL_WAIT_CAST_FINISHED:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            {
                u32 animationId = 0x388;
                u32 baseTile = 0x1B4;

                Battle_SetAnimationById(&actor->animation, animationId, baseTile, 12, 2);
            }
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_RELEASE_GRAPHICS;
        }
        break;
    case BATTLE_STATUS_SKILL_WAIT_RELEASE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            BattleAction_BeginTargetReactions(50);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_ALL_GRAPHICS;
        }
        break;
    case BATTLE_STATUS_SKILL_WAIT_ALL_GRAPHICS:
        if ((u8)Battle_AreCombatantAnimationsLoading() == 0)
        {
            Sound_PlaySfx(0x66, 1, 0);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_REACTIONS;
        }
        sBattleActionFrame++;
        break;
    case BATTLE_STATUS_SKILL_WAIT_REACTIONS:
        if (sBattleHitEffectPhase == 0 && sBattleDodgePhase == 0 && sBattleHitReactionPhase == 0)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_IDLE_GRAPHICS;
        }
        sBattleActionFrame++;
        break;
    case BATTLE_STATUS_SKILL_WAIT_IDLE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_APPLY;
        break;
    case BATTLE_STATUS_SKILL_APPLY:
    {
        struct BattleCombatant *combatants = Battle_GetCombatants();
        u8 index = 0;

        for (; index < sBattleActionEffectTargetCount; index++)
        {
            if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK) == BATTLE_TARGET_VALID
                && Battle_NextRandom() % 100 <= 39)
            {
                Battle_ApplyStatus(&combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK], 5);
            }
        }
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Execute skill-handler twenty, with a forty-percent status-six attempt.
 *  @note Uses hit-effect 0x395; the rules helper retains status eligibility checks.
 *  @address 0x08036034
 */
u8 BattleAction_UpdateStatus6Skill(struct BattleCombatant *actor, struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case BATTLE_STATUS_SKILL_INIT:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = BATTLE_STATUS_SKILL_START_CAST;
        actor->hitEffectId = 0x395;
        actor->hitReactionStartFrame = 35;
        sBattleActionFrame = 0;
        break;
    case BATTLE_STATUS_SKILL_START_CAST:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x387;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 12, 2);
        sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_CAST_GRAPHICS;
        break;
    }
    case BATTLE_STATUS_SKILL_WAIT_CAST_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 1, 0);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_CAST_FINISHED;
        }
        break;
    case BATTLE_STATUS_SKILL_WAIT_CAST_FINISHED:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            {
                u32 animationId = 0x388;
                u32 baseTile = 0x1B4;

                Battle_SetAnimationById(&actor->animation, animationId, baseTile, 12, 2);
            }
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_RELEASE_GRAPHICS;
        }
        break;
    case BATTLE_STATUS_SKILL_WAIT_RELEASE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            BattleAction_BeginTargetReactions(50);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_ALL_GRAPHICS;
        }
        break;
    case BATTLE_STATUS_SKILL_WAIT_ALL_GRAPHICS:
        if ((u8)Battle_AreCombatantAnimationsLoading() == 0)
        {
            Sound_PlaySfx(0x66, 1, 0);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_REACTIONS;
        }
        sBattleActionFrame++;
        break;
    case BATTLE_STATUS_SKILL_WAIT_REACTIONS:
        if (sBattleHitEffectPhase == 0 && sBattleDodgePhase == 0 && sBattleHitReactionPhase == 0)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_IDLE_GRAPHICS;
        }
        sBattleActionFrame++;
        break;
    case BATTLE_STATUS_SKILL_WAIT_IDLE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_APPLY;
        break;
    case BATTLE_STATUS_SKILL_APPLY:
    {
        struct BattleCombatant *combatants = Battle_GetCombatants();
        u8 index = 0;

        for (; index < sBattleActionEffectTargetCount; index++)
        {
            if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK) == BATTLE_TARGET_VALID
                && Battle_NextRandom() % 100 <= 39)
            {
                Battle_ApplyStatus(&combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK], 6);
            }
        }
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Execute skill-handler twenty-one, with a twenty-percent status-four attempt.
 *  @note Uses hit-effect 0x396 and inclusive threshold nineteen, not thirty-nine.
 *  @address 0x080362CC
 */
u8 BattleAction_UpdateStatus4Skill(struct BattleCombatant *actor, struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case BATTLE_STATUS_SKILL_INIT:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = BATTLE_STATUS_SKILL_START_CAST;
        actor->hitEffectId = 0x396;
        actor->hitReactionStartFrame = 35;
        sBattleActionFrame = 0;
        break;
    case BATTLE_STATUS_SKILL_START_CAST:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x387;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 12, 2);
        sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_CAST_GRAPHICS;
        break;
    }
    case BATTLE_STATUS_SKILL_WAIT_CAST_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 1, 0);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_CAST_FINISHED;
        }
        break;
    case BATTLE_STATUS_SKILL_WAIT_CAST_FINISHED:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            {
                u32 animationId = 0x388;
                u32 baseTile = 0x1B4;

                Battle_SetAnimationById(&actor->animation, animationId, baseTile, 12, 2);
            }
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_RELEASE_GRAPHICS;
        }
        break;
    case BATTLE_STATUS_SKILL_WAIT_RELEASE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            BattleAction_BeginTargetReactions(50);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_ALL_GRAPHICS;
        }
        break;
    case BATTLE_STATUS_SKILL_WAIT_ALL_GRAPHICS:
        if ((u8)Battle_AreCombatantAnimationsLoading() == 0)
        {
            Sound_PlaySfx(0x66, 1, 0);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_REACTIONS;
        }
        sBattleActionFrame++;
        break;
    case BATTLE_STATUS_SKILL_WAIT_REACTIONS:
        if (sBattleHitEffectPhase == 0 && sBattleDodgePhase == 0 && sBattleHitReactionPhase == 0)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_WAIT_IDLE_GRAPHICS;
        }
        sBattleActionFrame++;
        break;
    case BATTLE_STATUS_SKILL_WAIT_IDLE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_STATUS_SKILL_APPLY;
        break;
    case BATTLE_STATUS_SKILL_APPLY:
    {
        struct BattleCombatant *combatants = Battle_GetCombatants();
        u8 index = 0;

        for (; index < sBattleActionEffectTargetCount; index++)
        {
            if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK) == BATTLE_TARGET_VALID
                && Battle_NextRandom() % 100 <= 19)
            {
                Battle_ApplyStatus(&combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK], 4);
            }
        }
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Fire Blast 的施法、火弹位移和目标反应，恢复待机后消耗 MP。
 *  @note 普通 C 的 0x396 字节及重定位 100% 匹配；末尾两字节零填充不计。
 *  @address 0x08036564
 */
u8 BattleAction_UpdateFireBlastSkill(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = 1;
        actor->hitEffectId = 0xAA;
        actor->hitReactionStartFrame = 0;
        sBattleActionFrame = 0;
        break;
    case 1:
        Battle_SetAnimationById(&actor->animation, 0x3A0, 0x1B4, 13, 2);
        sBattleActionEffectPhase = 2;
        break;
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 1, 0);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 3:
    case 4:
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetAnimationById(&actor->animation, 0x3A1, 0x1B4, 13, 0x102);
            sBattleActionEffectPhase = 18;
        }
        break;
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            actor->animation.flags &= ~0x100;
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        Battle_SetEffectAnimation(actor, actor->homeX - 16, actor->homeY - 24,
            0x24A, 14, 0x3A7, 4);
        sBattleActionEffectPhase = 20;
        break;
    case 20:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x31, 1, 0);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->effectAnimation));
            actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->effectAnimation.flags |= 0x100;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        actor->effectAnimation.timelineFrame =
            (actor->effectAnimation.timelineFrame + 1) % 22 + 24;
        if (sBattleActionFrame <= 9)
        {
            actor->effectAnimation.x = Battle_InterpolateS16(actor->homeX - 16,
                target->homeX - (actor->homeX - 16), 10, sBattleActionFrame, 2);
            actor->effectAnimation.y = Battle_InterpolateS16(actor->homeY - 24,
                target->homeY - (actor->homeY - 24), 10, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            BattleAction_BeginTargetReactions(20);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (!(u8)Battle_AreCombatantAnimationsLoading())
        {
            Sound_PlaySfx(0x64, 1, 0);
            sBattleActionEffectPhase = 24;
        }
        sBattleActionFrame++;
        break;
    case 24:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 6;
        }
        sBattleActionFrame++;
        break;
    case 6:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 7:
    case 8:
        break;
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Advance the action selected by skill[23].
 *  @note Seeded GPBA replay returned 1 after 276 dispatch updates (225 entry hits).
 *  Dispatch update counts include the skill-name window.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x080368FC
 *  静态收尾：推进 Flame Circle 的施法、命中动画与目标反应，消耗选中技能槽 MP。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdateFlameCircleSkill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = 1;
        actor->hitEffectId = 0x3A9;
        actor->hitReactionStartFrame = 0;
        break;
    case 1:
        Battle_SetAnimationById(&actor->animation, 0x3A5, 0x1B4, 13, 2);
        sBattleActionEffectPhase = 2;
        break;
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 1, 0);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 3:
    case 4:
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetAnimationById(&actor->animation, 0x3A6, 0x1B4, 13, 0x102);
            sBattleActionEffectPhase = 18;
        }
        break;
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            actor->animation.flags &= ~0x100;
            BattleAction_BeginTargetReactions(50);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (!(u8)Battle_AreCombatantAnimationsLoading())
        {
            Sound_PlaySfx(0x64, 1, 0);
            sBattleActionEffectPhase = 20;
        }
        sBattleActionFrame++;
        break;
    case 20:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 6;
        }
        sBattleActionFrame++;
        break;
    case 6:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 7:
    case 8:
        break;
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Advance the action selected by skill[24].
 *  @note Seeded GPBA replay returned 1 after 361 dispatch updates (310 entry hits).
 *  Dispatch update counts include the skill-name window.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x08036B30
 *  静态收尾：推进 Inferno 的背景加载、调色板循环与混合插值，停音、恢复背景后消耗 MP。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdateInfernoSkill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = 1;
        actor->hitEffectId = 2;
        actor->hitReactionStartFrame = 0;
        break;
    case 1:
        Battle_SetAnimationById(&actor->animation, 0x3A0, 0x1B4, 13, 2);
        sBattleActionEffectPhase = 2;
        break;
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 1, 0);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 3:
    case 4:
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetAnimationById(&actor->animation, 0x3A1, 0x1B4, 13, 2);
            sBattleActionEffectPhase = 18;
        }
        break;
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Battle_ResetBackgroundLoader();
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if ((u8)Battle_UpdateBackgroundLoader(16, 3, 14, 1))
        {
            Sound_PlaySfx(0x50, 1, 0);
            sBattleEffectBlendEva = 0;
            Battle_SetBlend(0x1C42, sBattleEffectBlendEva, 16);
            sBattleActionFrame = 0;
            BattlePalette_StartBgCycle(14, 1, 1, 1, 15);
            BattleAction_BeginTargetReactions(70);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 12, 20, sBattleActionFrame, 2);
            Battle_SetBlend(0x1C42, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleEffectBlendEva = 12;
            Battle_SetBlend(0x1C42, 12, 16);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (!Sound_IsSfxPlaying(1))
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(12, -12, 20, sBattleActionFrame, 2);
            Battle_SetBlend(0x1C42, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Sound_StopSfx(1);
            Battle_SetBlend(0, 0, 0);
            actor->displayState &= BATTLE_DISPLAY_EFFECT_ANIMATION;
            BattlePalette_StopBgCycle(14, 1);
            *(vu16 *)REG_DISPCNT &= ~0x200;
            sBattleActionEffectPhase = 26;
        }
        break;
    case 23:
    case 24:
    case 25:
        break;
    case 26:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 27;
        }
        else
            sBattleActionFrame++;
        break;
    case 27:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 6:
    case 7:
    case 8:
        break;
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 两段施法后，对传入目标执行 30% ATC 增益。
 *  @note 动画 0x3A5/0x3A6 使用调色板 13；阶段 19 将 target 传给增益演出 0x3AD，阶段 9 扣除所选技能的 MP 并返回 2。
 *  @note 施法参数使用 u32 局部量，增益结果按 u8 比较；普通 C 的 0x1D2 字节及已解析重定位 100% 匹配，末尾两字节零填充不计。
 *  @address 0x08036EA4
 */
u8 BattleAction_UpdateTwoStageAtcBoostSkill(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        Battle_BeginStatBoost(0, 30, 0x3AD);
        sBattleActionEffectPhase = 1;
        break;
    case 1:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x3A5;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
        sBattleActionEffectPhase = 2;
        break;
    }
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 1, 0);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            {
                u32 animationId = 0x3A6;
                u32 baseTile = 0x1B4;

                Battle_SetAnimationById(&actor->animation, animationId, baseTile, 13, 2);
            }
            sBattleActionEffectPhase = 18;
        }
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if ((u8)Battle_UpdatePartyStatBoost(actor, target) == 1)
            sBattleActionEffectPhase = 20;
        break;
    case 20:
        Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
            sBattleActionSavedPaletteSlot, 0);
        sBattleActionEffectPhase = 6;
        break;
    case 6:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    return result;
}

/** @brief Advance the action selected by skill[26].
 *  @note Seeded GPBA replay returned 1 after 339 dispatch updates (288 entry hits).
 *  Dispatch update counts include the skill-name window.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x08037078
 *  静态收尾：按目标尺寸放置 Freeze Blow 的效果，等待图块与目标动画、恢复待机并消耗 MP。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdateFreezeBlowSkill(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = 1;
        actor->hitEffectId = 0xA7;
        actor->hitReactionStartFrame = 0;
        break;
    case 1:
        Battle_SetAnimationById(&actor->animation, 0x3A5, 0x1B4, 13, 2);
        sBattleActionEffectPhase = 2;
        break;
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 1, 0);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 3:
    case 4:
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetAnimationById(&actor->animation, 0x3A6, 0x1B4, 13, 0x102);
            sBattleActionEffectPhase = 28;
        }
        break;
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
        break;
    case 28:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            actor->animation.flags &= ~0x100;
            Battle_SetEffectAnimation(actor, target->homeX,
                target->homeY - Battle_GetCombatantDimension(target, 1),
                0x27C, 14, 0x3B0, 4);
            actor->effectAnimation.priority = 0;
            sBattleActionEffectPhase = 29;
        }
        break;
    case 29:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x6F, 1, 0);
            sBattleActionEffectPhase = 30;
        }
        break;
    case 30:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->effectAnimation));
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 18;
        }
        break;
    case 31:
    case 32:
    case 33:
        break;
    case 18:
        BattleAction_BeginTargetReactions(50);
        sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (!(u8)Battle_AreCombatantAnimationsLoading())
        {
            sBattleActionEffectPhase = 20;
        }
        sBattleActionFrame++;
        break;
    case 20:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 6;
        }
        sBattleActionFrame++;
        break;
    case 6:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 7:
    case 8:
        break;
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 在目标所在阵形列的纵向中点播放 Cold Storm，完成目标反应后消耗 MP。
 *  @note 按 homeY 排序定位效果；循环时间轴 8..15，淡变背景调色板并将效果向左移出，结束返回 1。
 *  @note 普通 C 的 0x4E0 字节及已解析重定位 100% 匹配；目标数保留 u8 截断，阶段 29 的加载标志保存为 u16。
 *  @address 0x08037388
 */
u8 BattleAction_UpdateColdStormSkill(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
    {
        struct BattleCombatant *combatants;
        u8 slots[BATTLE_ENEMY_COUNT];
        u8 count;

        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        combatants = Battle_GetCombatants();
        count = Battle_ListAlliesInFormation(target, slots, BATTLE_FORMATION_COLUMN_MASK, 0xFF);
        sBattleActionScratch86E = 128;
        Battle_SortCombatantSlots(slots, count, BATTLE_STAT_HOME_Y);
        sBattleActionScratch86F = combatants[slots[count - 1]].homeY
            + (combatants[slots[0]].homeY - combatants[slots[count - 1]].homeY) / 2;
        sBattleActionEffectPhase = 1;
        actor->hitEffectId = 0xA7;
        actor->hitReactionStartFrame = 0;
        break;
    }
    case 1:
        Battle_SetAnimationById(&actor->animation, 0x3A0, 0x1B4, 13, 2);
        sBattleActionEffectPhase = 2;
        break;
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 1, 0);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 3:
    case 4:
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetAnimationById(&actor->animation, 0x3A1, 0x1B4, 13, 0x402);
            sBattleActionEffectPhase = 34;
        }
        break;
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
        break;
    case 34:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Battle_SetEffectAnimation(actor, sBattleActionScratch86E,
                sBattleActionScratch86F, 0x27C, 14, 0xAB, 4);
            sBattleActionEffectPhase = 35;
        }
        break;
    case 35:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x6F, 1, 0);
            sBattleActionEffectPhase = 36;
        }
        break;
    case 36:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->effectAnimation));
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 28;
        }
        break;
    case 28:
        Battle_SetEffectAnimation(actor, sBattleActionScratch86E,
            sBattleActionScratch86F, 0x27C, 14, 0x3B1, 4);
        sBattleActionEffectPhase = 29;
        break;
    case 29:
    {
        u16 loading = actor->effectAnimation.flags & BATTLE_ANIM_LOADING;

        if (!loading)
        {
            sBattleActionFrame = 0;
            Sound_PlaySfx(0x54, 1, 0);
            sBattleActionEffectPhase = 30;
        }
        break;
    }
    case 30:
        if (actor->effectAnimation.timelineFrame > 7)
        {
            actor->effectAnimation.flags |= 0x100;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 31;
        }
        break;
    case 31:
        if (sBattleActionFrame <= 31)
        {
            actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 8 + 8;
            sBattleActionFrame++;
        }
        else
        {
            actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 8 + 8;
            sBattleActionFrame = 0;
            BattlePalette_StartBgFade(0, 3, 16, 28, 31, 4, 4, -1, 2);
            sBattleActionEffectPhase = 32;
        }
        break;
    case 32:
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            BattlePalette_ReturnBgFade(0, 3, 32);
            sBattleActionEffectPhase = 33;
        }
        break;
    case 33:
        if (sBattleActionFrame <= 29)
        {
            actor->effectAnimation.timelineFrame = (actor->effectAnimation.timelineFrame + 1) % 8 + 8;
            actor->effectAnimation.x = Battle_InterpolateS16(sBattleActionScratch86E,
                -sBattleActionScratch86E, 30, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 18;
        }
        break;
    case 18:
        BattleAction_BeginTargetReactions(50);
        sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (!(u8)Battle_AreCombatantAnimationsLoading())
            sBattleActionEffectPhase = 20;
        sBattleActionFrame++;
        break;
    case 20:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 6;
        }
        sBattleActionFrame++;
        break;
    case 6:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 7:
    case 8:
        break;
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Blizzard 的背景渐入、调色板循环、目标反应和渐出。
 *  @note 普通 C 的 0x3D8 字节及重定位 100% 匹配；保留原版 displayState &= 0x2000 的写入。
 *  @address 0x08037868
 */
u8 BattleAction_UpdateBlizzardSkill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = 1;
        actor->hitEffectId = 1;
        actor->hitReactionStartFrame = 20;
        break;
    case 1:
        Battle_SetAnimationById(&actor->animation, 0x3A0, 0x1B4, 13, 2);
        sBattleActionEffectPhase = 2;
        break;
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 1, 0);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 3:
    case 4:
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetAnimationById(&actor->animation, 0x3A1, 0x1B4, 13, 0x102);
            sBattleActionEffectPhase = 28;
        }
        break;
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
        break;
    case 28:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            actor->animation.flags &= ~0x100;
            Battle_ResetBackgroundLoader();
            sBattleActionEffectPhase = 34;
        }
        break;
    case 34:
        if ((u8)Battle_UpdateBackgroundLoader(4, 3, 14, 0))
        {
            Sound_PlaySfx(0x6D, 1, 0);
            sBattleEffectBlendEva = 0;
            Battle_SetBlend(0x1C42, 0, 16);
            sBattleActionEffectPhase = 29;
        }
        break;
    case 29:
        sBattleActionFrame = 0;
        BattlePalette_StartBgCycle(14, 1, 1, -1, 15);
        sBattleActionEffectPhase = 30;
        break;
    case 30:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 12, 20, sBattleActionFrame, 2);
            Battle_SetBlend(0x1C42, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleEffectBlendEva = 12;
            Battle_SetBlend(0x1C42, 12, 16);
            BattleAction_BeginTargetReactions(60);
            sBattleActionEffectPhase = 31;
        }
        break;
    case 31:
        if (sBattleActionFrame > 59)
            sBattleActionEffectPhase = 32;
        else
            sBattleActionFrame++;
        break;
    case 32:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 33;
        }
        break;
    case 33:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(12, -12, 20, sBattleActionFrame, 2);
            Battle_SetBlend(0x1C42, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Sound_StopSfx(1);
            Battle_SetBlend(0, 0, 0);
            actor->displayState &= BATTLE_DISPLAY_EFFECT_ANIMATION;
            BattlePalette_StopBgCycle(14, 1);
            *(vu16 *)REG_DISPCNT &= ~0x200;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 18:
        BattleAction_BeginTargetReactions(50);
        sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (!(u8)Battle_AreCombatantAnimationsLoading())
            sBattleActionEffectPhase = 20;
        sBattleActionFrame++;
        break;
    case 20:
        Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
            sBattleActionSavedPaletteSlot, 0);
        sBattleActionEffectPhase = 6;
        break;
    case 6:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 7:
    case 8:
        break;
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 播放两段施法动画并为传入目标增加 30% DEF 修正。
 *  @note 动画 0x3A5/0x3A6 后，阶段 19 播放目标加成效果 0x3AD；阶段 20 恢复施法者待机，阶段 9 扣除 MP 并返回 2。
 *  @note 施法参数使用 u32 局部量，加成结果按 u8 比较；普通 C 的 0x1D2 字节及已解析重定位 100% 匹配，末尾两字节零填充不计。
 *  @address 0x08037C40
 */
u8 BattleAction_UpdateTargetDefBoostSkill(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        Battle_BeginStatBoost(BATTLE_MODIFIER_DEF, 30, 0x3AD);
        sBattleActionEffectPhase = 1;
        break;
    case 1:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x3A5;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
        sBattleActionEffectPhase = 2;
        break;
    }
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 1, 0);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            {
                u32 animationId = 0x3A6;
                u32 baseTile = 0x1B4;

                Battle_SetAnimationById(&actor->animation, animationId, baseTile, 13, 2);
            }
            sBattleActionEffectPhase = 18;
        }
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if ((u8)Battle_UpdatePartyStatBoost(actor, target) == 1)
            sBattleActionEffectPhase = 20;
        break;
    case 20:
        Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
            sBattleActionSavedPaletteSlot, 0);
        sBattleActionEffectPhase = 6;
        break;
    case 6:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    return result;
}

/** @brief 播放两段施法动画并为施法者选中的目标回复最多 60 点 HP。
 *  @note 动画 0x3B9/0x3BA 使用调色板 12；阶段 22 回复后恢复待机，阶段 9 扣除 MP 并返回 2。
 *  @note 施法参数使用 u32 局部量，回复结果按 u8 比较；普通 C 的 0x1D2 字节及已解析重定位 100% 匹配，末尾两字节零填充不计。
 *  @address 0x08037E14
 */
u8 BattleAction_UpdateSelectedHp60Skill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case BATTLE_TWO_STAGE_RECOVERY_INIT:
        Battle_ResetEffectPhase();
        sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_START_CAST;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        break;
    case BATTLE_TWO_STAGE_RECOVERY_START_CAST:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x3B9;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 12, 2);
        sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_GRAPHICS;
        break;
    }
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x3E, 1, 0);
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_FINISHED;
        }
        break;
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_FINISHED:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            {
                u32 animationId = 0x3BA;
                u32 baseTile = 0x1B4;

                Battle_SetAnimationById(&actor->animation, animationId, baseTile, 12, 2);
            }
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_RELEASE_GRAPHICS;
        }
        break;
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_RELEASE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_APPLY;
        break;
    case BATTLE_TWO_STAGE_RECOVERY_APPLY:
        if ((u8)Battle_UpdateTargetRecoveryEffect(actor, Battle_GetCombatants(),
            BATTLE_RECOVERY_SELECTED_TARGET, BATTLE_RECOVERY_HP, 60) == 1)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_IDLE_GRAPHICS;
        }
        break;
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_IDLE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_COMPLETE;
        break;
    case BATTLE_TWO_STAGE_RECOVERY_COMPLETE:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    return result;
}

/** @brief 播放两段施法动画并为选中阵营的各目标回复最多 40 点 HP。
 *  @note 动画 0x3B9/0x3BA 使用调色板 13；回复模式 1 保留 targetSlot <= 10 阵营判定，阶段 9 扣除 MP 并返回 2。
 *  @note 施法参数使用 u32 局部量，回复结果按 u8 比较；普通 C 的 0x1D2 字节及已解析重定位 100% 匹配，末尾两字节零填充不计。
 *  @address 0x08037FE8
 */
u8 BattleAction_UpdateSideHp40Skill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case BATTLE_TWO_STAGE_RECOVERY_INIT:
        Battle_ResetEffectPhase();
        sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_START_CAST;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        break;
    case BATTLE_TWO_STAGE_RECOVERY_START_CAST:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x3B9;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
        sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_GRAPHICS;
        break;
    }
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x3E, 1, 0);
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_FINISHED;
        }
        break;
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_FINISHED:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            {
                u32 animationId = 0x3BA;
                u32 baseTile = 0x1B4;

                Battle_SetAnimationById(&actor->animation, animationId, baseTile, 13, 2);
            }
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_RELEASE_GRAPHICS;
        }
        break;
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_RELEASE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_APPLY;
        break;
    case BATTLE_TWO_STAGE_RECOVERY_APPLY:
        if ((u8)Battle_UpdateTargetRecoveryEffect(actor, Battle_GetCombatants(),
            BATTLE_RECOVERY_SIDE_TARGETS, BATTLE_RECOVERY_HP, 40) == 1)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_IDLE_GRAPHICS;
        }
        break;
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_IDLE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_COMPLETE;
        break;
    case BATTLE_TWO_STAGE_RECOVERY_COMPLETE:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    return result;
}

/** @brief 播放两段施法动画并为选中阵营的目标启用回合结束 HP 回复。
 *  @note 回复模式 1、效果 12 设置 turnEndEffects 的 0x8 位；助手忽略参数 30，阶段 9 扣除 MP 并返回 2。
 *  @note 施法参数使用 u32 局部量，回复结果按 u8 比较；普通 C 的 0x1D2 字节及已解析重定位 100% 匹配，末尾两字节零填充不计。
 *  @address 0x080381BC
 */
u8 BattleAction_UpdateSideHpRegenSkill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case BATTLE_TWO_STAGE_RECOVERY_INIT:
        Battle_ResetEffectPhase();
        sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_START_CAST;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        break;
    case BATTLE_TWO_STAGE_RECOVERY_START_CAST:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x3B9;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
        sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_GRAPHICS;
        break;
    }
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x3E, 1, 0);
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_FINISHED;
        }
        break;
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_FINISHED:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            {
                u32 animationId = 0x3BA;
                u32 baseTile = 0x1B4;

                Battle_SetAnimationById(&actor->animation, animationId, baseTile, 13, 2);
            }
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_RELEASE_GRAPHICS;
        }
        break;
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_RELEASE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_APPLY;
        break;
    case BATTLE_TWO_STAGE_RECOVERY_APPLY:
        if ((u8)Battle_UpdateTargetRecoveryEffect(actor, Battle_GetCombatants(),
            BATTLE_RECOVERY_SIDE_TARGETS, BATTLE_RECOVERY_TURN_END_HP, 30) == 1)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_IDLE_GRAPHICS;
        }
        break;
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_IDLE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_COMPLETE;
        break;
    case BATTLE_TWO_STAGE_RECOVERY_COMPLETE:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    return result;
}

/** @brief 播放两段施法动画并为施法者选中的目标回复最多 999 点 HP。
 *  @note 动画 0x3B9/0x3BA 使用调色板 13；回复量受缺失 HP 限制，回复后恢复待机，阶段 9 扣除 MP 并返回 2。
 *  @note 施法参数使用 u32 局部量，回复结果按 u8 比较；普通 C 的 0x1D6 字节及已解析重定位 100% 匹配，末尾两字节零填充不计。
 *  @address 0x08038390
 */
u8 BattleAction_UpdateSelectedHp999Skill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case BATTLE_TWO_STAGE_RECOVERY_INIT:
        Battle_ResetEffectPhase();
        sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_START_CAST;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        break;
    case BATTLE_TWO_STAGE_RECOVERY_START_CAST:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x3B9;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
        sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_GRAPHICS;
        break;
    }
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x3E, 1, 0);
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_FINISHED;
        }
        break;
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_FINISHED:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            {
                u32 animationId = 0x3BA;
                u32 baseTile = 0x1B4;

                Battle_SetAnimationById(&actor->animation, animationId, baseTile, 13, 2);
            }
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_RELEASE_GRAPHICS;
        }
        break;
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_RELEASE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_APPLY;
        break;
    case BATTLE_TWO_STAGE_RECOVERY_APPLY:
        if ((u8)Battle_UpdateTargetRecoveryEffect(actor, Battle_GetCombatants(),
            BATTLE_RECOVERY_SELECTED_TARGET, BATTLE_RECOVERY_HP, 999) == 1)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_IDLE_GRAPHICS;
        }
        break;
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_IDLE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_COMPLETE;
        break;
    case BATTLE_TWO_STAGE_RECOVERY_COMPLETE:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    return result;
}

/** @brief 播放两段施法动画，以传入目标最大 HP 的三分之一复活选中目标。
 *  @note 回复量为 floor(target->maxHp / 3)，实际目标取 actor->targetSlot；效果 10 仅复活倒地状态 8，阶段 9 扣除 MP 并返回 2。
 *  @note 施法参数使用 u32 局部量，回复量按 u16 保存并使用无符号除法，回复结果按 u8 比较；普通 C 的 0x1E2 字节及已解析重定位 100% 匹配，末尾两字节零填充不计。
 *  @address 0x08038568
 */
u8 BattleAction_UpdateReviveThirdMaxHpSkill(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case BATTLE_TWO_STAGE_RECOVERY_INIT:
        Battle_ResetEffectPhase();
        sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_START_CAST;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        break;
    case BATTLE_TWO_STAGE_RECOVERY_START_CAST:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x3B9;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
        sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_GRAPHICS;
        break;
    }
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x3E, 1, 0);
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_FINISHED;
        }
        break;
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_FINISHED:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            {
                u32 animationId = 0x3BA;
                u32 baseTile = 0x1B4;

                Battle_SetAnimationById(&actor->animation, animationId, baseTile, 13, 2);
            }
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_RELEASE_GRAPHICS;
        }
        break;
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_RELEASE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_APPLY;
        break;
    case BATTLE_TWO_STAGE_RECOVERY_APPLY:
    {
        u16 amount = target->maxHp / 3u;

        if ((u8)Battle_UpdateTargetRecoveryEffect(actor, Battle_GetCombatants(),
            BATTLE_RECOVERY_SELECTED_TARGET, BATTLE_RECOVERY_REVIVE, amount) == 1)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_IDLE_GRAPHICS;
        }
        break;
    }
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_IDLE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_COMPLETE;
        break;
    case BATTLE_TWO_STAGE_RECOVERY_COMPLETE:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    return result;
}

/** @brief 播放两段施法动画，清除 actor->targetSlot 的非倒地异常状态。
 *  @note 动画 0x3B9/0x3BA 使用调色板 13；效果 11 不将参数 30 用作 HP 回复量，保留倒地状态 8，阶段 9 扣除 MP 并返回 2。
 *  @note 施法参数使用 u32 局部量，治愈结果按 u8 比较；普通 C 的 0x1D2 字节及已解析重定位 100% 匹配，末尾两字节零填充不计。
 *  @address 0x0803874C
 */
u8 BattleAction_UpdateTwoStageStatusCureSkill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case BATTLE_TWO_STAGE_RECOVERY_INIT:
        Battle_ResetEffectPhase();
        sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_START_CAST;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        break;
    case BATTLE_TWO_STAGE_RECOVERY_START_CAST:
    {
        struct BattleAnimation *animation = &actor->animation;
        u32 animationId = 0x3B9;
        u32 baseTile = 0x1B4;

        Battle_SetAnimationById(animation, animationId, baseTile, 13, 2);
        sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_GRAPHICS;
        break;
    }
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x3E, 1, 0);
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_FINISHED;
        }
        break;
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_CAST_FINISHED:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            {
                u32 animationId = 0x3BA;
                u32 baseTile = 0x1B4;

                Battle_SetAnimationById(&actor->animation, animationId, baseTile, 13, 2);
            }
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_RELEASE_GRAPHICS;
        }
        break;
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_RELEASE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_APPLY;
        break;
    case BATTLE_TWO_STAGE_RECOVERY_APPLY:
        if ((u8)Battle_UpdateTargetRecoveryEffect(actor, Battle_GetCombatants(),
            BATTLE_RECOVERY_SELECTED_TARGET, BATTLE_RECOVERY_CURE_STATUS, 30) == 1)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_WAIT_IDLE_GRAPHICS;
        }
        break;
    case BATTLE_TWO_STAGE_RECOVERY_WAIT_IDLE_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = BATTLE_TWO_STAGE_RECOVERY_COMPLETE;
        break;
    case BATTLE_TWO_STAGE_RECOVERY_COMPLETE:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    return result;
}

/** @brief Advance the action selected by skill[36].
 *  @note Seeded GPBA replay returned 2 after 375 dispatch updates (324 entry hits).
 *  Dispatch update counts include the skill-name window.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x08038920
 *  静态收尾：推进たいまのいのり的单目标驱离效果；受遭遇标志限制，退场成功写状态 7、清 id 并递减敌人数。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdateTargetEnemyRetreatSkill(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionEffectPhase = 18;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        Battle_SetAnimationById(&actor->animation, 0x3B9, 0x1B4, 13, 2);
        sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x3E, 1, 0);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetAnimationById(&actor->animation, 0x3BA, 0x1B4, 13, 2);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 22;
        break;
    case 22:
        Battle_SetEffectAnimation(actor, target->homeX, target->homeY,
            0x27C, 14, 0x3C1, 0x2004);
        sBattleActionEffectPhase = 23;
        break;
    case 23:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Battle_SetEffectAnimation(target, target->homeX, 0, 0x2E0, 14, 0x3C2, 0x2005);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (!(target->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x59, 0, 0);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (sBattleActionFrame <= 39)
        {
            target->effectAnimation.y = Battle_InterpolateS16(0,
                target->homeY - (Battle_GetCombatantDimension(target, 1) >> 1),
                40, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            Battle_SetEffectAnimation(target, target->homeX, target->homeY,
                0x2E0, 14, 0x3C4, 0x2005);
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (!(target->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        if (sBattleActionFrame <= 49)
            sBattleActionFrame++;
        else
        {
            target->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            if (!(Battle_GetControlFlags() & 0x220))
            {
                target->status = BATTLE_STATUS_ESCAPED;
                target->id = BATTLE_NO_COMBATANT;
                Battle_DecrementEnemyCount();
            }
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 28;
        }
        break;
    case 28:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_StopSfx(1);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    return result;
}

/** @brief Advance the action selected by skill[38].
 *  @note Seeded GPBA replay returned 1 after 371 dispatch updates (320 entry hits).
 *  Dispatch update counts include the skill-name window.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x08038C84
 *  静态收尾：累积伤害，复用技能跳跃助手推进 Power Slash 的斩击与目标反应，最后消耗 MP。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdatePowerSlashSkill(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 1;
        sBattleActionSubphase = 0;
        break;
    case 1:
        if (BattleAction_UpdateSkillJump(actor, target, 0) == 1)
        {
            Battle_SetAnimationById(&actor->animation, 0x3C7, 0x1B4, 13, 2);
            if (target->id < BATTLE_CHARACTER_ID_COUNT)
                actor->animation.flags |= BATTLE_ANIM_FLIP_X;
            sBattleActionEffectPhase = 2;
        }
        break;
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 3;
        break;
    case 3:
        if (actor->animation.timelineFrame > 57)
        {
            Sound_PlaySfx(0x31, 1, 0);
            BattleAction_BeginTargetReactions(40);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            sBattleActionSubphase = 12;
            sBattleActionEffectPhase = 8;
        }
        break;
    case 8:
        if (BattleAction_UpdateSkillJump(actor, target, 0) == 1)
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
            result = 1;
        }
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Advance the action selected by skill[39].
 *  @note Seeded GPBA replay returned 1 after 330 dispatch updates (279 entry hits).
 *  Dispatch update counts include the skill-name window.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x08038E44
 *  静态收尾：处理输入并复用技能跳跃助手推进 Power Sweep 的横扫与反应，最后消耗 MP。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdatePowerSweepSkill(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 1;
        sBattleActionSubphase = 0;
        break;
    case 1:
        if (BattleAction_UpdateSkillJump(actor, target, 2) == 1)
        {
            Battle_SetAnimationById(&actor->animation, 0x3C8, 0x1B4, 13, 2);
            if (target->id < BATTLE_CHARACTER_ID_COUNT)
                actor->animation.flags |= BATTLE_ANIM_FLIP_X;
            sBattleActionEffectPhase = 2;
        }
        break;
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 3;
        break;
    case 3:
        if (actor->animation.timelineFrame == 36)
        {
            Sound_PlaySfx(0x31, 1, 0);
            BattleAction_BeginTargetReactions(30);
        }
        if (actor->animation.timelineFrame > 67)
        {
            Sound_PlaySfx(0x31, 1, 0);
            BattleAction_BeginTargetReactions(40);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            sBattleActionSubphase = 12;
            sBattleActionEffectPhase = 8;
        }
        break;
    case 8:
        if (BattleAction_UpdateSkillJump(actor, target, 0) == 1)
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
            result = 1;
        }
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Advance the action selected by skill[40].
 *  @note Seeded GPBA replay returned 1 after 342 dispatch updates (291 entry hits).
 *  Dispatch update counts include the skill-name window.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x08039024
 *  静态收尾：复用技能跳跃助手并启动附加效果推进 Sonic Riser，等待反应后消耗 MP。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdateSonicRiserSkill(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        sBattleActionSubphase = 0;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (BattleAction_UpdateSkillJump(actor, target, 3) == 1)
        {
            Battle_SetAnimationById(&actor->animation, 0x3C9, 0x1B4, 13, 2);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (actor->animation.timelineFrame > 64)
        {
            Battle_SetEffectAnimation(actor, actor->homeX - 125, actor->homeY - 16,
                0x27C, 15, 0x3CA, 0x105);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x1D, 1, 0);
            actor->effectAnimation.flags &= ~0x100;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (actor->animation.timelineFrame > 69)
        {
            BattleAction_BeginTargetReactions(40);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if ((actor->displayState & BATTLE_DISPLAY_EFFECT_ANIMATION)
            && actor->effectAnimation.timelineFrame > 14)
        {
            actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
        }
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags |= 0x100;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionSubphase = 12;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (BattleAction_UpdateSkillJump(actor, target, 0) == 1)
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
            result = 1;
        }
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Cast the alternate thirty-percent self ATC boost presentation.
 *  @note All 0x11E bytes and resolved relocations match; the last two padding bytes are excluded.
 *  Animation base 0x3CB uses the actor-only stat-boost interface; target is unused.
 *  Phase nine stays active and consumes caster MP on each call before returning two.
 *  @address 0x080392C0
 */
u8 BattleAction_UpdateAlternateSelfAtcBoostSkill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        Battle_BeginStatBoost(BATTLE_MODIFIER_ATC, 30, 0x3CB);
        sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (Battle_UpdateEnemyStatBoost(actor) == 1)
            sBattleActionEffectPhase = 20;
        break;
    case 20:
        Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
            sBattleActionSavedPaletteSlot, 0);
        sBattleActionEffectPhase = 6;
        break;
    case 6:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    return result;
}

/** @brief Advance the action selected by skill[42].
 *  @note Seeded GPBA replay returned 1 after 235 dispatch updates (184 entry hits).
 *  Dispatch update counts include the skill-name window.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x080393E0
 *  静态收尾：推进 Flash Arrow 的多组效果与目标反应，停止效果音、恢复待机并消耗 MP。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdateFlashArrowSkill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0;
        actor->hitReactionStartFrame = 0;
        Battle_SetAnimationById(&actor->animation, 0x83, 0x1B4, 13, 2);
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x42, 1, 0);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetAnimationById(&actor->animation, 0x3D8, 0x1B4, 13, 0x502);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            actor->animation.flags &= ~0x100;
            Battle_SetEffectAnimation(actor, actor->homeX - 100, actor->homeY,
                0x218, 14, 0x3DE, 0x425);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            BattleAction_BeginTargetReactions(90);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if ((u8)(sBattleHitEffectPhase - 1) > 5)
        {
            Sound_PlaySfx(0x3A, 1, 1);
            sBattleActionEffectPhase = 23;
        }
        sBattleActionFrame++;
        break;
    case 23:
        if (sBattleActionFrame == 80)
        {
            Sound_StopSfx(1);
            BattlePalette_ReleaseObjBanks(actor->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->effectAnimation));
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
        }
        if (sBattleActionFrame > 89)
        {
            Battle_SetAnimationById(&actor->animation, 0x3D9, 0x1B4, 13, 2);
            sBattleActionEffectPhase = 24;
        }
        sBattleActionFrame++;
        break;
    case 24:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 25;
        break;
    case 25:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            Battle_RestoreIdleAnimation(actor, actor->homeX, actor->homeY,
                sBattleActionSavedBaseTile, sBattleActionSavedPaletteSlot);
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进 Rush Wind 的敌人驱离、背景调色板循环和混合渐变，结束恢复待机并消耗 MP。
 *  @note 普通 C 的 0x512 字节及已解析重定位 100% 匹配，末尾两字节零填充不计。
 *  敌人循环保留 u8 索引并缓存记录指针，依次清 id、写逃跑状态和递减敌人数；控制标志 0x220 阻止驱离。
 *  阶段 9 每次调用消耗 MP 并返回 2，第二参数未使用。
 *  既有 GPBA 回放为 363 次派发更新（312 次入口命中，含技能名窗口）；证据见 docs/assets/battle-action-analysis-20261114/replay/。
 *  @address 0x08039724
 */
u8 BattleAction_UpdateRushWindSkill(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 index;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0;
        actor->hitReactionStartFrame = 0;
        Battle_SetAnimationById(&actor->animation, 0x3DA, 0x1B4, 13, 2);
        sBattleEffectCombatantCount = Battle_ListCombatants(combatants,
            sBattleEffectCombatantSlots, BATTLE_SIDE_ENEMY, 0x7F);
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x42, 1, 0);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetAnimationById(&actor->animation, 0x3DB, 0x1B4, 13, 0x502);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            actor->animation.flags &= ~0x100;
            Sound_PlaySfx(0x26, 1, 0);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (!Sound_IsSfxPlaying(1))
        {
            Battle_SetAnimationById(&actor->animation, 0x3DC, 0x1B4, 13, 2);
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x3A, 1, 0);
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags |= 0x100;
            Battle_ResetBackgroundLoader();
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if ((u8)Battle_UpdateBackgroundLoader(4, 3, 14, 0))
        {
            Sound_PlaySfx(0x6D, 1, 0);
            sBattleEffectBlendEva = 0;
            Battle_SetBlend(0x1C42, 0, 16);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        sBattleActionFrame = 0;
        BattlePalette_StartBgCycle(14, 1, 1, -1, 15);
        sBattleActionEffectPhase = 26;
        break;
    case 26:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 12, 20, sBattleActionFrame, 2);
            Battle_SetBlend(0x1C42, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleEffectBlendEva = 12;
            Battle_SetBlend(0x1C42, 12, 16);
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        if (sBattleActionFrame <= 49)
            sBattleActionFrame++;
        else
        {
            if (!(Battle_GetControlFlags() & 0x220))
            {
                for (index = 0; index < sBattleEffectCombatantCount; index++)
                {
                    struct BattleCombatant *enemy = &combatants[sBattleEffectCombatantSlots[index]];

                    enemy->id = BATTLE_NO_COMBATANT;
                    enemy->status = BATTLE_STATUS_ESCAPED;
                    Battle_DecrementEnemyCount();
                }
            }
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 28;
        }
        break;
    case 28:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(12, -12, 20, sBattleActionFrame, 2);
            Battle_SetBlend(0x1C42, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Sound_StopSfx(1);
            Battle_SetBlend(0, 0, 0);
            actor->displayState &= BATTLE_DISPLAY_EFFECT_ANIMATION;
            BattlePalette_StopBgCycle(14, 1);
            *(vu16 *)REG_DISPCNT &= ~0x200;
            sBattleActionEffectPhase = 29;
        }
        break;
    case 29:
        Battle_SetAnimationById(&actor->animation, 0x3DD, 0x1B4, 13, 0x102);
        sBattleActionEffectPhase = 30;
        break;
    case 30:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            actor->animation.flags &= ~0x100;
            sBattleActionEffectPhase = 31;
        }
        break;
    case 31:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            Battle_RestoreIdleAnimation(actor, actor->homeX, actor->homeY,
                sBattleActionSavedBaseTile, sBattleActionSavedPaletteSlot);
            sBattleActionEffectPhase = 32;
        }
        break;
    case 32:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        Battle_ConsumeSkillSlotMp(actor, actor->skillSelection);
        result = 2;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进攻击槽的阵型跳跃及斩击演出，排队目标伤害后清空攻击槽。
 *  @note skill[48]；跳跃模式 3 接近、返程，启动 20/80 帧目标反应并推进八轮特效位置变化，等待背景恢复后清空攻击槽并返回 1。
 *  @note 普通 C 的 0x840 字节及 173 项已解析重定位 100% 匹配；保留 30→42→31 的阶段顺序、动画编号半字读写及阶段 35 的公共增帧块，坐标表以子阶段 % 4 取项。
 *  既有受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x08039C38
 */
u8 BattleAction_UpdateGaugeFormationSlashes(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;
    struct BattleCombatant *cursor = Battle_GetCursor();

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        Battle_SetControlFlags(0x10);
        Battle_ResetBackgroundLoader();
        sBattleEffectBlendEva = 0;
        Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, 16);
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 39:
        break;
    case 18:
        if ((u8)Battle_UpdateBackgroundLoader(4, 2, 14, 1))
        {
            cursor->displayState |= 0x400;
            Sound_PlaySfx(0x59, 0, 1);
            sBattleActionFrame = 0;
            BattlePalette_StartBgCycle(14, 1, 1, -1, 15);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 10,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            Battle_ResetBackgroundLoader();
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
    {
        u8 backgroundId = actor->id == 0 ? 5 : 6;

        if ((u8)Battle_UpdateBackgroundLoader(backgroundId, 3, 15, 0))
        {
            sBattleActionFrame = 0;
            sBattleEffectBlendEva = 0;
            Battle_SetBlend(0x1F47, 0, 16);
            sBattleActionEffectPhase = 22;
        }
        break;
    }
    case 22:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 10,
                sBattleActionFrame, 1);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Battle_SetBlend(0, 0, 0);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame <= 49)
            sBattleActionFrame++;
        else
        {
            Sound_StopSfx(0);
            Battle_StartBrightnessTransition(5, 0x110);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearRasterFlags(0x4000);
            Battle_StartBrightnessTransition(10, 0x120);
            BattlePalette_StopBgCycle(14, 1);
            Battle_SetBgRequests(0xA0);
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearControlFlags(0x10);
            Battle_ClearRasterFlags(0x4000);
            Battle_EndRasterTransition();
            Battle_LoadStatusGraphics();
            Battle_SetBgRequests(8);
            cursor->displayState &= ~0x400;
            sBattleActionSubphase = 0;
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        if (BattleAction_UpdateSkillJump(actor, target, 3) == 1)
        {
            sBattleActionAnimationId = actor->id == 0 ? 0x365 : 0x377;
            Battle_SetAnimationById(&actor->animation, sBattleActionAnimationId,
                0x1B4, 13, 2);
            sBattleActionEffectPhase = 28;
        }
        break;
    case 28:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            sBattleActionEffectPhase = 29;
            Sound_PlaySfx(0x9A, 0, 0);
        }
        break;
    case 29:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            sBattleActionAnimationId++;
            Battle_SetAnimationById(&actor->animation, sBattleActionAnimationId,
                0x1B4, 13, 0x102);
            sBattleActionEffectPhase = 30;
        }
        break;
    case 30:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x6F, 1, 0);
            actor->animation.flags &= ~0x100;
            Battle_SetEffectAnimation(actor, 80, 100, 0x27C, 14, 0x381, 5);
            actor->effectAnimation.x = actor->homeX;
            actor->effectAnimation.y = actor->homeY;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 42;
        }
        break;
    case 42:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x55, 2, 0);
            sBattleActionEffectPhase = 31;
        }
        break;
    case 31:
        if (sBattleActionFrame <= 59)
            sBattleActionFrame++;
        else
        {
            sBattleActionAnimationId++;
            Battle_SetAnimationById(&actor->animation, sBattleActionAnimationId,
                0x1B4, 13, 2);
            sBattleActionEffectPhase = 32;
        }
        break;
    case 32:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x6E, 1, 0);
            sBattleActionEffectPhase = 33;
        }
        break;
    case 33:
        if (actor->animation.timelineFrame > 21)
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 34;
        }
        break;
    case 34:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags |= 0x100;
        }
        if (sBattleActionFrame <= 19)
        {
            actor->effectAnimation.x = Battle_InterpolateS16(actor->homeX, -72, 20,
                sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            Sound_StopSfx(2);
            sBattleActionFrame = 0;
            BattleAction_BeginTargetReactions(20);
            sBattleActionEffectPhase = 35;
        }
        break;
    case 35:
        if (!(actor->animation.flags & BATTLE_ANIM_FINISHED))
            sBattleActionFrame++;
        else
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags |= 0x100;
            Battle_SetEffectAnimation(actor, 80, 100, 0x27C, 14, 0x382, 5);
            sBattleActionEffectPhase = 36;
        }
        break;
    case 36:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            u16 loading = actor->effectAnimation.flags & BATTLE_ANIM_LOADING;

            if (!loading)
            {
                sBattleEffectBlendEvb = actor->effectAnimation.y;
                BattleAction_BeginTargetReactions(80);
                sBattleActionSubphase = loading;
                sBattleActionEffectPhase = 37;
            }
        }
        break;
    case 37:
        sBattleEffectBlendEva = D_0839DF67[sBattleActionSubphase % 4];
        sBattleActionEffectPhase = 38;
        break;
    case 38:
        if (sBattleActionFrame <= 9)
        {
            actor->effectAnimation.x = Battle_InterpolateS16(56,
                sBattleEffectBlendEva - 56, 10, sBattleActionFrame, 2);
            actor->effectAnimation.y = Battle_InterpolateS16(sBattleEffectBlendEvb,
                -sBattleEffectBlendEvb, 10, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionSubphase++;
            if (sBattleActionSubphase <= 7)
                sBattleActionEffectPhase = 37;
            else
            {
                actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
                sBattleActionSubphase = 12;
                sBattleActionEffectPhase = 40;
            }
        }
        break;
    case 40:
        if (BattleAction_UpdateSkillJump(actor, target, 3) == 1)
            sBattleActionEffectPhase = 41;
        break;
    case 41:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_ResetBackgroundLoadProgress();
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if ((u8)Battle_GetBackgroundLoadProgress() > 3)
        {
            Battle_ResetAttackGauge(actor);
            result = 1;
        }
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进攻击槽技能演出并为选中阵营回复最多 999 点 HP 和 MP。
 *  @note 技能处理器 49 使用阵营名单模式 1、HP/MP 效果 2、回复量 999；完成后清空施法者攻击槽并返回 2，不扣技能 MP，第二参数未使用。
 *  @note 保留 0..26 的空分支及阶段 0 两次写入阶段 18；普通 C 的 0x456 字节及已解析重定位 100% 匹配，末尾两字节零填充不计。
 *  @address 0x0803A478
 */
u8 BattleAction_UpdateGaugeHpMpRecovery(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;
    struct BattleCombatant *cursor = Battle_GetCursor();

    switch (sBattleActionEffectPhase)
    {
    case 0:
        Battle_ResetEffectPhase();
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionEffectPhase = 18;
        Battle_SetControlFlags(0x10);
        Battle_ResetBackgroundLoader();
        sBattleEffectBlendEva = 0;
        Battle_SetBlend(0x1F4F, 0, 16);
        sBattleActionEffectPhase = 18;
        break;
    case 3:
    case 4:
    case 7:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if ((u8)Battle_UpdateBackgroundLoader(4, 2, 14, 1))
        {
            cursor->displayState |= 0x400;
            Sound_PlaySfx(0x59, 0, 1);
            sBattleActionFrame = 0;
            BattlePalette_StartBgCycle(14, 1, 1, -1, 15);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 10,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            Battle_ResetBackgroundLoader();
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if ((u8)Battle_UpdateBackgroundLoader(7, 3, 15, 0))
        {
            sBattleActionFrame = 0;
            sBattleEffectBlendEva = 0;
            Battle_SetBlend(0x1F47, 0, 16);
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 10,
                sBattleActionFrame, 1);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Battle_SetBlend(0, 0, 0);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame <= 49)
            sBattleActionFrame++;
        else
        {
            Sound_StopSfx(0);
            Battle_StartBrightnessTransition(5, 0x110);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearRasterFlags(0x4000);
            Battle_StartBrightnessTransition(10, 0x120);
            BattlePalette_StopBgCycle(14, 1);
            Battle_SetBgRequests(0xA0);
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearControlFlags(0x10);
            Battle_ClearRasterFlags(0x4000);
            Battle_EndRasterTransition();
            Battle_LoadStatusGraphics();
            cursor->displayState &= ~0x400;
            Battle_SetBgRequests(8);
            sBattleActionEffectPhase = 1;
        }
        break;
    case 1:
        Battle_SetAnimationById(&actor->animation, 0x386, 0x1B4, 13, 2);
        sBattleActionEffectPhase = 2;
        break;
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x3E, 1, 0);
            sBattleActionEffectPhase = 5;
        }
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 6;
        }
        break;
    case 6:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 8;
        break;
    case 8:
        if ((u8)Battle_UpdateTargetRecoveryEffect(actor, Battle_GetCombatants(),
            BATTLE_RECOVERY_SIDE_TARGETS, BATTLE_RECOVERY_HP_AND_MP, 999) == 1)
        {
            Battle_ResetBackgroundLoadProgress();
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if ((u8)Battle_GetBackgroundLoadProgress() > 3)
        {
            Battle_ResetAttackGauge(actor);
            result = 2;
        }
        break;
    }
    return result;
}

/** @brief 推进攻击槽的头像、蓝白光束和群体攻击演出，结束清空攻击槽。
 *  @note skill[50]；依次播放特效 0x38D、0x38E、0x399、0x39A，以命中特效 0x39B 启动 40 帧目标反应，全部反应结束后清空攻击槽并返回 1；第二参数未使用。
 *  @note 普通 C 的 0x690 字节及 140 项已解析重定位 100% 匹配；保留 u16 加载掩码和阶段 37 的公共帧递增，固定基线独立构建的完整 ROM 与 baserom 逐字节一致。
 *  既有受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0803A8D0
 */
u8 BattleAction_UpdateGaugeLightBurst(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;
    struct BattleCombatant *cursor = Battle_GetCursor();

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0x39B;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        Battle_SetControlFlags(0x10);
        Battle_ResetBackgroundLoader();
        sBattleEffectBlendEva = 0;
        Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, 16);
        break;
    case 18:
        if ((u8)Battle_UpdateBackgroundLoader(4, 2, 14, 1))
        {
            cursor->displayState |= 0x400;
            Sound_PlaySfx(0x59, 0, 1);
            sBattleActionFrame = 0;
            BattlePalette_StartBgCycle(14, 1, 1, -1, 15);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 10,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            Battle_ResetBackgroundLoader();
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if ((u8)Battle_UpdateBackgroundLoader(8, 3, 15, 0))
        {
            sBattleActionFrame = 0;
            sBattleEffectBlendEva = 0;
            Battle_SetBlend(0x1F47, 0, 16);
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 10,
                sBattleActionFrame, 1);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Battle_SetBlend(0, 0, 0);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame <= 49)
            sBattleActionFrame++;
        else
        {
            Sound_StopSfx(0);
            Battle_StartBrightnessTransition(5, 0x110);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearRasterFlags(0x4000);
            Battle_StartBrightnessTransition(10, 0x120);
            BattlePalette_StopBgCycle(14, 1);
            Battle_SetBgRequests(0xA0);
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearControlFlags(0x10);
            Battle_ClearRasterFlags(0x4000);
            Battle_EndRasterTransition();
            Battle_LoadStatusGraphics();
            Battle_SetBgRequests(8);
            cursor->displayState &= ~0x400;
            Battle_ResetBackgroundLoadProgress();
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        if ((u8)Battle_GetBackgroundLoadProgress() > 3)
            sBattleActionEffectPhase = 28;
        break;
    case 28:
        Battle_SetEffectAnimation(actor, 80, 100, 0x27C, 14, 0x38D, 5);
        sBattleActionEffectPhase = 29;
        break;
    case 29:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x97, 1, 0);
            sBattleActionEffectPhase = 30;
        }
        break;
    case 30:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->effectAnimation));
            Battle_SetEffectAnimation(actor, 80, 100, 0x27C, 14, 0x38E, 0x405);
            sBattleActionEffectPhase = 31;
        }
        break;
    case 31:
    {
        u16 loading = actor->effectAnimation.flags & BATTLE_ANIM_LOADING;

        if (!loading)
        {
            Sound_PlaySfx(0x66, 1, 1);
            sBattleActionFrame = loading;
            sBattleActionEffectPhase = 32;
        }
        break;
    }
    case 32:
        if (sBattleActionFrame <= 29)
        {
            actor->effectAnimation.y = Battle_InterpolateS16(100, -130, 30,
                sBattleActionFrame, 1);
            sBattleActionFrame++;
        }
        else
        {
            Battle_SetEffectAnimation(actor, 120, 80, 0x1B4, 14, 0x399, 0x405);
            actor->effectAnimation.priority = 0;
            sBattleActionEffectPhase = 33;
        }
        break;
    case 33:
    {
        u16 loading = actor->effectAnimation.flags & BATTLE_ANIM_LOADING;

        if (!loading)
        {
            BattlePalette_StartBgPulse(0, 3, 10, 10, 4, 4, -1, 2);
            sBattleActionFrame = loading;
            sBattleActionEffectPhase = 34;
        }
        break;
    }
    case 34:
        if (sBattleActionFrame <= 29)
            sBattleActionFrame++;
        else
        {
            sBattleActionFrame = 0;
            BattlePalette_StopBgPulse(0, 3);
            Battle_SetEffectAnimation(actor, 80, 120, 0x1B4, 14, 0x39A, 5);
            actor->effectAnimation.priority = 0;
            sBattleActionEffectPhase = 35;
        }
        break;
    case 35:
    {
        u16 loading = actor->effectAnimation.flags & BATTLE_ANIM_LOADING;

        if (!loading)
        {
            BattleAction_BeginTargetReactions(40);
            BattlePalette_StartBgFade(0, 3, 15, 15, 6, 4, 4, -1, 2);
            sBattleActionFrame = loading;
            sBattleActionEffectPhase = 36;
        }
        break;
    }
    case 36:
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            BattlePalette_ReturnBgFade(0, 3, 32);
            sBattleActionEffectPhase = 37;
        }
        break;
    case 37:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->effectAnimation));
            Sound_StopSfx(1);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
        }
        sBattleActionFrame++;
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_ResetAttackGauge(actor);
            result = 1;
        }
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进攻击槽技能演出并为队员和施法者启用伤害保护。
 *  @note 技能处理器 51 在阶段 9 等待帧计数超过 31，再对状态掩码 0xFF 的队员及施法者置 displayState 的 0x10 并清除 +0xBA 计数；清空攻击槽并返回 2，第二参数未使用。
 *  @note 保留 0..30 的空分支、u16 特效加载掩码及 u8 名单索引；普通 C 的 0x524 字节及已解析重定位 100% 匹配。
 *  @address 0x0803AF60
 */
u8 BattleAction_UpdateGaugePartyProtection(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;
    struct BattleCombatant *cursor = Battle_GetCursor();

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 18;
        Battle_SetControlFlags(0x10);
        Battle_ResetBackgroundLoader();
        sBattleEffectBlendEva = 0;
        Battle_SetBlend(0x1F4F, 0, 16);
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if ((u8)Battle_UpdateBackgroundLoader(4, 2, 14, 1))
        {
            cursor->displayState |= 0x400;
            sBattleActionFrame = 0;
            Sound_PlaySfx(0x59, 0, 1);
            BattlePalette_StartBgCycle(14, 1, 1, -1, 15);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (sBattleActionFrame <= 39)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 40,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            Battle_ResetBackgroundLoader();
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if ((u8)Battle_UpdateBackgroundLoader(9, 3, 15, 0))
        {
            sBattleActionFrame = 0;
            sBattleEffectBlendEva = 0;
            Battle_SetBlend(0x1F47, 0, 16);
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 10,
                sBattleActionFrame, 1);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Battle_SetBlend(0, 0, 0);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame <= 49)
            sBattleActionFrame++;
        else
        {
            Sound_StopSfx(0);
            Battle_StartBrightnessTransition(5, 0x110);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearRasterFlags(0x4000);
            Battle_StartBrightnessTransition(10, 0x120);
            BattlePalette_StopBgCycle(14, 1);
            Battle_SetBgRequests(0xA0);
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearControlFlags(0x10);
            Battle_ClearRasterFlags(0x4000);
            Battle_EndRasterTransition();
            Battle_LoadStatusGraphics();
            Battle_SetBgRequests(8);
            cursor->displayState &= ~0x400;
            Battle_ResetBackgroundLoadProgress();
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        if ((u8)Battle_GetBackgroundLoadProgress() > 3)
        {
            Battle_SetEffectAnimation(actor, 128, 80, 0x1B4, 14, 0x3B5, 5);
            actor->effectAnimation.priority = 0;
            sBattleActionEffectPhase = 28;
        }
        break;
    case 28:
    {
        u16 loading = actor->effectAnimation.flags & BATTLE_ANIM_LOADING;

        if (loading == 0)
        {
            Sound_PlaySfx(0x58, 0, 0);
            BattlePalette_StartBgFade(0, 3, 15, 15, 6, 4, 4, -1, 2);
            sBattleActionFrame = loading;
            sBattleActionEffectPhase = 29;
        }
        break;
    }
    case 29:
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            BattlePalette_ReturnBgFade(0, 3, 32);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 30;
        }
        break;
    case 30:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->effectAnimation));
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
        }
        sBattleActionFrame++;
        break;
    case 9:
        if (sBattleActionFrame <= 31)
            sBattleActionFrame++;
        else
        {
            u8 slots[BATTLE_PARTY_COUNT];
            struct BattleCombatant *combatants = Battle_GetCombatants();
            u8 count = Battle_ListCombatants(combatants, slots, BATTLE_SIDE_PARTY, 0xFF);
            u8 index = 0;

            for (; index < count; index++)
            {
                combatants[slots[index]].displayState |= BATTLE_DISPLAY_DAMAGE_PROTECTED;
                combatants[slots[index]].unknownBA = 0;
            }
            actor->displayState |= BATTLE_DISPLAY_DAMAGE_PROTECTED;
            actor->unknownBA = 0;
            Battle_ResetAttackGauge(actor);
            result = 2;
        }
        break;
    }
    return result;
}

/** @brief 推进攻击槽的光刃演出，对被选中的普通敌人设置 9999 待伤并返回 3。
 *  @note skill[52]；以目标低四位槽号遍历，id <= 112 时按 80% 判定写入 9999 待伤，未命中清零；完成演出后清空攻击槽并返回 3，第二参数未使用。
 *  @note 普通 C 的 0x768 字节及 134 项已解析重定位 100% 匹配；保留 u8 名单索引、阶段 32 对 displayState 的 0x800 位检查及整份特效动画复制。
 *  既有受控回放返回 3。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0803B484
 */
u8 BattleAction_UpdateGaugeLethalStrike(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;
    u8 index;
    struct BattleCombatant *cursor = Battle_GetCursor();
    struct BattleCombatant *combatants = Battle_GetCombatants();

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0;
        actor->hitReactionStartFrame = 0;
        for (index = 0; index < sBattleActionEffectTargetCount; index++)
            sBattleInstantDefeatSlots[index] = 0;
        sBattleInstantDefeatCount = 0;
        for (index = 0; index < sBattleActionEffectTargetCount; index++)
        {
            if (combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].id <= 112)
            {
                if (Battle_NextRandom() % 100 <= 79)
                {
                    sBattleInstantDefeatSlots[sBattleInstantDefeatCount] =
                        sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK;
                    combatants[sBattleInstantDefeatSlots[sBattleInstantDefeatCount]].pendingDamage = 9999;
                    sBattleInstantDefeatCount++;
                }
                else
                    combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].pendingDamage = 0;
            }
        }
        sBattleActionEffectPhase = 18;
        Battle_SetControlFlags(0x10);
        Battle_ResetBackgroundLoader();
        sBattleEffectBlendEva = 0;
        Battle_SetBlend(0x1F4F, 0, 16);
        break;
    case 18:
        if ((u8)Battle_UpdateBackgroundLoader(4, 2, 14, 1))
        {
            cursor->displayState |= 0x400;
            sBattleActionFrame = 0;
            Sound_PlaySfx(0x59, 0, 1);
            BattlePalette_StartBgCycle(14, 1, 1, -1, 15);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (sBattleActionFrame <= 39)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 40,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            Battle_ResetBackgroundLoader();
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if ((u8)Battle_UpdateBackgroundLoader(10, 3, 15, 0))
        {
            sBattleActionFrame = 0;
            sBattleEffectBlendEva = 0;
            Battle_SetBlend(0x1F47, 0, 16);
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 10,
                sBattleActionFrame, 1);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Battle_SetBlend(0, 0, 0);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame <= 49)
            sBattleActionFrame++;
        else
        {
            Sound_StopSfx(0);
            Battle_StartBrightnessTransition(5, 0x110);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearRasterFlags(0x4000);
            Battle_StartBrightnessTransition(10, 0x120);
            BattlePalette_StopBgCycle(14, 1);
            Battle_SetBgRequests(0xA0);
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearControlFlags(0x10);
            Battle_ClearRasterFlags(0x4000);
            Battle_EndRasterTransition();
            Battle_LoadStatusGraphics();
            Battle_SetBgRequests(8);
            cursor->displayState &= ~0x400;
            Battle_ResetBackgroundLoadProgress();
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        if ((u8)Battle_GetBackgroundLoadProgress() > 3)
            sBattleActionEffectPhase = 28;
        break;
    case 28:
        Battle_SetEffectAnimation(actor, 150, 50, 0x1B4, 14, 0x3C3, 0x2405);
        actor->effectAnimation.priority = 0;
        sBattleActionEffectPhase = 29;
        break;
    case 29:
    {
        u16 loading = actor->effectAnimation.flags & BATTLE_ANIM_LOADING;

        if (!loading)
        {
            sBattleActionFrame = loading;
            Sound_PlaySfx(0x59, 0, 1);
            sBattleActionEffectPhase = 30;
        }
        break;
    }
    case 30:
        if (sBattleActionFrame <= 4)
        {
            actor->effectAnimation.x = Battle_InterpolateS16(150, -60, 5,
                sBattleActionFrame, 2);
            actor->effectAnimation.y = Battle_InterpolateS16(50, 80, 5,
                sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 31;
        }
        break;
    case 31:
        if (sBattleActionFrame <= 39)
            sBattleActionFrame++;
        else if (sBattleInstantDefeatCount)
        {
            struct BattleCombatant *first = &combatants[sBattleInstantDefeatSlots[0]];

            Battle_SetEffectAnimation(first, first->homeX, first->homeY,
                0x27C, 14, 0x3C4, 0x2404);
            combatants[sBattleInstantDefeatSlots[0]].effectAnimation.priority = 0;
            sBattleActionEffectPhase = 32;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 34;
        }
        break;
    case 32:
        if (!(combatants[sBattleInstantDefeatSlots[0]].displayState & 0x800))
        {
            for (index = 1; index < sBattleInstantDefeatCount; index++)
            {
                combatants[sBattleInstantDefeatSlots[index]].effectAnimation =
                    combatants[sBattleInstantDefeatSlots[0]].effectAnimation;
                combatants[sBattleInstantDefeatSlots[index]].displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
                combatants[sBattleInstantDefeatSlots[index]].effectAnimation.x =
                    combatants[sBattleInstantDefeatSlots[index]].homeX;
                combatants[sBattleInstantDefeatSlots[index]].effectAnimation.y =
                    combatants[sBattleInstantDefeatSlots[index]].homeY;
            }
            sBattleActionEffectPhase = 33;
            sBattleActionFrame = 0;
        }
        break;
    case 33:
        if (sBattleActionFrame <= 59)
            sBattleActionFrame++;
        else
        {
            for (index = 0; index < sBattleInstantDefeatCount; index++)
                combatants[sBattleInstantDefeatSlots[index]].displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 34;
            sBattleActionFrame = 0;
        }
        break;
    case 34:
        if (sBattleActionFrame <= 4)
        {
            actor->effectAnimation.x = Battle_InterpolateS16(90, 60, 5,
                sBattleActionFrame, 2);
            actor->effectAnimation.y = Battle_InterpolateS16(130, -80, 5,
                sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Sound_StopSfx(0);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        Battle_ResetAttackGauge(actor);
        result = 3;
        break;
    }
    return result;
}

/** @brief 推进攻击槽的头像、阵型跳跃和旋转斩击演出，结束清空攻击槽。
 *  @note skill[53], skill[54]；既有受控回放返回 1。普通 C 的 0x73A 字节及 149 项已解析重定位 100% 匹配。
 *  末尾两字节零填充不计；完整 ROM 与 baserom 一致。
 *  @note 阶段 34 手动循环特效时间轴尾段的四帧，保留 0xFFFB 正偏移及加法末项的帧数查询。
 *  主动画结束时统一置位 0x100，再更新目标反应。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0803BBEC
 */
u8 BattleAction_UpdateGaugeFormationSpinSlash(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;
    struct BattleCombatant *cursor = Battle_GetCursor();

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0x3D2;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        Battle_SetControlFlags(0x10);
        Battle_ResetBackgroundLoader();
        sBattleEffectBlendEva = 0;
        Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, 16);
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if ((u8)Battle_UpdateBackgroundLoader(4, 2, 14, 1))
        {
            cursor->displayState |= 0x400;
            sBattleActionFrame = 0;
            Sound_PlaySfx(0x59, 0, 1);
            BattlePalette_StartBgCycle(14, 1, 1, -1, 15);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (sBattleActionFrame <= 39)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 40,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            Battle_ResetBackgroundLoader();
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if ((u8)Battle_UpdateBackgroundLoader(11, 3, 15, 0))
        {
            sBattleActionFrame = 0;
            sBattleEffectBlendEva = 0;
            Battle_SetBlend(0x1F47, 0, 16);
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 10,
                sBattleActionFrame, 1);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Battle_SetBlend(0, 0, 0);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame <= 49)
            sBattleActionFrame++;
        else
        {
            Sound_StopSfx(0);
            Battle_StartBrightnessTransition(5, 0x110);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearRasterFlags(0x4000);
            Battle_StartBrightnessTransition(10, 0x120);
            BattlePalette_StopBgCycle(14, 1);
            Battle_SetBgRequests(0xA0);
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearControlFlags(0x10);
            Battle_ClearRasterFlags(0x4000);
            Battle_EndRasterTransition();
            Battle_LoadStatusGraphics();
            Battle_SetBgRequests(8);
            cursor->displayState &= ~0x400;
            Battle_ResetBackgroundLoadProgress();
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        if ((u8)Battle_GetBackgroundLoadProgress() > 3)
            sBattleActionEffectPhase = 28;
        break;
    case 28:
        sBattleActionSubphase = 0;
        sBattleActionEffectPhase = 29;
        break;
    case 29:
        if (BattleAction_UpdateSkillJump(actor, target, 3) == 1)
        {
            Battle_SetAnimationById(&actor->animation, 0x3CE, 0x1B4, 13, 2);
            sBattleActionEffectPhase = 30;
        }
        break;
    case 30:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 31;
        break;
    case 31:
        if (actor->animation.timelineFrame > 7)
        {
            Battle_SetEffectAnimation(actor, actor->homeX, actor->homeY,
                0x27C, 14, 0x3CF, 0x2005);
            sBattleActionEffectPhase = 32;
        }
        break;
    case 32:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x9A, 0, 0);
            sBattleActionEffectPhase = 33;
        }
        break;
    case 33:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->effectAnimation));
            actor->effectAnimation.flags |= 0x100;
            sBattleActionEffectPhase = 34;
        }
        break;
    case 34:
    {
        actor->effectAnimation.timelineFrame =
            ((actor->effectAnimation.timelineFrame + 1) % 4) + 0xFFFB
            + Battle_GetAnimationFrameCount(&actor->effectAnimation);
        if (actor->animation.timelineFrame > 112)
        {
            Battle_SetEffectAnimation(actor, actor->homeX + 30, actor->homeY,
                0x27C, 14, 0x3D0, 0x2005);
            sBattleActionEffectPhase = 35;
        }
        break;
    }
    case 35:
    {
        u16 loading = actor->effectAnimation.flags & BATTLE_ANIM_LOADING;

        if (!loading)
        {
            Sound_PlaySfx(0x5C, 1, 0);
            sBattleActionFrame = loading;
            sBattleActionEffectPhase = 36;
        }
        break;
    }
    case 36:
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            Battle_SetEffectAnimation(actor, actor->homeX - 120, actor->homeY - 15,
                0x27C, 14, 0x3D1, 0x2405);
            sBattleActionEffectPhase = 37;
        }
        break;
    case 37:
    {
        u16 loading = actor->effectAnimation.flags & BATTLE_ANIM_LOADING;

        if (!loading)
        {
            BattleAction_BeginTargetReactions(40);
            BattlePalette_StartBgFade(0, 3, 4, 7, 21, 32, 32, -1, 2);
            sBattleActionFrame = loading;
            sBattleActionEffectPhase = 38;
        }
        break;
    }
    case 38:
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            BattlePalette_ReturnBgFade(0, 3, 16);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 39;
        }
        break;
    case 39:
        if (sBattleActionFrame <= 15)
            sBattleActionFrame++;
        else
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 40;
        }
        break;
    case 40:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            sBattleActionEffectPhase = 41;
        }
        break;
    case 41:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            sBattleActionSubphase = 12;
            sBattleActionEffectPhase = 42;
        }
        break;
    case 42:
        if (BattleAction_UpdateSkillJump(actor, target, 3) == 1)
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        Battle_ResetAttackGauge(actor);
        result = 1;
        break;
    }
    if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        actor->animation.flags |= 0x100;
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进攻击槽的黑色背景和蓝绿色光阵群体攻击演出，结束清空攻击槽。
 *  @note skill[55]；按角色编号和目标阵形隐藏、恢复其余战斗动画，恢复双方坐标与施法者待机后清空攻击槽并返回 1。
 *  @note 普通 C 的 0xAE2 字节及 186 项已解析重定位 100% 匹配，末尾两字节零填充不计；共用 u16 动画编号，四个名单循环先按 sizeof 计算字节偏移再加基址。
 *  固定基线独立构建的完整 ROM 与 baserom 逐字节一致；既有受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0803C328
 */
u8 BattleAction_UpdateGaugeLightCircle(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 slots[BATTLE_ENEMY_COUNT];
    u8 result = 0;
    u8 count;
    u8 index;
    u16 animationId;
    struct BattleCombatant *combatants = Battle_GetCombatants();
    struct BattleCombatant *cursor = Battle_GetCursor();

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedTargetX = target->homeX;
        sBattleActionSavedTargetY = target->homeY;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0x385;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        Battle_SetControlFlags(0x10);
        Battle_ResetBackgroundLoader();
        sBattleEffectBlendEva = 0;
        Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, 16);
        break;
    case 18:
        if ((u8)Battle_UpdateBackgroundLoader(4, 2, 14, 1))
        {
            cursor->displayState |= 0x400;
            sBattleActionFrame = 0;
            Sound_PlaySfx(0x59, 0, 1);
            BattlePalette_StartBgCycle(14, 1, 1, -1, 15);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 10,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            Battle_ResetBackgroundLoader();
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
    {
        u8 backgroundId = actor->id == 0 ? 5 : 6;

        if ((u8)Battle_UpdateBackgroundLoader(backgroundId, 3, 15, 0))
        {
            sBattleActionFrame = 0;
            sBattleEffectBlendEva = 0;
            Battle_SetBlend(0x1F47, 0, 16);
            sBattleActionEffectPhase = 22;
        }
        break;
    }
    case 22:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 10,
                sBattleActionFrame, 1);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Battle_SetBlend(0, 0, 0);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame <= 49)
            sBattleActionFrame++;
        else
        {
            Sound_StopSfx(0);
            Battle_StartBrightnessTransition(60, 0x110);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearRasterFlags(0x4000);
            Battle_StartBrightnessTransition(65, 0x120);
            BattlePalette_StopBgCycle(14, 1);
            Battle_SetBgRequests(0xA0);
            count = Battle_ListCombatants(combatants, slots, BATTLE_SIDE_PARTY,
                BATTLE_STATUS_MASK_ALL);
            for (index = 0; index < count; index++)
            {
                s32 offset = slots[index] * sizeof(struct BattleCombatant);
                struct BattleCombatant *combatant = (struct BattleCombatant *)(offset + (u32)combatants);

                if (combatant->id != actor->id)
                    combatant->animation.flags |= BATTLE_ANIM_HIDDEN;
            }
            count = Battle_ListCombatants(combatants, slots, BATTLE_SIDE_ENEMY,
                BATTLE_STATUS_MASK_ALL);
            for (index = 0; index < count; index++)
            {
                s32 offset = slots[index] * sizeof(struct BattleCombatant);
                struct BattleCombatant *combatant = (struct BattleCombatant *)(offset + (u32)combatants);

                if (combatant->formationBits != target->formationBits)
                    combatant->animation.flags |= BATTLE_ANIM_HIDDEN;
            }
            sBattleActionScratch86E = 224;
            actor->homeX = 224;
            sBattleActionScratch86F = 104;
            actor->homeY = 104;
            target->homeX = 88;
            target->homeY = 104;
            BattlePalette_StartBgFade(0, 3, -31, -31, -31, 4, 4, -1, 2);
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearControlFlags(0x10);
            Battle_ClearRasterFlags(0x4000);
            Battle_EndRasterTransition();
            Battle_LoadStatusGraphics();
            Battle_SetBgRequests(8);
            Battle_ResetBackgroundLoadProgress();
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        if ((u8)Battle_GetBackgroundLoadProgress() > 3)
            sBattleActionEffectPhase = 28;
        break;
    case 28:
    {
        animationId = actor->id == 0 ? 0x35F : 0x36E;

        Battle_SetAnimationById(&actor->animation, animationId, 0x1B4, 13, 2);
        sBattleActionEffectPhase = 29;
        break;
    }
    case 29:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x9A, 0, 0);
            sBattleActionEffectPhase = 30;
        }
        break;
    case 30:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags |= 0x100;
            sBattleActionFrame = 0;
            Sound_PlaySfx(0x31, 1, 0);
            BattlePalette_StartObjFade(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation),
                7, 14, 28, 4, 4, -1, 2);
            sBattleActionEffectPhase = 31;
        }
        break;
    case 31:
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            BattlePalette_ReturnObjFade(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation), 16);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 32;
        }
        break;
    case 32:
        if (sBattleActionFrame <= 15)
            sBattleActionFrame++;
        else
        {
            animationId = actor->id == 0 ? 0x360 : 0x36F;

            Battle_SetAnimationById(&actor->animation, animationId, 0x1B4, 13, 0x502);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 33;
        }
        break;
    case 33:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 34;
        break;
    case 34:
        Battle_SetEffectAnimation(actor, 120, 80, 0x2E0, 14, 0x384, 0x2004);
        actor->effectAnimation.priority = 0;
        sBattleActionEffectPhase = 35;
        break;
    case 35:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            actor->animation.flags &= ~0x100;
            Sound_PlaySfx(0x31, 1, 1);
            sBattleActionEffectPhase = 36;
        }
        break;
    case 36:
        if (sBattleActionFrame <= 19)
        {
            if (sBattleActionFrame == 8)
                BattleAction_BeginTargetReactions(20);
            actor->homeX = Battle_InterpolateS16(sBattleActionScratch86E,
                -sBattleActionScratch86E + 24, 20, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            Sound_StopSfx(1);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 37;
        }
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->effectAnimation));
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
        }
        break;
    case 37:
    {
        animationId = actor->id == 0 ? 0x361 : 0x37A;

        Battle_SetAnimationById(&actor->animation, animationId, 0x1B4, 13, 2);
        sBattleActionEffectPhase = 38;
        break;
    }
    case 38:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 39;
        break;
    case 39:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags |= 0x100;
            Battle_SetEffectAnimation(actor, 30, 25, 0x27C, 14, 0x383, 0x14);
            actor->effectAnimation.priority = 0;
            sBattleEffectBlendEva = 0;
            Battle_SetBlend(0x410, 0, 0);
            sBattleActionEffectPhase = 40;
        }
        break;
    case 40:
    {
        u16 loading = actor->effectAnimation.flags & BATTLE_ANIM_LOADING;

        if (!loading)
        {
            sBattleActionFrame = loading;
            sBattleActionEffectPhase = 41;
        }
        break;
    }
    case 41:
        if (sBattleActionFrame <= 19)
        {
            Battle_SetBlend(0x410, Battle_InterpolateS16(0, 16, 20,
                sBattleActionFrame, 2), 0);
            sBattleActionFrame++;
        }
        else
        {
            actor->effectAnimation.flags &= ~BATTLE_ANIM_SEMITRANSPARENT;
            Battle_StartBrightnessTransition(5, 0x110);
            sBattleActionEffectPhase = 42;
        }
        break;
    case 42:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearRasterFlags(0x4000);
            Battle_StartBrightnessTransition(10, 0x120);
            count = Battle_ListCombatants(combatants, slots, BATTLE_SIDE_PARTY,
                BATTLE_STATUS_MASK_ALL);
            for (index = 0; index < count; index++)
            {
                s32 offset = slots[index] * sizeof(struct BattleCombatant);
                struct BattleCombatant *combatant = (struct BattleCombatant *)(offset + (u32)combatants);

                if (combatant->id != actor->id)
                    combatant->animation.flags &= ~BATTLE_ANIM_HIDDEN;
            }
            count = Battle_ListCombatants(combatants, slots, BATTLE_SIDE_ENEMY,
                BATTLE_STATUS_MASK_ALL);
            for (index = 0; index < count; index++)
            {
                s32 offset = slots[index] * sizeof(struct BattleCombatant);
                struct BattleCombatant *combatant = (struct BattleCombatant *)(offset + (u32)combatants);

                if (combatant->formationBits != target->formationBits)
                    combatant->animation.flags &= ~BATTLE_ANIM_HIDDEN;
            }
            actor->homeX = sBattleActionSavedX;
            actor->homeY = sBattleActionSavedY;
            target->homeX = sBattleActionSavedTargetX;
            target->homeY = sBattleActionSavedTargetY;
            BattlePalette_ReturnBgFade(0, 3, 4);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 43;
        }
        break;
    case 43:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            cursor->displayState &= ~0x400;
            Battle_ClearRasterFlags(0x4000);
            Battle_EndRasterTransition();
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 44;
        }
        break;
    case 44:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        Battle_ResetAttackGauge(actor);
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进攻击槽技能演出并提升施法者 MEN 与一次魔法攻击威力。
 *  @note 技能处理器 56 使用 MEN 加成 30%、动画 0x39C；恢复待机后置 displayState 的 0x4000，清空攻击槽并返回 2，第二参数未使用。
 *  @note 保留 0..29 的空分支和逐阶段渐变；普通 C 的 0x3FE 字节及已解析重定位 100% 匹配，末尾两字节零填充不计。
 *  @address 0x0803CE0C
 */
u8 BattleAction_UpdateGaugeMagicBoost(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;
    struct BattleCombatant *cursor = Battle_GetCursor();

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        Battle_BeginStatBoost(BATTLE_MODIFIER_MEN, 30, 0x39C);
        Battle_SetControlFlags(0x10);
        Battle_ResetBackgroundLoader();
        sBattleEffectBlendEva = 0;
        Battle_SetBlend(0x1F4F, 0, 16);
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if ((u8)Battle_UpdateBackgroundLoader(4, 2, 14, 1))
        {
            sBattleActionFrame = 0;
            cursor->displayState |= 0x400;
            Sound_PlaySfx(0x59, 0, 1);
            BattlePalette_StartBgCycle(14, 1, 1, -1, 15);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (sBattleActionFrame <= 39)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 40,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            Battle_ResetBackgroundLoader();
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if ((u8)Battle_UpdateBackgroundLoader(8, 3, 15, 0))
        {
            sBattleActionFrame = 0;
            sBattleEffectBlendEva = 0;
            Battle_SetBlend(0x1F47, 0, 16);
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 10,
                sBattleActionFrame, 1);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Battle_SetBlend(0, 0, 0);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame <= 49)
            sBattleActionFrame++;
        else
        {
            Sound_StopSfx(0);
            Battle_StartBrightnessTransition(5, 0x110);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearRasterFlags(0x4000);
            Battle_StartBrightnessTransition(10, 0x120);
            BattlePalette_StopBgCycle(14, 1);
            Battle_SetBgRequests(0xA0);
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearControlFlags(0x10);
            Battle_ClearRasterFlags(0x4000);
            Battle_EndRasterTransition();
            Battle_LoadStatusGraphics();
            Battle_SetBgRequests(8);
            cursor->displayState &= ~0x400;
            Battle_ResetBackgroundLoadProgress();
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        if ((u8)Battle_GetBackgroundLoadProgress() > 3)
            sBattleActionEffectPhase = 28;
        break;
    case 28:
        if ((u8)Battle_UpdateEnemyStatBoost(actor) == 1)
            sBattleActionEffectPhase = 29;
        break;
    case 29:
        Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
            sBattleActionSavedPaletteSlot, 0);
        sBattleActionEffectPhase = 6;
        break;
    case 6:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        actor->displayState |= BATTLE_DISPLAY_MAGIC_POWER_BOOST;
        Battle_ResetAttackGauge(actor);
        result = 2;
        break;
    }
    return result;
}

/** @brief 播放另一套攻击槽技能演出，提升施法者 MEN 与一次魔法攻击威力。
 *  @note 技能处理器 57 使用 MEN 加成 30%、动画 0x3B6 与背景 9；恢复待机后置 displayState 的 0x4000，清空攻击槽并返回 2，第二参数未使用。
 *  @note 保留空 case，阶段 18 先写光标标志再清帧计数；普通 C 的 0x3FE 字节及已解析重定位 100% 匹配，末尾两字节零填充不计；完整 ROM 与 baserom 一致。
 *  @note Seeded GPBA replay returned 2 after 445 dispatch updates (394 entry hits).
 *  Dispatch update counts include the skill-name window.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x0803D20C
 */
u8 BattleAction_UpdateAlternateGaugeMagicBoost(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;
    struct BattleCombatant *cursor = Battle_GetCursor();

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        Battle_BeginStatBoost(BATTLE_MODIFIER_MEN, 30, 0x3B6);
        Battle_SetControlFlags(0x10);
        Battle_ResetBackgroundLoader();
        sBattleEffectBlendEva = 0;
        Battle_SetBlend(0x1F4F, 0, 16);
        sBattleActionEffectPhase = 18;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if ((u8)Battle_UpdateBackgroundLoader(4, 2, 14, 1))
        {
            cursor->displayState |= 0x400;
            sBattleActionFrame = 0;
            Sound_PlaySfx(0x59, 0, 1);
            BattlePalette_StartBgCycle(14, 1, 1, -1, 15);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (sBattleActionFrame <= 39)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 40,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            Battle_ResetBackgroundLoader();
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if ((u8)Battle_UpdateBackgroundLoader(9, 3, 15, 0))
        {
            sBattleActionFrame = 0;
            sBattleEffectBlendEva = 0;
            Battle_SetBlend(0x1F47, 0, 16);
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 10,
                sBattleActionFrame, 1);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Battle_SetBlend(0, 0, 0);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame <= 49)
            sBattleActionFrame++;
        else
        {
            Sound_StopSfx(0);
            Battle_StartBrightnessTransition(5, 0x110);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearRasterFlags(0x4000);
            Battle_StartBrightnessTransition(10, 0x120);
            BattlePalette_StopBgCycle(14, 1);
            Battle_SetBgRequests(0xA0);
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearControlFlags(0x10);
            Battle_ClearRasterFlags(0x4000);
            Battle_EndRasterTransition();
            Battle_LoadStatusGraphics();
            Battle_SetBgRequests(8);
            cursor->displayState &= ~0x400;
            Battle_ResetBackgroundLoadProgress();
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        if ((u8)Battle_GetBackgroundLoadProgress() > 3)
            sBattleActionEffectPhase = 28;
        break;
    case 28:
        if ((u8)Battle_UpdateEnemyStatBoost(actor) == 1)
            sBattleActionEffectPhase = 29;
        break;
    case 29:
        Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
            sBattleActionSavedPaletteSlot, 0);
        sBattleActionEffectPhase = 6;
        break;
    case 6:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        actor->displayState |= BATTLE_DISPLAY_MAGIC_POWER_BOOST;
        Battle_ResetAttackGauge(actor);
        result = 2;
        break;
    }
    return result;
}

/** @brief 推进攻击槽的持光球、腾跃和光面攻击演出，结束清空攻击槽。
 *  @note skill[58]；接近目标后启动 20 帧反应，回位后用特效 0x3C6 启动 50 帧反应，全部反应结束后清空攻击槽并返回 1。
 *  @note 普通 C 的 0x8C0 字节及已解析重定位 100% 匹配，固定本轮基线独立构建的完整 ROM 与 baserom 逐字节一致。
 *  保留 u16 加载掩码及 35→44→36 的阶段顺序；X 坐标表达式先写目标位置再加半宽，保持尺寸截断与坐标读取顺序。
 *  既有受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0803D60C
 */
u8 BattleAction_UpdateGaugeLightOrbStrike(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;
    struct BattleCombatant *cursor = Battle_GetCursor();

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        Battle_SetControlFlags(0x10);
        Battle_ResetBackgroundLoader();
        sBattleEffectBlendEva = 0;
        Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, 16);
        break;
    case 18:
        if ((u8)Battle_UpdateBackgroundLoader(4, 2, 14, 1))
        {
            cursor->displayState |= 0x400;
            sBattleActionFrame = 0;
            Sound_PlaySfx(0x59, 0, 1);
            BattlePalette_StartBgCycle(14, 1, 1, -1, 15);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 10,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            Battle_ResetBackgroundLoader();
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if ((u8)Battle_UpdateBackgroundLoader(10, 3, 15, 0))
        {
            sBattleActionFrame = 0;
            sBattleEffectBlendEva = 0;
            Battle_SetBlend(0x1F47, 0, 16);
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 10,
                sBattleActionFrame, 1);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Battle_SetBlend(0, 0, 0);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame <= 49)
            sBattleActionFrame++;
        else
        {
            Sound_StopSfx(0);
            Battle_StartBrightnessTransition(60, 0x110);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearRasterFlags(0x4000);
            Battle_StartBrightnessTransition(65, 0x120);
            BattlePalette_StopBgCycle(14, 1);
            Battle_SetBgRequests(0xA0);
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearControlFlags(0x10);
            Battle_ClearRasterFlags(0x4000);
            Battle_EndRasterTransition();
            Battle_LoadStatusGraphics();
            Battle_SetBgRequests(8);
            cursor->displayState &= ~0x400;
            Battle_ResetBackgroundLoadProgress();
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        if ((u8)Battle_GetBackgroundLoadProgress() > 3)
            sBattleActionEffectPhase = 28;
        break;
    case 28:
        Battle_SetAnimationById(&actor->animation, 0x3BB, 0x1B4, 13, 2);
        sBattleActionEffectPhase = 29;
        break;
    case 29:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 30;
        break;
    case 30:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags |= 0x100;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 31;
        }
        break;
    case 31:
        if (sBattleActionFrame <= 9)
        {
            actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                target->homeX + (Battle_GetCombatantDimension(target, 0) >> 1)
                    - sBattleActionSavedX,
                10, sBattleActionFrame, 2);
            actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                target->homeY - sBattleActionSavedY + 1,
                10, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Battle_SetAnimationById(&actor->animation, 0x3BC, 0x1B4, 13, 0x102);
            sBattleActionEffectPhase = 32;
        }
        break;
    case 32:
    {
        u16 loading = actor->animation.flags & BATTLE_ANIM_LOADING;

        if (!loading)
        {
            actor->animation.flags &= ~0x100;
            actor->hitEffectId = loading;
            actor->hitReactionStartFrame = 5;
            BattleAction_BeginTargetReactions(20);
            sBattleActionEffectPhase = 33;
        }
        break;
    }
    case 33:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            Battle_SetAnimationById(&actor->animation, 0x3BD, 0x1B4, 13, 0x102);
            sBattleActionEffectPhase = 34;
        }
        break;
    case 34:
    {
        u16 loading = actor->animation.flags & BATTLE_ANIM_LOADING;

        if (!loading)
        {
            BattlePalette_StartObjFade(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation),
                28, 14, 7, 32, 32, -1, 2);
            sBattleActionFrame = loading;
            sBattleActionEffectPhase = 35;
        }
        break;
    }
    case 35:
        if (sBattleActionFrame <= 31)
            sBattleActionFrame++;
        else
        {
            BattlePalette_ReturnObjFade(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation), 4);
            sBattleActionEffectPhase = 44;
        }
        break;
    case 44:
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            Battle_SetAnimationById(&actor->animation, 0x3BE, 0x1B4, 13, 0x102);
            sBattleActionEffectPhase = 36;
        }
        break;
    case 36:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Battle_SetEffectAnimation(target, target->homeX,
                target->homeY - (Battle_GetCombatantDimension(target, 0) >> 1),
                0x27C, 14, 0x3C5, 4);
            sBattleActionEffectPhase = 37;
        }
        break;
    case 37:
        if (!(target->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x58, 0, 0);
            actor->animation.flags &= ~0x100;
            sBattleActionEffectPhase = 38;
        }
        break;
    case 38:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags |= 0x100;
        }
        if (target->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            target->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            Battle_StartBrightnessTransition(5, 0x110);
            sBattleActionEffectPhase = 39;
        }
        break;
    case 39:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            actor->homeX = sBattleActionSavedX;
            actor->homeY = sBattleActionSavedY;
            sBattleActionEffectPhase = 40;
        }
        break;
    case 40:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Battle_ClearRasterFlags(0x4000);
            Battle_StartBrightnessTransition(5, 0x120);
            sBattleActionEffectPhase = 41;
        }
        break;
    case 41:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearRasterFlags(0x4000);
            Battle_EndRasterTransition();
            actor->hitEffectId = 0x3C6;
            actor->hitReactionStartFrame = 0;
            BattleAction_BeginTargetReactions(50);
            BattlePalette_StartBgFade(0, 3, 28, 14, 7, 4, 4, -1, 2);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 42;
        }
        break;
    case 42:
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            BattlePalette_ReturnBgFade(0, 3, 32);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 43;
        }
        break;
    case 43:
        if (sBattleActionFrame <= 31)
            sBattleActionFrame++;
        else
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_ResetAttackGauge(actor);
            result = 1;
        }
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进攻击槽的反复跳跃斩击及返程演出，结束清空攻击槽。
 *  @note skill[59]；跳跃模式 4，斩击音时间轴为 50/80/104/160，启动 190/20 帧目标反应，返程及全部反应结束后清空攻击槽并返回 1。
 *  @note 普通 C 的 0x6C0 字节及 156 项已解析重定位 100% 匹配；保留 37..47 的空阶段、时间轴 switch 及反应等待条件。
 *  既有受控回放返回 1。
 *  语义和逐例证据见 docs/BATTLE-ACTION-CONFIRMATION-20261115.md。
 *  @address 0x0803DECC
 */
u8 BattleAction_UpdateGaugeRepeatedJumpSlashes(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;
    struct BattleCombatant *cursor = Battle_GetCursor();

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionFrame = 0;
        BattleAction_ClearTargetPendingDamage(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 1;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        Battle_SetControlFlags(0x10);
        Battle_ResetBackgroundLoader();
        sBattleEffectBlendEva = 0;
        Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, 16);
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 37:
    case 38:
    case 39:
    case 40:
    case 41:
    case 42:
    case 43:
    case 44:
    case 45:
    case 46:
    case 47:
        break;
    case 18:
        if ((u8)Battle_UpdateBackgroundLoader(4, 2, 14, 1))
        {
            cursor->displayState |= 0x400;
            sBattleActionFrame = 0;
            Sound_PlaySfx(0x59, 0, 1);
            BattlePalette_StartBgCycle(14, 1, 1, -1, 15);
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 10,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F4F, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            Battle_ResetBackgroundLoader();
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if ((u8)Battle_UpdateBackgroundLoader(11, 3, 15, 0))
        {
            sBattleActionFrame = 0;
            sBattleEffectBlendEva = 0;
            Battle_SetBlend(0x1F47, 0, 16);
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame <= 9)
        {
            sBattleEffectBlendEva = Battle_InterpolateS16(0, 16, 10,
                sBattleActionFrame, 1);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, 16);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame <= 19)
        {
            sBattleEffectBlendEvb = Battle_InterpolateS16(16, -16, 20,
                sBattleActionFrame, 2);
            Battle_SetBlend(0x1F47, sBattleEffectBlendEva, sBattleEffectBlendEvb);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Battle_SetBlend(0, 0, 0);
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame <= 49)
            sBattleActionFrame++;
        else
        {
            Sound_StopSfx(0);
            Battle_StartBrightnessTransition(5, 0x110);
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearRasterFlags(0x4000);
            Battle_StartBrightnessTransition(10, 0x120);
            BattlePalette_StopBgCycle(14, 1);
            Battle_SetBgRequests(0xA0);
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_ClearControlFlags(0x10);
            Battle_ClearRasterFlags(0x4000);
            Battle_EndRasterTransition();
            Battle_LoadStatusGraphics();
            Battle_SetBgRequests(8);
            cursor->displayState &= ~0x400;
            Battle_ResetBackgroundLoadProgress();
            sBattleActionEffectPhase = 27;
        }
        break;
    case 27:
        if ((u8)Battle_GetBackgroundLoadProgress() > 3)
        {
            sBattleActionSubphase = 0;
            sBattleActionEffectPhase = 28;
        }
        break;
    case 28:
        if (BattleAction_UpdateSkillJump(actor, target, 4) == 1)
        {
            Battle_SetAnimationById(&actor->animation, 0x3D3, 0x1B4, 13, 2);
            if (target->id < BATTLE_CHARACTER_ID_COUNT)
                actor->animation.flags |= BATTLE_ANIM_FLIP_X;
            sBattleActionEffectPhase = 29;
        }
        break;
    case 29:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            BattleAction_BeginTargetReactions(190);
            sBattleActionEffectPhase = 30;
        }
        break;
    case 30:
        switch (actor->animation.timelineFrame)
        {
        case 50:
        case 80:
        case 104:
        case 160:
            Sound_PlaySfx(0x37, 0, 0);
            break;
        }
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            Battle_SetAnimationById(&actor->animation, 0x3D4, 0x1B4, 13, 0x102);
            sBattleActionEffectPhase = 31;
        }
        break;
    case 31:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x1D, 2, 0);
            actor->animation.flags &= ~0x100;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 32;
            sBattleActionSavedTargetY = actor->homeY - 2;
        }
        break;
    case 32:
        if (sBattleActionFrame <= 39)
        {
            actor->verticalLift = Battle_InterpolateS16(0, actor->homeY >> 1,
                40, sBattleActionFrame, 2);
            actor->homeX = Battle_InterpolateS16(sBattleActionScratch86E,
                240 - sBattleActionScratch86E, 40, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            Battle_SetAnimationById(&actor->animation, 0x3D5, 0x1B4, 13, 2);
            sBattleActionEffectPhase = 33;
        }
        break;
    case 33:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 34;
        break;
    case 34:
        if (sBattleActionFrame <= 9)
        {
            actor->verticalLift = Battle_InterpolateS16(actor->homeY >> 1,
                -(actor->homeY >> 1), 10, sBattleActionFrame, 2);
            actor->homeX = Battle_InterpolateS16(240, sBattleActionScratch86E - 240,
                10, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            actor->verticalLift = 0;
            sBattleActionSubphase = 12;
            sBattleActionScratch86E = actor->homeX;
            sBattleActionScratch86F = actor->homeY;
            BattleAction_BeginTargetReactions(20);
            sBattleActionEffectPhase = 35;
        }
        break;
    case 35:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            sBattleActionEffectPhase = 36;
        break;
    case 36:
        if (BattleAction_UpdateSkillJump(actor, target, 4) == 1)
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_ResetAttackGauge(actor);
            result = 1;
        }
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Advance the shared skill jump toward the target and back to the saved position.
 *  @note Subphase zero prepares approach, twelve prepares return, and twenty-three completes.
 *  Both legs interpolate for thirty frames; the target-mode byte selects placement.
 *  GPBA Sword Dance replay observed both caller sites with mode zero (151 calls total).
 *  @address 0x0803E58C
 */
INCLUDE_ASM("asm/nonmatchings/battle_action_effects", BattleAction_UpdateSkillJump);

/** @brief Jump to the target formation's center, then descend and land.
 *  @note All 0x4E8 bytes and resolved relocations match from ordinary C.
 *  Keep signed division by two for the sorted coordinate extrema; X also includes the spacing offset.
 *  Approach lasts twenty frames and descent ten; the third argument is unused and subphase 23 returns one.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x0803ED34
 */
u8 BattleAction_UpdateFormationCenterJump(struct BattleCombatant *actor,
    struct BattleCombatant *target, u8 unusedTargetMode)
{
    u8 slots[BATTLE_ENEMY_COUNT];
    u8 result = 0;
    u8 count;
    struct BattleCombatant *combatants;
    u16 loading;
    u16 animationId;

    switch (sBattleActionSubphase)
    {
    case 0:
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionFrame = 0;
        combatants = Battle_GetCombatants();
        count = Battle_ListAlliesInFormation(target, slots, 0xF, 0xFF);
        Battle_SortCombatantSlots(slots, count, BATTLE_STAT_HOME_X);
        sBattleActionScratch86E = combatants[slots[count - 1]].homeX
            + (combatants[slots[0]].homeX - combatants[slots[count - 1]].homeX) / 2;
        sBattleActionScratch86E += (actor->attackSpacingWidth >> 1)
            + (combatants[slots[0]].unknownC4 >> 2);
        Battle_SortCombatantSlots(slots, count, BATTLE_STAT_HOME_Y);
        sBattleActionScratch86F = combatants[slots[count - 1]].homeY
            + (combatants[slots[0]].homeY - combatants[slots[count - 1]].homeY) / 2;
        sBattleActionSubphase = 3;
        break;
    case 3:
        animationId = actor->id == 0 ? 0x350 : 0x353;
        Battle_SetAnimationById(&actor->animation, animationId, 0x1B4, 13, 2);
        if (target->id <= 10)
            actor->animation.flags |= BATTLE_ANIM_FLIP_X;
        sBattleActionSubphase = 4;
        break;
    case 4:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionSubphase = 5;
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            sBattleActionSubphase = 6;
        }
        break;
    case 6:
        animationId = actor->id == 0 ? 0x351 : 0x354;
        Battle_SetAnimationById(&actor->animation, animationId, 0x1B4, 13, 0x402);
        if (target->id <= 10)
            actor->animation.flags |= BATTLE_ANIM_FLIP_X;
        sBattleActionSubphase = 7;
        break;
    case 7:
        loading = actor->animation.flags & BATTLE_ANIM_LOADING;
        if (loading == 0)
        {
            Sound_PlaySfx(0x1D, 2, 0);
            sBattleActionSubphase = 8;
            sBattleActionFrame = loading;
        }
        break;
    case 8:
        if (sBattleActionFrame <= 19)
        {
            actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                sBattleActionScratch86E - sBattleActionSavedX, 20,
                sBattleActionFrame, 0);
            actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                sBattleActionScratch86F - sBattleActionSavedY, 20,
                sBattleActionFrame, 0);
            actor->verticalLift = Battle_InterpolateS16(0, 50, 20,
                sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionSubphase = 13;
        }
        break;
    case 18:
        if (sBattleActionFrame <= 9)
        {
            actor->verticalLift = Battle_InterpolateS16(50, -50, 10,
                sBattleActionFrame, 1);
            sBattleActionFrame++;
        }
        else
        {
            actor->verticalLift = 0;
            actor->homeY = sBattleActionScratch86F;
            sBattleActionFrame = 0;
            sBattleActionSubphase = 23;
        }
        break;
    case 13:
        animationId = actor->id == 0 ? 0x35A : 0x369;
        Battle_SetAnimationById(&actor->animation, animationId, 0x1B4, 13, 2);
        if (target->id <= 10)
            actor->animation.flags |= BATTLE_ANIM_FLIP_X;
        sBattleActionSubphase = 14;
        break;
    case 14:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags = (actor->animation.flags & ~BATTLE_ANIM_FINISHED) | 0x100;
            sBattleActionSubphase = 16;
        }
        break;
    case 16:
        animationId = actor->id == 0 ? 0x35B : 0x36A;
        Battle_SetAnimationById(&actor->animation, animationId, 0x1B4, 13, 2);
        if (target->id <= 10)
            actor->animation.flags |= BATTLE_ANIM_FLIP_X;
        sBattleActionSubphase = 17;
        break;
    case 17:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionSubphase = 18;
        break;
    case 9:
        Battle_SetAnimationById(&actor->animation, sBattleActionAnimationId + 2,
            0x1B4, 13, 2);
        if (target->id <= 10)
            actor->animation.flags |= BATTLE_ANIM_FLIP_X;
        sBattleActionSubphase = 10;
        break;
    case 10:
        loading = actor->animation.flags & BATTLE_ANIM_LOADING;
        if (loading == 0)
        {
            Sound_PlaySfx(0x43, 2, 0);
            sBattleActionSubphase = 11;
            sBattleActionFrame = loading;
        }
        break;
    case 11:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            sBattleActionSubphase = 23;
        }
        break;
    case 23:
        result = 1;
        break;
    }
    return result;
}

/** @brief Draw a zero-based skill name into two rows of the action window.
 *  @note All 0x10C bytes and resolved relocations match from ordinary C.
 *  Keep the palette as u8 and stream/cache indices as u16; glyphs start at
 *  tilemap offset 0x63, and an unmatched extended glyph uses glyphCount + 0xE0.
 *  @address 0x0803F21C
 */
void BattleAction_DrawSkillName(u16 *tilemap, u16 skillId)
{
    u16 *tiles = tilemap + 0x63;
    u8 palette = 11;
    u16 index = 0;
    u16 entry = 0;
    u16 glyphCount;
    u16 *glyphIds;
    const u16 *text;

    while (entry < skillId)
    {
        if (D_0839B462[index] == SCRIPT_TEXT_END)
            entry++;
        index++;
    }
    text = D_0839B462 + index;
    Script_CacheGlyphsFromWords(text,
        SCRIPT_GLYPH_SCAN_NAME & ~SCRIPT_GLYPH_SCAN_KEEP_CACHE);
    glyphCount = Script_GetGlyphCache(&glyphIds);
    for (index = 0; text[index] != SCRIPT_TEXT_END; index++)
    {
        if (text[index] < SCRIPT_EXTENDED_GLYPH_BASE)
        {
            tiles[index] = (palette << 12) + (text[index] & 0xFF) * 2;
            tiles[index + SCRIPT_TILEMAP_WIDTH] = (palette << 12) + ((text[index] & 0xFF) * 2 + 1);
        }
        else
        {
            u16 glyph;

            for (glyph = 0; glyph < glyphCount; glyph++)
            {
                if (text[index] == glyphIds[glyph])
                    break;
            }
            glyph += SCRIPT_EXTENDED_GLYPH_BASE;
            tiles[index] = (palette << 12) + glyph * 2;
            tiles[index + SCRIPT_TILEMAP_WIDTH] = (palette << 12) + (glyph * 2 + 1);
        }
    }
}

/** @brief Open, draw, hold and close the skill-name window before an action.
 *  @note All 0x11C bytes and resolved relocations match from ordinary C.
 *  Keep explicit u8 window-state casts; wait for glyph upload and hold forty updates.
 *  Closing returns zero; completion is reported on the following idle update.
 *  @address 0x0803F328
 */
u8 BattleAction_UpdateSkillNameWindow(u8 skillId)
{
    u8 result = 0;

    switch (sBattleSkillNamePhase)
    {
    case BATTLE_SKILL_NAME_IDLE:
        result = 1;
        break;
    case BATTLE_SKILL_NAME_OPEN:
        Battle_ResetWindows();
        Battle_InitWindowTilemap();
        Battle_OpenWindow(0, gBattleWindowTilemap, 11, 2, 2, 1, 2, 12, 4);
        sBattleActionFrame = 0;
        sBattleSkillNamePhase = BATTLE_SKILL_NAME_WAIT_OPEN;
        break;
    case BATTLE_SKILL_NAME_WAIT_OPEN:
        if ((u8)Battle_GetWindowState(0) == BATTLE_WINDOW_OPEN)
        {
            BattleAction_DrawSkillName(gBattleWindowTilemap, skillId);
            Battle_SetControlFlags(0x400);
            sBattleSkillNamePhase = BATTLE_SKILL_NAME_WAIT_GLYPH_UPLOAD;
        }
        break;
    case BATTLE_SKILL_NAME_WAIT_GLYPH_UPLOAD:
        if (!(Battle_GetControlFlags() & 0x400))
            sBattleSkillNamePhase = BATTLE_SKILL_NAME_HOLD;
        break;
    case BATTLE_SKILL_NAME_HOLD:
        if (sBattleActionFrame < 40)
        {
            sBattleActionFrame++;
        }
        else
        {
            Battle_CloseWindow(0, 2, 2);
            sBattleSkillNamePhase = BATTLE_SKILL_NAME_WAIT_CLOSED;
        }
        break;
    case BATTLE_SKILL_NAME_WAIT_CLOSED:
        if ((u8)Battle_GetWindowState(0) == BATTLE_WINDOW_CLOSED)
        {
            Battle_HideWindowLayer();
            sBattleSkillNamePhase = BATTLE_SKILL_NAME_IDLE;
        }
        break;
    }
    Battle_UpdateWindows();
    return result;
}

/**
 * @brief 按角色编号与行动类别派发队员、普通敌人和 Boss 的行动
 * @note 普通 C 的 0x170 字节及解析后的重定位完全匹配。各分支直接返回低八位，
 *       队员技能与 Boss 共用 u8 索引，技能派发先等待名称窗口完成。
 *       调用者已限定攻击/技能；未派发路径不提供定义的返回值。
 * @address 0x0803F444
 */
u8 BattleAction_Update(struct BattleCombatant *actor, struct BattleCombatant *target)
{
    u8 handlerId;

    if (actor->id < BATTLE_CHARACTER_ID_COUNT)
    {
        switch (actor->action)
        {
        case BATTLE_ACTION_ATTACK:
            if (actor->id == 7 && actor->equipment[0] != 0)
                return BattleAction_UpdateCharacter7ProjectileAttack(actor, target);
            else if (actor->id == 4 && Battle_GetWeaponActionVariant(actor) == 2)
                return BattleAction_UpdateCharacter4RandomProjectileAttack(actor, target);
            else
                return BattleAction_UpdateNormalAttack(actor, target);
        case BATTLE_ACTION_SKILL:
        {
            if (actor->skillSelection < BATTLE_SKILL_COUNT)
                handlerId = actor->skills[actor->skillSelection];
            else
                handlerId = actor->skillSelection;
            if (BattleAction_UpdateSkillNameWindow(handlerId) == 1)
                return gBattleSkillActionHandlers[handlerId](actor, target);
            else
                return 0;
        }
        }
    }
    else if ((u8)(actor->id - BATTLE_CHARACTER_ID_COUNT) <= 101)
    {
        switch (actor->action)
        {
        case BATTLE_ACTION_ATTACK:
            return gBattleEnemyActionHandlers[actor->source.enemy->actionSelection.handlers.attackHandlerId](actor, target);
        case BATTLE_ACTION_SKILL:
            return gBattleEnemyActionHandlers[actor->source.enemy->actionSelection.handlers.skillHandlerId](actor, target);
        }
    }
    else if (actor->id > 112)
    {
        const struct BattleBossInfo *info = actor->source.boss;

        handlerId = info->attackHandlerId;

        switch (actor->action)
        {
        case BATTLE_ACTION_ATTACK:
            break;
        case BATTLE_ACTION_SKILL:
            handlerId = info->skillHandlerIds[actor->enemySkillIndex];
            break;
        }
        return gBattleBossActionHandlers[handlerId](actor, target);
    }
}

/**
 * @brief 重建行动目标列表并累加有效目标的待结算伤害
 * @note 0xA4 字节及解析后的重定位完全匹配。仅处理 (entry & 0xF0) == 0x10；
 *       助手结果保留 u32，调用后重读共享槽号；旧值转 u16 后先累加再半字写回，索引为 u8。
 * @address 0x0803F5B4
 */
void BattleAction_AccumulateTargetDamage(struct BattleCombatant *actor)
{
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 index;

    Battle_BuildActionTargets(actor);
    sBattleActionEffectTargetCount = Battle_GetActionTargetCount();
    sBattleActionTargetEntries = Battle_GetActionTargets();
    sBattleActionValidTargetCount = 0;
    for (index = 0; index < sBattleActionEffectTargetCount; index++)
    {
        if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK) == BATTLE_TARGET_VALID)
        {
            u32 damage = Battle_CalculateActionDamage(actor,
                &combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK]);

            damage += (u16)combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].pendingDamage;
            combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].pendingDamage = damage;
            sBattleActionValidTargetCount++;
        }
    }
}

/** @brief 更新行动目标的命中特效、闪避位移和限时受击高亮。
 *  @note 普通 C 的 0x8FC 字节及解析后的重定位完全匹配。保留三组显式状态 0
 *  和入口处未使用的有效目标计数遍历；目标索引为 u8，动画逐个加载有效目标。
 *  @address 0x0803F658
 */
void BattleAction_UpdateTargetReactions(struct BattleCombatant *actor)
{
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 index;
    u8 validTargetCount = 0;
    s16 delta;

    switch (sBattleHitEffectPhase)
    {
    case 0:
        break;
    case 1:
        sBattleHitEffectTargetIndex = 0;
        sBattleHitReactionTargetIndex = 0;
        for (index = 0; index < sBattleActionEffectTargetCount; index++)
        {
            if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK) == BATTLE_TARGET_VALID)
            {
                validTargetCount++;
            }
        }
        if (sBattleActionValidTargetCount == 0)
        {
            sBattleHitEffectPhase = 0;
        }
        else if (sBattleActionEffectTargetCount <= 1)
        {
            if ((sBattleActionTargetEntries[0] & BATTLE_TARGET_FLAGS_MASK) == BATTLE_TARGET_VALID)
            {
                Battle_StartCombatantEffect(&combatants[sBattleActionTargetEntries[0] & BATTLE_TARGET_SLOT_MASK],
                    0, actor->hitEffectId);
                combatants[sBattleActionTargetEntries[0] & BATTLE_TARGET_SLOT_MASK].displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
                sBattleHitEffectTargetIndex = sBattleActionTargetEntries[0];
                sBattleHitEffectPhase = 4;
            }
            else
            {
                sBattleHitEffectPhase = 0;
            }
        }
        else
        {
            for (index = 0; index < sBattleActionEffectTargetCount; index++)
            {
                if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK) == BATTLE_TARGET_VALID)
                {
                    sBattleHitEffectTargetIndex = index;
                    break;
                }
            }
            Battle_StartCombatantEffect(&combatants[sBattleActionTargetEntries[sBattleHitEffectTargetIndex] & BATTLE_TARGET_SLOT_MASK],
                0, actor->hitEffectId);
            combatants[sBattleActionTargetEntries[sBattleHitEffectTargetIndex] & BATTLE_TARGET_SLOT_MASK].effectAnimation.flags |= 0x100;
            combatants[sBattleActionTargetEntries[sBattleHitEffectTargetIndex] & BATTLE_TARGET_SLOT_MASK].displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleHitEffectPhase = 2;
        }
        sBattleDodgePhase = 1;
        break;
    case 2:
        if (!(combatants[sBattleActionTargetEntries[sBattleHitEffectTargetIndex] & BATTLE_TARGET_SLOT_MASK].effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            combatants[sBattleActionTargetEntries[sBattleHitEffectTargetIndex] & BATTLE_TARGET_SLOT_MASK].effectAnimation.flags &= ~BATTLE_ANIM_LOADING;
            combatants[sBattleActionTargetEntries[sBattleHitEffectTargetIndex] & BATTLE_TARGET_SLOT_MASK].effectAnimation.flags &= ~0x100;
            for (index = 0; index < sBattleActionEffectTargetCount; index++)
            {
                if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK) == BATTLE_TARGET_VALID
                    && index != sBattleHitEffectTargetIndex)
                {
                    Battle_StartCombatantEffect(&combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK],
                        0, actor->hitEffectId);
                    Battle_BindAnimationTables(&combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].effectAnimation);
                    combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].effectAnimation.flags &= ~BATTLE_ANIM_LOADING;
                    combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
                }
            }
            sBattleHitEffectPhase = 3;
        }
        break;
    case 3:
        if (sBattleActionFrame >= actor->hitReactionStartFrame)
            sBattleHitEffectPhase = 4;
        break;
    case 4:
        if (sBattleHitReactionTargetIndex < sBattleActionEffectTargetCount)
        {
            if ((u8)Battle_AreCombatantAnimationsLoading() == 0)
            {
                for (; sBattleHitReactionTargetIndex < sBattleActionEffectTargetCount; sBattleHitReactionTargetIndex++)
                {
                    if ((sBattleActionTargetEntries[sBattleHitReactionTargetIndex] & BATTLE_TARGET_FLAGS_MASK) == BATTLE_TARGET_VALID)
                    {
                        Battle_SetHitReactionAnimation(&combatants[sBattleActionTargetEntries[sBattleHitReactionTargetIndex] & BATTLE_TARGET_SLOT_MASK]);
                        break;
                    }
                }
                sBattleHitEffectPhase = 5;
            }
        }
        else
        {
            Sound_PlaySfx(sBattleHitSfxId, 0, sBattleHitSfxLoop);
            sBattleHitReactionPhase = 1;
            sBattleHitReactionTargetIndex = 0;
            sBattleHitEffectPhase = 7;
        }
        break;
    case 5:
        if ((sBattleActionTargetEntries[sBattleHitReactionTargetIndex] & BATTLE_TARGET_FLAGS_MASK) == BATTLE_TARGET_VALID)
        {
            if (!(combatants[sBattleActionTargetEntries[sBattleHitReactionTargetIndex] & BATTLE_TARGET_SLOT_MASK].animation.flags & BATTLE_ANIM_LOADING))
            {
                sBattleHitReactionTargetIndex++;
                sBattleHitEffectPhase = 4;
            }
        }
        else
        {
            sBattleHitReactionTargetIndex++;
            sBattleHitEffectPhase = 4;
        }
        break;
    case 7:
        if (sBattleHitReactionFrame >= sBattleHitReactionDuration - 1
            && (u8)Sound_IsSfxPlaying(0) != 0)
            Sound_StopSfx(0);
        if (sBattleHitReactionFrame >= sBattleHitReactionDuration)
        {
            if (sBattleHitReactionTargetIndex < sBattleActionEffectTargetCount)
            {
                if ((u8)Battle_AreCombatantAnimationsLoading() == 0)
                {
                    for (; sBattleHitReactionTargetIndex < sBattleActionEffectTargetCount; sBattleHitReactionTargetIndex++)
                    {
                        if ((sBattleActionTargetEntries[sBattleHitReactionTargetIndex] & BATTLE_TARGET_FLAGS_MASK) == BATTLE_TARGET_VALID)
                        {
                            struct BattleCombatant *target = &combatants[sBattleActionTargetEntries[sBattleHitReactionTargetIndex] & BATTLE_TARGET_SLOT_MASK];

                            Battle_RestoreIdleAnimation(target, target->homeX, target->homeY,
                                target->animation.baseTile, target->animation.paletteSlot);
                            break;
                        }
                    }
                    sBattleHitEffectPhase = 6;
                }
            }
            else
            {
                sBattleHitEffectPhase = 8;
            }
        }
        else
        {
            for (index = 0; index < sBattleActionEffectTargetCount; index++)
            {
                if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK) == BATTLE_TARGET_VALID)
                {
                    if (combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].effectAnimation.flags & BATTLE_ANIM_FINISHED)
                    {
                        BattlePalette_ReleaseObjBanks(combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].effectAnimation.paletteSlot,
                            Battle_GetAnimationPaletteCount(&combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].effectAnimation));
                        combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
                        combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
                        sBattleCompletedHitEffectCount++;
                    }
                }
            }
        }
        break;
    case 6:
        if ((sBattleActionTargetEntries[sBattleHitReactionTargetIndex] & BATTLE_TARGET_FLAGS_MASK) == BATTLE_TARGET_VALID)
        {
            if (!(combatants[sBattleActionTargetEntries[sBattleHitReactionTargetIndex] & BATTLE_TARGET_SLOT_MASK].animation.flags & BATTLE_ANIM_LOADING))
            {
                sBattleHitReactionTargetIndex++;
                sBattleHitEffectPhase = 7;
            }
        }
        else
        {
            sBattleHitReactionTargetIndex++;
            sBattleHitEffectPhase = 7;
        }
        break;
    case 8:
        for (index = 0; index < sBattleActionEffectTargetCount; index++)
        {
            if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK) == BATTLE_TARGET_VALID)
            {
                if (combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].effectAnimation.flags & BATTLE_ANIM_FINISHED)
                {
                    combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
                    combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
                    sBattleCompletedHitEffectCount++;
                    break;
                }
            }
        }
        if (sBattleCompletedHitEffectCount >= sBattleActionValidTargetCount)
        {
            BattlePalette_ReleaseObjBanks(combatants[sBattleActionTargetEntries[0] & BATTLE_TARGET_SLOT_MASK].effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&combatants[sBattleActionTargetEntries[0] & BATTLE_TARGET_SLOT_MASK].effectAnimation));
            sBattleHitEffectPhase = 0;
        }
        break;
    }

    switch (sBattleDodgePhase)
    {
    case 0:
        break;
    case 1:
        for (index = 0; index < sBattleActionEffectTargetCount; index++)
        {
            sBattleDodgeSavedX[index] = 0;
            if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK) == 0)
                sBattleDodgeSavedX[index] = combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].homeX;
        }
        sBattleDodgeFrame = 0;
        Sound_PlaySfx(29, 3, 0);
        sBattleDodgePhase = 2;
        break;
    case 2:
        for (index = 0; index < sBattleActionEffectTargetCount; index++)
        {
            if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK) == 0)
            {
                if (combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].id < BATTLE_CHARACTER_ID_COUNT)
                    delta = 5;
                else
                    delta = -5;
                combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].homeX =
                    Battle_InterpolateS16(sBattleDodgeSavedX[index], delta, 3, sBattleDodgeFrame, 2);
            }
        }
        if (sBattleDodgeFrame < 3)
        {
            sBattleDodgeFrame++;
        }
        else
        {
            sBattleDodgeFrame = 0;
            sBattleDodgePhase = 3;
        }
        break;
    case 3:
        for (index = 0; index < sBattleActionEffectTargetCount; index++)
        {
            if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK) == 0)
            {
                if (combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].id < BATTLE_CHARACTER_ID_COUNT)
                    delta = -5;
                else
                    delta = 5;
                combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].homeX =
                    Battle_InterpolateS16(sBattleDodgeSavedX[index] - delta, delta, 3, sBattleDodgeFrame, 1);
            }
        }
        if (sBattleDodgeFrame < 3)
        {
            sBattleDodgeFrame++;
        }
        else
        {
            sBattleDodgeFrame = 0;
            sBattleDodgePhase = 0;
        }
        break;
    }

    switch (sBattleHitReactionPhase)
    {
    case 0:
        break;
    case 1:
        for (index = 0; index < sBattleActionEffectTargetCount; index++)
        {
            if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK) == BATTLE_TARGET_VALID)
                Battle_StartCombatantHighlight(&combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK], 4);
        }
        sBattleHitReactionPhase = 2;
        break;
    case 2:
        if (sBattleHitReactionFrame < sBattleHitReactionDuration)
            sBattleHitReactionFrame++;
        else
            sBattleHitReactionPhase = 3;
        break;
    case 3:
        for (index = 0; index < sBattleActionEffectTargetCount; index++)
        {
            if ((sBattleActionTargetEntries[index] & BATTLE_TARGET_FLAGS_MASK) == BATTLE_TARGET_VALID)
                Battle_StopCombatantHighlight(&combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK]);
        }
        sBattleHitReactionPhase = 0;
        break;
    }
}

/** @brief Advance one party member's escape and restore idle animation after failure.
 *  @note All 0x258 bytes match after relocation. Uses a fifty-frame hold and a
 *  twenty-five-frame exit; removal sets id FF/status 7, increments the count and returns one.
 *  Failure restores the saved base tile/palette; completion is reported on the next update.
 *  @address 0x0803FF54
 */
u8 BattleAction_UpdatePartyMemberEscape(struct BattleCombatant *combatant)
{
    u8 result = BATTLE_ESCAPE_RUNNING;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedPaletteSlot = combatant->animation.paletteSlot;
        sBattleActionSavedBaseTile = combatant->animation.baseTile;
        sBattleActionSavedX = combatant->homeX;
        sBattleActionSavedY = combatant->homeY;
        Battle_SetPartyAnimation(combatant, 8, 0x1B4, 13, 0);
        sBattleActionEffectPhase = 0x30;
        sBattleActionFrame = 0;
        break;
    case 0x30:
        if (Battle_RollCombatantEscape(combatant) == 1)
            sBattleEscapeFailed = 0;
        else
            sBattleEscapeFailed = 1;
        sBattleActionEffectPhase = 0x33;
        break;
    case 0x33:
        if (sBattleActionFrame < 50)
        {
            sBattleActionFrame++;
        }
        else
        {
            if (sBattleEscapeFailed == 0)
                sBattleActionEffectPhase = 0x34;
            else
                sBattleActionEffectPhase = 0x36;
            sBattleActionFrame = 0;
        }
        break;
    case 0x34:
        if (sBattleActionFrame < 25)
        {
            combatant->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                256 - sBattleActionSavedX, 25, sBattleActionFrame, 1);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionEffectPhase = 0x35;
        }
        break;
    case 0x35:
        combatant->id = BATTLE_NO_COMBATANT;
        combatant->status = BATTLE_STATUS_ESCAPED;
        sBattleActionEffectPhase = 0x38;
        sBattleEscapedCombatantCount++;
        result = BATTLE_ESCAPE_PARTY_COMPLETE;
        break;
    case 0x36:
        Battle_SetPartyAnimation(combatant, 0, sBattleActionSavedBaseTile,
            sBattleActionSavedPaletteSlot, 0);
        sBattleActionEffectPhase = 0x38;
        break;
    case 0x38:
        result = BATTLE_ESCAPE_PARTY_COMPLETE;
        break;
    }
    return result;
}

/** @brief Advance the party-wide escape sequence and restore idle animations on failure.
 *  @note All 0x3F6 bytes match after relocation; the final two padding bytes are excluded.
 *  Keep the action-info pointer before the call, byte loop indices and id |= FF.
 *  Success moves for twenty-five frames after a fifty-frame hold; failure restores idle and sets flag 0x1000.
 *  @address 0x080401AC
 */
s32 BattleAction_UpdatePartyEscape(void)
{
    s32 result = 0;
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 index;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleEffectCombatantCount = Battle_ListCombatants(combatants,
            sBattleEffectCombatantSlots, BATTLE_SIDE_PARTY, 0x43);
        for (index = 0; index < sBattleEffectCombatantCount; index++)
            sBattleEscapeSavedX[index] = combatants[sBattleEffectCombatantSlots[index]].homeX;
        sBattleActionScratch86E = 0;
        sBattleActionEffectPhase = 0x31;
        sBattleActionFrame = 0;
        break;
    case 0x31:
        if (sBattleActionScratch86E < sBattleEffectCombatantCount)
        {
            struct BattleCombatant *combatant = (struct BattleCombatant *)(
                sBattleEffectCombatantSlots[sBattleActionScratch86E] * sizeof(*combatants)
                + (u32)combatants);
            const struct BattleCharacterActionInfo *info = &gBattleCharacterActionInfo[combatant->id];

            Battle_SetAnimationById(&combatant->animation, info->escapeAnimationId,
                0x1B4 + sBattleActionScratch86E * 0x40,
                combatant->animation.paletteSlot, 0x401);
            sBattleActionEffectPhase = 0x32;
        }
        else
        {
            sBattleActionEffectPhase = 0x30;
        }
        break;
    case 0x32:
        if (!(combatants[sBattleEffectCombatantSlots[sBattleActionScratch86E]].animation.flags & BATTLE_ANIM_LOADING))
        {
            sBattleActionScratch86E++;
            sBattleActionEffectPhase = 0x31;
        }
        break;
    case 0x30:
        if (Battle_RollPartyEscape() == 1)
            sBattleEscapeFailed = 0;
        else
            sBattleEscapeFailed = 1;
        sBattleActionScratch86E = 0;
        sBattleActionEffectPhase = 0x33;
        break;
    case 0x33:
        if (sBattleActionFrame < 50)
        {
            sBattleActionFrame++;
        }
        else
        {
            if (sBattleEscapeFailed == 0)
                sBattleActionEffectPhase = 0x34;
            else
                sBattleActionEffectPhase = 0x36;
            sBattleActionFrame = 0;
        }
        break;
    case 0x34:
        if (sBattleActionFrame < 25)
        {
            for (index = 0; index < sBattleEffectCombatantCount; index++)
            {
                combatants[sBattleEffectCombatantSlots[index]].homeX = Battle_InterpolateS16(
                    sBattleEscapeSavedX[index], 256 - sBattleEscapeSavedX[index],
                    25, sBattleActionFrame, 1);
            }
            sBattleActionFrame++;
        }
        else
        {
            for (index = 0; index < sBattleEffectCombatantCount; index++)
                combatants[sBattleEffectCombatantSlots[index]].id |= BATTLE_NO_COMBATANT;
            sBattleActionEffectPhase = 0x35;
        }
        break;
    case 0x35:
        sBattleActionEffectPhase = 0x38;
        result = 1;
        break;
    case 0x36:
        if (sBattleActionScratch86E < sBattleEffectCombatantCount)
        {
            u8 slot = sBattleEffectCombatantSlots[sBattleActionScratch86E];
            struct BattleCombatant *combatant = &combatants[slot];

            Battle_SetPartyAnimation(combatant, 0, slot * 22 + 9,
                combatant->animation.paletteSlot, 0);
            sBattleActionEffectPhase = 0x37;
        }
        else
        {
            Battle_SetControlFlags(0x1000);
            sBattleActionEffectPhase = 0x38;
        }
        break;
    case 0x37:
        if (!(combatants[sBattleEffectCombatantSlots[sBattleActionScratch86E]].animation.flags & BATTLE_ANIM_LOADING))
        {
            sBattleActionScratch86E++;
            sBattleActionEffectPhase = 0x36;
        }
        break;
    case 0x38:
        result = 1;
        break;
    }
    return result;
}

/** @brief Animate an enemy escape attempt and remove successful escapees.
 *  @note All 0xEC bytes match after relocation. Phase 0 tests random % 100 < 70 unless
 *  control flags 0x220 block escape. Phase 2 moves for twenty frames with curve 2,
 *  then decrements the count and sets id FF/status 7; phase 9 returns two even on failure.
 *  @address 0x080405A4
 */
u8 BattleAction_UpdateEnemyEscape(struct BattleCombatant *combatant)
{
    u8 result = BATTLE_ESCAPE_RUNNING;

    switch (sBattleActionEffectPhase)
    {
    case 0:
        sBattleActionSavedPaletteSlot = combatant->animation.paletteSlot;
        sBattleActionSavedBaseTile = combatant->animation.baseTile;
        sBattleActionSavedX = combatant->homeX;
        sBattleActionSavedY = combatant->homeY;
        sBattleActionFrame = 0;
        if (Battle_NextRandom() % 100 < 70 && !(Battle_GetControlFlags() & 0x220))
        {
            combatant->animation.flags |= 0x20;
            sBattleActionEffectPhase = 2;
        }
        else
        {
            sBattleActionEffectPhase = 9;
        }
        break;
    case 2:
        if (sBattleActionFrame < 20)
        {
            combatant->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                -sBattleActionSavedX, 20, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            Battle_DecrementEnemyCount();
            combatant->id = BATTLE_NO_COMBATANT;
            combatant->status = BATTLE_STATUS_ESCAPED;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        result = BATTLE_ESCAPE_ENEMY_COMPLETE;
        break;
    }
    return result;
}

/** @brief 推进普通攻击的接近、攻击动画、连击和目标反应。
 *  @note 普通 C 的 0x856 字节及解析后的重定位 100% 匹配，末尾两字节零填充不计。
 *  保留字节坐标和 32 位工作量复用；装备类型 3/4 追加效果写 999 并返回 3，正常完成返回 1。
 *  @address 0x08040690
 */
u8 BattleAction_UpdateNormalAttack(struct BattleCombatant *actor, struct BattleCombatant *target)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
    {
        struct BattleCombatant *combatants;
        u8 slots[BATTLE_COMBATANT_COUNT];
        u8 count;
        u8 index;

        sBattleLastAttackDamage = 0;
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionFrame = 0;
        actor->animation.flags &= ~BATTLE_ANIM_LOOP;
        combatants = Battle_GetCombatants();
        count = Battle_ListActionTargetSlots(actor, slots, 0x6F);
        for (index = 0; index < count; index++)
            combatants[slots[index]].pendingDamage = 0;
        if (actor->id > 112)
            sBattleActionEffectPhase = 3;
        else
            sBattleActionEffectPhase = 1;
        Battle_SetPartyAttackSpacingWidth(actor);
        for (index = 0; index < BATTLE_ATTACK_POSITION_CONFIG_COUNT; index++)
        {
            if (gBattleAttackPositionConfigs[index].combatantId == actor->id)
                break;
        }
        sBattleAttackConfigIndex = index;
        Battle_UpdateEnemyAttackSpacingWidth(actor);
        sBattleAttackSoundFrame = 0;
        break;
    }
    case 1:
        if (actor->animation.flags & BATTLE_ANIM_HIDDEN)
            actor->animation.flags &= ~BATTLE_ANIM_HIDDEN;
        else
            actor->animation.flags |= BATTLE_ANIM_HIDDEN;
        if (sBattleActionFrame < 5)
        {
            sBattleActionFrame++;
            if (actor->id < BATTLE_CHARACTER_ID_COUNT)
            {
                if (target->id >= BATTLE_CHARACTER_ID_COUNT)
                {
                    actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                        -10, 5, sBattleActionFrame, 2);
                    actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                        (target->homeY - sBattleActionSavedY) / 10, 5, sBattleActionFrame, 2);
                }
                else
                {
                    actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                        10, 5, sBattleActionFrame, 2);
                    actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                        (target->homeY - sBattleActionSavedY) / 10, 5, sBattleActionFrame, 2);
                }
            }
            else
            {
                if (target->id < BATTLE_CHARACTER_ID_COUNT)
                {
                    actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                        10, 5, sBattleActionFrame, 2);
                    actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                        (target->homeY - sBattleActionSavedY) / 10, 5, sBattleActionFrame, 2);
                }
                else
                {
                    actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                        -10, 5, sBattleActionFrame, 2);
                    actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                        (target->homeY - sBattleActionSavedY) / 10, 5, sBattleActionFrame, 2);
                }
            }
        }
        else
        {
            sBattleActionFrame = 0;
            if (actor->id < BATTLE_CHARACTER_ID_COUNT)
            {
                if (target->id < BATTLE_CHARACTER_ID_COUNT)
                {
                    u32 work;
                    u8 coordinate;
                    const u8 *targetX = &target->homeX;

                    work = (actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1);
                    coordinate = *targetX - work - 10;
                    work = (u32)actor + offsetof(struct BattleCombatant, homeX);
                    *(u8 *)work = coordinate;
                    coordinate = target->homeY + 1;
                    work++;
                    *(u8 *)work = coordinate;
                }
                else
                {
                    actor->homeX = target->homeX
                        + ((actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1)) + 10;
                    actor->homeY = target->homeY + 1;
                }
            }
            else
            {
                if (target->id < BATTLE_CHARACTER_ID_COUNT)
                {
                    u32 work;
                    u8 coordinate;
                    const u8 *targetX = &target->homeX;

                    work = (actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1);
                    coordinate = *targetX - work - 10;
                    work = (u32)actor + offsetof(struct BattleCombatant, homeX);
                    *(u8 *)work = coordinate;
                    coordinate = target->homeY + 1;
                    work++;
                    *(u8 *)work = coordinate;
                }
                else
                {
                    actor->homeX = target->homeX
                        + ((actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1)) + 10;
                    actor->homeY = target->homeY + 1;
                }
            }
            sBattleActionEffectPhase = 2;
        }
        break;
    case 2:
        if (actor->animation.flags & BATTLE_ANIM_HIDDEN)
            actor->animation.flags &= ~BATTLE_ANIM_HIDDEN;
        else
            actor->animation.flags |= BATTLE_ANIM_HIDDEN;
        if (sBattleActionFrame < 5)
        {
            sBattleActionFrame++;
            if (actor->id < BATTLE_CHARACTER_ID_COUNT)
            {
                if (target->id < BATTLE_CHARACTER_ID_COUNT)
                    actor->homeX = Battle_InterpolateS16(target->homeX
                        - ((actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1)) - 10,
                        10, 5, sBattleActionFrame, 2);
                else
                    actor->homeX = Battle_InterpolateS16(target->homeX
                        + ((actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1)) + 10,
                        -10, 5, sBattleActionFrame, 2);
            }
            else
            {
                if (target->id < BATTLE_CHARACTER_ID_COUNT)
                    actor->homeX = Battle_InterpolateS16(target->homeX
                        - ((actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1)) - 10,
                        10, 5, sBattleActionFrame, 2);
                else
                    actor->homeX = Battle_InterpolateS16(target->homeX
                        + ((actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1)) + 10,
                        -10, 5, sBattleActionFrame, 2);
            }
        }
        else
        {
            if (actor->animation.flags & BATTLE_ANIM_HIDDEN)
                actor->animation.flags &= ~BATTLE_ANIM_HIDDEN;
            sBattleActionEffectPhase = 3;
        }
        break;
    case 3:
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_AccumulateTargetDamage(actor);
        if (actor->id < BATTLE_CHARACTER_ID_COUNT)
        {
            Battle_SetPartyAnimation(actor, 1, 0x1B4, 13, (u8)sBattleActionHitIndex);
            sBattleActionHitIndex++;
        }
        else if (actor->id <= 112)
        {
            Battle_SetEnemyAnimation(actor, 3, 0x1B4, 12, 0);
        }
        else
        {
            Battle_SetBossAnimation(actor, 1, 0x1B4, 13, 0);
        }
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 4;
        break;
    case 4:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 5;
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->animation.flags |= 0x100;
        }
        else
        {
            BattleAction_UpdateEnemyActionSound(actor);
        }
        if (sBattleHitEffectPhase != 4 && sBattleHitEffectPhase != 5
            && (actor->animation.flags & 0x100))
        {
            if (actor->id == 35 || actor->id == 70 || actor->id == 34
                || actor->id == 71 || actor->id == 89 || actor->id == 37 || actor->id == 42)
                actor->displayState &= ~BATTLE_DISPLAY_HIDE_STATUS_ICON;
            Battle_RestoreIdleAnimation(actor, actor->homeX, actor->homeY,
                sBattleActionSavedBaseTile, sBattleActionSavedPaletteSlot);
            sBattleActionEffectPhase = 6;
        }
        else
        {
            if (actor->id == 35 || actor->id == 70 || actor->id == 34
                || actor->id == 71 || actor->id == 89 || actor->id == 37 || actor->id == 42)
            {
                if (actor->animation.timelineFrame == 12)
                    actor->displayState |= BATTLE_DISPLAY_HIDE_STATUS_ICON;
            }
        }
        if (sBattleAttackConfigIndex < BATTLE_ATTACK_POSITION_CONFIG_COUNT)
        {
            const struct BattleAttackPositionConfig *configs = gBattleAttackPositionConfigs;
            const struct BattleAttackPositionConfig *config = &configs[sBattleAttackConfigIndex];

            if (actor->animation.timelineFrame == config->beginFrame)
            {
                actor->displayState |= 0x40;
                actor->homeX += config->xOffset;
            }
            else if (actor->animation.timelineFrame == config->endFrame)
            {
                u8 *homeX;
                u8 xOffset;

                actor->displayState &= ~0x40;
                homeX = &actor->homeX;
                xOffset = config->xOffset;
                *homeX -= xOffset;
            }
        }
        sBattleActionFrame++;
        if (sBattleActionFrame == actor->hitReactionStartFrame - 5)
        {
            s16 duration = 20;

            sBattleHitEffectPhase = 1;
            sBattleDodgePhase = 0;
            sBattleHitReactionPhase = 0;
            sBattleHitReactionDuration = duration;
            sBattleHitReactionFrame = 0;
            sBattleCompletedHitEffectCount = 0;
            sBattleHitSfxId = BATTLE_HIT_REACTION_DEFAULT_SFX;
            sBattleHitSfxLoop = 0;
        }
        if (actor->id == 9)
        {
            switch (sBattleActionHitIndex)
            {
            case 1:
                if (sBattleAttackSoundFrame == 50)
                    Sound_PlaySfx(90, 1, 0);
                break;
            case 2:
                if (sBattleAttackSoundFrame == 10)
                    Sound_PlaySfx(90, 1, 0);
                break;
            }
            sBattleAttackSoundFrame++;
        }
        break;
    case 6:
        if (sBattleActionValidTargetCount != 0 && actor->id < BATTLE_CHARACTER_ID_COUNT
            && (u8)Battle_RollPartyAttackFollowup(actor, target, (u8)sBattleActionHitIndex) == 1)
        {
            Battle_SeedRandom(Battle_NextRandom());
            sBattleActionEffectPhase = 8;
        }
        else
        {
            sBattleActionEffectPhase = 7;
        }
        sBattleActionFrame = 0;
        break;
    case 8:
        if (sBattleActionFrame >= 15)
            sBattleActionEffectPhase = 3;
        sBattleActionFrame++;
        break;
    case 7:
        if (sBattleActionFrame >= 25)
        {
            sBattleActionEffectPhase = 9;
        }
        else if (sBattleActionFrame == 8 && actor->id <= 112)
        {
            actor->animation.flags |= BATTLE_ANIM_SEMITRANSPARENT;
        }
        else if (sBattleActionFrame == 10 && actor->id <= 112)
        {
            actor->homeX = sBattleActionSavedX;
            actor->homeY = sBattleActionSavedY;
        }
        else if (sBattleActionFrame == 13 && actor->id <= 112)
        {
            actor->animation.flags &= ~BATTLE_ANIM_SEMITRANSPARENT;
        }
        sBattleActionFrame++;
        break;
    case 9:
        if (sBattleHitEffectPhase == 0 && sBattleDodgePhase == 0 && sBattleHitReactionPhase == 0)
        {
            sBattleLastAttackDamage = target->pendingDamage;
            if (target->id <= 112)
            {
                if (BattleItem_FindEquipmentByType(actor, 3) >= 0)
                {
                    if (Battle_NextRandom() % 100 < 10)
                    {
                        target->pendingDamage = 999;
                        result = 3;
                    }
                    else
                    {
                        result = 1;
                    }
                }
                else if (BattleItem_FindEquipmentByType(actor, 4) >= 0)
                {
                    if (Battle_NextRandom() % 100 < 20)
                    {
                        target->pendingDamage = 999;
                        result = 3;
                    }
                    else
                    {
                        result = 1;
                    }
                }
                else
                {
                    result = 1;
                }
            }
            else
            {
                result = 1;
            }
        }
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进角色 7 的投射攻击、目标尺寸定位和连击判定。
 *  @note 普通 C 的 0x41E 字节及重定位 100% 匹配；末尾两字节零填充不计，保留武器变体的低字节读取。
 *  @address 0x08040EE8
 */
u8 BattleAction_UpdateCharacter7ProjectileAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;
    const struct BattleCharacterActionInfo *info =
        (const struct BattleCharacterActionInfo *)(actor->id
            * sizeof(struct BattleCharacterActionInfo) + (u32)gBattleCharacterActionInfo);

    switch (sBattleActionEffectPhase)
    {
    case 0:
    {
        u8 slots[BATTLE_COMBATANT_COUNT];
        struct BattleCombatant *combatants;
        u8 count;
        u8 index;

        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        combatants = Battle_GetCombatants();
        count = Battle_ListActionTargetSlots(actor, slots, 0x6F);
        for (index = 0; index < count; index++)
            combatants[slots[index]].pendingDamage = 0;
        sBattleActionEffectPhase = 1;
        break;
    }
    case 1:
        BattleAction_AccumulateTargetDamage(actor);
        Battle_SetPartyAnimation(actor, 1, 0x1B4, 13, (u8)sBattleActionHitIndex);
        sBattleActionHitIndex++;
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 2;
        break;
    case 2:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 5;
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->animation.flags |= 0x100;
            Battle_SetEffectAnimation(actor, actor->homeX, actor->homeY - 16,
                0x1F4, 14, 0x8D, 5);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 3;
        }
        break;
    case 3:
        if (sBattleActionFrame <= 9)
        {
            actor->effectAnimation.x = Battle_InterpolateS16(actor->homeX,
                target->homeX + (Battle_GetCombatantDimension(target, 0) >> 1) - actor->homeX,
                10, sBattleActionFrame, 2);
            actor->effectAnimation.y = Battle_InterpolateS16(actor->homeY - 16,
                target->homeY - (Battle_GetCombatantDimension(target, 1) >> 1) - (actor->homeY - 16),
                10, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            {
                s16 duration = 20;

                sBattleHitEffectPhase = 1;
                sBattleDodgePhase = 0;
                sBattleHitReactionPhase = 0;
                if (duration < 0)
                    sBattleHitReactionDuration = BATTLE_HIT_REACTION_DEFAULT_DURATION;
                else
                    sBattleHitReactionDuration = duration;
                sBattleHitReactionFrame = 0;
                sBattleCompletedHitEffectCount = 0;
                sBattleHitSfxId = BATTLE_HIT_REACTION_DEFAULT_SFX;
                sBattleHitSfxLoop = 0;
            }
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 6;
        }
        break;
    case 4:
        break;
    case 6:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            if (sBattleActionValidTargetCount && actor->id < BATTLE_CHARACTER_ID_COUNT)
            {
                u8 variant = gBattleWeaponActionInfo[actor->equipment[0]].actionVariant;

                if (sBattleActionHitIndex < info->maxHitCounts[variant]
                    && (u8)(Battle_NextRandom() % 100) < info->followupChances[sBattleActionHitIndex]
                    && !(u8)Battle_WouldPendingDamageDefeat(target))
                {
                    Battle_SeedRandom(Battle_NextRandom());
                    sBattleActionEffectPhase = 1;
                    break;
                }
            }
            Battle_SetAnimationById(&actor->animation, 0x8C, 0x1B4, 13, 2);
            sBattleActionEffectPhase = 8;
        }
        break;
    case 8:
        sBattleActionEffectPhase = 7;
        break;
    case 7:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            Battle_RestoreIdleAnimation(actor, sBattleActionSavedX, sBattleActionSavedY,
                sBattleActionSavedBaseTile, sBattleActionSavedPaletteSlot);
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            if (target->id <= 112)
            {
                if (BattleItem_FindEquipmentByType(actor, 3) >= 0)
                {
                    if (Battle_NextRandom() % 100 < 10)
                    {
                        target->pendingDamage = 999;
                        result = 3;
                    }
                    else
                    {
                        result = 1;
                    }
                }
                else if (BattleItem_FindEquipmentByType(actor, 4) >= 0)
                {
                    if (Battle_NextRandom() % 100 < 25)
                    {
                        target->pendingDamage = 999;
                        result = 3;
                    }
                    else
                    {
                        result = 1;
                    }
                }
                else
                {
                    result = 1;
                }
            }
            else
            {
                result = 1;
            }
        }
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进角色 4 的随机投射、逐次减小的弹跳和装备追加效果。
 *  @note 普通 C 的 0x3E6 字节及重定位 100% 匹配；末尾两字节零填充不计，弹跳位移保留 0xFFEC 的无符号半字基值。
 *  @address 0x08041308
 */
u8 BattleAction_UpdateCharacter4RandomProjectileAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
    {
        u8 slots[BATTLE_COMBATANT_COUNT];
        struct BattleCombatant *combatants;
        u8 count;
        u8 index;

        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        combatants = Battle_GetCombatants();
        count = Battle_ListActionTargetSlots(actor, slots, 0x6F);
        for (index = 0; index < count; index++)
            combatants[slots[index]].pendingDamage = 0;
        BattleAction_AccumulateTargetDamage(actor);
        Battle_SetPartyAnimation(actor, 1, 0x1B4, 12, 0);
        actor->hitEffectId = 0x45;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    }
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (actor->animation.timelineFrame > 139)
        {
            Sound_PlaySfx(0x16, 0, 0);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            Battle_SetPartyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            actor->animation.flags |= 0x100;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            actor->animation.flags &= ~0x100;
            Battle_SetEffectAnimation(actor, target->homeX, 0, 0x1B4, 13,
                Battle_NextRandom() % 5 + 64, 5);
            actor->effectAnimation.priority = 0;
            sBattleActionFrame = 0;
            sBattleActionScratch86F = target->homeY;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (sBattleActionFrame <= 19)
        {
            actor->effectAnimation.y = Battle_InterpolateS16(0,
                sBattleActionScratch86F, 20, sBattleActionFrame, 1);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            {
                s16 duration = 40;

                sBattleHitEffectPhase = 1;
                sBattleDodgePhase = 0;
                sBattleHitReactionPhase = 0;
                if (duration < 0)
                    sBattleHitReactionDuration = BATTLE_HIT_REACTION_DEFAULT_DURATION;
                else
                    sBattleHitReactionDuration = duration;
                sBattleHitReactionFrame = 0;
                sBattleCompletedHitEffectCount = 0;
                sBattleHitSfxId = BATTLE_HIT_REACTION_DEFAULT_SFX;
                sBattleHitSfxLoop = 0;
            }
            actor->effectAnimation.y = sBattleActionScratch86F;
            sBattleActionScratch86E = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
        if (sBattleActionFrame < 20 - sBattleActionScratch86E * 2)
        {
            actor->effectAnimation.y = Battle_InterpolateS16(sBattleActionScratch86F,
                sBattleActionScratch86E * 2 + 0xFFECU, 10 - sBattleActionScratch86E,
                sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            if (sBattleActionScratch86E <= 4)
                sBattleActionScratch86E += 2;
            else
            {
                actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
                sBattleActionEffectPhase = 24;
            }
        }
        break;
    case 24:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        if (target->id <= 112)
        {
            if (BattleItem_FindEquipmentByType(actor, 3) >= 0)
            {
                if (Battle_NextRandom() % 100 < 10)
                {
                    target->pendingDamage = 999;
                    result = 3;
                }
                else
                {
                    result = 1;
                }
            }
            else if (BattleItem_FindEquipmentByType(actor, 4) >= 0)
            {
                if (Battle_NextRandom() % 100 < 25)
                {
                    target->pendingDamage = 999;
                    result = 3;
                }
                else
                {
                    result = 1;
                }
            }
            else
            {
                result = 1;
            }
        }
        else
        {
            result = 1;
        }
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Advance the action selected by enemy[1].
 *  @note Seeded GPBA replay returned 1 after 111 dispatch updates (111 entry hits).
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x080416F0
 *  静态收尾：普通敌人行动 1：启动技能动画，按敌人资源记录在目标坐标创建效果，恢复待机并等待目标反应。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdateEnemyConfiguredTargetEffectAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;
    const struct BattleEnemyInfo *info = actor->source.enemy;

    switch (sBattleActionEffectPhase)
    {
    case 0:
    {
        struct BattleCombatant *combatants;
        u8 slots[BATTLE_COMBATANT_COUNT];
        u8 count;
        u8 index;

        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        actor->animation.flags &= ~BATTLE_ANIM_LOOP;
        target->pendingDamage = 0;
        combatants = Battle_GetCombatants();
        count = Battle_ListActionTargetSlots(actor, slots, 0x6F);
        for (index = 0; index < count; index++)
            combatants[slots[index]].pendingDamage = 0;
        BattleAction_AccumulateTargetDamage(actor);
        Battle_SetEnemyAnimation(actor, 3, 0x1B4, 13, 0);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 1;
        break;
    }
    case 1:
        if (actor->animation.timelineFrame == 15)
            Sound_PlaySfx(0x87, 1, 0);
        if (actor->animation.timelineFrame > 40)
        {
            s16 duration;

            Battle_SetEffectAnimation(actor, target->homeX, target->homeY,
                0x2EA, 10, info->animationBaseId + 4, 5);
            actor->effectAnimation.flags |= 0x2000;
            actor->animation.flags |= 0x100;
            actor->animation.timelineFrame = 93;
            actor->displayState |= BATTLE_DISPLAY_HIDE_STATUS_ICON;
            duration = 30;
            sBattleHitEffectPhase = 1;
            sBattleDodgePhase = 0;
            sBattleHitReactionPhase = 0;
            if (duration < 0)
                sBattleHitReactionDuration = BATTLE_HIT_REACTION_DEFAULT_DURATION;
            else
                sBattleHitReactionDuration = duration;
            sBattleHitReactionFrame = 0;
            sBattleCompletedHitEffectCount = 0;
            sBattleHitSfxId = BATTLE_HIT_REACTION_DEFAULT_SFX;
            sBattleHitSfxLoop = 0;
            sBattleActionEffectPhase = 2;
        }
        break;
    case 2:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 3;
        break;
    case 3:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->effectAnimation));
            actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            actor->animation.flags &= ~0x100;
            actor->displayState &= ~BATTLE_DISPLAY_HIDE_STATUS_ICON;
            Sound_PlaySfx(0x87, 1, 0);
            sBattleActionEffectPhase = 6;
        }
        break;
    case 6:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            Battle_RestoreIdleAnimation(actor, actor->homeX, actor->homeY,
                sBattleActionSavedBaseTile, sBattleActionSavedPaletteSlot);
            sBattleActionEffectPhase = 7;
        }
        break;
    case 7:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进敌人闪烁突进、调色板循环和淡出返位。
 *  @note 普通 C 的 0x4FC 字节及重定位 100% 匹配；保留局部插值、反应状态初始化及原有完成码。
 *  @address 0x080419E0
 */
u8 BattleAction_UpdateEnemyLungeFadeAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;
    const struct BattleEnemyInfo *info = actor->source.enemy;

    switch (sBattleActionEffectPhase)
    {
    case 0:
    {
        struct BattleCombatant *combatants;
        u8 slots[BATTLE_COMBATANT_COUNT];
        u8 count;
        u8 index;

        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        actor->attackSpacingWidth = info->skillSpacingWidth;
        actor->animation.flags &= ~BATTLE_ANIM_LOOP;
        target->pendingDamage = 0;
        combatants = Battle_GetCombatants();
        count = Battle_ListActionTargetSlots(actor, slots, 0x6F);
        for (index = 0; index < count; index++)
            combatants[slots[index]].pendingDamage = 0;
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = 1;
        break;
    }
    case 1:
        if (actor->animation.flags & BATTLE_ANIM_HIDDEN)
            actor->animation.flags &= ~BATTLE_ANIM_HIDDEN;
        else
            actor->animation.flags |= BATTLE_ANIM_HIDDEN;
        if (sBattleActionFrame <= 4)
        {
            sBattleActionFrame++;
            if (actor->id < BATTLE_CHARACTER_ID_COUNT)
            {
                actor->homeX = Battle_InterpolateS16(sBattleActionSavedX, -10,
                    5, sBattleActionFrame, 2);
                actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                    (target->homeY - sBattleActionSavedY) / 10, 5, sBattleActionFrame, 2);
            }
            else
            {
                actor->homeX = Battle_InterpolateS16(sBattleActionSavedX, 10,
                    5, sBattleActionFrame, 2);
                actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                    (target->homeY - sBattleActionSavedY) / 10, 5, sBattleActionFrame, 2);
            }
        }
        else
        {
            sBattleActionFrame = 0;
            if (actor->id < BATTLE_CHARACTER_ID_COUNT)
            {
                actor->homeX = target->homeX
                    + ((actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1)) + 10;
                actor->homeY = target->homeY + 1;
            }
            else
            {
                u32 work;
                u8 coordinate;
                const u8 *targetX = &target->homeX;

                work = (actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1);
                coordinate = *targetX - work - 10;
                work = (u32)actor + offsetof(struct BattleCombatant, homeX);
                *(u8 *)work = coordinate;
                coordinate = target->homeY + 1;
                work++;
                *(u8 *)work = coordinate;
            }
            sBattleActionEffectPhase = 2;
        }
        break;
    case 2:
        if (actor->animation.flags & BATTLE_ANIM_HIDDEN)
            actor->animation.flags &= ~BATTLE_ANIM_HIDDEN;
        else
            actor->animation.flags |= BATTLE_ANIM_HIDDEN;
        if (sBattleActionFrame <= 4)
        {
            sBattleActionFrame++;
            if (actor->id < BATTLE_CHARACTER_ID_COUNT)
                actor->homeX = Battle_InterpolateS16(target->homeX
                    + ((actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1)) + 10,
                    -10, 5, sBattleActionFrame, 2);
            else
                actor->homeX = Battle_InterpolateS16(target->homeX
                    - ((actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1)) - 10,
                    10, 5, sBattleActionFrame, 2);
        }
        else
        {
            if (actor->animation.flags & BATTLE_ANIM_HIDDEN)
                actor->animation.flags &= ~BATTLE_ANIM_HIDDEN;
            sBattleActionEffectPhase = 3;
        }
        break;
    case 3:
        Battle_SetEnemyAnimation(actor, 4, 0x1B4, 13, 0);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 4;
        break;
    case 4:
        if (actor->animation.timelineFrame == actor->hitReactionStartFrame - 5)
        {
            s16 duration = 20;

            sBattleHitEffectPhase = 1;
            sBattleDodgePhase = 0;
            sBattleHitReactionPhase = 0;
            if (duration < 0)
                sBattleHitReactionDuration = BATTLE_HIT_REACTION_DEFAULT_DURATION;
            else
                sBattleHitReactionDuration = duration;
            sBattleHitReactionFrame = 0;
            sBattleCompletedHitEffectCount = 0;
            sBattleHitSfxId = BATTLE_HIT_REACTION_DEFAULT_SFX;
            sBattleHitSfxLoop = 0;
        }
        else if (actor->animation.timelineFrame == actor->hitReactionStartFrame)
        {
            Sound_PlaySfx(0xAE, 1, 0);
            BattlePalette_StartObjCycle(14, 1, 3, -2, 4);
            BattlePalette_StartBgFade(0, 16, 10, 10, 10, 4, 0, -1, 2);
            BattlePalette_StartObjFade(0, 14, 10, 10, 10, 4, 0, -1, 2);
            sBattleActionEffectPhase = 5;
            sBattleActionFrame = 0;
        }
        else
            sBattleActionFrame++;
        break;
    case 5:
        if (sBattleActionFrame <= 9)
            sBattleActionFrame++;
        else
        {
            BattlePalette_StopBgFade(0, 16);
            BattlePalette_StopObjFade(0, 14);
            sBattleActionEffectPhase = 8;
        }
        break;
    case 8:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_RestoreIdleAnimation(actor, sBattleActionSavedX, sBattleActionSavedY,
                sBattleActionSavedBaseTile, sBattleActionSavedPaletteSlot);
            actor->homeX = sBattleActionSavedX;
            actor->homeY = sBattleActionSavedY;
            BattlePalette_StopObjCycle(14, 1);
            sBattleActionEffectPhase = 6;
        }
        break;
    case 6:
        Battle_GetCombatants();
        sBattleActionEffectPhase = 9;
        sBattleActionFrame = 0;
        break;
    case 7:
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            target->pendingDamage = 0;
            result = 4;
        }
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Advance the action selected by enemy[3].
 *  @note Seeded GPBA replay returned 1 after 108 dispatch updates (108 entry hits).
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x08041EDC
 *  静态收尾：普通敌人行动 3：顺序播放技能/攻击动画并循环 OBJ 调色板，淡出背景与角色，恢复后等待反应。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdateEnemyAnimationFadeAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
    {
        struct BattleCombatant *combatants;
        u8 slots[BATTLE_COMBATANT_COUNT];
        u8 count;
        u8 index;

        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        actor->animation.flags &= ~BATTLE_ANIM_LOOP;
        target->pendingDamage = 0;
        combatants = Battle_GetCombatants();
        count = Battle_ListActionTargetSlots(actor, slots, 0x6F);
        for (index = 0; index < count; index++)
            combatants[slots[index]].pendingDamage = 0;
        BattleAction_AccumulateTargetDamage(actor);
        if (actor->id == 55)
            Battle_SetEnemyAnimation(actor, 3, 0x1B4, 13, 0);
        else
            Battle_SetEnemyAnimation(actor, 4, 0x1B4, 12, 0);
        sBattleActionEffectPhase = 1;
        break;
    }
    case 1:
        if (actor->id == 94)
        {
            if (actor->animation.timelineFrame == 31)
                BattlePalette_StartObjCycle(13, 1, 2, 2, 4);
            if (actor->animation.timelineFrame == 61)
            {
                Sound_PlaySfx(0xA6, 1, 0);
                BattlePalette_StopObjCycle(13, 1);
            }
            if (actor->animation.timelineFrame >= actor->hitReactionStartFrame)
            {
                s16 duration = 20;

                sBattleHitEffectPhase = 1;
                sBattleDodgePhase = 0;
                sBattleHitReactionPhase = 0;
                if (duration < 0)
                    sBattleHitReactionDuration = BATTLE_HIT_REACTION_DEFAULT_DURATION;
                else
                    sBattleHitReactionDuration = duration;
                sBattleHitReactionFrame = 0;
                sBattleCompletedHitEffectCount = 0;
                sBattleHitSfxId = BATTLE_HIT_REACTION_DEFAULT_SFX;
                sBattleHitSfxLoop = 0;
                sBattleActionFrame = actor->animation.timelineFrame;
                sBattleActionEffectPhase = 2;
            }
        }
        else if (actor->animation.timelineFrame >= actor->hitReactionStartFrame - 5)
        {
            s16 duration = 20;

            sBattleHitEffectPhase = 1;
            sBattleDodgePhase = 0;
            sBattleHitReactionPhase = 0;
            if (duration < 0)
                sBattleHitReactionDuration = BATTLE_HIT_REACTION_DEFAULT_DURATION;
            else
                sBattleHitReactionDuration = duration;
            sBattleHitReactionFrame = 0;
            sBattleCompletedHitEffectCount = 0;
            sBattleHitSfxId = BATTLE_HIT_REACTION_DEFAULT_SFX;
            sBattleHitSfxLoop = 0;
            Sound_PlaySfx(0x69, 1, 0);
            sBattleActionFrame = actor->animation.timelineFrame;
            sBattleActionEffectPhase = 2;
        }
        break;
    case 2:
        if (actor->id != 94)
        {
            if (sBattleActionFrame == actor->hitReactionStartFrame - 1)
            {
                BattlePalette_StartBgFade(0, 16, 10, 10, 10, 4, 0, -1, 2);
                BattlePalette_StartObjFade(7, 7, 10, 10, 10, 4, 0, -1, 2);
            }
            else if (sBattleActionFrame == actor->hitReactionStartFrame + 5)
            {
                BattlePalette_StopBgFade(0, 16);
                BattlePalette_StopObjFade(7, 7);
            }
        }
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_RestoreIdleAnimation(actor, actor->homeX, actor->homeY,
                sBattleActionSavedBaseTile, sBattleActionSavedPaletteSlot);
            sBattleActionEffectPhase = 9;
        }
        sBattleActionFrame++;
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Play the ordinary-enemy attack animation without applying an attack.
 *  @note Handler four saves the tile/palette, waits for FINISHED, releases the
 *  temporary palette banks and restores idle. Completion is delayed one update.
 *  @address 0x08042200
 */
u8 BattleAction_UpdateEnemyAnimationOnly(struct BattleCombatant *combatant,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case BATTLE_ENEMY_ANIMATION_INIT:
        sBattleActionSavedPaletteSlot = combatant->animation.paletteSlot;
        sBattleActionSavedBaseTile = combatant->animation.baseTile;
        Battle_SetEnemyAnimation(combatant, 3, 0x1B4, 13, 0);
        sBattleActionEffectPhase = BATTLE_ENEMY_ANIMATION_WAIT;
        break;
    case BATTLE_ENEMY_ANIMATION_WAIT:
        if (combatant->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(combatant->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&combatant->animation));
            combatant->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_RestoreIdleAnimation(combatant, combatant->homeX, combatant->homeY,
                sBattleActionSavedBaseTile, sBattleActionSavedPaletteSlot);
            sBattleActionEffectPhase = BATTLE_ENEMY_ANIMATION_DONE;
        }
        break;
    case BATTLE_ENEMY_ANIMATION_DONE:
        result = 2;
        break;
    }
    return result;
}

/** @brief 按敌人配置播放攻击特效，等待主动画与特效分别收尾。
 *  @note 六项配置按敌人编号搜索，未命中回退首项；坐标半字只取低八位，配置指定的帧启动 40 帧命中反应。阶段 9 等待反应结束后返回 1，每次更新仍推进帧计数。
 *  @note 搜索索引使用 u8 并直接访问原表，保留阶段 2 的两处独立结束判断；普通 C 的完整 0x4CC 字节及已解析重定位 100% 匹配，完整 ROM 与 baserom 一致。
 *  @note Seeded GPBA replay returned 1 after 178 dispatch updates (178 entry hits).
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x080422B8
 */
u8 BattleAction_UpdateConfiguredEnemyEffect(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 slots[BATTLE_COMBATANT_COUNT];
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
    {
        u8 index = 0;

        for (; index < BATTLE_ENEMY_ACTION_EFFECT_CONFIG_COUNT; index++)
        {
            if (actor->id == gBattleEnemyActionEffectConfigs[index].enemyId)
            {
                sBattleActionEffectConfig = &gBattleEnemyActionEffectConfigs[index];
                break;
            }
        }
        if (index >= BATTLE_ENEMY_ACTION_EFFECT_CONFIG_COUNT)
            sBattleActionEffectConfig = gBattleEnemyActionEffectConfigs;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        actor->animation.flags &= ~BATTLE_ANIM_LOOP;
        target->pendingDamage = 0;
        {
            struct BattleCombatant *combatants = Battle_GetCombatants();
            u8 count = Battle_ListActionTargetSlots(actor, slots, 0x6F);
            u8 targetIndex;

            for (targetIndex = 0; targetIndex < count; targetIndex++)
                combatants[slots[targetIndex]].pendingDamage = 0;
        }
        BattleAction_AccumulateTargetDamage(actor);
        if (actor->id == 0x52)
            Battle_SetEnemyAnimation(actor, 3, 0x1B4, 13, 0);
        else
            Battle_SetEnemyAnimation(actor, 4, 0x1B4, 13, 0);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 1;
        break;
    }
    case 1:
    {
        const struct BattleActionEffectConfig *config = sBattleActionEffectConfig;

        if (actor->animation.timelineFrame == config->effectStartFrame)
        {
            Battle_SetupAnimation(&actor->effectAnimation,
                actor->homeX + (u8)config->xOffset,
                actor->homeY + (u8)config->yOffset, 0x2EA, 14,
                (const u16 *)gBattleAnimationConfigs[config->animationId].animationData,
                gBattleAnimationConfigs[config->animationId].palettes,
                gBattleAnimationConfigs[config->animationId].graphicsId,
                gBattleAnimationConfigs[config->animationId].graphicsBlockCount, 5);
            actor->displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
            actor->effectAnimation.priority = 0;
            sBattleActionEffectPhase = 18;
        }
        break;
    }
    case 18:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0x51, 1, 0);
            sBattleActionEffectPhase = 2;
        }
        break;
    case 2:
        if ((actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
            && (actor->displayState & BATTLE_DISPLAY_EFFECT_ANIMATION))
        {
            BattlePalette_ReleaseObjBanks(actor->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->effectAnimation));
            actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            Sound_StopSfx(1);
            sBattleActionEffectPhase = 8;
        }
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Sound_StopSfx(1);
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetEnemyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            actor->homeX = sBattleActionSavedX;
            actor->homeY = sBattleActionSavedY;
            sBattleActionEffectPhase = 7;
        }
        break;
    case 7:
        if ((actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
            && (actor->displayState & BATTLE_DISPLAY_EFFECT_ANIMATION))
        {
            BattlePalette_ReleaseObjBanks(actor->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->effectAnimation));
            actor->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
            Sound_StopSfx(1);
        }
        break;
    case 8:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Sound_StopSfx(1);
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetEnemyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            actor->homeX = sBattleActionSavedX;
            actor->homeY = sBattleActionSavedY;
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if (sBattleHitEffectPhase == 0 && sBattleDodgePhase == 0
            && sBattleHitReactionPhase == 0)
            result = 1;
        break;
    }
    if (sBattleActionFrame == sBattleActionEffectConfig->reactionFrame)
    {
        s16 duration = 40;

        sBattleHitEffectPhase = 1;
        sBattleDodgePhase = 0;
        sBattleHitReactionPhase = 0;
        sBattleHitReactionDuration = duration;
        sBattleHitReactionFrame = 0;
        sBattleCompletedHitEffectCount = 0;
        sBattleHitSfxId = BATTLE_HIT_REACTION_DEFAULT_SFX;
        sBattleHitSfxLoop = 0;
    }
    BattleAction_UpdateTargetReactions(actor);
    sBattleActionFrame++;
    return result;
}

/** @brief Advance the action selected by enemy[6].
 *  @note Seeded GPBA replay returned 1 after 131 dispatch updates (131 entry hits).
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x08042784
 *  静态收尾：普通敌人行动 6：处理输入与特殊攻击音，启动并插值移动目标效果，淡变背景后恢复待机。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdateEnemyInterpolatedEffectAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;
    const struct BattleEnemyInfo *info;

    Battle_GetInput();
    info = actor->source.enemy;
    switch (sBattleActionEffectPhase)
    {
    case 0:
    {
        struct BattleCombatant *combatants;
        u8 slots[BATTLE_COMBATANT_COUNT];
        u8 count;
        u8 index;

        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        actor->animation.flags &= ~BATTLE_ANIM_LOOP;
        target->pendingDamage = 0;
        combatants = Battle_GetCombatants();
        count = Battle_ListActionTargetSlots(actor, slots, 0x6F);
        for (index = 0; index < count; index++)
            combatants[slots[index]].pendingDamage = 0;
        BattleAction_AccumulateTargetDamage(actor);
        Battle_SetEnemyAnimation(actor, 4, 0x1B4, 13, 0);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    }
    case 3:
    case 4:
    case 5:
    case 6:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 1;
        break;
    case 1:
        BattleAction_UpdateSpecialEnemyAttackSound(actor);
        if (actor->animation.timelineFrame >= actor->hitReactionStartFrame - 8)
        {
            Sound_PlaySfx(0x69, 1, 0);
            Battle_SetEffectAnimation(actor, target->homeX, target->homeY - 96,
                0x2EA, 10, info->animationBaseId + 5, 5);
            sBattleActionEffectPhase = 2;
        }
        break;
    case 2:
        actor->effectAnimation.y = Battle_InterpolateS16(target->homeY - 96,
            72, 8, sBattleActionFrame - (actor->hitReactionStartFrame - 8), 1);
        if (sBattleActionFrame >= actor->hitReactionStartFrame)
        {
            s16 duration = 25;

            sBattleHitEffectPhase = 1;
            sBattleDodgePhase = 0;
            sBattleHitReactionPhase = 0;
            if (duration < 0)
                sBattleHitReactionDuration = BATTLE_HIT_REACTION_DEFAULT_DURATION;
            else
                sBattleHitReactionDuration = duration;
            sBattleHitReactionFrame = 0;
            sBattleCompletedHitEffectCount = 0;
            sBattleHitSfxId = BATTLE_HIT_REACTION_DEFAULT_SFX;
            sBattleHitSfxLoop = 0;
            BattlePalette_StartBgFade(0, 3, 7, 7, 7, 4, 4, -1, 2);
            sBattleActionEffectPhase = 7;
        }
        break;
    case 7:
        if (sBattleActionFrame >= actor->hitReactionStartFrame + 4)
        {
            BattlePalette_ReturnBgFade(0, 3, 16);
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 8;
        }
        break;
    case 8:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            Battle_SetEnemyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 9;
        }
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    sBattleActionFrame++;
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Slide an enemy left and remove it without testing escape probability.
 *  @note Handler seven mirrors the animation horizontally and uses interpolation curve two.
 *  The twenty samples are frames 0..19, not 1..20; homeX need not reach zero.
 *  Phase nine does not advance after decrementing the enemy count: callers must
 *  stop on result two. Flag 0x200 returns two through phase eighteen, with no removal.
 *  @address 0x08042AB4
 */
u8 BattleAction_UpdateEnemyRetreat(struct BattleCombatant *combatant,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case BATTLE_ENEMY_RETREAT_INIT:
        sBattleActionSavedPaletteSlot = combatant->animation.paletteSlot;
        sBattleActionSavedBaseTile = combatant->animation.baseTile;
        sBattleActionSavedX = combatant->homeX;
        sBattleActionSavedY = combatant->homeY;
        sBattleActionFrame = 0;
        if (!(Battle_GetControlFlags() & 0x200))
        {
            combatant->animation.flags |= BATTLE_ANIM_FLIP_X;
            sBattleActionEffectPhase = BATTLE_ENEMY_RETREAT_MOVE;
        }
        else
        {
            sBattleActionEffectPhase = BATTLE_ENEMY_RETREAT_SKIPPED;
        }
        break;
    case BATTLE_ENEMY_RETREAT_MOVE:
        if (sBattleActionFrame < 20)
        {
            combatant->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                -sBattleActionSavedX, 20, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionEffectPhase = BATTLE_ENEMY_RETREAT_REMOVE;
        }
        break;
    case BATTLE_ENEMY_RETREAT_REMOVE:
        Battle_DecrementEnemyCount();
        combatant->status = BATTLE_STATUS_ESCAPED;
        combatant->id = BATTLE_NO_COMBATANT;
        /* fall through: removal and the suppressed path share result two. */
    case BATTLE_ENEMY_RETREAT_SKIPPED:
        result = 2;
        break;
    }
    return result;
}

/** @brief Advance the action selected by enemy[8].
 *  @note Seeded GPBA replay returned 1 after 75 dispatch updates (75 entry hits).
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x08042B90
 *  静态收尾：普通敌人行动 8：准备目标伤害，以五帧插值改变 verticalLift，复位并等待目标反应。
 *  依据：原汇编、派发表及既有回放；本轮未新增动态/画面覆盖。
 */
u8 BattleAction_UpdateEnemyLiftAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    Battle_GetInput();
    switch (sBattleActionEffectPhase)
    {
    case 0:
    {
        struct BattleCombatant *combatants;
        u8 slots[BATTLE_COMBATANT_COUNT];
        u8 count;
        u8 index;

        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        actor->animation.flags &= ~BATTLE_ANIM_LOOP;
        target->pendingDamage = 0;
        combatants = Battle_GetCombatants();
        count = Battle_ListActionTargetSlots(actor, slots, 0x6F);
        for (index = 0; index < count; index++)
            combatants[slots[index]].pendingDamage = 0;
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 1;
        actor->hitReactionStartFrame = 0;
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    }
    case 1:
    case 2:
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
    case 17:
        break;
    case 18:
        if (sBattleActionFrame <= 14)
        {
            actor->verticalLift = Battle_InterpolateS16(0, actor->homeY - 2,
                15, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            actor->homeX = target->homeX;
            actor->homeY = target->homeY;
            actor->verticalLift = target->homeY - 2;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 19;
        }
        break;
    case 19:
        if (sBattleActionFrame <= 4)
        {
            actor->verticalLift = Battle_InterpolateS16(target->homeY - 2,
                -target->homeY, 5, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            s16 duration;

            actor->verticalLift = 0;
            actor->homeY = target->homeY + 1;
            sBattleActionFrame = 0;
            duration = 40;
            sBattleHitEffectPhase = 1;
            sBattleDodgePhase = 0;
            sBattleHitReactionPhase = 0;
            if (duration < 0)
                sBattleHitReactionDuration = BATTLE_HIT_REACTION_DEFAULT_DURATION;
            else
                sBattleHitReactionDuration = duration;
            sBattleHitReactionFrame = 0;
            sBattleCompletedHitEffectCount = 0;
            sBattleHitSfxId = BATTLE_HIT_REACTION_DEFAULT_SFX;
            sBattleHitSfxLoop = 0;
            Sound_PlaySfx(0x4E, 2, 0);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (sBattleActionFrame > 14)
        {
            actor->homeX = sBattleActionSavedX;
            actor->homeY = sBattleActionSavedY;
            sBattleActionEffectPhase = 21;
        }
        else
            sBattleActionFrame++;
        break;
    case 21:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            sBattleActionEffectPhase = 28;
        else
            sBattleActionFrame++;
        break;
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
        break;
    case 28:
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进近战闪烁突进、追加攻击判定和装备特殊伤害。
 *  @note 普通 C 的 0x6E2 字节及重定位 100% 匹配，完整 ROM 与 baserom 一致；末尾两字节零填充不计。
 *  字节 ID 临时量和首次目标类别分支顺序保留 actor/target 的 r7/r6 分配及帧指针重取。
 *  保留五帧位移和连击间隔；装备类型 3/4 分别以 10%/25% 概率写入 999 并返回 3，正常完成返回 1。
 *  @address 0x08042E70
 */
u8 BattleAction_UpdateEnemyMeleeLungeAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;
    u8 combatantId;

    switch (sBattleActionEffectPhase)
    {
    case 0:
    {
        struct BattleCombatant *combatants;
        u8 slots[BATTLE_COMBATANT_COUNT];
        u8 count;
        u8 index;

        sBattleLastAttackDamage = 0;
        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionFrame = 0;
        actor->animation.flags &= ~BATTLE_ANIM_LOOP;
        combatants = Battle_GetCombatants();
        count = Battle_ListActionTargetSlots(actor, slots, 0x6F);
        for (index = 0; index < count; index++)
            combatants[slots[index]].pendingDamage = 0;
        Battle_SetPartyAttackSpacingWidth(actor);
        sBattleActionEffectPhase = 1;
        Battle_UpdateEnemyAttackSpacingWidth(actor);
        break;
    }
    case 1:
        if (actor->animation.flags & BATTLE_ANIM_HIDDEN)
            actor->animation.flags &= ~BATTLE_ANIM_HIDDEN;
        else
            actor->animation.flags |= BATTLE_ANIM_HIDDEN;
        if (sBattleActionFrame <= 4)
        {
            sBattleActionFrame++;
            combatantId = actor->id;
            if (combatantId < BATTLE_CHARACTER_ID_COUNT)
            {
                combatantId = target->id;
                if (combatantId >= BATTLE_CHARACTER_ID_COUNT)
                {
                    actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                        -10, 5, sBattleActionFrame, 2);
                    actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                        (target->homeY - sBattleActionSavedY) / 10, 5, sBattleActionFrame, 2);
                }
                else
                {
                    actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                        10, 5, sBattleActionFrame, 2);
                    actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                        (target->homeY - sBattleActionSavedY) / 10, 5, sBattleActionFrame, 2);
                }
            }
            else
            {
                combatantId = target->id;
                if (combatantId < BATTLE_CHARACTER_ID_COUNT)
                {
                    actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                        10, 5, sBattleActionFrame, 2);
                    actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                        (target->homeY - sBattleActionSavedY) / 10, 5, sBattleActionFrame, 2);
                }
                else
                {
                    actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                        -10, 5, sBattleActionFrame, 2);
                    actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                        (target->homeY - sBattleActionSavedY) / 10, 5, sBattleActionFrame, 2);
                }
            }
        }
        else
        {
            sBattleActionFrame = 0;
            combatantId = actor->id;
            if (combatantId < BATTLE_CHARACTER_ID_COUNT)
            {
                combatantId = target->id;
                if (combatantId < BATTLE_CHARACTER_ID_COUNT)
                {
                    u32 work;
                    u8 coordinate;
                    const u8 *targetX = &target->homeX;

                    work = (actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1);
                    coordinate = *targetX - work - 10;
                    work = (u32)actor + offsetof(struct BattleCombatant, homeX);
                    *(u8 *)work = coordinate;
                    coordinate = target->homeY + 1;
                    work++;
                    *(u8 *)work = coordinate;
                }
                else
                {
                    actor->homeX = target->homeX
                        + ((actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1)) + 10;
                    actor->homeY = target->homeY + 1;
                }
            }
            else
            {
                combatantId = target->id;
                if (combatantId < BATTLE_CHARACTER_ID_COUNT)
                {
                    u32 work;
                    u8 coordinate;
                    const u8 *targetX = &target->homeX;

                    work = (actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1);
                    coordinate = *targetX - work - 10;
                    work = (u32)actor + offsetof(struct BattleCombatant, homeX);
                    *(u8 *)work = coordinate;
                    coordinate = target->homeY + 1;
                    work++;
                    *(u8 *)work = coordinate;
                }
                else
                {
                    actor->homeX = target->homeX
                        + ((actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1)) + 10;
                    actor->homeY = target->homeY + 1;
                }
            }
            sBattleActionEffectPhase = 2;
        }
        break;
    case 2:
        if (actor->animation.flags & BATTLE_ANIM_HIDDEN)
            actor->animation.flags &= ~BATTLE_ANIM_HIDDEN;
        else
            actor->animation.flags |= BATTLE_ANIM_HIDDEN;
        if (sBattleActionFrame <= 4)
        {
            sBattleActionFrame++;
            combatantId = actor->id;
            if (combatantId < BATTLE_CHARACTER_ID_COUNT)
            {
                combatantId = target->id;
                if (combatantId < BATTLE_CHARACTER_ID_COUNT)
                    actor->homeX = Battle_InterpolateS16(target->homeX
                        - ((actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1)) - 10,
                        10, 5, sBattleActionFrame, 2);
                else
                    actor->homeX = Battle_InterpolateS16(target->homeX
                        + ((actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1)) + 10,
                        -10, 5, sBattleActionFrame, 2);
            }
            else
            {
                combatantId = target->id;
                if (combatantId < BATTLE_CHARACTER_ID_COUNT)
                    actor->homeX = Battle_InterpolateS16(target->homeX
                        - ((actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1)) - 10,
                        10, 5, sBattleActionFrame, 2);
                else
                    actor->homeX = Battle_InterpolateS16(target->homeX
                        + ((actor->attackSpacingWidth >> 1) + (target->unknownC4 >> 1)) + 10,
                        -10, 5, sBattleActionFrame, 2);
            }
        }
        else
        {
            if (actor->animation.flags & BATTLE_ANIM_HIDDEN)
                actor->animation.flags &= ~BATTLE_ANIM_HIDDEN;
            sBattleActionEffectPhase = 3;
        }
        break;
    case 3:
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        BattleAction_AccumulateTargetDamage(actor);
        combatantId = actor->id;
        if (combatantId < BATTLE_CHARACTER_ID_COUNT)
        {
            Battle_SetPartyAnimation(actor, 1, 0x1B4, 13, (u8)sBattleActionHitIndex);
            sBattleActionHitIndex++;
        }
        else if (actor->id <= 112)
            Battle_SetEnemyAnimation(actor, 4, 0x1B4, 12, 0);
        else
            Battle_SetBossAnimation(actor, 1, 0x1B4, 13, 0);
        sBattleActionFrame = 0;
        sBattleActionEffectPhase = 4;
        break;
    case 4:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 5;
        break;
    case 5:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(actor->animation.paletteSlot,
                Battle_GetAnimationPaletteCount(&actor->animation));
            actor->animation.flags &= ~BATTLE_ANIM_FINISHED;
            actor->animation.flags |= 0x100;
        }
        else
            BattleAction_UpdateEnemyActionSound(actor);
        if ((u8)(sBattleHitEffectPhase - 4) > 1 && (actor->animation.flags & 0x100))
        {
            Battle_RestoreIdleAnimation(actor, actor->homeX, actor->homeY,
                sBattleActionSavedBaseTile, sBattleActionSavedPaletteSlot);
            sBattleActionEffectPhase = 6;
        }
        sBattleActionFrame++;
        if (sBattleActionFrame == actor->hitReactionStartFrame - 5)
        {
            s16 duration = 20;

            sBattleHitEffectPhase = 1;
            sBattleDodgePhase = 0;
            sBattleHitReactionPhase = 0;
            sBattleHitReactionDuration = duration;
            sBattleHitReactionFrame = 0;
            sBattleCompletedHitEffectCount = 0;
            sBattleHitSfxId = BATTLE_HIT_REACTION_DEFAULT_SFX;
            sBattleHitSfxLoop = 0;
        }
        break;
    case 6:
        if (sBattleActionValidTargetCount && actor->id < BATTLE_CHARACTER_ID_COUNT)
        {
            if ((u8)Battle_RollPartyAttackFollowup(actor, target, (u8)sBattleActionHitIndex) == 1)
            {
                Battle_SeedRandom(Battle_NextRandom());
                sBattleActionEffectPhase = 8;
            }
            else
                sBattleActionEffectPhase = 7;
        }
        else
            sBattleActionEffectPhase = 7;
        sBattleActionFrame = 0;
        break;
    case 8:
        if (sBattleActionFrame > 14)
            sBattleActionEffectPhase = 3;
        sBattleActionFrame++;
        break;
    case 7:
        if (sBattleActionFrame > 24)
            sBattleActionEffectPhase = 9;
        else if (sBattleActionFrame == 8 && actor->id <= 112)
            actor->animation.flags |= BATTLE_ANIM_SEMITRANSPARENT;
        else if (sBattleActionFrame == 10 && actor->id <= 112)
        {
            actor->homeX = sBattleActionSavedX;
            actor->homeY = sBattleActionSavedY;
        }
        else if (sBattleActionFrame == 13 && actor->id <= 112)
            actor->animation.flags &= ~BATTLE_ANIM_SEMITRANSPARENT;
        sBattleActionFrame++;
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            sBattleLastAttackDamage = target->pendingDamage;
            if (target->id <= 112)
            {
                if (BattleItem_FindEquipmentByType(actor, 3) >= 0)
                {
                    if (Battle_NextRandom() % 100 < 10)
                    {
                        target->pendingDamage = 999;
                        result = 3;
                    }
                    else
                        result = 1;
                }
                else if (BattleItem_FindEquipmentByType(actor, 4) >= 0)
                {
                    if (Battle_NextRandom() % 100 < 25)
                    {
                        target->pendingDamage = 999;
                        result = 3;
                    }
                    else
                        result = 1;
                }
                else
                    result = 1;
            }
            else
                result = 1;
        }
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 清空当前行动中状态筛选有效目标的待结算伤害。
 *  @note 私有数组在内联函数内分配，保留原版内联后的栈地址计算。
 */
static inline void BattleAction_ClearPendingDamageInline(struct BattleCombatant *actor)
{
    u8 slots[BATTLE_COMBATANT_COUNT];
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 count = Battle_ListActionTargetSlots(actor, slots, 0x6F);
    u8 index;

    for (index = 0; index < count; index++)
        combatants[slots[index]].pendingDamage = 0;
}

/** @brief 按目标横坐标排序并在范围中心推进敌人效果与受击。
 *  @note 普通 C 的 0x3E4 字节及重定位 100% 匹配；伤害清零助手完全内联，目标槽排序及反应状态写入顺序不变。
 *  @address 0x08043554
 */
u8 BattleAction_UpdateEnemySequentialTargetEffectAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 sortedSlots[BATTLE_ENEMY_COUNT];
    u8 result = 0;
    struct BattleCombatant *allCombatants = Battle_GetCombatants();

    switch (sBattleActionEffectPhase)
    {
    case 0:
    {
        u8 index;

        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionFrame = 0;
        BattleAction_ClearPendingDamageInline(actor);
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 0;
        actor->hitReactionStartFrame = 0;
        for (index = 0; index < sBattleActionEffectTargetCount; index++)
            sortedSlots[index] = sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK;
        Battle_SortCombatantSlots(sortedSlots, sBattleActionEffectTargetCount, BATTLE_STAT_HOME_X);
        sBattleActionScratch86E = allCombatants[sortedSlots[sBattleActionEffectTargetCount - 1]].homeX
            + (allCombatants[sortedSlots[0]].homeX
                - allCombatants[sortedSlots[sBattleActionEffectTargetCount - 1]].homeX) / 2;
        sBattleActionEffectPhase = 18;
        break;
    }
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        Battle_SetEnemyAnimation(actor, 4, 0x1B4, 12, 0);
        sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 1, 0);
            sBattleActionEffectPhase = 20;
        }
        break;
    case 20:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Sound_StopSfx(1);
            Battle_SetEnemyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            u16 animationId;

            switch (actor->id)
            {
            case 93:
            default:
                animationId = 0x34E;
                break;
            case 99:
                animationId = 0x34F;
                break;
            }
            Battle_SetEffectAnimation(actor, sBattleActionScratch86E, 130,
                0x1B4, 12, animationId, 5);
            actor->effectAnimation.priority = 2;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 23;
        break;
    case 23:
        if (actor->effectAnimation.timelineFrame > 45)
        {
            BattlePalette_StartBgFade(0, 3, 16, 28, 31, 4, 4, -1, 2);
            Sound_PlaySfx(0x1E, 1, 0);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            BattlePalette_ReturnBgFade(0, 3, 16);
            {
                s16 duration = 40;

                sBattleHitEffectPhase = 1;
                sBattleDodgePhase = 0;
                sBattleHitReactionPhase = 0;
                sBattleHitReactionDuration = duration;
                sBattleHitReactionFrame = 0;
                sBattleCompletedHitEffectCount = 0;
                sBattleHitSfxId = BATTLE_HIT_REACTION_DEFAULT_SFX;
                sBattleHitSfxLoop = 0;
            }
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionEffectPhase = 9;
        }
        sBattleActionFrame++;
        break;
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
    case 31:
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        else
            sBattleActionFrame++;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Finish the enemy's skill animation before starting target hit reactions.
 *  @note Handler twelve plays SFX A5 on slot one, restores idle, then starts a
 *  fifty-frame reaction with hit sound 64. All paths update target reactions;
 *  result one requires all three reaction phases to be idle on entry.
 *  @address 0x08043938
 */
u8 BattleAction_UpdateEnemyAnimationHit(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case BATTLE_ENEMY_ANIMATION_HIT_INIT:
    {
        u8 slots[BATTLE_COMBATANT_COUNT];
        struct BattleCombatant *combatants;
        u8 count;
        u8 index;

        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionFrame = 0;
        combatants = Battle_GetCombatants();
        count = Battle_ListActionTargetSlots(actor, slots, 0x6F);
        for (index = 0; index < count; index++)
            combatants[slots[index]].pendingDamage = 0;
        BattleAction_AccumulateTargetDamage(actor);
        sBattleActionEffectPhase = BATTLE_ENEMY_ANIMATION_HIT_START;
        break;
    }
    case BATTLE_ENEMY_ANIMATION_HIT_START:
        Battle_SetEnemyAnimation(actor, 4, 0x1B4, 12, 0);
        sBattleActionEffectPhase = BATTLE_ENEMY_ANIMATION_HIT_WAIT_GRAPHICS;
        break;
    case BATTLE_ENEMY_ANIMATION_HIT_WAIT_GRAPHICS:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            Sound_PlaySfx(0xA5, 1, 0);
            sBattleActionEffectPhase = BATTLE_ENEMY_ANIMATION_HIT_WAIT_FINISHED;
        }
        break;
    case BATTLE_ENEMY_ANIMATION_HIT_WAIT_FINISHED:
        if (actor->animation.flags & BATTLE_ANIM_FINISHED)
        {
            Sound_StopSfx(1);
            Battle_SetEnemyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = BATTLE_ENEMY_ANIMATION_HIT_WAIT_IDLE;
        }
        break;
    case BATTLE_ENEMY_ANIMATION_HIT_WAIT_IDLE:
    {
        u16 loading = actor->animation.flags & BATTLE_ANIM_LOADING;

        if (loading == 0)
        {
            s16 duration = 50;
            u16 sfxId = 0x64;

            sBattleHitEffectPhase = 1;
            sBattleDodgePhase = 0;
            sBattleHitReactionPhase = 0;
            sBattleHitReactionDuration = duration;
            sBattleHitReactionFrame = 0;
            sBattleCompletedHitEffectCount = 0;
            sBattleHitSfxId = sfxId;
            sBattleHitSfxLoop = 0;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = BATTLE_ENEMY_ANIMATION_HIT_WAIT_REACTIONS;
        }
        break;
    }
    case BATTLE_ENEMY_ANIMATION_HIT_WAIT_REACTIONS:
        if (sBattleHitEffectPhase == 0 && sBattleDodgePhase == 0 && sBattleHitReactionPhase == 0)
            result = 1;
        else
            sBattleActionFrame++;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 按目标尺寸定位并移动敌人效果，结束后恢复待机。
 *  @note 普通 C 的 0x434 字节及重定位 100% 匹配；保留局部插值、反应状态初始化及原有完成码。
 *  @address 0x08043B5C
 */
u8 BattleAction_UpdateEnemyMovingTargetEffectAttack(struct BattleCombatant *actor,
    struct BattleCombatant *target)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
    {
        u8 slots[BATTLE_COMBATANT_COUNT];
        struct BattleCombatant *combatants;
        u8 count;
        u8 index;

        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionFrame = 0;
        combatants = Battle_GetCombatants();
        count = Battle_ListActionTargetSlots(actor, slots, 0x6F);
        for (index = 0; index < count; index++)
            combatants[slots[index]].pendingDamage = 0;
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 1;
        actor->hitReactionStartFrame = 0;
        sBattleActionScratch86E = target->homeX;
        sBattleActionScratch86F = target->homeY - ((u8)Battle_GetCombatantDimension(target, 1) >> 1);
        sBattleActionEffectPhase = 18;
        break;
    }
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        Battle_SetEnemyAnimation(actor, 3, 0x1B4, 12, 0);
        sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 20;
        break;
    case 20:
        if (actor->animation.timelineFrame > 28)
        {
            Sound_PlaySfx(0x8B, 1, 0);
            BattlePalette_StartObjCycle(13, 1, 4, -3, 3);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (sBattleActionFrame <= 19)
            sBattleActionFrame++;
        else
        {
            BattlePalette_StopObjCycle(13, 1);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        if (actor->animation.timelineFrame > 69)
        {
            Battle_SetEffectAnimation(actor, actor->homeX, actor->homeY - 30,
                0x27C, 14, 0x34C, 5);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 23;
        }
        break;
    case 23:
    {
        u16 loading = actor->effectAnimation.flags & BATTLE_ANIM_LOADING;

        if (!loading)
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 24;
        }
        break;
    }
    case 24:
        if (sBattleActionFrame <= 14)
            sBattleActionFrame++;
        else
        {
            Sound_PlaySfx(0x31, 1, 0);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (sBattleActionFrame <= 14)
        {
            actor->effectAnimation.x = Battle_InterpolateS16(actor->homeX,
                sBattleActionScratch86E - actor->homeX, 15, sBattleActionFrame, 2);
            actor->effectAnimation.y = Battle_InterpolateS16(actor->homeY - 30,
                sBattleActionScratch86F - (actor->homeY - 20), 15, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionFrame = 0;
            {
                s16 duration = 30;

                sBattleHitEffectPhase = 1;
                sBattleDodgePhase = 0;
                sBattleHitReactionPhase = 0;
                if (duration < 0)
                    sBattleHitReactionDuration = BATTLE_HIT_REACTION_DEFAULT_DURATION;
                else
                    sBattleHitReactionDuration = duration;
                sBattleHitReactionFrame = 0;
                sBattleCompletedHitEffectCount = 0;
                sBattleHitSfxId = BATTLE_HIT_REACTION_DEFAULT_SFX;
                sBattleHitSfxLoop = 0;
            }
            actor->animation.flags |= 0x100;
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_SetEnemyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            sBattleActionEffectPhase = 27;
        }
        else
            sBattleActionFrame++;
        break;
    case 27:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 9;
        break;
    case 28:
    case 29:
    case 30:
    case 31:
        break;
    case 9:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
            result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief 推进普通敌人的定点效果、目标受击及原位恢复。
 *  @note 普通 C 的 0x404 字节及重定位 100% 匹配；保留目标伤害清零、背景渐变和反应状态初始化顺序。
 *  @address 0x08043F90
 */
u8 BattleAction_UpdateEnemyFixedPointEffectAttack(struct BattleCombatant *actor,
    struct BattleCombatant *unusedTarget)
{
    u8 result = 0;

    switch (sBattleActionEffectPhase)
    {
    case 0:
    {
        u8 slots[BATTLE_COMBATANT_COUNT];
        struct BattleCombatant *combatants;
        u8 count;
        u8 index;

        sBattleActionSavedX = actor->homeX;
        sBattleActionSavedY = actor->homeY;
        sBattleActionSavedPaletteSlot = actor->animation.paletteSlot;
        sBattleActionSavedBaseTile = actor->animation.baseTile;
        sBattleActionFrame = 0;
        combatants = Battle_GetCombatants();
        count = Battle_ListActionTargetSlots(actor, slots, 0x6F);
        for (index = 0; index < count; index++)
            combatants[slots[index]].pendingDamage = 0;
        BattleAction_AccumulateTargetDamage(actor);
        actor->hitEffectId = 1;
        actor->hitReactionStartFrame = 0;
        sBattleActionEffectPhase = 18;
        break;
    }
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        Battle_SetEnemyAnimation(actor, 4, 0x1B4, 12, 0);
        sBattleActionEffectPhase = 19;
        break;
    case 19:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
            sBattleActionEffectPhase = 20;
        break;
    case 20:
        if (actor->animation.timelineFrame > 66)
        {
            actor->animation.flags |= 0x100;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 21;
        }
        break;
    case 21:
        if (sBattleActionFrame <= 19)
        {
            actor->homeX = Battle_InterpolateS16(sBattleActionSavedX,
                186 - sBattleActionSavedX, 20, sBattleActionFrame, 2);
            actor->homeY = Battle_InterpolateS16(sBattleActionSavedY,
                104 - sBattleActionSavedY, 20, sBattleActionFrame, 2);
            sBattleActionFrame++;
        }
        else
        {
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 22;
        }
        break;
    case 22:
        Battle_SetEffectAnimation(actor, actor->homeX, actor->homeY - 32,
            0x27C, 14, 0x341, 5);
        sBattleActionEffectPhase = 23;
        break;
    case 23:
        if (!(actor->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            actor->animation.flags |= BATTLE_ANIM_HIDDEN;
            actor->displayState |= BATTLE_DISPLAY_HIDE_STATUS_ICON;
            BattlePalette_StartBgFade(0, 3, 28, 14, 7, 4, 4, -1, 2);
            Sound_PlaySfx(0x31, 1, 0);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 24;
        }
        break;
    case 24:
        if (sBattleActionFrame <= 3)
            sBattleActionFrame++;
        else
        {
            BattlePalette_ReturnBgFade(0, 3, 16);
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 25;
        }
        break;
    case 25:
        if (actor->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            s16 duration = 30;

            sBattleHitEffectPhase = 1;
            sBattleDodgePhase = 0;
            sBattleHitReactionPhase = 0;
            sBattleHitReactionDuration = duration;
            sBattleHitReactionFrame = 0;
            sBattleCompletedHitEffectCount = 0;
            sBattleHitSfxId = BATTLE_HIT_REACTION_DEFAULT_SFX;
            sBattleHitSfxLoop = 0;
            actor->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleActionFrame = 0;
            sBattleActionEffectPhase = 26;
        }
        break;
    case 26:
        if (!sBattleHitEffectPhase && !sBattleDodgePhase && !sBattleHitReactionPhase)
        {
            Battle_SetEnemyAnimation(actor, 0, sBattleActionSavedBaseTile,
                sBattleActionSavedPaletteSlot, 0);
            actor->homeX = sBattleActionSavedX;
            actor->homeY = sBattleActionSavedY;
            sBattleActionEffectPhase = 27;
        }
        else
            sBattleActionFrame++;
        break;
    case 27:
        if (!(actor->animation.flags & BATTLE_ANIM_LOADING))
        {
            actor->displayState &= ~BATTLE_DISPLAY_HIDE_STATUS_ICON;
            sBattleActionEffectPhase = 28;
        }
        break;
    case 28:
        sBattleActionEffectPhase = 9;
        break;
    case 29:
    case 30:
    case 31:
        break;
    case 9:
        result = 1;
        break;
    }
    BattleAction_UpdateTargetReactions(actor);
    return result;
}

/** @brief Play the extra attack sound for five special enemy ids.
 *  @note The two frame-49 cases intentionally remain separate; thresholds are strict.
 *  @address 0x08044394
 */
void BattleAction_UpdateSpecialEnemyAttackSound(const struct BattleCombatant *combatant)
{
    u8 play = 0;
    u16 sfxId = 0;

    if (combatant->id > 11 && sBattleAttackSfxPlayed == 0)
    {
        switch (combatant->id)
        {
        case 45:
            if (combatant->animation.timelineFrame > 0)
            {
                sfxId = 0x69;
                play = 1;
            }
            break;
        case 97:
            if (combatant->animation.timelineFrame > 49)
            {
                sfxId = 0x4E;
                play = 1;
            }
            break;
        case 104:
            if (combatant->animation.timelineFrame > 49)
            {
                sfxId = 0x4E;
                play = 1;
            }
            break;
        case 54:
        case 106:
            if (combatant->animation.timelineFrame > 34)
            {
                sfxId = 0x4E;
                play = 1;
            }
            break;
        }
        if (play == 1)
        {
            Sound_PlaySfx(sfxId, 2, 0);
            sBattleAttackSfxPlayed = 1;
        }
    }
}

/** @brief Reset only the removal-effect state machine.
 *  @note Called before combatant removal; preserves all other action state.
 *  @address 0x08044414
 */
void BattleAction_ResetRemovalEffect(void)
{
    sBattleRemovalEffectPhase = 0;
}

/** @brief Read the most recently recorded attack damage.
 *  @note Returns the full unsigned halfword without clearing it.
 *  @address 0x08044420
 */
u16 BattleAction_GetLastAttackDamage(void)
{
    return sBattleLastAttackDamage;
}

/** @brief Reset the action-effect, reaction, sound and pending-status state.
 *  @note Preserves the requested starting phase and clears all twelve queued status bytes.
 *  @address 0x0804442C
 */
void BattleAction_ResetState(u8 phase)
{
    u8 slot;

    sBattleActionEffectPhase = phase;
    sBattleActionHitIndex = 0;
    sBattleHitEffectPhase = 0;
    sBattleDodgePhase = 0;
    sBattleHitReactionPhase = 0;
    sBattleActionFrame = 0;
    sBattleSkillNamePhase = BATTLE_SKILL_NAME_OPEN;
    sBattleAttackSfxPlayed = 0;
    for (slot = 0; slot < BATTLE_COMBATANT_COUNT; slot++)
        gBattleActionPendingStatuses[slot] = 0;
}

/** @brief Reset the count of combatants that completed their escape animation.
 *  @note The escape result and other phase state remain separate objects.
 *  @address 0x0804448C
 */
void BattleAction_ResetEscapeCount(void)
{
    sBattleEscapedCombatantCount = 0;
}

/** @brief Read the completed party-member escape count.
 *  @note This counter is separate from the scene's party-wide escape result.
 *  @address 0x08044498
 */
u8 BattleAction_GetEscapedCombatantCount(void)
{
    return sBattleEscapedCombatantCount;
}

/** @brief Clear pending damage on the actor's filtered target formation.
 *  @note The rules helper writes plain slot ids, not encoded action-target entries.
 *  @address 0x080444A4
 */
void BattleAction_ClearTargetPendingDamage(struct BattleCombatant *actor)
{
    u8 slots[BATTLE_COMBATANT_COUNT];
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 count = Battle_ListActionTargetSlots(actor, slots, 0x6F);
    u8 index;

    for (index = 0; index < count; index++)
        combatants[slots[index]].pendingDamage = 0;
}

/** @brief Test whether all three target-reaction state machines are idle.
 *  @note The main action phase is deliberately not part of this completion test.
 *  @address 0x080444E8
 */
u32 BattleAction_AreTargetReactionsDone(void)
{
    if (sBattleHitEffectPhase == 0 && sBattleDodgePhase == 0 && sBattleHitReactionPhase == 0)
        return 1;
    return 0;
}

/** @brief Start the hit-effect sequence with the default nonlooping hit sound.
 *  @note Negative signed-halfword durations select twelve frames. Keep the reset
 *  local: extracting it to an inline helper changes agbcc's instruction ordering.
 *  @address 0x08044514
 */
void BattleAction_BeginTargetReactions(s16 duration)
{
    sBattleHitEffectPhase = 1;
    sBattleDodgePhase = 0;
    sBattleHitReactionPhase = 0;
    if (duration < 0)
        sBattleHitReactionDuration = BATTLE_HIT_REACTION_DEFAULT_DURATION;
    else
        sBattleHitReactionDuration = duration;
    sBattleHitReactionFrame = 0;
    sBattleCompletedHitEffectCount = 0;
    sBattleHitSfxId = BATTLE_HIT_REACTION_DEFAULT_SFX;
    sBattleHitSfxLoop = 0;
}

/** @brief Start the hit-effect sequence with caller-selected sound parameters.
 *  @note The sound is queued here and played later by the reaction update.
 *  @address 0x08044574
 */
void BattleAction_BeginTargetReactionsWithSound(s16 duration, u16 sfxId, u8 loop)
{
    sBattleHitEffectPhase = 1;
    sBattleDodgePhase = 0;
    sBattleHitReactionPhase = 0;
    if (duration < 0)
        sBattleHitReactionDuration = BATTLE_HIT_REACTION_DEFAULT_DURATION;
    else
        sBattleHitReactionDuration = duration;
    sBattleHitReactionFrame = 0;
    sBattleCompletedHitEffectCount = 0;
    sBattleHitSfxId = sfxId;
    sBattleHitSfxLoop = loop;
}

/** @brief Return the per-slot queued status array.
 *  @note The shared COMMON definition remains owned by battle_action_effects.c.
 *  @address 0x080445E0
 */
u8 *BattleAction_GetPendingStatuses(void)
{
    return gBattleActionPendingStatuses;
}

/** @brief Queue a status for the first action target at the combatant's formation.
 *  @note Silence additionally checks whether the combatant has affected commands.
 *  @address 0x080445E8
 */
void BattleAction_QueueTargetStatus(struct BattleCombatant *combatant, u8 status)
{
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 index;

    for (index = 0; index < sBattleActionEffectTargetCount; index++)
    {
        if (combatants[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK].formationBits
            == combatant->formationBits)
        {
            if (status != BATTLE_STATUS_SILENCED || Battle_HasSilenceBlockedCommands(combatant) == 1)
                gBattleActionPendingStatuses[sBattleActionTargetEntries[index] & BATTLE_TARGET_SLOT_MASK] = status;
            break;
        }
    }
}

/** @brief Advance one combatant's escape using the party or enemy handler.
 *  @note Preserve their different completion codes, one for party and two for enemy.
 *  @address 0x08044680
 */
u8 BattleAction_UpdateCombatantEscape(struct BattleCombatant *combatant)
{
    u8 result;

    if (combatant->id > 10)
        result = BattleAction_UpdateEnemyEscape(combatant);
    else
        result = BattleAction_UpdatePartyMemberEscape(combatant);
    return result;
}

/** @brief Read the current party attack's hit index.
 *  @note Enemy ids return zero; the original read addresses only the low byte.
 *  @address 0x080446A4
 */
u8 BattleAction_GetAttackHitIndex(const struct BattleCombatant *combatant)
{
    u8 hitIndex;

    if (combatant->id > 10)
        hitIndex = 0;
    else
        hitIndex = *(const u8 *)&sBattleActionHitIndex;
    return hitIndex;
}

/** @brief Trigger the enemy's attack/skill sound once its timeline reaches the cue.
 *  @note Halfword indexing retains the original pair selection and record stride.
 *  @address 0x080446BC
 */
void BattleAction_UpdateEnemyActionSound(const struct BattleCombatant *combatant)
{
    u16 sfxIndex = 0;
    u16 frameIndex = 1;

    if (combatant->id > 11 && sBattleAttackSfxPlayed == 0)
    {
        const u8 *sounds;
        s32 frameOffset;
        s32 offset;

        switch (combatant->action)
        {
        case BATTLE_ACTION_ATTACK:
            break;
        case BATTLE_ACTION_SKILL:
            sfxIndex = 2;
            frameIndex = 3;
            break;
        }
        sounds = (const u8 *)gBattleEnemyActionSounds;
        frameOffset = frameIndex * sizeof(u16);
        offset = (combatant->id - 12) * sizeof(struct BattleEnemyActionSounds);
        if (combatant->animation.timelineFrame >= *(const u16 *)(sounds + (frameOffset + offset)))
        {
            Sound_PlaySfx(*(const u16 *)(sounds + (sfxIndex * sizeof(u16) + offset)), 2, 0);
            sBattleAttackSfxPlayed = 1;
        }
    }
}

/** @brief Complete the empty skill-table handler immediately with result two.
 *  @note Skill handler 37 ignores both arguments and does not update action state; the skill-name window is handled by its caller.
 *  @note Seeded GPBA replay returned 2 after 52 dispatch updates (1 entry hits).
 *  Dispatch update counts include the skill-name window.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x08044728
 */
u8 BattleAction_UpdateEmptySkill(struct BattleCombatant *actor, struct BattleCombatant *target)
{
    return 2;
}

/** @brief Return zero without advancing skill-table entry 44.
 *  @note Both arguments are ignored. The original four-byte entry always returns zero; bounded replay is expected and is not action completion.
 *  @note Seeded GPBA replay stayed at phase 0 with result zero through 220 dispatch updates.
 *  Dispatch update counts include the skill-name window.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x0804472C
 */
u8 BattleAction_UpdateNoOpSkill44(struct BattleCombatant *actor, struct BattleCombatant *target)
{
    return 0;
}

/** @brief Return zero without advancing skill-table entry 45.
 *  @note Both arguments are ignored. The original four-byte entry always returns zero; bounded replay is expected and is not action completion.
 *  @note Seeded GPBA replay stayed at phase 0 with result zero through 220 dispatch updates.
 *  Dispatch update counts include the skill-name window.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x08044730
 */
u8 BattleAction_UpdateNoOpSkill45(struct BattleCombatant *actor, struct BattleCombatant *target)
{
    return 0;
}

/** @brief Return zero without advancing skill-table entry 46.
 *  @note Both arguments are ignored. The original four-byte entry always returns zero; bounded replay is expected and is not action completion.
 *  @note Seeded GPBA replay stayed at phase 0 with result zero through 220 dispatch updates.
 *  Dispatch update counts include the skill-name window.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x08044734
 */
u8 BattleAction_UpdateNoOpSkill46(struct BattleCombatant *actor, struct BattleCombatant *target)
{
    return 0;
}

/** @brief Return zero without advancing skill-table entry 47.
 *  @note Both arguments are ignored. The original four-byte entry always returns zero; bounded replay is expected and is not action completion.
 *  @note Seeded GPBA replay stayed at phase 0 with result zero through 220 dispatch updates.
 *  Dispatch update counts include the skill-name window.
 *  Replay evidence: docs/assets/battle-action-analysis-20261114/replay/.
 *  @address 0x08044738
 */
u8 BattleAction_UpdateNoOpSkill47(struct BattleCombatant *actor, struct BattleCombatant *target)
{
    return 0;
}

/* Split read-only data, in ROM address order. */

/* ROM 0x0839CD5C..0x0839CE38; .rodata. */
BattleActionHandler const gBattleBossActionHandlers[BATTLE_BOSS_ACTION_HANDLER_COUNT] __attribute__((aligned(1))) =
{
    (BattleActionHandler)BattleAction_UpdateBoss113ProjectileAttack, (BattleActionHandler)BattleAction_UpdateBoss113FireBurstAttack, (BattleActionHandler)BattleAction_UpdateBoss114FireBreathAttack,
    (BattleActionHandler)BattleAction_UpdateBoss114PoisonBreathAttack, (BattleActionHandler)BattleAction_UpdateBlinkStrike,
    (BattleActionHandler)BattleAction_UpdateBossPoisonSkillVariant, (BattleActionHandler)BattleAction_UpdateBoss115WhirlwindAnimationVariant, (BattleActionHandler)BattleAction_UpdateBoss116WindAttack,
    (BattleActionHandler)BattleAction_UpdateBoss116GustAttack, (BattleActionHandler)BattleAction_UpdateBoss116PoisonAttackVariant, (BattleActionHandler)BattleAction_UpdateBoss117TongueAttack,
    (BattleActionHandler)BattleAction_UpdateBoss117JumpStompAttack, (BattleActionHandler)BattleAction_UpdateBoss118GreenProjectileAttack, (BattleActionHandler)BattleAction_UpdateBoss118ChargeAttack,
    (BattleActionHandler)BattleAction_UpdateBoss118BeamAttack, (BattleActionHandler)BattleAction_UpdateBoss119PounceAttack, (BattleActionHandler)BattleAction_UpdateSharedBossFireBreathAttack,
    (BattleActionHandler)BattleAction_UpdatePairedBossAttack, (BattleActionHandler)BattleAction_UpdateBoss120JumpStrikeAttack,
    (BattleActionHandler)BattleAction_UpdateBossFireBreathVariant, (BattleActionHandler)BattleAction_UpdateBoss120PaletteCycleAttackVariant, (BattleActionHandler)BattleAction_UpdateBossPalettePulseAttackVariant,
    (BattleActionHandler)BattleAction_UpdateBoss121RedEnergyAttack, (BattleActionHandler)BattleAction_UpdateBossWaypointAttack,
    (BattleActionHandler)BattleAction_UpdateBoss121BlueEnergyAttack, (BattleActionHandler)BattleAction_UpdateBoss121FlashAnimation, (BattleActionHandler)BattleAction_UpdateBoss122And124SpikeAttack,
    (BattleActionHandler)BattleAction_UpdateBoss122And124WindAttack, (BattleActionHandler)BattleAction_UpdateBoss122And124FireBurstAttack, (BattleActionHandler)BattleAction_UpdateBoss123SparkAttack,
    (BattleActionHandler)BattleAction_UpdateBoss123FireBlastAttack, (BattleActionHandler)BattleAction_UpdateBoss123RingAttackVariant, (BattleActionHandler)BattleAction_UpdateBoss123WindAttack,
    (BattleActionHandler)BattleAction_UpdateBoss123RockfallAttack, (BattleActionHandler)BattleAction_UpdateBoss125LightningAttack, (BattleActionHandler)BattleAction_UpdateBoss125PurpleMistAttack,
    (BattleActionHandler)BattleAction_UpdateBoss125FireballAttack, (BattleActionHandler)BattleAction_UpdateBossParalysisAttack,
    (BattleActionHandler)BattleAction_UpdateBoss126LightAttack, (BattleActionHandler)BattleAction_UpdateBoss126CrossLightAttack,
    (BattleActionHandler)BattleAction_UpdateBossHalfMaxHpRecovery, (BattleActionHandler)BattleAction_UpdateBoss126FireballAttack,
    (BattleActionHandler)BattleAction_UpdateBoss127DragonChargeAttack, (BattleActionHandler)BattleAction_UpdateBoss127SingleTargetSpellAttack, (BattleActionHandler)BattleAction_UpdateBoss127FireBlastAttack,
    (BattleActionHandler)BattleAction_UpdateBoss127ThreeEffectSpellVariant, (BattleActionHandler)BattleAction_UpdateBoss127WindAttack, (BattleActionHandler)BattleAction_UpdateBoss127RockfallAttack,
    (BattleActionHandler)BattleAction_UpdateBossFireballAttackVariant, (BattleActionHandler)BattleAction_UpdateBoss128WingLeapAttack, (BattleActionHandler)BattleAction_UpdateBoss128FlameWaveAttack,
    (BattleActionHandler)BattleAction_UpdateBoss128BlueCircleAttack, (BattleActionHandler)BattleAction_UpdateBoss129BiteAttack, (BattleActionHandler)BattleAction_UpdateBoss129DarkWaveAttack,
    (BattleActionHandler)BattleAction_UpdateBoss129DarkFaceAttackVariant,
};

/* ROM 0x0839CEC4..0x0839CEFC; .rodata.0039CEC4. */
BattleActionHandler const gBattleEnemyActionHandlers[BATTLE_ENEMY_ACTION_HANDLER_COUNT] __attribute__((section(".rodata.0039CEC4"), aligned(1))) =
{
    (BattleActionHandler)BattleAction_UpdateNormalAttack, (BattleActionHandler)BattleAction_UpdateEnemyConfiguredTargetEffectAttack,
    (BattleActionHandler)BattleAction_UpdateEnemyLungeFadeAttack, (BattleActionHandler)BattleAction_UpdateEnemyAnimationFadeAttack,
    (BattleActionHandler)BattleAction_UpdateEnemyAnimationOnly,
    (BattleActionHandler)BattleAction_UpdateConfiguredEnemyEffect, (BattleActionHandler)BattleAction_UpdateEnemyInterpolatedEffectAttack,
    (BattleActionHandler)BattleAction_UpdateEnemyRetreat, (BattleActionHandler)BattleAction_UpdateEnemyLiftAttack,
    (BattleActionHandler)BattleAction_UpdateEnemyMeleeLungeAttack, (BattleActionHandler)BattleAction_UpdateEnemySequentialTargetEffectAttack, (BattleActionHandler)BattleAction_UpdateEnemyMovingTargetEffectAttack,
    (BattleActionHandler)BattleAction_UpdateEnemyAnimationHit, (BattleActionHandler)BattleAction_UpdateEnemyFixedPointEffectAttack,
};

/* ROM 0x0839D47C..0x0839D5BC; .rodata.0039D47C. */
const struct BattleActionEffectConfig gBattleEnemyActionEffectConfigs[BATTLE_ENEMY_ACTION_EFFECT_CONFIG_COUNT] __attribute__((section(".rodata.0039D47C"), aligned(1))) =
{
    { 0x0022, 0x0344, 0x005A, 0x0064, 0x0087, 0x0000 },
    { 0x0047, 0x0346, 0x005A, 0x0064, 0x0087, 0x0000 },
    { 0x0052, 0x0343, 0x004D, 0x004D, 0x008C, 0x000F },
    { 0x0056, 0x0343, 0x004C, 0x004C, 0x008C, 0xFFE7 },
    { 0x0057, 0x033E, 0x004F, 0x005A, 0x009B, 0xFFD8 },
    { 0x0059, 0x0348, 0x005A, 0x0064, 0x0087, 0x0000 },
};

const struct BattleAttackPositionConfig gBattleAttackPositionConfigs[BATTLE_ATTACK_POSITION_CONFIG_COUNT] __attribute__((section(".rodata.0039D47C"), aligned(1))) =
{ { 0x002D, 0x0018, 0x0031, 0x18, 0x00 } };

BattleActionHandler const gBattleSkillActionHandlers[BATTLE_SKILL_ACTION_HANDLER_COUNT] __attribute__((section(".rodata.0039D47C"), aligned(1))) =
{
    (BattleActionHandler)BattleAction_UpdateSwordDance,
    (BattleActionHandler)BattleAction_UpdateFormationJumpSkill, (BattleActionHandler)BattleAction_UpdateFormationLeapStrikeSkill,
    (BattleActionHandler)BattleAction_UpdateSelfAtcBoostSkill,
    (BattleActionHandler)BattleAction_UpdateSideMagicGuardSkill, (BattleActionHandler)BattleAction_UpdateRedDragonSkill,
    (BattleActionHandler)BattleAction_UpdateSideHp999Skill, (BattleActionHandler)BattleAction_UpdateDragonEnemyRetreatSkill,
    (BattleActionHandler)BattleAction_UpdateSelectedHp50Skill,
    (BattleActionHandler)BattleAction_UpdateStatusCureSkill,
    (BattleActionHandler)BattleAction_UpdateTargetAtcBoostSkill, (BattleActionHandler)BattleAction_UpdateAreaEnemyRetreatSkill,
    (BattleActionHandler)BattleAction_UpdateSleepSong, (BattleActionHandler)BattleAction_UpdateSideHp60Skill,
    (BattleActionHandler)BattleAction_UpdateThunderboltSkill, (BattleActionHandler)BattleAction_UpdateRiotSkill, (BattleActionHandler)BattleAction_UpdateLightningBgPulseSkill,
    (BattleActionHandler)BattleAction_UpdateStatus2Skill, (BattleActionHandler)BattleAction_UpdateStatus3Skill,
    (BattleActionHandler)BattleAction_UpdateStatus5Skill, (BattleActionHandler)BattleAction_UpdateStatus6Skill,
    (BattleActionHandler)BattleAction_UpdateStatus4Skill, (BattleActionHandler)BattleAction_UpdateFireBlastSkill,
    (BattleActionHandler)BattleAction_UpdateFlameCircleSkill, (BattleActionHandler)BattleAction_UpdateInfernoSkill,
    (BattleActionHandler)BattleAction_UpdateTwoStageAtcBoostSkill, (BattleActionHandler)BattleAction_UpdateFreezeBlowSkill,
    (BattleActionHandler)BattleAction_UpdateColdStormSkill, (BattleActionHandler)BattleAction_UpdateBlizzardSkill,
    (BattleActionHandler)BattleAction_UpdateTargetDefBoostSkill,
    (BattleActionHandler)BattleAction_UpdateSelectedHp60Skill,
    (BattleActionHandler)BattleAction_UpdateSideHp40Skill,
    (BattleActionHandler)BattleAction_UpdateSideHpRegenSkill,
    (BattleActionHandler)BattleAction_UpdateSelectedHp999Skill,
    (BattleActionHandler)BattleAction_UpdateReviveThirdMaxHpSkill,
    (BattleActionHandler)BattleAction_UpdateTwoStageStatusCureSkill, (BattleActionHandler)BattleAction_UpdateTargetEnemyRetreatSkill,
    (BattleActionHandler)BattleAction_UpdateEmptySkill, (BattleActionHandler)BattleAction_UpdatePowerSlashSkill,
    (BattleActionHandler)BattleAction_UpdatePowerSweepSkill, (BattleActionHandler)BattleAction_UpdateSonicRiserSkill,
    (BattleActionHandler)BattleAction_UpdateAlternateSelfAtcBoostSkill, (BattleActionHandler)BattleAction_UpdateFlashArrowSkill,
    (BattleActionHandler)BattleAction_UpdateRushWindSkill, (BattleActionHandler)BattleAction_UpdateNoOpSkill44,
    (BattleActionHandler)BattleAction_UpdateNoOpSkill45, (BattleActionHandler)BattleAction_UpdateNoOpSkill46,
    (BattleActionHandler)BattleAction_UpdateNoOpSkill47, (BattleActionHandler)BattleAction_UpdateGaugeFormationSlashes,
    (BattleActionHandler)BattleAction_UpdateGaugeHpMpRecovery, (BattleActionHandler)BattleAction_UpdateGaugeLightBurst,
    (BattleActionHandler)BattleAction_UpdateGaugePartyProtection, (BattleActionHandler)BattleAction_UpdateGaugeLethalStrike,
    (BattleActionHandler)BattleAction_UpdateGaugeFormationSpinSlash, (BattleActionHandler)BattleAction_UpdateGaugeFormationSpinSlash, (BattleActionHandler)BattleAction_UpdateGaugeLightCircle,
    (BattleActionHandler)BattleAction_UpdateGaugeMagicBoost,
    (BattleActionHandler)BattleAction_UpdateAlternateGaugeMagicBoost, (BattleActionHandler)BattleAction_UpdateGaugeLightOrbStrike,
    (BattleActionHandler)BattleAction_UpdateGaugeRepeatedJumpSlashes,
};

/* ROM 0x0839DBF6..0x0839DF1E; .rodata.0039DBF6. */
const struct BattleEnemyActionSounds gBattleEnemyActionSounds[BATTLE_ENEMY_ACTION_SOUND_COUNT] __attribute__((section(".rodata.0039DBF6"), aligned(1))) =
{
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x000A }, { 0x0031, 0x000A } },
    { { 0x0031, 0x000A }, { 0x0031, 0x000A } },
    { { 0x0031, 0x000F }, { 0x0031, 0x000F } },
    { { 0x0031, 0x000F }, { 0x0031, 0x000F } },
    { { 0x0031, 0x0019 }, { 0x0031, 0x0019 } },
    { { 0x0031, 0x0019 }, { 0x0031, 0x0019 } },
    { { 0x0041, 0x000F }, { 0x0041, 0x000F } },
    { { 0x0031, 0x003C }, { 0x0031, 0x003C } },
    { { 0x0031, 0x001E }, { 0x0031, 0x001E } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0023 }, { 0x0031, 0x0023 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0019 }, { 0x0031, 0x0019 } },
    { { 0x0031, 0x000F }, { 0x0031, 0x000F } },
    { { 0x0031, 0x0023 }, { 0x0031, 0x0023 } },
    { { 0x0031, 0x0019 }, { 0x0031, 0x0019 } },
    { { 0x0031, 0x0019 }, { 0x0031, 0x0019 } },
    { { 0x00AE, 0x0028 }, { 0x00AE, 0x0028 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0037 }, { 0x0031, 0x0037 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0078 }, { 0x0031, 0x0078 } },
    { { 0x00AE, 0x0000 }, { 0x00AE, 0x0000 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0086, 0x0028 }, { 0x0086, 0x0028 } },
    { { 0x0031, 0x0019 }, { 0x0031, 0x0019 } },
    { { 0x0031, 0x000F }, { 0x0031, 0x000F } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0019 }, { 0x0031, 0x0019 } },
    { { 0x006D, 0x0000 }, { 0x006D, 0x0000 } },
    { { 0x0031, 0x0032 }, { 0x0031, 0x0032 } },
    { { 0x0031, 0x0023 }, { 0x0031, 0x0023 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0014 }, { 0x0031, 0x0014 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0019 }, { 0x0031, 0x0019 } },
    { { 0x0031, 0x0019 }, { 0x0031, 0x0019 } },
    { { 0x0031, 0x0019 }, { 0x0031, 0x0019 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0032 }, { 0x0031, 0x0032 } },
    { { 0x0031, 0x0019 }, { 0x0031, 0x0019 } },
    { { 0x0031, 0x001E }, { 0x0031, 0x001E } },
    { { 0x0031, 0x001E }, { 0x0031, 0x001E } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x00AE, 0x0000 }, { 0x00B7, 0x002D } },
    { { 0x0031, 0x0028 }, { 0x0031, 0x0028 } },
    { { 0x0031, 0x0046 }, { 0x0031, 0x0046 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x00AE, 0x0028 }, { 0x00AE, 0x0028 } },
    { { 0x0041, 0x000F }, { 0x0041, 0x000F } },
    { { 0x0031, 0x000A }, { 0x0031, 0x000A } },
    { { 0x0031, 0x0014 }, { 0x0031, 0x0014 } },
    { { 0x0031, 0x001E }, { 0x0031, 0x001E } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0064, 0x0041 }, { 0x0064, 0x0041 } },
    { { 0x0031, 0x0023 }, { 0x0031, 0x0023 } },
    { { 0x0031, 0x0028 }, { 0x0031, 0x0028 } },
    { { 0x0031, 0x0078 }, { 0x0031, 0x0078 } },
    { { 0x0031, 0x0028 }, { 0x0031, 0x0028 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0014 }, { 0x0031, 0x0014 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x003C }, { 0x0031, 0x003C } },
    { { 0x00AE, 0x0028 }, { 0x00AE, 0x0028 } },
    { { 0x009B, 0x0000 }, { 0x009B, 0x0000 } },
    { { 0x00AA, 0x0032 }, { 0x00AA, 0x0032 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x00AE, 0x001E }, { 0x006F, 0x001E } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0031, 0x0019 }, { 0x0031, 0x0019 } },
    { { 0x00AE, 0x0000 }, { 0x00AE, 0x0000 } },
    { { 0x0050, 0x0041 }, { 0x0050, 0x0041 } },
    { { 0x0031, 0x0037 }, { 0x0031, 0x0037 } },
    { { 0x00AE, 0x0000 }, { 0x00AE, 0x0000 } },
    { { 0x0031, 0x001E }, { 0x0031, 0x001E } },
    { { 0x00A7, 0x0023 }, { 0x00A7, 0x0023 } },
    { { 0x005C, 0x0032 }, { 0x005C, 0x0032 } },
    { { 0x0031, 0x0000 }, { 0x0031, 0x0000 } },
    { { 0x0050, 0x0041 }, { 0x0050, 0x0041 } },
    { { 0x0031, 0x000F }, { 0x0031, 0x000F } },
    { { 0x0031, 0x0019 }, { 0x0031, 0x0019 } },
    { { 0x0031, 0x0019 }, { 0x0031, 0x0019 } },
    { { 0x0031, 0x0028 }, { 0x0031, 0x0028 } },
    { { 0x0031, 0x000A }, { 0x0031, 0x000A } },
    { { 0x0031, 0x003C }, { 0x0031, 0x003C } },
    { { 0x0031, 0x0023 }, { 0x0031, 0x0023 } },
    { { 0x0031, 0x000F }, { 0x0031, 0x000F } },
};

/* End of split read-only data. */
