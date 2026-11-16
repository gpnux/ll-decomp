#include "common.h"
#include "include_asm.h"
#include "gba.h"
#include "battle.h"
#include "battle_sprite_wipe.h"
#include "battle_command_menu.h"
#include "battle_rules.h"
#include "battle_action_effects.h"
#include "battle_palette.h"
#include "battle_items.h"
#include "battle_ai.h"
#include "core.h"
#include "menu.h"
#include "party.h"
#include "script.h"
#include "sound.h"
#include "system.h"

/* Private .lcomm definitions, in original address order.
 * Reserved ranges preserve bytes with no identified object or access. */
static u8 sBattlePhase; /* 03000240 */
static struct BattleCombatant *sBattleCombatants; /* 03000244 */
static struct BattleCombatant sBattleCursor; /* 03000248 */
static u16 sBattleKeysHeld; /* 03000310 */
static u16 sBattleKeysPressed; /* 03000312 */
static u16 sBattleInput; /* 03000314 */
static u8 sBattleInputDelay; /* 03000316 */
static u8 sBattleDirectionRepeatTimer; /* 03000317 */
static struct BattleDrawNode sBattleDrawList; /* 03000318 */
static u16 sBattleControlFlags; /* 03000324 */
static u32 sBattleRandomState; /* 03000328 */
static u16 sBattlePhaseTimer; /* 0300032C */
static u8 sBattleEncounterId; /* 0300032E */
static u16 sBattleSelections[6]; /* 03000330 */
static u8 sBattleReserved033C[4]; /* 0300033C */
static const u8 *sBattleHintText; /* 03000340 */
static u8 sBattleOamCursor; /* 03000344 */
static struct BattleWindow sBattleWindows[BATTLE_WINDOW_COUNT]; /* 03000348 */
static u16 sBattleRasterFlags; /* 03000384 */
static s16 sBattleRasterPhase; /* 03000386 */
static u8 sBattleReserved0388[8]; /* 03000388 */
static s16 sBattleRasterLines[160]; /* 03000390 */
static s8 *sBattleWaveSamples; /* 030004D0 */
static u8 sBattleRasterSpeed; /* 030004D4 */
static u8 sBattleWaveAmplitude; /* 030004D5 */
static u8 sBattleWaveLineStep; /* 030004D6 */
static u8 sBattleWaveLayerMask; /* 030004D7 */
static vu16 *sBattleWaveXRegisters[4]; /* 030004D8; pointees are BG0..3 HOFS MMIO. */
static vu16 *sBattleWaveYRegisters[4]; /* 030004E8; pointees are BG0..3 VOFS MMIO. */
static u8 sBattleBackgroundLoadProgress; /* 030004F8 */
static BattleBgScroll sBattleBgScroll; /* 03000500; eight cached halfwords. */
static u16 sBattleBgRequests; /* 03000510 */
static u8 sBattleBackgroundLoadPhase; /* 03000512 */
static u8 sBattleBackgroundPaletteOffset; /* 03000513 */
static u8 sBattleBackgroundLoadChunk; /* 03000514 */
static u8 sBattleUploadedTiles[128]; /* 03000518 */
static u8 sBattleUploadScratch[128]; /* 03000598 */

/* COMMON: shared EWRAM storage; positioning is in config/ram.ll.ld. */
u16 gBattleStatusTilemap[BATTLE_TILEMAP_SIZE]; /* 020352C0 */
u16 gBattleWindowTilemap[BATTLE_TILEMAP_SIZE]; /* 02035AC0 */
u16 gBattleAnimationTilemap[BATTLE_TILEMAP_SIZE]; /* 020362C0 */
s8 gBattleWaveSamples[360]; /* 02036EC0 */



/* Six-byte frame part: three GBA OBJ attributes, without the OAM padding word.
 * Background animation uses signed pixel offsets and the OBJ size/shape tables. */
struct BattleAnimationPart
{
    s16 y : 8;
    u16 unused0 : 6;
    u16 shape : 2;
    s16 x : 9;
    u16 unused1 : 3;
    u16 hFlip : 1;
    u16 vFlip : 1;
    u16 size : 2;
    u16 tile : 10;
    u16 priority : 2;
    u16 palette : 4;
} __attribute__((packed, aligned(2)));

typedef char BattleAnimationPartSizeCheck[(sizeof(struct BattleAnimationPart) == 6) ? 1 : -1];

/* Word-sized OBJ attribute fields used to construct the offscreen fill pattern.
 * The final halfword is padding, not an affine coefficient initialized here. */
struct BattleOamAttributes
{
    u32 y : 8;
    u32 affineMode : 2;
    u32 objMode : 2;
    u32 mosaic : 1;
    u32 color256 : 1;
    u32 shape : 2;
    u32 x : 9;
    u32 affineIndexLow : 3;
    u32 hFlip : 1;
    u32 vFlip : 1;
    u32 size : 2;
    u32 tile : 10;
    u32 priority : 2;
    u32 palette : 4;
};

typedef char BattleOamAttributesSizeCheck[
    (sizeof(struct BattleOamAttributes) == sizeof(union SpriteOamEntry)) ? 1 : -1];

/* Ordinary local DISPCNT value; the read-only CGB mode bit is not assigned. */
union BattleDisplayControl
{
    u16 value;
    struct
    {
        u16 mode : 3;
        u16 cgbMode : 1;
        u16 frameSelect : 1;
        u16 hblankOamAccess : 1;
        u16 objMapping1D : 1;
        u16 forcedBlank : 1;
        u16 bg0 : 1;
        u16 bg1 : 1;
        u16 bg2 : 1;
        u16 bg3 : 1;
        u16 obj : 1;
        u16 win0 : 1;
        u16 win1 : 1;
        u16 objWindow : 1;
    } bits;
};


/*
 * 0801768C..0801B964：共享插值、战斗调度与渲染。
 * 运行记录与阶段转移见 docs/BATTLE-SCENE.md（2026-11-11）。
 * 全部 battle 模块的职责及是否合并见 docs/BATTLE-MODULES.md。
 * 下方未命中说明来自先前场景；入口命中不代表覆盖了全部分支。
 * 基础接口已有普通 C 实现，其余入口继续保留参考汇编。
 */

/** @brief Find the first interval whose exclusive end exceeds frame.
 *  @note Both public timeline queries inline this scan; tracks begin with a halfword header. */
static inline const struct BattleKeyframe *Battle_FindKeyframeInline(const u16 *track, u16 frame)
{
    const struct BattleKeyframe *keyframe = (const struct BattleKeyframe *)(track + 1);

    while (keyframe->endFrame <= frame)
        keyframe++;
    return keyframe;
}

/**
 * @brief 按曲线模式插值有符号半字位移，并与起点相加
 *
 * @note 前四参按 s16 运算，第五参按 s8 选择曲线；模式 0 为线性插值。战斗动画和 Sound_Update 均调用此共享助手。
 * @note curve 仅支持 0–3，duration 必须非零；参考实现没有初始化其他模式的 adjustedDelta。
 * @address 0x0801768C
 */
s16 Battle_InterpolateS16(s16 start, s16 delta, s16 duration, s16 frame, s8 curve)
{
    u16 adjustedDelta;
    u16 durationValue = duration;
    u16 frameValue = frame;

    switch (curve)
    {
    case 0:
        adjustedDelta = delta;
        break;
    case 1:
    {
        float ratio = (float)(s16)frameValue / (s16)durationValue;

        adjustedDelta = (s16)(delta * ratio);
        break;
    }
    case 2:
    {
        float ratio = 2.0f - (float)(s16)frameValue / (s16)durationValue;

        adjustedDelta = (s16)(delta * ratio);
        break;
    }
    case 3:
        adjustedDelta = (s16)(delta * (((s16)frameValue * -10.0f
            / (s16)durationValue + 20.0f) / 10.0f));
        break;
    }
    return start + (s16)adjustedDelta * (s16)frameValue / (s16)durationValue;
}

/**
 * @brief 按战斗阶段调度入场、命令处理、胜负结算和场景返回
 *
 * @note 由 gGameLoopHandlers[1] 调用；阶段字节位于 03000240。每帧还重建显示排序链并推进动画。
 * @address 0x080177AC
 */
void Battle_Update(void)
{
    u16 i;
    u8 oamCursor;

    Battle_ReadInput();
    oamCursor = 127;
    Battle_InitDrawList(&sBattleDrawList);

    switch (sBattlePhase)
    {
    case 0:
    {
        u16 offset;
        u16 count;

        Sound_EnableVSync();
        /* The original DMA clears 0xC00 bytes, not just sizeof(gBattleCombatants). */
        {
            u16 fillValue;
            u16 *fillSource = &fillValue;
        
            *fillSource = (u16)(0);
            DmaSet(3, fillSource, gBattleCombatants,
                DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS
                    | ((u32)(0xC00) / 2));
        }
        DmaWait(3);
        sBattleCombatants = gBattleCombatants;
        if (sBattleControlFlags & 1)
            Battle_PrepareEncounter(sBattleEncounterId);
        BattlePalette_Init();
        Battle_StopWaveEffect();
        {
            u32 seed = gRandomTableIndex;

            sBattleRandomState = seed;
        }
        Battle_LoadBackground(0);
        Battle_StartBrightnessTransition(10, 0x120);
        Battle_InitCombatants(sBattleCombatants);
        Battle_InitCursor(&sBattleCursor);
        sBattleCursor.animation.x = 175;
        sBattleCursor.animation.y = 20;
        sBattleDirectionRepeatTimer = 0;
        sBattleInput = 0;
        Battle_PlayEncounterBgm();
        Battle_ResetDamageNumbers(0);
        Battle_ResetWindows();
        BattleResult_BeginVictory();
        if (sBattleControlFlags & 1)
            sBattleControlFlags &= 0xFFFE;
        {
            u16 *selections = sBattleSelections;
            s32 noSelection = -1;

            selections[0] = noSelection;
            selections[1] = noSelection;
            selections[2] = noSelection;
            selections[3] = noSelection;
            selections[4] = noSelection;
            selections[5] = noSelection;
        }
        BattleAction_ResetEscapeCount();
        BattleItem_SaveInventory();
        offset = 0;
        count = 0;
        while (count <= 296)
        {
            if (gMenuHintText[offset] == 0xFF)
                count++;
            offset++;
        }
        sBattleHintText = gMenuHintText + offset;
        sBattlePhase++;
        sBattleInputDelay = 10;
        break;
    }
    case 1:
        if (Battle_GetRasterFlags() & 0x4000)
        {
            Battle_EndRasterTransition();
            Battle_LoadPartyPalettes();
            sBattlePhase++;
        }
        else if (sBattleControlFlags & BATTLE_CONTROL_RASTER_DISABLED)
        {
            for (i = 0; i < BATTLE_COMBATANT_COUNT; i++)
            {
                if (sBattleCombatants[i].id != BATTLE_NO_COMBATANT)
                    sBattleCombatants[i].animation.flags &= 0xFDFF;
            }
            sBattleControlFlags &= 0xFFF7;
            (*(vu16 *)REG_DISPCNT) &= 0xFF7F;
        }
        break;
    case 2:
        {
            u32 seed = gRandomTableIndex;

            sBattleRandomState = seed;
        }
        Battle_ResetWindows();
        if (sBattleControlFlags & 2)
            sBattleControlFlags &= 0xFFFD;
        if (BattleMenu_BeginInput(0, &sBattleCursor) == 0)
        {
            sBattleControlFlags |= 0x80;
            sBattlePhase = 3;
        }
        else
            sBattlePhase = 5;
        break;
    case 3:
        if (BattleMenu_UpdateCommandReset() == 1)
            sBattlePhase = 4;
        break;
    case 4:
        Battle_UpdateWindows();
        if ((u8)Battle_GetWindowState(0) != 0 || (u8)Battle_GetWindowState(2) != 0)
        {
            if ((u8)Battle_GetWindowState(0) != 6 && (u8)Battle_GetWindowState(0) != 7)
            {
                if (BattleMenu_UpdateInput(sBattleCombatants, &sBattleCursor, &oamCursor))
                    Battle_CloseWindow(0, 4, 2);
            }
        }
        else
        {
            u16 flags;

            Battle_HideWindowLayer();
            flags = sBattleControlFlags;
            if (!(flags & 0x2000))
                sBattlePhase = 5;
            else
            {
                BattleAction_ResetState(0);
                sBattlePhase = 20;
            }
            Battle_ResetWindows();
            {
                u16 *selections = sBattleSelections;
                s32 noSelection = -1;

                selections[0] = noSelection;
                selections[1] = noSelection;
                selections[2] = noSelection;
                selections[3] = noSelection;
                selections[4] = noSelection;
                selections[5] = noSelection;
            }
        }
        break;
    case 5:
        switch (Battle_UpdateTurnActions(sBattleCombatants))
        {
        case 0:
            break;
        case 1:
            sBattlePhase = 13;
            sBattleControlFlags |= 2;
            break;
        case 2:
            sBattlePhase = 6;
            sBattleControlFlags |= 4;
            break;
        case 3:
            Sound_FadeOutBgm(20);
            sBattlePhase = 8;
            sBattleControlFlags |= 4;
            break;
        case 4:
            sBattlePhase = 19;
            sBattlePhaseTimer = 0;
            break;
        }
        {
            u16 flags = sBattleControlFlags;
            u32 autoFlag = BATTLE_CONTROL_AUTO;

            if (flags & autoFlag)
            {
                u16 input = sBattleInput;

                if ((input & 0xF000) == 0x2000)
                {
                    flags &= ~autoFlag;
                    sBattleControlFlags = flags;
                }
            }
        }
        break;
    case 6:
        sBattleControlFlags |= 0x40;
        BattleResult_InitBanner();
        sBattlePhase = 7;
        break;
    case 7:
        if (BattleResult_UpdateBanner(&oamCursor))
        {
            BattleResult_BeginVictory();
            sBattlePhase = 17;
        }
        break;
    case 8:
        BattleResult_InitDefeat();
        if (sBattleEncounterId == 60)
        {
            sBattlePhase = 19;
            sBattlePhaseTimer = 0;
        }
        else
            sBattlePhase = 9;
        break;
    case 9:
        if (sBattlePhaseTimer < 50)
            sBattlePhaseTimer++;
        else
            sBattlePhase = 10;
        break;
    case 10:
        switch (BattleResult_UpdateDefeat(sBattleCombatants, &sBattleCursor))
        {
        case 0:
            break;
        case 1:
            sBattleControlFlags |= 1;
            (*(vu16 *)REG_DISPCNT) |= DISP_LCDC_OFF;
            BattleItem_RestoreInventory();
            sBattlePhase = 0;
            break;
        case 2:
            System_RequestTitle();
            sBattlePhase = 0;
            gGameLoopMode = 0;
            return;
        }
        break;
    case 13:
        BattleResult_CollectTurnEndEffects();
        sBattlePhase = 14;
        break;
    case 14:
        if (BattleResult_UpdateTurnEndEffects(sBattleCombatants) == 1)
            sBattlePhase = 15;
        break;
    case 15:
    {
        u16 flags = sBattleControlFlags & 2;
        u8 combatantId;

        if (flags)
        {
            combatantId = Battle_SelectRevivalCandidate(0);
            if ((s8)combatantId >= 0)
            {
                Battle_SetCursorTarget(&sBattleCursor, combatantId, 0);
                sBattlePhase = 16;
            }
            else
                sBattlePhase = 2;
        }
        else if (sBattleControlFlags & 4)
        {
            combatantId = Battle_SelectRevivalCandidate(1);
            if ((s8)combatantId >= 0)
            {
                Battle_SetCursorTarget(&sBattleCursor, combatantId, 0x200);
                sBattlePhase = 16;
            }
            else
            {
                sBattlePhase = 19;
                sBattlePhaseTimer = 0;
            }
        }
        break;
    }
    case 16:
    {
        u16 state = sBattleCursor.displayState & 0xF0;

        if (state == 0x40 || state == 0x50)
        {
            u16 flags = sBattleControlFlags & 2;

            if (flags)
                sBattlePhase = 2;
            else if (sBattleControlFlags & 4)
            {
                sBattlePhaseTimer = 0;
                sBattlePhase = 19;
            }
        }
        break;
    }
    case 17:
        BattleResult_InitVictory();
        sBattlePhase = 18;
        break;
    case 18:
        if (BattleResult_UpdateVictory())
        {
            sBattlePhaseTimer = 0;
            sBattlePhase = 19;
        }
        break;
    case 19:
        if (sBattlePhaseTimer < 50)
            sBattlePhaseTimer++;
        else
        {
            Sound_FadeOutBgm(20);
            sBattlePhase = 21;
            Battle_StartBrightnessTransition(10, 0x110);
            Battle_SavePartyCombatantState();
        }
        break;
    case 20:
        if (BattleAction_UpdatePartyEscape() == 1)
        {
            u16 flags = sBattleControlFlags;

            flags &= 0x1000;
            if (flags)
                sBattlePhase = 5;
            else
            {
                sBattlePhaseTimer = 0;
                Battle_SavePartyCombatantState();
                sBattlePhase = 19;
            }
            {
                u16 *controlFlags = &sBattleControlFlags;
                s32 mask = ~0x2000;

                *controlFlags = mask & *controlFlags;
            }
        }
        break;
    case 21:
        if (!(Sound_GetFlags() & 4))
        {
            (*(vu16 *)REG_DISPCNT) |= DISP_LCDC_OFF;
            sBattlePhase = 0;
            gSceneState = 6;
            gGameLoopMode = 0;
            gVBlankUpdateMode = 0;
            Sound_StopBgm();
            Sound_DisableVSync();
        }
        break;
    }

    for (i = 0; i < BATTLE_COMBATANT_COUNT; i++)
    {
        if (sBattleCombatants[i].id != BATTLE_NO_COMBATANT)
        {
            if (i < BATTLE_PARTY_COUNT && !(sBattleControlFlags & 0x10))
                Battle_DrawPartyStatus(&sBattleCombatants[i], i);
            Battle_InitDrawNode(&sBattleCombatants[i].drawNode, sBattleCombatants[i].animation.y);
            Battle_InsertDrawNode(&sBattleDrawList, &sBattleCombatants[i].drawNode);
        }
    }
    if (!(sBattleCursor.displayState & 0x400))
    {
        Battle_UpdateCursor(&sBattleCursor, sBattleCombatants);
        Battle_InitDrawNode(&sBattleCursor.drawNode, 0);
        Battle_InsertDrawNode(&sBattleDrawList, &sBattleCursor.drawNode);
    }
    oamCursor = Battle_DrawDamageNumbers(oamCursor);
    Battle_BuildDrawListSprites(sBattleDrawList.next, oamCursor);
}

/**
 * @brief 设置遭遇编号并重置战斗会话状态
 *
 * @note 保存 s8 遭遇编号到 0300032E，重置阶段、输入、效果及显示链；按原掩码保留部分战斗标志。
 * @address 0x08017FA4
 */
#if 0 /* Nonmatching C: the final control-flag update uses r0 instead of r1. */
void Battle_PrepareEncounter(s8 encounterId)
{
    if (!(sBattleControlFlags & 1))
    {
        if (sBattleControlFlags & BATTLE_CONTROL_SPECIAL_ENCOUNTER)
            sBattleControlFlags &= 0xFFDF;
        if (sBattleControlFlags & 0x200)
            sBattleControlFlags &= 0xFDFF;
    }

    if (encounterId > 57)
        sBattleControlFlags |= BATTLE_CONTROL_SPECIAL_ENCOUNTER;
    if (encounterId > 27)
        sBattleControlFlags |= 0x200;

    sBattleEncounterId = encounterId;
    Battle_ResetRasterFlags();
    Battle_InitDrawList(&sBattleDrawList);
    sBattlePhase = 0;
    sBattleInput = 0;
    BattleResult_BeginVictory();
    BattleSpriteWipe_Reset();
    BattlePalette_Init();
    sBattleControlFlags = (sBattleControlFlags & 0x261) | BATTLE_CONTROL_RASTER_DISABLED;
    Script_ClearGlyphCache();
    Battle_ResetBgRequests();
    if (sBattleControlFlags & 0x40)
        sBattleControlFlags &= 0xFFBF;
}
#endif
INCLUDE_ASM("asm/nonmatchings/battle", Battle_PrepareEncounter);

/**
 * @brief 在 VBlank 更新战斗效果并提交背景、精灵和文字资源
 *
 * @note 由 System_VBlank 的战斗分支调用；最终把软件 OAM 复制到硬件 OAM，并处理待上传字形。
 * @address 0x08018070
 */
void Battle_UpdateVBlank(void)
{
    struct BattleDrawNode *node;
    u8 oamCursor;

    Battle_UploadBgScroll();
    Battle_UpdateTransitionVBlank();
    BattlePalette_UpdateEffects();
    if (!(sBattleControlFlags & 0x10))
    {
        DmaCopy(3, gBattleStatusTilemap, BG_SCREEN_ADDR(13), sizeof(gBattleStatusTilemap), 32);
        DmaWait(3);
    }
    Battle_UploadStatusIconGraphics(&sBattleControlFlags);
    Battle_UploadWindowTilemap();
    sBattleOamCursor = 127;
    oamCursor = BattleMenu_UploadCommandIcons(127);
    oamCursor = BattleResult_UploadGraphics(oamCursor);
    Battle_ClearUploadWorkspaces();

    for (node = sBattleDrawList.next;
         node->sortKey < 0xFF && !(sBattleControlFlags & BATTLE_CONTROL_RASTER_DISABLED);
         node = node->next)
    {
        struct BattleCombatant *combatant = (struct BattleCombatant *)node;

        if ((combatant->animation.flags & 0xF) == 6
            || (combatant->effectAnimation.flags & 0xF) == 6
            || (combatant->animation.flags & 0xF) == 7
            || (combatant->effectAnimation.flags & 0xF) == 7)
        {
            DmaCopy(3, gBattleAnimationTilemap, BG_SCREEN_ADDR(15), sizeof(gBattleAnimationTilemap), 32);
            DmaWait(3);
        }
        oamCursor = Battle_UploadAnimationFrame(&combatant->animation, combatant->animation.oamStart);
        if ((combatant->displayState & 0x2000) && !(combatant->effectAnimation.flags & 0x800))
            oamCursor = Battle_UploadAnimationFrame(&combatant->effectAnimation, combatant->effectAnimation.oamStart);
    }

    if (!(sBattleControlFlags & 0x10))
        oamCursor = Battle_DrawPartyGauges(sBattleCombatants, oamCursor);
    Battle_HideUnusedOam(oamCursor);
    Battle_InterleaveAffineMatrices();
    sBattleOamCursor = oamCursor;
    DmaCopy(3, gSpriteOamBuffer, ((void *)OAM), sizeof(gSpriteOamBuffer), 32);
    DmaWait(3);

    if (sBattleControlFlags & 0x400)
    {
        if (Script_UploadGlyphCache() < 0)
            sBattleControlFlags &= 0xFBFF;
    }
    if (sBattleControlFlags & 0x800)
    {
        Script_RestoreDefaultGlyphs(0);
        sBattleControlFlags &= 0xF7FF;
    }
    DmaCopy(3, D_0861A7E4, &OBJ_GRAPHICS[0x12E], sizeof(D_0861A7E4), 32);
    DmaWait(3);
    Battle_ApplyBgRequests();
}

/**
 * @brief 把原始按键转换为战斗按住、触发和方向连发状态
 *
 * @note 03000314 的位 0..7 依次为上、下、左、右、A、B、L、R；高字节保存本次触发。
 * @note 四个方向共用连发计时器，首次按下或计时到 15 时触发并清零；处理顺序不可交换。
 * @note A/B/L/R 首次触发设置十帧输入间隔；此函数不检查该间隔，也不使用第二参数。
 * @address 0x080182A8
 */
void Battle_UpdateInputRepeat(u16 keys, u16 *unusedInputScratch)
{
    if (keys & 0xF0)
        sBattleDirectionRepeatTimer = (sBattleDirectionRepeatTimer + 1) % 16;
    else
        sBattleDirectionRepeatTimer = 0;

    sBattleInput &= 0xFF;
    if (keys & 0x40) /* Up */
    {
        if (!(sBattleInput & 1) || sBattleDirectionRepeatTimer > 14)
        {
            sBattleInput |= 0x101;
            sBattleDirectionRepeatTimer = 0;
        }
    }
    else
        sBattleInput &= 0xFFFE;

    if (keys & 0x80) /* Down */
    {
        if (!(sBattleInput & 2) || sBattleDirectionRepeatTimer > 14)
        {
            sBattleInput |= 0x202;
            sBattleDirectionRepeatTimer = 0;
        }
    }
    else
        sBattleInput &= 0xFFFD;

    if (keys & 0x20) /* Left */
    {
        if (!(sBattleInput & 4) || sBattleDirectionRepeatTimer > 14)
        {
            sBattleInput |= 0x404;
            sBattleDirectionRepeatTimer = 0;
        }
    }
    else
        sBattleInput &= 0xFFFB;

    if (keys & 0x10) /* Right */
    {
        if (!(sBattleInput & 8) || sBattleDirectionRepeatTimer > 14)
        {
            sBattleInput |= 0x808;
            sBattleDirectionRepeatTimer = 0;
        }
    }
    else
        sBattleInput &= 0xFFF7;

    if (keys & 1) /* A */
    {
        if (!(sBattleInput & 0x10))
        {
            sBattleInput |= 0x1010;
            sBattleInputDelay = 10;
        }
    }
    else
        sBattleInput &= 0xFFEF;

    if (keys & 2) /* B */
    {
        if (!(sBattleInput & 0x20))
        {
            sBattleInput |= 0x2020;
            sBattleInputDelay = 10;
        }
    }
    else
        sBattleInput &= 0xFFDF;

    if (keys & 0x200) /* L */
    {
        if (!(sBattleInput & 0x40))
        {
            sBattleInputDelay = 10;
            sBattleInput |= 0x4040;
        }
    }
    else
        sBattleInput &= 0xFFBF;

    if (keys & 0x100) /* R */
    {
        if (!(sBattleInput & 0x80))
        {
            sBattleInputDelay = 10;
            sBattleInput |= 0x8080;
        }
    }
    else
        sBattleInput &= 0xFF7F;
}

/**
 * @brief 按排序链加载至多一个动画分块，再生成各节点的主、副动画 OAM
 *
 * @note 两遍直接推进节点参数；加载遍到达 FF 哨兵后，绘制遍从哨兵 next 开始，保留参数保存和 OAM 游标截断顺序。
 * @note 共用一个八位仿射游标，分别记录主、副动画的 OAM 起点和数量；返回值恒为 0。
 * @note 编号 12..112 的状态 4 节点临时设置主动画的 0x8000 调色板覆盖位，生成该帧后只清除本次设置的覆盖位。
 * @note 实际模块 C 输出按原址独立链接后，0x1F4 字节及已解析重定位完全匹配；整 ROM 与修改前逐字节一致，保留已有失配。
 * @address 0x080184A8
 */
s32 Battle_BuildDrawListSprites(struct BattleDrawNode *node, u8 oamCursor)
{
    u8 loaded = 0;
    s32 result = 0;
    u8 affineCursor = 0;
    struct BattleCombatant *combatant;
    u8 paletteOverride;

    Battle_UpdateAutoIndicator();
    Battle_LoadBackgroundBlock();
    while (node->sortKey < 0xFF)
    {
        combatant = (struct BattleCombatant *)node;
        if ((combatant->animation.flags & 0x800) && !loaded)
        {
            Battle_LoadAnimationBlock(&combatant->animation);
            loaded = 1;
        }
        if ((combatant->displayState & 0x2000)
            && (combatant->effectAnimation.flags & 0x800)
            && !(combatant->animation.flags & 0x800)
            && !loaded)
        {
            Battle_LoadAnimationBlock(&combatant->effectAnimation);
            loaded = 1;
        }
        node = node->next;
    }

    for (node = node->next; node->sortKey < 0xFF; node = node->next)
    {
        paletteOverride = 0;
        combatant = (struct BattleCombatant *)node;
        if ((combatant->animation.flags & 0xF) != 3)
        {
            if (!(combatant->displayState & 0x40))
            {
                combatant->animation.x = combatant->homeX;
                combatant->animation.y = combatant->homeY - combatant->verticalLift;
            }
            oamCursor = Battle_DrawStatusIcon(combatant, oamCursor, sBattleControlFlags);
            if (!(combatant->displayState & 4))
            {
                oamCursor = Battle_DrawCombatantGroundSprite(combatant, oamCursor);
                combatant->animation.oamStart = oamCursor;
            }
            else
                combatant->animation.oamStart = oamCursor;
        }
        else
            combatant->animation.oamStart = oamCursor;

        if ((combatant->animation.flags & 0xF) == 6
            || (combatant->effectAnimation.flags & 0xF) == 6)
            Battle_ClearAnimationTilemap();

        if ((u8)(combatant->id - 12) <= 100
            && !(combatant->animation.flags & 0x8000)
            && combatant->status == 4)
        {
            combatant->animation.flags |= 0x8000;
            combatant->animation.initialPaletteSlot = Battle_GetSharedEffectPaletteSlot();
            paletteOverride = 1;
        }
        oamCursor = Battle_BuildAnimationFrame(&combatant->animation, oamCursor, &affineCursor);
        combatant->animation.oamCount = combatant->animation.oamStart - oamCursor;
        if (paletteOverride == 1)
            combatant->animation.flags &= 0x7FFF;
        if ((combatant->displayState & 0x2000) && !(combatant->effectAnimation.flags & 0x800))
        {
            combatant->effectAnimation.oamStart = oamCursor;
            oamCursor = Battle_BuildAnimationFrame(&combatant->effectAnimation, oamCursor, &affineCursor);
            combatant->effectAnimation.oamCount = combatant->effectAnimation.oamStart - oamCursor;
        }
    }
    return result;
}

/**
 * @brief 按遭遇类别选择战斗 BGM 并启动淡入
 *
 * @note 特殊遭遇标志 0x20 优先：编号 70/71 播放 3，72 播放 4，其余播放 2；否则标志 0x200 选择 2，未设置则选择 0。
 * @note 所有路径先以音量 0 调用 Sound_PlayBgm，再以 20 次 Sound_Update 为时长淡入；保留 58–74 的显式 case 以复现 17 项跳转表。
 * @address 0x0801869C
 */
void Battle_PlayEncounterBgm(void)
{
    if (sBattleControlFlags & BATTLE_CONTROL_SPECIAL_ENCOUNTER)
    {
        switch (sBattleEncounterId)
        {
        case 58:
        case 59:
        case 60:
        case 61:
        case 62:
        case 63:
        case 64:
        case 65:
        case 66:
        case 67:
        case 68:
        case 69:
            Sound_PlayBgm(2, 0);
            Sound_FadeInBgm(20);
            break;
        case 70:
        case 71:
            Sound_PlayBgm(3, 0);
            Sound_FadeInBgm(20);
            break;
        case 72:
            Sound_PlayBgm(4, 0);
            Sound_FadeInBgm(20);
            break;
        case 73:
        case 74:
            Sound_PlayBgm(2, 0);
            Sound_FadeInBgm(20);
            break;
        default:
            Sound_PlayBgm(2, 0);
            Sound_FadeInBgm(20);
            break;
        }
    }
    else if (sBattleControlFlags & 0x200)
    {
        Sound_PlayBgm(2, 0);
        Sound_FadeInBgm(20);
    }
    else
    {
        Sound_PlayBgm(0, 0);
        Sound_FadeInBgm(20);
    }
}

/**
 * @brief 将战斗输入间隔计时器重置为十帧
 *
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x08018744
 */
void Battle_ResetInputDelay(void)
{
    sBattleInputDelay = 10;
}

/**
 * @brief 定位并保存战斗提示文本区的起始地址
 *
 * @note 从 gMenuHintText 跳过 297 个以 FF 结束的条目，保存后继地址到 03000340。
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x08018750
 */
void Battle_InitHintText(void)
{
    u16 offset = 0;
    u16 entry = 0;

    while (entry <= 296)
    {
        if (gMenuHintText[offset] == 0xFF)
            entry++;
        offset++;
    }
    sBattleHintText = gMenuHintText + offset;
}

/**
 * @brief 取得当前战斗提示文本区地址
 *
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x0801878C
 */
const u8 * Battle_GetHintText(void)
{
    return sBattleHintText;
}

/**
 * @brief 按槽写入战斗选择数组中的半字值
 *
 * @note 索引按 u8 使用，数组基址为 03000330；初始化路径清除六个半字槽。
 * @address 0x08018798
 */
void Battle_SetSelection(u8 slot, u32 selection)
{
    sBattleSelections[slot] = selection;
}

/**
 * @brief 取得当前遭遇编号
 *
 * @address 0x080187A8
 */
u8 Battle_GetEncounterId(void)
{
    return sBattleEncounterId;
}

/**
 * @brief 读取战斗控制标志
 *
 * @address 0x080187B4
 */
u16 Battle_GetControlFlags(void)
{
    return sBattleControlFlags;
}

/**
 * @brief 设置指定的战斗控制标志位
 *
 * @address 0x080187C0
 */
void Battle_SetControlFlags(u16 flags)
{
    sBattleControlFlags |= flags;
}

/**
 * @brief 清除指定的战斗控制标志位
 *
 * @address 0x080187D4
 */
void Battle_ClearControlFlags(u16 flags)
{
    sBattleControlFlags &= ~flags;
}

/**
 * @brief 读取转换后的战斗按键状态
 *
 * @address 0x080187E8
 */
u16 Battle_GetInput(void)
{
    return sBattleInput;
}

/**
 * @brief 读取本帧新按下的原始按键掩码
 *
 * @address 0x080187F4
 */
u16 Battle_GetPressedKeys(void)
{
    return sBattleKeysPressed;
}

/**
 * @brief 初始化带 FF 哨兵的循环双向排序链表
 *
 * @address 0x08018800
 */
void Battle_InitDrawList(struct BattleDrawNode *head)
{
    head->previous = head;
    head->next = head;
    head->sortKey = 0xFF;
}

/**
 * @brief 设置显示节点排序键并清空前后链指针
 *
 * @address 0x0801880C
 */
void Battle_InitDrawNode(struct BattleDrawNode *node, u8 sortKey)
{
    node->previous = NULL;
    node->next = NULL;
    node->sortKey = sortKey;
}

/**
 * @brief 按首字节排序键升序插入双向链表
 *
 * @note 同键节点插入到已有同键节点之前；前驱和后继指针位于偏移 4 和 8。
 * @address 0x08018818
 */
void Battle_InsertDrawNode(struct BattleDrawNode *head, struct BattleDrawNode *node)
{
    struct BattleDrawNode *next = head->next;

    while (next->sortKey < node->sortKey)
        next = next->next;
    node->next = next;
    node->previous = next->previous;
    next->previous->next = node;
    next->previous = node;
}

/**
 * @brief 设置战斗随机数发生器的三十二位状态
 *
 * @address 0x08018838
 */
void Battle_SeedRandom(u32 seed)
{
    sBattleRandomState = seed;
}

/**
 * @brief 推进战斗线性同余随机数并返回十五位结果
 *
 * @note state = state * 0x41C64E6D + 0x3039，返回 (state >> 16) & 0x7FFF。
 * @address 0x08018844
 */
u32 Battle_NextRandom(void)
{
    sBattleRandomState = sBattleRandomState * 0x41C64E6D + 0x3039;
    return (sBattleRandomState >> 16) & 0x7FFF;
}

/**
 * @brief 取得战斗角色工作区首地址
 *
 * @address 0x08018864
 */
struct BattleCombatant * Battle_GetCombatants(void)
{
    return gBattleCombatants;
}

/**
 * @brief 取得独立战斗 UI 节点的地址
 *
 * @note 返回 03000248；该节点参与指令菜单和战斗显示排序。
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x0801886C
 */
struct BattleCombatant * Battle_GetCursor(void)
{
    return &sBattleCursor;
}

/**
 * @brief 取得战斗敌方槽位区的首地址
 *
 * @note 返回 02037410，即角色工作区 02037028 加 5 * 0xC8，指向敌方第一个槽。
 * @address 0x08018874
 */
struct BattleCombatant * Battle_GetEnemies(void)
{
    return BATTLE_ENEMY_COMBATANTS;
}

/**
 * @brief 在 HBlank 派发战斗扫描线效果
 *
 * @note 战斗标志 0008 清除后才执行扫描线混合和精灵擦除服务。
 * @address 0x0801887C
 */
void Battle_UpdateHBlank(void)
{
    if (!(sBattleControlFlags & BATTLE_CONTROL_RASTER_DISABLED))
    {
        Battle_ApplyRasterLine();
        BattleSpriteWipe_UpdateHBlank();
    }
}

/**
 * @brief 推进 VBlank 阶段的战斗转场和精灵擦除效果
 *
 * @address 0x0801889C
 */
void Battle_UpdateTransitionVBlank(void)
{
    Battle_UpdateRasterEffect();
    if (!(sBattleControlFlags & BATTLE_CONTROL_RASTER_DISABLED))
        BattleSpriteWipe_Update();
}

/**
 * @brief 按输入间隔采样按键并更新战斗输入缓存
 *
 * @note 读取 KEYINPUT 后取反，计算新按下的位，再调用 Battle_UpdateInputRepeat 生成战斗专用状态。
 * @address 0x080188BC
 */
void Battle_ReadInput(void)
{
    if ((s8)sBattleInputDelay > 0)
        sBattleInputDelay--;

    if ((s8)sBattleInputDelay <= 0)
    {
        u16 keys = ~(*(vu16 *)REG_KEYINPUT);

        sBattleKeysPressed = keys & ~sBattleKeysHeld;
        sBattleKeysHeld = keys;
    }
    else
    {
        sBattleKeysPressed = 0;
        sBattleKeysHeld = 0;
    }
    Battle_UpdateInputRepeat(sBattleKeysHeld, sBattleSelections);
}

/**
 * @brief 依次应用 BG0..BG3 的开启、关闭请求，并逐位消费已处理请求
 *
 * @note 03000510 的位 0..3 请求开启 BG0..BG3，位 4..7 请求关闭；先开启再关闭，同层冲突时最终关闭。
 * @note 每项请求单独读改写 DISPCNT，随后仅清除对应请求位；保留其他显示设置及请求字的高八位。
 * @address 0x08018928
 */
void Battle_ApplyBgRequests(void)
{
    if (sBattleBgRequests & 1)
    {
        (*(vu16 *)REG_DISPCNT) |= 0x100;
        sBattleBgRequests &= 0xFFFE;
    }
    if (sBattleBgRequests & 2)
    {
        (*(vu16 *)REG_DISPCNT) |= 0x200;
        sBattleBgRequests &= 0xFFFD;
    }
    if (sBattleBgRequests & 4)
    {
        (*(vu16 *)REG_DISPCNT) |= 0x400;
        sBattleBgRequests &= 0xFFFB;
    }
    if (sBattleBgRequests & 8)
    {
        (*(vu16 *)REG_DISPCNT) |= 0x800;
        sBattleBgRequests &= 0xFFF7;
    }
    if (sBattleBgRequests & 0x10)
    {
        (*(vu16 *)REG_DISPCNT) &= 0xFEFF;
        sBattleBgRequests &= 0xFFEF;
    }
    if (sBattleBgRequests & 0x20)
    {
        (*(vu16 *)REG_DISPCNT) &= 0xFDFF;
        sBattleBgRequests &= 0xFFDF;
    }
    if (sBattleBgRequests & 0x40)
    {
        (*(vu16 *)REG_DISPCNT) &= 0xFBFF;
        sBattleBgRequests &= 0xFFBF;
    }
    if (sBattleBgRequests & 0x80)
    {
        (*(vu16 *)REG_DISPCNT) &= 0xF7FF;
        sBattleBgRequests &= 0xFF7F;
    }
}

/**
 * @brief 按遭遇背景编号解压图形和图块表，配置显示并初始化波动效果工作区
 *
 * @note 调度器传入的首参数未使用；背景记录依次保存图形、调色板和图块表指针，调色板写入 0..2 和 11..13 号 bank。
 * @note 逐字段构造 DISPCNT/BG2CNT，保留参考中未赋值的只读 CGB 模式位；DMA 保留控制读回和完成等待。
 * @note 清零 360 字节波形缓冲，绑定四层 HOFS/VOFS 地址，各层按 Y、X 顺序清滚动缓存，最后将分块加载进度设为 4。
 * @address 0x08018A58
 */
void Battle_LoadBackground(u32 unused)
{
    u32 backgroundId = Battle_GetBackgroundId();
    union BattleDisplayControl display;
    BgCnt bg2;

    LZ77UnCompVram((void *)(D_087ED394[backgroundId].tiles), ((void *)VRAM));
    BattlePalette_LoadBgBanks(D_087ED394[backgroundId].palette, 0, 3);
    LZ77UnCompVram((void *)(D_087ED394[backgroundId].tilemap), BG_SCREEN_ADDR(12));
    DmaCopy(3, D_0861A4A4, (void *)(VRAM + 0x5000), sizeof(D_0861A4A4), 32);
    DmaWait(3);
    Battle_LoadStatusGraphics();

    display.bits.mode = 0;
    display.bits.frameSelect = 0;
    display.bits.hblankOamAccess = 1;
    display.bits.objMapping1D = 1;
    display.bits.forcedBlank = 0;
    display.bits.bg0 = 0;
    display.bits.bg1 = 0;
    display.bits.bg2 = 1;
    display.bits.bg3 = 1;
    display.bits.obj = 1;
    display.bits.win0 = 1;
    display.bits.win1 = 0;
    display.bits.objWindow = 0;

    bg2.Priority = 3;
    bg2.CharBasep = 0;
    bg2.Dummy_5_4 = 0;
    bg2.Mosaic = 0;
    bg2.ColorMode = 0;
    bg2.ScBasep = 12;
    bg2.Loop = 1;
    bg2.Size = 0;

    (*(vu16 *)REG_DISPCNT) = display.value;
    *(vu16 *)REG_BG2CNT = *(u16 *)&bg2;
    {
        u16 fillValue;
        u16 *fillSource = &fillValue;
    
        *fillSource = (u16)(0);
        DmaSet(3, fillSource, gBattleWaveSamples,
            DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS
                | ((u32)(sizeof(gBattleWaveSamples)) / 2));
    }
    DmaWait(3);
    sBattleWaveSamples = gBattleWaveSamples;
    BattlePalette_LoadBgBanks(gSceneSharedPalettes, 11, 3);
    sBattleWaveLayerMask = 0;
    sBattleWaveXRegisters[0] = (vu16 *)REG_BG0HOFS;
    sBattleWaveXRegisters[1] = (vu16 *)REG_BG1HOFS;
    sBattleWaveXRegisters[2] = (vu16 *)REG_BG2HOFS;
    sBattleWaveXRegisters[3] = (vu16 *)REG_BG3HOFS;
    sBattleWaveYRegisters[0] = (vu16 *)REG_BG0VOFS;
    sBattleWaveYRegisters[1] = (vu16 *)REG_BG1VOFS;
    sBattleWaveYRegisters[2] = (vu16 *)REG_BG2VOFS;
    sBattleWaveYRegisters[3] = (vu16 *)REG_BG3VOFS;
    sBattleBgScroll.bg0Hofs = sBattleBgScroll.bg0Vofs = 0;
    sBattleBgScroll.bg1Hofs = sBattleBgScroll.bg1Vofs = 0;
    sBattleBgScroll.bg2Hofs = sBattleBgScroll.bg2Vofs = 0;
    sBattleBgScroll.bg3Hofs = sBattleBgScroll.bg3Vofs = 0;
    Battle_FinishBackgroundLoadProgress();
}

/**
 * @brief 上传战斗状态栏及成员名称图块、装载调色板，并初始化 BG3 状态栏
 *
 * @note 先复制 13 字节名称图块索引表，再扫描五个队伍槽；跳过 FF 空槽，将有效成员的 0x80 字节名称图块连续上传。
 * @note 装载 3..8 号调色板 bank，另从该调色板地址前 0x40 字节取一组写入 bank 9；随后初始化图块表并逐字段配置 BG3CNT。
 * @note 各 DMA 保留参考的读回顺序；最后一段 0x80 字节图块复制仅读回控制寄存器，不额外添加完成等待。
 * @note 共享图块使用 D_08619AA4 的 +0xB40/+0xB60 地址视图，配合官方宏保持原寄存器分配。
 * @address 0x08018BF8
 */
void Battle_LoadStatusGraphics(void)
{
    u8 nameTileIds[13];
    u8 nameSlot;
    u8 memberSlot;
    BgCnt bg3;

    memcpy(nameTileIds, D_083939F8, sizeof(nameTileIds));
    DmaCopy(3, D_0861A4A4, (void *)(VRAM + 0x5000), sizeof(D_0861A4A4), 32);
    DmaWait(3);

    nameSlot = 0;
    for (memberSlot = 0; memberSlot < PARTY_MAX_MEMBERS; memberSlot++)
    {
        if (gPartyMembers[memberSlot] != PARTY_NO_MEMBER)
        {
            DmaCopy(3, D_08619FA4 + (nameTileIds[gPartyMembers[memberSlot]] << 7),
                (void *)(VRAM + 0x5140 + (nameSlot << 7)), 0x80, 32);
            DmaWait(3);
            nameSlot++;
        }
    }

    DmaCopy(3, D_08619AA4 + 0xB40, (void *)(VRAM + 0x53E0), 0x20, 32);
    DmaWait(3);
    DmaCopy(3, D_08619AA4 + 0xB60, (void *)(VRAM + 0x5400), 0x40, 32);
    DmaWait(3);
    DmaCopy(3, D_08619AA4 + (gBattleMenuCommandIcons[BATTLE_MENU_ICON_AUTO].sourceTile << 5),
        (void *)(VRAM + 0x5440), 0x80, 32);
    BattlePalette_LoadBgBanks(D_0861C664, 3, 6);
    BattlePalette_LoadBgBanks((const u16 *)((u32)D_0861C664 - 0x40), 9, 1);
    Battle_InitStatusTilemap();

    bg3.Priority = 1;
    bg3.CharBasep = 0;
    bg3.Dummy_5_4 = 0;
    bg3.Mosaic = 0;
    bg3.ColorMode = 0;
    bg3.ScBasep = 13;
    bg3.Loop = 1;
    bg3.Size = 0;
    *(vu16 *)REG_BG3CNT = *(u16 *)&bg3;
}

/**
 * @brief 按自动战斗标志刷新左下角的提示图标
 *
 * @note 战斗标志 4000 决定在状态栏图块表中绘制或擦除一个 2×2 图标。
 * @address 0x08018D9C
 */
 #if 0
void Battle_UpdateAutoIndicator(void)
{
    u16 index = 0x221;
    u16 row = index + 32;
    u16 tile;

    if (Battle_GetControlFlags() & BATTLE_CONTROL_AUTO)
    {
        tile = 0x92A2;
        gBattleStatusTilemap[index] = tile;
        tile = 0x92A3;
        gBattleStatusTilemap[index + 1] = tile;
        tile = 0x92A4;
        gBattleStatusTilemap[row] = tile;
        tile = 0x92A5;
        gBattleStatusTilemap[row + 1] = tile;
    }
    else
    {
        tile = 0x92C0;
        gBattleStatusTilemap[index] = tile;
        gBattleStatusTilemap[index + 1] = tile;
        gBattleStatusTilemap[row] = tile;
        gBattleStatusTilemap[row + 1] = tile;
    }
}
    #else
    INCLUDE_ASM("asm/nonmatchings/battle", Battle_UpdateAutoIndicator);
    #endif

/**
 * @brief 按遭遇类别查找战斗背景资源编号
 *
 * @note 控制位 0x20 优先于 0x200；否则按场景遭遇配置查表，配置 0 和 1 均使用首项。
 * @note 遭遇编号先按 u8 读取，再以 s32 计算偏移，保留先减编号后加表地址的顺序。
 * @note 各分支写入同一返回变量，使 agbcc 共用末尾 ldrb；116 字节及 9 项重定位匹配。
 * @address 0x08018E34
 */
u32 Battle_GetBackgroundId(void)
{
    u32 backgroundId;

    if (Battle_GetControlFlags() & BATTLE_CONTROL_SPECIAL_ENCOUNTER)
    {
        backgroundId = D_083989CB[Battle_GetEncounterId() - 0x3A];
    }
    else if (Battle_GetControlFlags() & 0x200)
    {
        backgroundId = D_083989DC[Battle_GetEncounterId() - 0x1C];
    }
    else if (gSceneEncounterConfigId != 0)
    {
        backgroundId = D_083989B0[gSceneEncounterConfigId - 1];
    }
    else
        backgroundId = D_083989B0[gSceneEncounterConfigId];
    return backgroundId;
}

/**
 * @brief 在战斗状态栏指定位置绘制右对齐的三位十进制数
 *
 * @note 数值上限为 999，前导零使用 0x2C0 空白图块；清除翻转位，调色板取 paletteBase + paletteOffset 的低四位。
 * @note 百位使用 100u 作无符号除法；保留空 case 2、十位局部指针和循环前的表基址，以匹配分支及寄存器分配。
 * @note 图块表步长为 2 字节，BgScData 仅作字段视图，不能用其 sizeof 计算步长。
 * @address 0x08018EA8
 */
void Battle_DrawStatusNumber(u16 value, u8 x, u8 y, u8 paletteOffset, u8 paletteBase)
{
    BgScData *base = (BgScData *)gBattleStatusTilemap;
    u16 digits[3];
    u8 digit;

    if (value > 999)
        value = 999;

    digits[0] = value / 100u;
    digits[1] = (value - digits[0] * 100) / 10;
    digits[2] = value - (digits[0] * 100 + digits[1] * 10);

    for (digit = 0; digit < 3; digit++)
    {
        BgScData *entry;

        switch (digit)
        {
        case 0:
            if (digits[0] == 0)
                digits[0] = 0x40;
            break;
        case 1:
        {
            u16 *tens = &digits[1];

            if (*tens == 0 && digits[0] > 10)
                *tens = 0x40;
            break;
        }
        case 2:
            break;
        }

        entry = (BgScData *)((u8 *)base + (x + digit + y * 32) * 2);
        entry->CharNo = 0x280 + digits[digit];
        entry->HFlip = 0;
        entry->VFlip = 0;
        entry->Pltt = paletteBase + paletteOffset;
    }
}

/**
 * @brief 绘制成员名称的四个图块及上方两行 HP、MP 标签
 *
 * @note nameSlot 对应连续上传的名称图块槽；名称和左侧标记共用调色板，HP、MP 标签分别叠加各自的调色板偏移。
 * @note 左侧标记先计算带 -1 的 s32 行偏移再加 x；名称循环预先保存表基址，以保留原地址计算及寄存器分配。
 * @note 坐标不裁剪，图块表按 u16 步长寻址，逐字段清除翻转位并设置调色板。
 * @address 0x08018FC0
 */
void Battle_DrawStatusLabels(u8 nameSlot, u8 x, u8 y, u8 paletteOffset,
    u8 hpPaletteOffset, u8 mpPaletteOffset, u8 paletteBase)
{
    u16 *base = gBattleStatusTilemap;
    BgScData *entry;
    u8 tile;
    s32 offset;

    offset = (y - 2) * 32 - 1;
    entry = (BgScData *)&gBattleStatusTilemap[offset + x];
    entry->CharNo = 0x2A1;
    entry->HFlip = 0;
    entry->VFlip = 0;
    entry->Pltt = paletteOffset + paletteBase;

    offset = (y - 1) * 32 - 1;
    entry = (BgScData *)&gBattleStatusTilemap[offset + x];
    entry->CharNo = 0x2A1;
    entry->HFlip = 0;
    entry->VFlip = 0;
    entry->Pltt = paletteOffset + paletteBase;

    entry = (BgScData *)&gBattleStatusTilemap[(y - 2) * 32 + x];
    entry->CharNo = 0x29F;
    entry->HFlip = 0;
    entry->VFlip = 0;
    entry->Pltt = hpPaletteOffset + paletteBase;

    entry = (BgScData *)&gBattleStatusTilemap[(y - 1) * 32 + x];
    entry->CharNo = 0x2A0;
    entry->HFlip = 0;
    entry->VFlip = 0;
    entry->Pltt = paletteBase + mpPaletteOffset;

    for (tile = 0; tile < 4; tile++)
    {
        entry = (BgScData *)(base + (y * 32 + (x + tile)));
        entry->CharNo = 0x28A + nameSlot * 4 + tile;
        entry->HFlip = 0;
        entry->VFlip = 0;
        entry->Pltt = paletteOffset + paletteBase;
    }
}

/**
 * @brief 清空战斗窗口图块表并启用 BG0
 *
 * @address 0x08019148
 */
void Battle_InitWindowTilemap(void)
{
    u16 *tilemap = gBattleWindowTilemap;
    u16 *vram = BATTLE_WINDOW_TILEMAP_VRAM;
    u16 i;
    BgCnt bg0cnt;

    for (i = 0; i <= 0x3FF; i++)
    {
        tilemap[i] = 0;
        vram[i] = 0;
    }

    (*(vu16 *)REG_DISPCNT) |= 0x100;

    bg0cnt.Priority = 0;
    bg0cnt.CharBasep = 2;
    bg0cnt.Dummy_5_4 = 0;
    bg0cnt.Mosaic = 0;
    bg0cnt.ColorMode = 0;
    bg0cnt.ScBasep = 14;
    bg0cnt.Loop = 0;
    bg0cnt.Size = 0;

    (*(vu16 *)REG_BG0CNT) = *(u16 *)&bg0cnt;
}

/**
 * @brief 在三十二列图块表中绘制带边框的矩形窗口
 *
 * @address 0x080191CC
 */
void Battle_DrawWindowBorder(u16 *tilemap, u16 baseTile, u8 x, u8 y, u8 width, u8 height)
{
    u8 startX;
    u8 startY;

    width += x;
    height += y;
    startX = x;
    startY = y;
    while (y < height)
    {
        while (x < width)
        {
            if ((y == startY && (x == startX || x == width - 1))
                || (y == height - 1 && (x == width - 1 || x == startX)))
                tilemap[y * 32 + x] = (baseTile << 12) + 0x1FF;
            else if (y == startY)
                tilemap[y * 32 + x] = (baseTile << 12) + 0x1FE;
            else if (y == height - 1)
                tilemap[y * 32 + x] = ((baseTile << 12) | 0x800) + 0x1FE;
            else if (x == startX)
                tilemap[y * 32 + x] = (baseTile << 12) + 0x1FD;
            else if (x == width - 1)
                tilemap[y * 32 + x] = ((baseTile << 12) | 0x400) + 0x1FD;
            else
                tilemap[y * 32 + x] = (baseTile << 12) + 1;
            x++;
        }
        x = startX;
        y++;
    }
}

/**
 * @brief 重置三个战斗窗口动画槽的状态和几何参数
 *
 * @address 0x08019304
 */
void Battle_ResetWindows(void)
{
    u8 slot;

    for (slot = 0; slot < BATTLE_WINDOW_COUNT; slot++)
    {
        struct BattleWindow *window = &sBattleWindows[slot];

        window->targetX = 0;
        window->targetY = 0;
        window->targetWidth = 0;
        window->targetHeight = 0;
        window->x = 0;
        window->y = 0;
        window->width = 0;
        window->height = 0;
        window->duration = 0;
        window->frame = 0;
        window->curve = 0;
        window->state = BATTLE_WINDOW_CLOSED;
        window->baseTile = 0;
    }
}

/**
 * @brief 逐帧展开或收起三个战斗窗口，并重绘边框
 *
 * @note 展开先宽后高，收起顺序相反；目标尺寸为奇数时仅在当前尺寸为奇数且发生增减时重绘。
 * @note 两段收起循环共用 u8 行号和普通 u16 填充值；DmaSet / DmaWait 保留原版每行 targetWidth * 16 个半字的传输计数。
 * @note 展开入口先保存目标高度，收起时在钳制高度前读取目标高度，以保留 agbcc 的公共表达式顺序；原址 0x398 字节完全匹配。
 * @address 0x0801933C
 */
void Battle_UpdateWindows(void)
{
    u16 fillValue;
    u8 slot;
    u8 row;

    for (slot = 0; slot < BATTLE_WINDOW_COUNT; slot++)
    {
        struct BattleWindow *window = &sBattleWindows[slot];
        u8 previousSize;

        switch (window->state)
        {
        case BATTLE_WINDOW_CLOSED:
            break;
        case BATTLE_WINDOW_OPENING:
        {
            u8 targetHeight;

            window->frame = 0;
            targetHeight = window->targetHeight;
            window->y = window->targetY + (targetHeight >> 1);
            window->state = 2;
            break;
        }
        case 2:
            previousSize = window->width;
            window->width = Battle_InterpolateS16(0, window->targetWidth,
                window->duration, window->frame, 0);
            if (((window->targetWidth & 1) == 0 && window->width > previousSize)
                || (window->width > previousSize && (window->targetWidth & 1) != 0
                    && (window->width & 1) != 0))
            {
                if (window->width < 2)
                    window->width = 2;
                window->x = window->targetX + (window->targetWidth >> 1)
                    - (window->width >> 1);
                Battle_DrawWindowBorder(window->tilemap, window->baseTile,
                    window->x, window->y, window->width, 2);
            }
            window->frame++;
            if (window->frame > window->duration)
            {
                window->frame = 0;
                window->unknown0B = 0;
                window->state = 3;
            }
            break;
        case 3:
            previousSize = window->height;
            window->height = Battle_InterpolateS16(0, window->targetHeight,
                window->duration, window->frame, (s8)window->curve);
            if (((window->targetHeight & 1) == 0 && window->height > previousSize)
                || (window->height > previousSize && (window->targetHeight & 1) != 0
                    && (window->height & 1) != 0))
            {
                if (window->height < 2)
                    window->height = 2;
                window->y = window->targetY + (window->targetHeight >> 1)
                    - (window->height >> 1);
                Battle_DrawWindowBorder(window->tilemap, window->baseTile,
                    window->x, window->y, window->width, window->height);
            }
            window->frame++;
            if (window->frame > window->duration)
                window->state = BATTLE_WINDOW_OPEN;
            break;
        case BATTLE_WINDOW_OPEN:
            break;
        case BATTLE_WINDOW_CLOSING:
            window->state = 7;
            break;
        case 7:
            previousSize = window->height;
            window->height = Battle_InterpolateS16(window->targetHeight,
                -window->targetHeight + 2, window->duration, window->frame,
                (s8)window->curve);
            if (((window->targetHeight & 1) == 0 && window->height < previousSize)
                || (window->height < previousSize && (window->targetHeight & 1) != 0
                    && (window->height & 1) != 0))
            {
                u8 targetHeight;

                for (row = window->targetY;
                    row < window->targetY + window->targetHeight; row++)
                {
                    u16 *fillSource = &fillValue;

                    *fillSource = 0;
                    DmaSet(3, &fillValue, &window->tilemap[row * 32 + window->targetX],
                        DMA_ENABLE | DMA_SRC_FIX | (window->targetWidth << 4));
                    DmaWait(3);
                }
                targetHeight = window->targetHeight;
                if (window->height < 2)
                    window->height = 2;
                window->y = window->targetY + (targetHeight >> 1)
                    - (window->height >> 1);
                Battle_DrawWindowBorder(window->tilemap, window->baseTile,
                    window->x, window->y, window->width, window->height);
            }
            window->frame++;
            if (window->frame > window->duration)
            {
                window->frame = 0;
                window->unknown0B = 0;
                window->state = 6;
            }
            break;
        case 6:
            previousSize = window->width;
            window->width = Battle_InterpolateS16(window->targetWidth,
                -window->targetWidth + 2, window->duration, window->frame,
                (s8)window->curve);
            if (((window->targetWidth & 1) == 0 && window->width < previousSize)
                || (window->width < previousSize && (window->targetWidth & 1) != 0
                    && (window->width & 1) != 0))
            {
                for (row = window->targetY;
                    row < window->targetY + window->targetHeight; row++)
                {
                    u16 *fillSource = &fillValue;

                    *fillSource = 0;
                    DmaSet(3, &fillValue, &window->tilemap[row * 32 + window->targetX],
                        DMA_ENABLE | DMA_SRC_FIX | (window->targetWidth << 4));
                    DmaWait(3);
                }
                if (window->width < 2)
                    window->width = 2;
                window->x = window->targetX + (window->targetWidth >> 1)
                    - (window->width >> 1);
                Battle_DrawWindowBorder(window->tilemap, window->baseTile,
                    window->x, window->y, window->width, window->height);
            }
            window->frame++;
            if (window->frame > window->duration)
            {
                Battle_SetTilemapTiles(window->tilemap, 0, window->targetX,
                    window->targetY, window->targetWidth, window->targetHeight);
                window->state = BATTLE_WINDOW_CLOSED;
            }
            break;
        }
    }
}

/**
 * @brief 配置指定窗口槽并启动展开动画
 *
 * @address 0x080196D4
 */
void Battle_OpenWindow(u8 slot, u16 *tilemap, u16 baseTile, u8 duration, u8 curve,
                      u8 x, u8 y, u8 width, u8 height)
{
    sBattleWindows[slot].targetX = x;
    sBattleWindows[slot].targetY = y;
    sBattleWindows[slot].targetWidth = width;
    sBattleWindows[slot].targetHeight = height;
    sBattleWindows[slot].x = 0;
    sBattleWindows[slot].y = 0;
    sBattleWindows[slot].width = 0;
    sBattleWindows[slot].height = 0;
    sBattleWindows[slot].duration = duration;
    sBattleWindows[slot].frame = 0;
    sBattleWindows[slot].curve = curve;
    sBattleWindows[slot].unknown0B = 0;
    sBattleWindows[slot].state = BATTLE_WINDOW_OPENING;
    sBattleWindows[slot].baseTile = baseTile;
    sBattleWindows[slot].tilemap = tilemap;
}

/**
 * @brief 同时设置窗口槽的目标矩形和当前矩形
 *
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x08019748
 */
void Battle_SetWindowRect(u8 slot, u8 x, u8 y, u8 width, u8 height)
{
    sBattleWindows[slot].targetX = x;
    sBattleWindows[slot].targetY = y;
    sBattleWindows[slot].targetWidth = width;
    sBattleWindows[slot].targetHeight = height;
    sBattleWindows[slot].x = x;
    sBattleWindows[slot].y = y;
    sBattleWindows[slot].width = width;
    sBattleWindows[slot].height = height;
}

/**
 * @brief 推进战斗扫描线效果并生成偏移或亮度表
 *
 * @address 0x08019784
 */
void Battle_UpdateRasterEffect(void)
{
    extern const float D_0839C26C[360];
    extern const float D_0839BCCC[360];
    u16 active = sBattleRasterFlags & 0x1000;
    s16 line;
    s16 samplePhase;
    s16 max;

    if (active)
    {
        switch (sBattleRasterFlags & 0xF)
        {
        case 0:
            break;
        case 1:
            sBattleRasterPhase = (sBattleRasterPhase + sBattleRasterSpeed) % 360;
            for (line = 0, samplePhase = sBattleRasterPhase; line < 160;
                line++, samplePhase = sBattleWaveLineStep + samplePhase)
            {
                sBattleRasterLines[line] =
                    sBattleWaveSamples[(samplePhase % 360)];
            }
            break;
        case 2:
            if (sBattleRasterPhase <= 271)
            {
                max = 24;
                sBattleRasterPhase += sBattleRasterSpeed;
                for (line = 80; line < 160; line++)
                {
                    samplePhase = (s16)(sBattleRasterPhase - line) >> 2;
                    if (samplePhase > 24)
                        samplePhase = 24;
                    if (samplePhase < 0)
                        samplePhase = 0;
                    switch (sBattleRasterFlags & 0xF0)
                    {
                    case 0x10:
                        sBattleRasterLines[line] = samplePhase;
                        break;
                    case 0x20:
                        sBattleRasterLines[line] = max - samplePhase;
                        break;
                    }
                }
                for (line = 0; line < 80; line++)
                    sBattleRasterLines[line] = sBattleRasterLines[160 - line];
            }
            else
                sBattleRasterFlags |= 0x4000;
            break;
        }
    }
    else if (sBattleRasterFlags & 0x2000)
    {
        switch (sBattleRasterFlags & 0xF)
        {
        case 0:
            break;
        case 1:
            for (line = sBattleRasterPhase; (s16)line < (s16)sBattleRasterPhase + 18; line++)
            {
                int l = (s16)line;
                s8 *sample = &sBattleWaveSamples[l];
                float amplitude = (s32)sBattleWaveAmplitude;

                *sample = amplitude * D_0839C26C[l]
                    - amplitude * D_0839BCCC[l] + amplitude;
            }
            if (line  <= 359)
                sBattleRasterPhase = line;
            else
            {
                sBattleRasterFlags &= 0xDFFF;
                sBattleRasterFlags |= 0x1000;
                sBattleRasterPhase = 0;
            }
            break;
        case 2:
            sBattleRasterFlags &= 0xDFFF;
            sBattleRasterFlags |= 0x1000;
            sBattleRasterPhase = 0;
            break;
        }
    }
}

/**
 * @brief 按当前扫描线表更新战斗波形偏移或亮度
 *
 * @note 模式 1 按层掩码写水平偏移和逻辑右移一位的垂直偏移；模式 2 写 BLDY，0x100/0x200 分别选择 BLDCNT 0xBF/0xFF。
 * @note 保留模式 0 空分支和波形的两次 VCOUNT 半字读取；亮度半字读取后掩码由 agbcc 合并为 ldrb 并复用 0xFF。原址 0xF0 字节完全匹配。
 * @address 0x080199E0
 */
void Battle_ApplyRasterLine(void)
{
    u16 flags = sBattleRasterFlags;
    u8 layer;

    if (flags & 0x1000)
    {
        switch (flags & 0xF)
        {
        case 0:
            break;
        case 1:
            for (layer = 0; layer < 4; layer++)
            {
                if ((sBattleWaveLayerMask >> layer) & 1)
                {
                    *sBattleWaveXRegisters[layer] =
                        sBattleRasterLines[*(vu16 *)REG_VCOUNT & 0xFF];
                    *sBattleWaveYRegisters[layer] =
                        (u16)sBattleRasterLines[*(vu16 *)REG_VCOUNT & 0xFF] >> 1;
                }
            }
            break;
        case 2:
            *(vu16 *)REG_BLDY = sBattleRasterLines[*(u16 *)REG_VCOUNT & 0xFF];
            switch (flags & 0xF00)
            {
            case 0x100:
                *(vu16 *)REG_BLDCNT = 0xBF;
                break;
            case 0x200:
                *(vu16 *)REG_BLDCNT = 0xFF;
                break;
            }
            break;
        }
    }
}

/**
 * @brief 以指定步进和模式启动扫描线亮度转场
 *
 * @note 清除状态的三个低半字节后合入 flags，并置位 0x1002；重置相位和振幅，保存步长并启用 WIN0。
 * @note 方向 0x20 将 BLDY 设为 24，0x10 保留当前亮度；模式 0x100、0x200 分别写 BLDCNT 为 0xBF、0xFF。
 * @note 保留三次掩码、两个 switch 和 0x10 的空分支；MMIO 写入顺序与参考一致。
 * @address 0x08019AD0
 */
void Battle_StartBrightnessTransition(u8 speed, u16 flags)
{
    sBattleRasterFlags &= 0xFFF0;
    sBattleRasterFlags &= 0xFF0F;
    sBattleRasterFlags &= 0xF0FF;
    sBattleRasterFlags |= 2;
    sBattleRasterFlags |= flags;
    sBattleRasterFlags |= 0x1000;
    sBattleRasterPhase = 0;
    *(vu16 *)REG_WININ = 0x3F;
    *(vu16 *)REG_WIN0H = 0xF0;
    *(vu16 *)REG_WIN0V = 0x2A0;
    *(vu16 *)REG_DISPCNT |= 0x2000;
    sBattleRasterSpeed = speed;
    sBattleWaveAmplitude = 0;

    switch (sBattleRasterFlags & 0xF0)
    {
    case 0x10:
        break;
    case 0x20:
        *(vu16 *)REG_BLDY = 0x18;
        break;
    }

    switch (sBattleRasterFlags & 0xF00)
    {
    case 0x100:
        *(vu16 *)REG_BLDCNT = 0xBF;
        break;
    case 0x200:
        *(vu16 *)REG_BLDCNT = 0xFF;
        break;
    }
}

struct BattleBackgroundLoaderResources
{
    const u16 *palette;
    const void *tilemap;
    u16 firstGraphicsBlock;
    u16 graphicsBlockCount;
};

typedef char BattleBackgroundLoaderResourcesSizeCheck[
    (sizeof(struct BattleBackgroundLoaderResources) == 12) ? 1 : -1];

extern const struct BattleBackgroundLoaderResources D_0839D8EC[];
extern const void *const D_087ED544[];

/**
 * @brief 分阶段解压背景图形、配置图块表调色板并上传，完成时返回 1
 *
 * @note 四参为资源编号、字符块、调色板 bank 和优先级；字符块 3 配置 BG1/屏幕块 15，字符块 2 配置 BG3/屏幕块 13。
 * @note 图形按 0x1000 字节步长解压；两轮各处理 512 个图块表项，随后按 0x200 字节分四次 DMA 上传并等待完成。
 * @note 调色板计算前初始化局部图块表指针，ScBasep 使用条件表达式赋值，以保留原访存顺序和寄存器分配。
 * @note 实际模块 C 输出按原址独立链接后，0x260 字节及已解析重定位完全匹配；整 ROM 与修改前逐字节一致，保留已有失配。
 * @address 0x08019B98
 */
u8 Battle_UpdateBackgroundLoader(u8 resourceId, u8 charBase, u8 paletteBank, u8 priority)
{
    BgCnt bgcnt;
    u8 done = 0;
    u8 *graphics = (u8 *)VRAM + (charBase << 14);
    u16 index;
    u16 palette;
    u16 *tilemap;
    u8 screenBlock;

    switch (sBattleBackgroundLoadPhase)
    {
    case 0:
        sBattleBackgroundLoadChunk = 0;
        sBattleBackgroundLoadPhase = 1;
        break;
    case 1:
        if (sBattleBackgroundLoadChunk < D_0839D8EC[resourceId].graphicsBlockCount)
        {
            LZ77UnCompVram((void *)D_087ED544[D_0839D8EC[resourceId].firstGraphicsBlock
                + sBattleBackgroundLoadChunk], graphics + (sBattleBackgroundLoadChunk << 12));
            sBattleBackgroundLoadChunk++;
        }
        else
        {
            BattlePalette_LoadBgBanks(D_0839D8EC[resourceId].palette, paletteBank, 1);
            sBattleBackgroundLoadPhase = 2;
        }
        break;
    case 2:
        LZ77UnCompWram((void *)D_0839D8EC[resourceId].tilemap, gBattleAnimationTilemap);
        sBattleBackgroundLoadPhase = 3;
        break;
    case 3:
        tilemap = gBattleAnimationTilemap;
        palette = paletteBank << 12;
        for (index = 0; index < 512; index++)
            tilemap[index] = (tilemap[index] & 0xFFF) | palette;
        sBattleBackgroundLoadPhase = 4;
        break;
    case 4:
        tilemap = gBattleAnimationTilemap;
        palette = paletteBank << 12;
        for (index = 512; index < 1024; index++)
            tilemap[index] = (tilemap[index] & 0xFFF) | palette;
        sBattleBackgroundPaletteOffset = 0;
        sBattleBackgroundLoadPhase = 5;
        break;
    case 5:
        if (sBattleBackgroundPaletteOffset <= 3)
        {
            screenBlock = 13;
            if (charBase == 3)
                screenBlock = 15;
            DmaCopy(3, (u8 *)gBattleAnimationTilemap + (sBattleBackgroundPaletteOffset << 9),
                (void *)(VRAM + (screenBlock << 11) + (sBattleBackgroundPaletteOffset << 9)),
                0x200, 32);
            DmaWait(3);
            sBattleBackgroundPaletteOffset++;
        }
        else
        {
            bgcnt.Priority = priority;
            bgcnt.CharBasep = charBase;
            bgcnt.Dummy_5_4 = 0;
            bgcnt.Mosaic = 0;
            bgcnt.ColorMode = 0;
            bgcnt.ScBasep = charBase == 3 ? 15 : 13;
            bgcnt.Loop = 1;
            bgcnt.Size = 0;
            if (charBase == 3)
            {
                *(vu16 *)REG_BG1CNT = *(u16 *)&bgcnt;
                *(vu16 *)REG_DISPCNT |= DISP_BG1_ON;
            }
            else if (charBase == 2)
                *(vu16 *)REG_BG3CNT = *(u16 *)&bgcnt;
            done = 1;
            sBattleBackgroundLoadPhase = 0;
            sBattleBackgroundPaletteOffset = 0;
        }
        break;
    }
    return done;
}

/**
 * @brief 清空背景层显示开关请求
 *
 * @address 0x08019DF8
 */
void Battle_ResetBgRequests(void)
{
    sBattleBgRequests = 0;
}

/**
 * @brief 设置背景层显示开关请求位
 *
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x08019E04
 */
void Battle_SetBgRequests(u16 flags)
{
    sBattleBgRequests |= flags;
}

/**
 * @brief 读取背景层显示开关请求
 *
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x08019E18
 */
u16 Battle_GetBgRequests(void)
{
    return sBattleBgRequests;
}

/**
 * @brief 清除指定的背景层显示开关请求位
 *
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x08019E24
 */
void Battle_ClearBgRequests(u16 flags)
{
    sBattleBgRequests &= ~flags;
}

/**
 * @brief 立即关闭 BG3 显示
 *
 * @note 清除 DISPCNT 的 BG3 使能位，保留其他显示设置。
 * @address 0x08019E38
 */
void Battle_HideBg3(void)
{
    (*(vu16 *)REG_DISPCNT) &= 0xF7FF;
}

/**
 * @brief 立即打开 BG3 显示
 *
 * @note 置位 DISPCNT 的 BG3 使能位，保留其他显示设置。
 * @address 0x08019E4C
 */
void Battle_ShowBg3(void)
{
    (*(vu16 *)REG_DISPCNT) |= 0x800;
}

/**
 * @brief 清空空白图块并初始化战斗状态栏图块表
 *
 * @note 逐字节清零 06005800 的 32 字节空白图块，再以 0x32C0（图块 0x2C0、调色板 3）填充 1024 个图块项。
 * @note 两个循环均保留 u16 索引截断；BgScData 逐字段赋值后只读取低 16 位，保留参考的掩码顺序。
 * @address 0x08019E60
 */
void Battle_InitStatusTilemap(void)
{
    u16 *tilemap = gBattleStatusTilemap;
    u8 *blankTile = BATTLE_STATUS_BLANK_TILE_VRAM;
    u16 i;
    BgScData entry;

    for (i = 0; i < 0x20; i++)
        blankTile[i] = 0;

    entry.CharNo = 0x2C0;
    entry.HFlip = 0;
    entry.VFlip = 0;
    entry.Pltt = 3;

    for (i = 0; i < BATTLE_TILEMAP_SIZE; i++)
        tilemap[i] = *(u16 *)&entry;
}

/**
 * @brief 立即关闭 BG0 战斗窗口层
 *
 * @note 清除 DISPCNT 的 BG0 使能位，保留其他显示设置。
 * @address 0x08019ECC
 */
void Battle_HideWindowLayer(void)
{
    (*(vu16 *)REG_DISPCNT) &= 0xFEFF;
}

/**
 * @brief 设置指定窗口槽的收起速度与曲线并请求关闭
 *
 * @address 0x08019EE0
 */
void Battle_CloseWindow(u8 slot, u8 duration, u8 curve)
{
    sBattleWindows[slot].duration = duration;
    sBattleWindows[slot].frame = 0;
    sBattleWindows[slot].curve = curve;
    sBattleWindows[slot].state = BATTLE_WINDOW_CLOSING;
}

/**
 * @brief 改写矩形区域的图块编号并保留原属性位
 *
 * @note 每项先保留高 6 位，再直接加 tile；tile 不预先截为 10 位，坐标不裁剪。
 * @note 独立行指针使用 y * 32 + x 的完整元素偏移，每行前进 32 项；行、列计数器均保留 u8 截断。
 * @address 0x08019F08
 */
void Battle_SetTilemapTiles(u16 *tilemap, u16 tile, u8 x, u8 y, u8 width, u8 height)
{
    u16 *rowTiles = tilemap + (y * 32 + x);
    u8 row;
    u8 column;

    for (row = 0; row < height; row++)
    {
        for (column = 0; column < width; column++)
            rowTiles[column] = (rowTiles[column] & 0xFC00) + tile;
        rowTiles += 32;
    }
}

/**
 * @brief 按有符号位移横向搬移图块表矩形
 *
 * @note 仅使用 dx，mode 和 dy 忽略；dx 为 0 时不写入，也不清空搬移后的源区域。
 * @note 正向从 (u8)(x + width) 开始倒序复制 width 列，反向从 x 开始顺序复制；保留原版正向起始列。
 * @note 坐标与计数器保持 u8，行上限使用 height + y 的整型和，保留比较与加法顺序；边界不裁剪。
 * @address 0x08019F78
 */
void Battle_ShiftTilemapRect(u16 *tilemap, u8 mode, s8 dx, s8 dy,
    u8 x, u8 y, u8 width, u8 height)
{
    u8 sourceX;
    u8 column;
    u8 row;

    if (dx == 0)
        return;

    if (dx > 0)
    {
        sourceX = x + width;
        for (column = 0; column < width; column++)
        {
            for (row = y; row < height + y; row++)
                tilemap[row * 32 + (sourceX + dx)] = tilemap[row * 32 + sourceX];
            sourceX--;
        }
    }
    else
    {
        sourceX = x;
        for (column = 0; column < width; column++)
        {
            for (row = y; row < height + y; row++)
                tilemap[row * 32 + (sourceX + dx)] = tilemap[row * 32 + sourceX];
            sourceX++;
        }
    }
}

/**
 * @brief 读取指定窗口槽状态的低字节
 *
 * @address 0x0801A05C
 */
u32 Battle_GetWindowState(u8 slot)
{
    return (u8)sBattleWindows[slot].state;
}

/**
 * @brief 修改图块表矩形区域的调色板编号
 *
 * @note 每项保留低 12 位，再加 palette 左移 12 位的结果，最终按 u16 写回。
 * @note 独立行指针先计算 y * 32 + x，再按每行 32 项推进；u8 行、列索引保留原截断和寄存器分配。
 * @address 0x0801A074
 */
void Battle_SetTilemapPalette(u16 *tilemap, u32 paletteArg, u32 xArg, u32 yArg,
    u32 widthArg, u32 heightArg)
{
    u16 palette = paletteArg;
    u8 x = xArg;
    u8 y = yArg;
    u8 width = widthArg;
    u8 height = heightArg;
    u16 *rowTiles = tilemap + (y * 32 + x);
    u8 row;
    u8 column;

    for (row = 0; row < height; row++)
    {
        for (column = 0; column < width; column++)
            rowTiles[column] = (palette << 12) + (rowTiles[column] & 0xFFF);
        rowTiles += 32;
    }
}

/**
 * @brief 在窗口槽活动时向 VRAM 上传战斗窗口图块表
 *
 * @address 0x0801A0F0
 */
void Battle_UploadWindowTilemap(void)
{
    if (sBattleWindows[0].state != BATTLE_WINDOW_CLOSED
        || sBattleWindows[1].state != BATTLE_WINDOW_CLOSED
        || sBattleWindows[2].state != BATTLE_WINDOW_CLOSED)
    {
        DmaCopy(3, gBattleWindowTilemap, BATTLE_WINDOW_TILEMAP_VRAM, sizeof(gBattleWindowTilemap), 32);
        DmaWait(3);
    }
}

/**
 * @brief 清空战斗扫描线效果标志
 *
 * @address 0x0801A13C
 */
void Battle_ResetRasterFlags(void)
{
    sBattleRasterFlags = 0;
}

/**
 * @brief 读取战斗扫描线效果标志
 *
 * @address 0x0801A148
 */
u16 Battle_GetRasterFlags(void)
{
    return sBattleRasterFlags;
}

/**
 * @brief 清除指定的战斗扫描线效果标志位
 *
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x0801A154
 */
void Battle_ClearRasterFlags(u16 flags)
{
    sBattleRasterFlags &= ~flags;
}

/**
 * @brief 配置步进、振幅、行间相位和图层掩码并启动战斗扫描线波动
 *
 * @note 清除模式低四位后置位 0x2001，若 0x1000 已置位则单独清除；相位归零，四个参数按 u8 保存。原址 0x74 字节完全匹配。
 * @address 0x0801A168
 */
void Battle_StartWaveEffect(u8 speed, u8 amplitude, u8 lineStep, u8 layerMask)
{
    sBattleRasterFlags &= 0xFFF0;
    sBattleRasterFlags |= 1;
    sBattleRasterFlags |= 0x2000;
    if (sBattleRasterFlags & 0x1000)
        sBattleRasterFlags &= 0xEFFF;
    sBattleRasterPhase = 0;
    sBattleRasterSpeed = speed;
    sBattleWaveAmplitude = amplitude;
    sBattleWaveLineStep = lineStep;
    sBattleWaveLayerMask = layerMask;
}

/**
 * @brief 停用扫描线效果并清除目标图层掩码
 *
 * @address 0x0801A1DC
 */
void Battle_StopWaveEffect(void)
{
    sBattleRasterFlags &= 0xFFF0;
    sBattleRasterFlags &= 0xEFFF;
    if (sBattleRasterFlags & 0x4000)
        sBattleRasterFlags &= 0xBFFF;
    sBattleWaveLayerMask = 0;
}

/**
 * @brief 关闭 WIN0 并结束战斗扫描线转场
 *
 * @note 按 DISPCNT、BLDY、BLDCNT 顺序关闭窗口并清除亮度和混合控制；依次清除模式位、0x1000 和已置位的 0x4000，图层掩码归零。原址 0x58 字节完全匹配。
 * @address 0x0801A218
 */
void Battle_EndRasterTransition(void)
{
    *(vu16 *)REG_DISPCNT &= 0xDFFF;
    *(vu16 *)REG_BLDY = 0;
    *(vu16 *)REG_BLDCNT = 0;
    sBattleRasterFlags &= 0xFFF0;
    sBattleRasterFlags &= 0xEFFF;
    if (sBattleRasterFlags & 0x4000)
        sBattleRasterFlags &= 0xBFFF;
    sBattleWaveLayerMask = 0;
}

/**
 * @brief 以图块 0064 填充战斗背景动画工作图块表
 *
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x0801A270
 */
void Battle_ClearAnimationTilemap(void)
{
    {
        u16 fillValue;
        u16 *fillSource = &fillValue;
    
        *fillSource = (u16)(0x64);
        DmaSet(3, fillSource, gBattleAnimationTilemap,
            DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS
                | ((u32)(sizeof(gBattleAnimationTilemap)) / 2));
    }
    DmaWait(3);
}

/**
 * @brief 设置战斗混合模式、透明系数和亮度系数
 *
 * @note eva 同时用于亮度模式；保留原来的 & 2 掩码及 case 2/3。
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x0801A2AC
 */
void Battle_SetBlend(u16 control, u8 eva, u8 evb)
{
    /* Plain halfword stores preserve the original agbcc instruction sequence. */
    *(u16 *)REG_BLDCNT = control;
    *(u16 *)REG_BLDALPHA = eva | (evb << 8);

    control = (control >> 6) & 2;
    switch (control)
    {
    case 2:
    case 3:
        *(u16 *)REG_BLDY = eva;
        break;
    }
}

/**
 * @brief 逐块解压四份背景图形到 BG 字符块 2
 *
 * @address 0x0801A2EC
 */
void Battle_LoadBackgroundBlock(void)
{
    if (sBattleBackgroundLoadProgress < 4)
    {
        LZ77UnCompVram((void *)(D_087EBDF0[sBattleBackgroundLoadProgress]),
            (void *)(VRAM + 0x8000 + (sBattleBackgroundLoadProgress << 12)));
        sBattleBackgroundLoadProgress++;
    }
}

/**
 * @brief 将分块背景图形加载进度重置为零
 *
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x0801A324
 */
void Battle_ResetBackgroundLoadProgress(void)
{
    sBattleBackgroundLoadProgress = 0;
}

/**
 * @brief 将分块背景图形加载进度标记为完成
 *
 * @address 0x0801A330
 */
void Battle_FinishBackgroundLoadProgress(void)
{
    sBattleBackgroundLoadProgress = 4;
}

/**
 * @brief 读取分块背景图形加载进度
 *
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x0801A33C
 */
u8 Battle_GetBackgroundLoadProgress(void)
{
    return sBattleBackgroundLoadProgress;
}

/**
 * @brief 重置分阶段背景加载的状态和分块计数
 *
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x0801A348
 */
void Battle_ResetBackgroundLoader(void)
{
    sBattleBackgroundLoadPhase = 0;
    sBattleBackgroundLoadChunk = 0;
}

/**
 * @brief 恢复战斗状态栏资源并解除其隐藏标志
 *
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x0801A35C
 */
void Battle_RestoreStatusGraphics(void)
{
    Battle_LoadStatusGraphics();
    Battle_ClearControlFlags(0x10);
}

/**
 * @brief 把四个背景层的滚动缓存写入硬件寄存器
 *
 * @note 依次写入 BG0–BG3 的 HOFS/VOFS，共八个半字。
 * @address 0x0801A36C
 */
void Battle_UploadBgScroll(void)
{
    *(vu16*)REG_BG0HOFS = sBattleBgScroll.bg0Hofs;
    *(vu16*)REG_BG0VOFS = sBattleBgScroll.bg0Vofs;
    *(vu16*)REG_BG1HOFS = sBattleBgScroll.bg1Hofs;
    *(vu16*)REG_BG1VOFS = sBattleBgScroll.bg1Vofs;
    *(vu16*)REG_BG2HOFS = sBattleBgScroll.bg2Hofs;
    *(vu16*)REG_BG2VOFS = sBattleBgScroll.bg2Vofs;
    *(vu16*)REG_BG3HOFS = sBattleBgScroll.bg3Hofs;
    *(vu16*)REG_BG3VOFS = sBattleBgScroll.bg3Vofs;
}

/**
 * @brief 设置指定背景层的横向和纵向滚动缓存
 *
 * @note 坐标参数为 s16，保留调用者的负偏移；缓存仍按原顺序半字写入。
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x0801A3A8
 */
void Battle_SetBgScroll(u8 layer, s16 x, s16 y)
{
    u16 *offsets = (u16 *)&sBattleBgScroll;
    s32 index = layer * 2;

    offsets[index] = x;
    offsets++;
    offsets[index] = y;
}

#define BATTLE_ANIMATION_TYPE2_BUFFER ((u8 *)0x020212C0)
#define BATTLE_ANIMATION_TYPE3_BUFFER ((u8 *)0x02020E00)
#define BATTLE_ANIMATION_TYPE4_BUFFER ((u8 *)0x0202B2C0)
#define BATTLE_ANIMATION_TYPE5_BUFFER ((u8 *)0x020302C0)
#define BATTLE_ANIMATION_TYPE9_BUFFER ((u8 *)0x02037C28)

/**
 * @brief 按动画类型和 0x1000 字节步长解压图形分块，完成后绑定动画表
 *
 * @note 首块时类型 6 清空字符块 3 的 0x4000 字节；类型 7/8 分别清空字符块 3/2 的首个 32 字节图块。
 * @note WRAM 缓冲使用固定地址视图，保留类型 2/4 与 3/5 的两组解压尾部；DMA 保留控制读回和完成等待。
 * @note 分块计数按 u16 递增；完成后清除 0x800 加载位，类型 9 留待 OBJ 图块上传完成后清除。
 * @note 实际模块 C 输出按原址独立链接后，0x228 字节及已解析重定位完全匹配；整 ROM 仍有其他入口的既有失配。
 * @address 0x0801A3C4
 */
void Battle_LoadAnimationBlock(struct BattleAnimation *animation)
{
    u16 fillValue;

    switch (animation->flags & 0xF)
    {
    case 1:
        LZ77UnCompVram((void *)gBattleAnimationGraphicsTable[animation->graphicsId + animation->graphicsBlock],
            animation->initialBaseTile * 32 + (u8 *)OBJ_GRAPHICS + (animation->graphicsBlock << 12));
        break;

    case 2:
        LZ77UnCompWram((void *)gBattleAnimationGraphicsTable[animation->graphicsId + animation->graphicsBlock],
            BATTLE_ANIMATION_TYPE2_BUFFER + (animation->graphicsBlock << 12));
        break;

    case 3:
        LZ77UnCompWram((void *)gBattleAnimationGraphicsTable[animation->graphicsId + animation->graphicsBlock],
            BATTLE_ANIMATION_TYPE3_BUFFER + (animation->graphicsBlock << 12));
        break;

    case 6:
        if (animation->graphicsBlock == 0)
        {
            fillValue = 0;
            DmaSet(3, &fillValue, (void *)(VRAM + 0xC000),
                DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS | 0x2000);
            DmaWait(3);
        }
        /* fallthrough */

    case 4:
        LZ77UnCompWram((void *)gBattleAnimationGraphicsTable[animation->graphicsId + animation->graphicsBlock],
            BATTLE_ANIMATION_TYPE4_BUFFER + (animation->graphicsBlock << 12));
        break;

    case 5:
        LZ77UnCompWram((void *)gBattleAnimationGraphicsTable[animation->graphicsId + animation->graphicsBlock],
            BATTLE_ANIMATION_TYPE5_BUFFER + (animation->graphicsBlock << 12));
        break;

    case 7:
        if (animation->graphicsBlock == 0)
        {
            fillValue = 0;
            DmaSet(3, &fillValue, (void *)(VRAM + 0xC000),
                DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS | 0x10);
            DmaWait(3);
        }
        LZ77UnCompVram((void *)gBattleAnimationGraphicsTable[animation->graphicsId + animation->graphicsBlock],
            (void *)(VRAM + 0xC020 + (animation->graphicsBlock << 12)));
        break;

    case 8:
        if (animation->graphicsBlock == 0)
        {
            fillValue = 0;
            DmaSet(3, &fillValue, (void *)(VRAM + 0x8000),
                DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS | 0x10);
            DmaWait(3);
        }
        LZ77UnCompVram((void *)gBattleAnimationGraphicsTable[animation->graphicsId + animation->graphicsBlock],
            (void *)(VRAM + 0x8020 + (animation->graphicsBlock << 12)));
        break;

    case 9:
        LZ77UnCompWram((void *)gBattleAnimationGraphicsTable[animation->graphicsId + animation->graphicsBlock],
            BATTLE_ANIMATION_TYPE9_BUFFER + (animation->graphicsBlock << 12));
        break;
    }

    animation->graphicsBlock++;
    if (animation->graphicsBlock >= animation->graphicsBlockCount)
    {
        Battle_BindAnimationTables(animation);
        if ((animation->flags & 0xF) != 9)
            animation->flags &= 0xF7FF;
    }
}

/**
 * @brief 按字段复制一个 0x30 字节的战斗动画实例
 *
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x0801A5EC
 */
void Battle_CopyAnimation(struct BattleAnimation *destination, const struct BattleAnimation *source)
{
    destination->frames = source->frames;
    destination->timeline = source->timeline;
    destination->frameOffsets = source->frameOffsets;
    destination->timelineOffsets = source->timelineOffsets;
    destination->flags = source->flags;
    destination->unknown28 = source->unknown28;
    destination->frame = source->frame;
    destination->timelineFrame = source->timelineFrame;
    destination->baseTile = source->baseTile;
    destination->paletteSlot = source->paletteSlot;
    destination->priority = source->priority;
    destination->x = source->x;
    destination->y = source->y;
    destination->oamStart = source->oamStart;
    destination->oamCount = source->oamCount;
    destination->definition = source->definition;
    destination->palettes = source->palettes;
    destination->graphicsBlockCount = source->graphicsBlockCount;
    destination->graphicsBlock = source->graphicsBlock;
    destination->initialBaseTile = source->initialBaseTile;
    destination->initialPaletteSlot = source->initialPaletteSlot;
    destination->graphicsId = source->graphicsId;
}

/**
 * @brief 绑定战斗动画帧表和时间线，重置播放状态并恢复初始图块及调色板槽
 *
 * @note definition 的两个半字是字节偏移；类型 6–8 初始化背景，其余类型装载 OBJ 调色板。
 * @note 类型减 6 后截断为 u16 再比较范围，保留参考汇编的无符号判断。
 * @note 两次绑定之间保留 definition 的递增、递减，避开 agbcc 临时量同优先级时的寄存器分配差异；编译后不增加指令。
 * @note C 有效指令 0x6E 字节、完整跨度 0x70 字节及三处调用重定位均匹配；整 ROM 与 baserom.gba 逐字节一致。
 * @address 0x0801A684
 */
void Battle_BindAnimationTables(struct BattleAnimation *animation)
{
    const u16 *definition = animation->definition;

    animation->frames = (const struct BattleFrameTableHeader *)
        ((const u8 *)definition + definition[0]);
    definition++;
    definition--;
    animation->timeline = (const struct BattleTimelineHeader *)
        ((const u8 *)definition + definition[1]);
    animation->frameOffsets = (const u16 *)(animation->frames + 1);
    animation->timelineOffsets = (const u16 *)(animation->timeline + 1);
    animation->frame = 0;
    animation->timelineFrame = 0;
    animation->baseTile = animation->initialBaseTile;
    animation->paletteSlot = animation->initialPaletteSlot;
    animation->unknown28 = 0;

    if ((u16)((animation->flags & 0xF) - 6) <= 2)
    {
        Battle_InitBackgroundAnimation(animation);
    }
    else
    {
        BattlePalette_LoadObjBanks(animation->palettes, animation->paletteSlot,
            Battle_GetAnimationPaletteCount(animation));
    }
}

/**
 * @brief 配置背景型战斗动画的图层，清空屏幕块并装载调色板
 *
 * @note 类型 6/7 配置 BG1 的字符块 3、屏幕块 15 并启用 BG1；类型 8 配置 BG3 的字符块 2、屏幕块 13。
 * @note 优先级取 priority 的低两位；两路共用普通 u16 填充值，保留官方 DMA 宏的控制读回和完成等待。
 * @note 实际模块 C 输出按原址独立链接后，0x190 字节和已解析重定位完全匹配；整 ROM 保持修改前的失配基线。
 * @address 0x0801A6F4
 */
void Battle_InitBackgroundAnimation(struct BattleAnimation *animation)
{
    BgCnt bgcnt;
    u16 fillValue;

    switch (animation->flags & 0xF)
    {
    case 6:
    case 7:
        bgcnt.Priority = animation->priority;
        bgcnt.CharBasep = 3;
        bgcnt.Dummy_5_4 = 0;
        bgcnt.Mosaic = 0;
        bgcnt.ColorMode = 0;
        bgcnt.ScBasep = 15;
        bgcnt.Loop = 1;
        bgcnt.Size = 0;
        *(vu16 *)REG_BG1CNT = *(u16 *)&bgcnt;
        {
            u16 *fillSource = &fillValue;

            *fillSource = 0;
            DmaSet(3, fillSource, BG_SCREEN_ADDR(15),
                DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS | BATTLE_TILEMAP_SIZE);
        }
        DmaWait(3);
        *(vu16 *)REG_DISPCNT |= 0x200;
        break;
    case 8:
        bgcnt.Priority = animation->priority;
        bgcnt.CharBasep = 2;
        bgcnt.Dummy_5_4 = 0;
        bgcnt.Mosaic = 0;
        bgcnt.ColorMode = 0;
        bgcnt.ScBasep = 13;
        bgcnt.Loop = 1;
        bgcnt.Size = 0;
        *(vu16 *)REG_BG3CNT = *(u16 *)&bgcnt;
        {
            u16 *fillSource = &fillValue;

            *fillSource = 0;
            DmaSet(3, fillSource, BG_SCREEN_ADDR(13),
                DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS | BATTLE_TILEMAP_SIZE);
        }
        DmaWait(3);
        break;
    }

    BattlePalette_LoadBgBanks(animation->palettes, animation->paletteSlot,
        Battle_GetAnimationPaletteCount(animation));
}

struct BattleSpriteAnimationPart
{
    s16 y : 8;
    u16 affineMode : 2;
    u16 objMode : 2;
    u16 mosaic : 1;
    u16 color256 : 1;
    u16 shape : 2;
    s16 x : 9;
    u16 affineIndexLow : 3;
    u16 hFlip : 1;
    u16 vFlip : 1;
    u16 size : 2;
    u16 tile : 10;
    u16 priority : 2;
    u16 palette : 4;
} __attribute__((packed, aligned(2)));

typedef char BattleSpriteAnimationPartSizeCheck[
    (sizeof(struct BattleSpriteAnimationPart) == 6) ? 1 : -1];

/**
 * @brief 推进精灵动画时间线，将帧片段从软件 OAM 末端向前组装，返回剩余游标
 *
 * @note 处理隐藏、两种闪烁、整体翻转及 OBJ 模式；类型 1 使用片段图块编号，其余类型按片段尺寸累计图块偏移。
 * @note 六字节片段保留有符号坐标；SDK OamData 与两字传输共用 union，整体赋值保留原来的八字节写入。
 * @note 仿射记录依次写入角度、X/Y 比率，保留 ObjAffineSet 的参数 4、2；整条轨道完成后才更新八位仿射游标。
 * @note 实际模块 C 输出按原址独立链接后，0x488 字节及已解析重定位完全匹配；整 ROM 与修改前逐字节一致，保留已有失配。
 * @address 0x0801A884
 */
u8 Battle_BuildSpriteAnimation(struct BattleAnimation *animation, u8 oamCursor, u8 *affineCursor)
{
    ObjAffineSrcData affine;
    union BattleSpriteOam
    {
        OamData fields;
        u32 words[2];
    } sprite;
    union BattleSpriteOam *oam;
    u16 tileOffset;
    s16 x;
    s16 y;
    u16 partCount;
    u16 index;
    u8 nextAffine;
    const u16 *affineData;
    u8 paletteOffset;
    s16 track;
    u16 affineCount;
    const struct BattleKeyframe *keyframe;
    const u16 *data;
    const struct BattleSpriteAnimationPart *part;
    u8 mode;

    if ((animation->flags & 0x800) && (animation->flags & 0xF) != 3)
        animation->timelineFrame = 0;
    else
    {
        if (!(animation->flags & 0x100))
            animation->timelineFrame = (animation->timelineFrame + 1)
                % animation->timeline->frameCount;
        if (!(animation->flags & 0x400)
            && animation->timelineFrame == animation->timeline->frameCount - 1)
            animation->flags |= 0x1000;
    }

    if ((u16)(animation->flags & 0x200))
        return oamCursor;
    if ((animation->flags & 0x2000) && (animation->timelineFrame & 1))
        return oamCursor;
    if ((animation->flags & 0x4000) && (u16)(animation->timelineFrame % 3u) == 2)
        return oamCursor;

    oam = (union BattleSpriteOam *)&gSpriteOamBuffer[oamCursor];
    tileOffset = 0;
    if (animation->flags & 0x8000)
        paletteOffset = animation->initialPaletteSlot;
    else
        paletteOffset = animation->paletteSlot;

    for (track = animation->timeline->trackCount - 1; track >= 0; track--)
    {
        keyframe = Battle_FindKeyframe((const u16 *)((const u8 *)animation->timeline
            + animation->timelineOffsets[track]), animation->timelineFrame);
        data = (const u16 *)((const u8 *)animation->frames
            + animation->frameOffsets[keyframe->frameIndex]);
        affineCount = *data++;
        partCount = *data++;
        affineData = data;
        data += affineCount * 4;
        index = 0;
        nextAffine = *affineCursor;
        for (; index < affineCount;)
        {
            affine.Theta = affineData[index * 4];
            affine.RatioX = affineData[index * 4 + 1];
            affine.RatioY = affineData[index * 4 + 2];
            ObjAffineSet(&affine, &gSpriteAffineMatrices[*affineCursor], 4, 2);
            index++;
            nextAffine++;
        }

        for (index = 0; index < partCount; index++)
        {
            part = (const struct BattleSpriteAnimationPart *)data;
            if (animation->flags & 0x10)
                mode = 1;
            else if (animation->flags & 0x80)
                mode = 2;
            else
                mode = part->objMode;

            if (animation->flags & 0x20)
                x = -part->x - D_08393A18[part->size + part->shape * 4] * 8;
            else
                x = part->x;
            if (animation->flags & 0x40)
                y = -part->y - gBattleObjHeightTiles[part->size + part->shape * 4] * 8;
            else
                y = part->y;

            if (animation->flags & 0x20)
            {
                if (!part->hFlip)
                    sprite.fields.HFlip = 1;
                else
                    sprite.fields.HFlip = 0;
            }
            else
                sprite.fields.HFlip = part->hFlip;
            if (animation->flags & 0x40)
            {
                if (!part->vFlip)
                    sprite.fields.VFlip = 1;
                else
                    sprite.fields.VFlip = 0;
            }
            else
                sprite.fields.VFlip = part->vFlip;

            sprite.fields.VPos = animation->y + y;
            sprite.fields.AffineMode = part->affineMode;
            sprite.fields.ObjMode = mode;
            sprite.fields.Mosaic = part->mosaic;
            sprite.fields.ColorMode = part->color256;
            sprite.fields.Shape = part->shape;
            sprite.fields.HPos = animation->x + x;
            sprite.fields.AffineParamNo_L = part->affineIndexLow + *affineCursor;
            sprite.fields.Size = part->size;
            if ((animation->flags & 0xF) == 1)
                sprite.fields.CharNo = part->tile + animation->baseTile;
            else
                sprite.fields.CharNo = tileOffset + animation->baseTile;
            sprite.fields.Priority = animation->priority;
            sprite.fields.Pltt = part->palette + paletteOffset;
            tileOffset += gBattleObjTileCounts[sprite.fields.Size + sprite.fields.Shape * 4];
            *oam = sprite;
            oam--;
            data += 3;
            oamCursor--;
        }
        *affineCursor = nextAffine;
    }
    return oamCursor;
}

/**
 * @brief 推进背景型动画并把当前帧拼入工作图块表
 *
 * @note +1C 为时间线计数；类型 6 连续编号并支持水平翻转，7/8 使用片段的图块编号。
 * @note 类型 8 写状态栏图块表，其余类型写动画图块表；保留原版不裁剪坐标的行为。
 * @address 0x0801AD0C
 */
void Battle_BuildBackgroundAnimation(struct BattleAnimation *animation)
{
    u16 tile;
    s16 x;
    s16 y;
    u16 partCount;
    u16 *tilemap;
    u16 type;
    s16 track;
    const struct BattleKeyframe *keyframe;
    const u16 *data;
    u16 matrixCount;
    u16 partIndex;
    const struct BattleAnimationPart *part;
    s32 width;
    s32 height;
    s32 tileCount;
    s16 row;
    s16 column;
    u8 destinationColumn;

    if ((animation->flags & 0x800) && (animation->flags & 0xF) != 3)
        animation->timelineFrame = 0;
    else
    {
        if (!(animation->flags & 0x100))
            animation->timelineFrame = (animation->timelineFrame + 1)
                % animation->timeline->frameCount;
        if (!(animation->flags & 0x400)
            && animation->timelineFrame == animation->timeline->frameCount - 1)
            animation->flags |= 0x1000;
    }

    if ((u16)(animation->flags & 0x200))
        return;
    if ((animation->flags & 0x2000) && (animation->timelineFrame & 1))
        return;
    if ((animation->flags & 0x4000)
        && (u16)(animation->timelineFrame % 3u) == 2)
        return;

    type = animation->flags & 0xF;
    tilemap = gBattleAnimationTilemap;
    if (type == 8)
        tilemap = gBattleStatusTilemap;
    {
        u16 fillValue;
        u16 *fillSource = &fillValue;

        *fillSource = 0;
        DmaSet(3, fillSource, tilemap,
            DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS | BATTLE_TILEMAP_SIZE);
    }
    DmaWait(3);

    tile = 1;
    for (track = animation->timeline->trackCount - 1; track >= 0; track--)
    {
        keyframe = Battle_FindKeyframe((const u16 *)((const u8 *)animation->timeline
            + animation->timelineOffsets[track]), animation->timelineFrame);
        data = (const u16 *)((const u8 *)animation->frames
            + animation->frameOffsets[keyframe->frameIndex]);
        matrixCount = *data++;
        partCount = *data++;
        data += matrixCount * 4;
        for (partIndex = 0; partIndex < partCount; partIndex++)
        {
            part = (const struct BattleAnimationPart *)data;
            x = part->x / 8;
            y = part->y / 8;

            x += animation->x;
            y += animation->y;
            width = D_08393A18[part->size + part->shape * 4];
            height = gBattleObjHeightTiles[part->size + part->shape * 4];
            tileCount = gBattleObjTileCounts[part->size + part->shape * 4];
            switch (animation->flags & 0xF)
            {
            case 6:
                if (!part->hFlip)
                {
                    for (row = 0; row < height; row++)
                    {
                        for (column = 0; column < width; column++)
                        {
                            tilemap[y * 32 + (x + column)] = tile + (animation->paletteSlot << 12);
                            tile++;
                        }
                        y++;
                    }
                }
                else
                {
                    for (row = 0; row < height; row++)
                    {
                        for (column = width - 1, destinationColumn = 0;
                             column >= 0; column--, destinationColumn++)
                            tilemap[(x + destinationColumn) + y * 32] = tile
                                + (((animation->paletteSlot << 12) | 0x400)
                                    + (column + width * row));
                        y++;
                    }
                    tile += tileCount;
                }
                break;
            case 7:
            case 8:
                tile = 1;
                for (row = 0; row < height; row++)
                {
                    for (column = 0; column < width; column++)
                    {
                        tilemap[y * 32 + (x + column)] = (animation->paletteSlot << 12)
                            + (tile + part->tile);
                        tile++;
                    }
                    y++;
                }
                break;
            }
            data += 3;
        }
    }
}

/* The upload path embeds the initial query, but marking retains the public query call. */
static inline u8 Battle_IsTileUploadedInline(u16 tile)
{
    if ((sBattleUploadedTiles[tile / 8] >> (tile % 8)) & 1)
        return 1;
    return 0;
}

static inline void Battle_MarkUploadedTileInline(u16 tile)
{
    if (!Battle_IsTileUploaded(tile))
        sBattleUploadedTiles[tile >> 3] |= 1 << (tile & 7);
}

/* type is a constant 2, 4 or 5 at each call site; each tile is 32 bytes.
 * Select the source within the DMA expression to preserve the reference load order. */
static inline void Battle_UploadPartInline(const struct BattleAnimationPart *part,
    const struct BattleAnimationPart *sprite, u8 type)
{
    DmaCopy(3,
        (type == 2 ? D_020212C0 : type == 4 ? D_0202B2C0 : D_020302C0) + part->tile * 32,
        OBJ_GRAPHICS[sprite->tile],
        (u32)(gBattleObjTileCounts[sprite->size + sprite->shape * 4] * 32), 32);
    DmaWait(3);
    Battle_MarkUploadedTileInline(sprite->tile);
}

/**
 * @brief 在 VBlank 按动画类型上传当前帧所需的 OBJ 图块
 *
 * @note 类型 2/4/5 按目标首图块去重，类型 3 选择固定帧，类型 9 完成整批上传后清除加载标志。
 * @note 不推进时间线；六字节片段与八字节软件 OAM 槽分别前进、后退，返回剩余 OAM 游标。
 * @address 0x0801B0B8
 */
u8 Battle_UploadSpriteAnimation(struct BattleAnimation *animation, u8 oamCursor)
{
    u16 partCount;
    u16 baseTile;
    u8 firstPart = 1;
    u8 tileCount = 0;
    const struct BattleAnimationPart *sprite =
        (const struct BattleAnimationPart *)&gSpriteOamBuffer[oamCursor];
    s16 track;
    u16 partIndex;
    u16 matrixCount;
    const u16 *data;
    const struct BattleKeyframe *keyframe;
    const struct BattleAnimationPart *part;

    if (animation->flags & 0x200)
        return oamCursor;
    if ((animation->flags & 0x2000) && (animation->timelineFrame & 1))
        return oamCursor;
    if ((animation->flags & 0x4000) && (u16)(animation->timelineFrame % 3u) == 2)
        return oamCursor;

    for (track = animation->timeline->trackCount - 1; track >= 0; track--)
    {
        keyframe = Battle_FindKeyframe((const u16 *)((const u8 *)animation->timeline
            + animation->timelineOffsets[track]), animation->timelineFrame);
        data = (const u16 *)((const u8 *)animation->frames
            + animation->frameOffsets[keyframe->frameIndex]);
        matrixCount = *data++;
        partCount = *data++;
        data += matrixCount * 4;
        for (partIndex = 0; partIndex < partCount; partIndex++)
        {
            part = (const struct BattleAnimationPart *)data;
            switch (animation->flags & 0xF)
            {
            case 1:
                break;
            case 2:
                if (animation->flags & 0x800)
                    break;
                if (Battle_IsTileUploadedInline(sprite->tile))
                    break;
                Battle_UploadPartInline(part, sprite, 2);
                break;
            case 3:
                DmaCopy(3, animation->timelineFrame > 3 ? D_02020F20 : D_02020E00,
                    OBJ_GRAPHICS, 0x120, 32);
                DmaWait(3);
                break;
            case 4:
                if (animation->flags & 0x800)
                    break;
                if (Battle_IsTileUploadedInline(sprite->tile))
                    break;
                Battle_UploadPartInline(part, sprite, 4);
                break;
            case 5:
                if (animation->flags & 0x800)
                    break;
                if (Battle_IsTileUploadedInline(sprite->tile))
                    break;
                Battle_UploadPartInline(part, sprite, 5);
                break;
            case 9:
                if (!(animation->flags & 0x800))
                    break;
                if (animation->graphicsBlock < animation->graphicsBlockCount)
                    break;
                if (firstPart == 1)
                {
                    baseTile = sprite->tile;
                    firstPart = 0;
                }
                tileCount += gBattleObjTileCounts[sprite->size + sprite->shape * 4];
                break;
            }
            sprite = (const struct BattleAnimationPart *)
                ((const u8 *)sprite - sizeof(union SpriteOamEntry));
            data += 3;
            oamCursor--;
        }
    }
    if ((animation->flags & 0x80F) == 0x809
        && animation->graphicsBlock >= animation->graphicsBlockCount)
    {
        DmaCopy(3, D_02037C28, OBJ_GRAPHICS[baseTile], (u32)(tileCount * 32), 32);
        DmaWait(3);
        animation->flags &= 0xF7FF;
    }
    return oamCursor;
}

/**
 * @brief 上传背景型动画当前帧使用的图块
 *
 * @note 类型 6 的上传路径；从 0202B2C0 复制到 VRAM 0600C000，目标图块从 1 连续编号。
 * @note 仅检查隐藏和加载标志，不推进时间线、不处理闪烁或去重；目标编号按 u8 截断。
 * @address 0x0801B570
 */
void Battle_UploadBackgroundAnimation(struct BattleAnimation *animation)
{
    u8 tile;
    s16 track;
    u16 matrixCount;
    u16 partCount;
    u16 partIndex;
    const u16 *data;
    const struct BattleKeyframe *keyframe;
    const struct BattleAnimationPart *part;

    if (animation->flags & 0x200)
        return;

    tile = 1;
    for (track = animation->timeline->trackCount - 1; track >= 0; track--)
    {
        keyframe = Battle_FindKeyframe((const u16 *)((const u8 *)animation->timeline
            + animation->timelineOffsets[track]), animation->timelineFrame);
        data = (const u16 *)((const u8 *)animation->frames
            + animation->frameOffsets[keyframe->frameIndex]);
        matrixCount = *data++;
        partCount = *data++;
        data += matrixCount * 4;
        for (partIndex = 0; partIndex < partCount; partIndex++)
        {
            part = (const struct BattleAnimationPart *)data;
            if (!(animation->flags & 0x800))
            {
                DmaCopy(3, D_0202B2C0 + part->tile * 32,
                    (u8 *)BG_SCREEN_ADDR(24) + tile * 32,
                    (u32)(gBattleObjTileCounts[part->size + part->shape * 4] * 32), 16);
                DmaWait(3);
                tile += gBattleObjTileCounts[part->size + part->shape * 4];
            }
            data += 3;
        }
    }
}

/**
 * @brief 将尚未占用的前段软件 OAM 填成屏幕外精灵
 *
 * @note 游标 0..126 填充前 cursor+1 槽，127..255 填满；固定 32 位源重复写入 010400A1。
 * @note 仅修改软件缓冲区；实际硬件 OAM 上传由战斗 VBlank 后续完成。
 * @address 0x0801B688
 */
void Battle_HideUnusedOam(u8 oamCursor)
{
    u32 fillValue;
    u32 *fillSource = &fillValue;
    struct BattleOamAttributes oam;
    u16 size;

    oam.y = 161;
    oam.affineMode = 0;
    oam.objMode = 0;
    oam.mosaic = 0;
    oam.color256 = 0;
    oam.shape = 0;
    oam.x = 260;
    oam.affineIndexLow = 0;
    oam.hFlip = 0;
    oam.vFlip = 0;
    oam.size = 0;
    oam.tile = 0;
    oam.priority = 0;
    oam.palette = 0;

    if (oamCursor > 126)
        size = sizeof(gSpriteOamBuffer);
    else
    {
        size = oamCursor * sizeof(union SpriteOamEntry);
        size += sizeof(union SpriteOamEntry);
    }
    /* Re-form the address here: agbcc keeps the fill word before the attribute
     * object without keeping a source-address register live through its setup. */
    fillSource = &fillValue;
    *fillSource = *(u32 *)&oam;
    DmaSet(3, fillSource, gSpriteOamBuffer,
        (u32)(DMA_ENABLE | DMA_SRC_FIX | DMA_32BIT_BUS) | (size / 4));
    DmaWait(3);
}

/**
 * @brief 标记一个 OBJ 图块编号已在本帧上传
 *
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x0801B760
 */
void Battle_MarkUploadedTile(u16 tile)
{
    if (!Battle_IsTileUploaded(tile))
        sBattleUploadedTiles[tile >> 3] |= 1 << (tile & 7);
}

/**
 * @brief 查询一个 OBJ 图块编号是否已在本帧上传
 *
 * @address 0x0801B790
 */
u8 Battle_IsTileUploaded(u16 tile)
{
    if ((sBattleUploadedTiles[tile / 8] >> (tile % 8)) & 1)
        return 1;
    return 0;
}
/**
 * @brief 清空两段逐帧图形工作区
 *
 * @note 两段各 0x80 字节；03000518 用于 OBJ 图块上传去重，03000598 的具体用途待确认。
 * @address 0x0801B7B8
 */
void Battle_ClearUploadWorkspaces(void)
{
    u32 fillValue = 0;

    DmaSet(3, &fillValue, sBattleUploadedTiles,
        (u32)(DMA_ENABLE | DMA_SRC_FIX | DMA_32BIT_BUS)
            | (sizeof(sBattleUploadedTiles) / 4));
    DmaWait(3);
    fillValue = 0;
    DmaSet(3, &fillValue, sBattleUploadScratch,
        (u32)(DMA_ENABLE | DMA_SRC_FIX | DMA_32BIT_BUS)
            | (sizeof(sBattleUploadScratch) / 4));
    DmaWait(3);
}

/**
 * @brief 设置战斗动画的资源、坐标、初始图块和调色板槽
 *
 * @note 置加载标志，清零图形块进度与时间线帧；坐标按字节保存。
 * @note 资源赋值顺序保留调色板指针的提前写入；0x5A 字节函数体匹配，末尾两字节为对齐填充。
 * @address 0x0801B81C
 */
void Battle_SetupAnimation(struct BattleAnimation *animation, u8 x, u8 y,
    u16 initialBaseTile, u8 initialPaletteSlot, const u16 *definition,
    const u16 *palettes, u16 graphicsId, u16 graphicsBlockCount, u16 flags)
{
    animation->flags = flags | BATTLE_ANIM_LOADING;
    animation->graphicsId = graphicsId;
    animation->definition = definition;
    animation->palettes = palettes;
    animation->graphicsBlockCount = graphicsBlockCount;
    animation->initialBaseTile = initialBaseTile;
    animation->initialPaletteSlot = initialPaletteSlot;
    animation->graphicsBlock = 0;
    animation->timelineFrame = 0;
    animation->x = x;
    animation->y = y;
}

/**
 * @brief 按动画类型派发精灵或背景帧生成
 *
 * @address 0x0801B878
 */
u8 Battle_BuildAnimationFrame(struct BattleAnimation *animation, u8 oamCursor, u8 *affineCursor)
{
    switch (animation->flags & 0xF)
    {
    case 6:
    case 7:
    case 8:
        Battle_BuildBackgroundAnimation(animation);
        return oamCursor;
    default:
        return Battle_BuildSpriteAnimation(animation, oamCursor, affineCursor);
    }
}

/**
 * @brief 按动画类型派发 VBlank 图块上传
 *
 * @address 0x0801B8AC
 */
u8 Battle_UploadAnimationFrame(struct BattleAnimation *animation, u8 oamCursor)
{
    switch (animation->flags & 0xF)
    {
    case 6:
        Battle_UploadBackgroundAnimation(animation);
        return oamCursor;
    case 7:
    case 8:
        return oamCursor;
    default:
        return Battle_UploadSpriteAnimation(animation, oamCursor);
    }
}

/**
 * @brief 查找覆盖当前帧的四字节时间线记录
 *
 * @address 0x0801B8E8
 */
const struct BattleKeyframe * Battle_FindKeyframe(const u16 *track, u16 frame)
{
    return Battle_FindKeyframeInline(track, frame);
}

/**
 * @brief 选择指定动画轨道并查找当前帧对应的时间线记录
 *
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x0801B8FC
 */
const struct BattleKeyframe * Battle_GetTrackKeyframe(const struct BattleAnimation *animation, u8 track, u16 frame)
{
    const u16 *timeline = (const u16 *)((const u8 *)animation->timeline + animation->timelineOffsets[track]);

    return Battle_FindKeyframeInline(timeline, frame);
}

/**
 * @brief 把三十二组仿射矩阵交错写入软件 OAM 的第四半字
 *
 * @address 0x0801B920
 */
void Battle_InterleaveAffineMatrices(void)
{
    union SpriteOamEntry *oam = gSpriteOamBuffer;
    const struct SpriteAffineMatrix *matrix = gSpriteAffineMatrices;
    u8 index;

    for (index = 0; index < CORE_SPRITE_AFFINE_COUNT; index++)
    {
        oam->attributes.unknown06 = matrix->pa;
        oam++;
        oam->attributes.unknown06 = matrix->pb;
        oam++;
        oam->attributes.unknown06 = matrix->pc;
        oam++;
        oam->attributes.unknown06 = matrix->pd;
        oam++;
        matrix++;
    }
}

/**
 * @brief 读取动画图形表头中的调色板数量
 *
 * @address 0x0801B954
 */
u8 Battle_GetAnimationPaletteCount(const struct BattleAnimation *animation)
{
    return animation->frames->paletteCount;
}

/**
 * @brief 读取动画时间线表头中的总帧数
 *
 * @note 本轮运行未命中；说明依据参考汇编静态核对。
 * @address 0x0801B95C
 */
u16 Battle_GetAnimationFrameCount(const struct BattleAnimation *animation)
{
    return animation->timeline->frameCount;
}

extern const u16 D_08399B58[][2];
extern const u8 D_08393B14[];
extern const u8 D_08393A44[];
extern const u16 D_08556DD4[];
extern const u16 gBattleSpritePalettes[437][16];
extern const u16 D_0856B440[];
extern const u16 D_0856B494[];
extern const u16 *const gBattlePartyPetrifiedPalettes[BATTLE_CHARACTER_ID_COUNT];
extern const u16 *const gBattlePartyStatModifierPalettes[BATTLE_CHARACTER_ID_COUNT][4];
extern const u8 D_0839DF5C[8];
extern const u8 D_0861A004[];
extern const u8 D_0861A484[];
extern const u8 D_08619FE4[];
extern const u16 D_0861C744[];
extern const struct BattleSharedEffectResources D_0839B2B0[];
extern const struct BattleSharedEffectResources D_0839B2D4;

#define BATTLE_GAUGE_GRAPHICS ((u8 *)0x02021040)



/* Private .lcomm definitions, in original address order.
 * Reserved ranges preserve bytes with no identified object or access. */
static u16 sBattleCursorMoveDuration; /* 03000618 */
static u16 sBattleCursorMoveFrame; /* 0300061A */
static s16 sBattleCursorStartX; /* 0300061C */
static s16 sBattleCursorStartY; /* 0300061E */
static s16 sBattleCursorDeltaX; /* 03000620 */
static s16 sBattleCursorDeltaY; /* 03000622 */
static u8 sBattleCursorMoveCurve; /* 03000624 */
static u8 sBattleActionPhase; /* 03000625 */
static u8 sBattleReserved0626[2]; /* 03000626; no identified access. */
static u8 sBattleReserved0628[4]; /* 03000628; no identified access. */
static const struct BattleEncounter *sBattleEncounter; /* 0300062C */
static u8 sBattleEnemyCount; /* 03000630 */
static struct BattleCombatant *sBattleTurnOrder[BATTLE_COMBATANT_COUNT]; /* 03000638 */
static u8 sBattleTurnOrderCount; /* 03000668 */
static u8 sBattleTurnOrderIndex; /* 03000669 */
static struct BattleDamageNumber sBattleDamageNumbers[BATTLE_DAMAGE_NUMBER_COUNT]; /* 03000670 */
static u8 sBattleDamageNumberCount; /* 0300068C */
static u8 sBattleDamageNumberFrame; /* 0300068D */
static u8 sBattleDamageNumberBounce; /* 0300068E */
static struct BattleDrawNode sBattleFaintedList; /* 03000690 */
static struct BattleFaintedNode sBattleFaintedNodes[BATTLE_PARTY_COUNT]; /* 030006A0 */
static u8 sBattleFaintedCount; /* 030006F0 */
static struct BattleCombatant *sBattleDefeatQueue[BATTLE_ENEMY_COUNT]; /* 030006F8 */
static u8 sBattleDefeatQueueCount; /* 03000714 */
static u8 sBattleDefeatQueueIndex; /* 03000715 */
static u8 sBattleDefeatPhase; /* 03000716 */
static struct BattleCombatant *sBattleLastDefeatedEnemy; /* 03000718; id 11 is excluded. */
static u8 sBattleEffectPhase; /* 0300071C */
static u8 sBattleEffectTargets[BATTLE_COMBATANT_COUNT]; /* 03000720 */
static u8 sBattleEffectTargetCount; /* 0300072C */
static u8 sBattleActionTargets[BATTLE_COMBATANT_COUNT]; /* 03000730 */
static u8 sBattleValidTargetCount; /* 0300073C */
static u8 sBattleActionTargetCount; /* 0300073D */
/** @brief Number of completed action repetitions for the current actor.
 *  @note Phase 9 increments this before comparing with noa; this is not the
 *  hit index within a multi-hit animation.
 *  @address 0x0300073E
 */
static u8 sBattleActionRepeatCount;
/** @brief Weapon recovery scheduling state: zero unqueued, one pending, two done.
 *  @note Separate from the shared recovery animation phase sBattleEffectPhase.
 *  @address 0x0300073F
 */
static u8 sBattleWeaponRecoveryState;
/** @brief Weapon recovery selector: zero restores HP, one restores MP.
 *  @note Weapon effects 5/6 choose these values; 03000741 remains alignment space.
 *  @address 0x03000740
 */
static u8 sBattleWeaponRecoveryType;
/** @brief Accumulated attack damage, later converted in place to recovery amount.
 *  @note Sixteen-bit accumulation is divided by noa and ten, with minimum one
 *  for a nonzero total. This is not an invariant damage total or proven drain.
 *  @address 0x03000742
 */
static u16 sBattleWeaponRecoveryValue;
static u8 sBattleSharedPaletteSlot; /* 03000744 */
static u8 sBattleCharacterMarks[BATTLE_CHARACTER_ID_COUNT]; /* 03000748 */
static u8 sBattlePartyPaletteIds[BATTLE_CHARACTER_ID_COUNT]; /* 03000758 */
static u8 sBattlePartyPaletteCount; /* 03000763 */
/** @brief Unclassified action-state byte.
 *  @note The normal action queue writes 9 after action result 2; no consumer is confirmed.
 *  @address 0x03000764
 */
static u8 sBattleActionUnknown0764;
/** @brief Unclassified palette-related byte.
 *  @note Highlight setup writes animation.paletteSlot + 1; an allocation role is unproven.
 *  @address 0x03000765
 */
static u8 sBattleActionUnknown0765;

/* COMMON: shared EWRAM storage; positioning is in config/ram.ll.ld. */
struct BattleCombatant gBattleCombatants[BATTLE_COMBATANT_COUNT]; /* 02037028 */


/*
 * 0801B964..08021130：敌我角色记录、行动队列及角色动画。
 * 既处理 EWRAM 中的 12 条 0xC8 字节角色记录，也接收 scene 的光标记录。
 * 目标列表由本模块提供缓冲，经 rules 填充，再供 actions 读取。
 * 职责、运行覆盖和合并结论见 docs/BATTLE-MODULES.md（2026-11-11）。
 * 私有 .lcomm 的顺序和共享 .comm 分配保持不变，ROM 表由资源对象定义。
 */

/**
 * @brief 按遭遇变体和队伍成员初始化战斗角色、动画及调色板资源。
 * @note 先将 12 槽的 id 置 FF、status 清零；保留 partySlot 递增后检查 id 为 0
 *       的原顺序，firstCharacter 仅传给敌人初始化的未使用参数。
 *       enemyIdCount 用 u32 保存并在递增后截为 u8，保持原栈布局。
 *       普通 C 的 0x4CE 字节及已解析重定位匹配，末尾 2 字节零填充不计。
 * @address 0x0801B964
 */
void Battle_InitCombatants(struct BattleCombatant *combatants)
{
    u8 enemyIds[BATTLE_ENEMY_COUNT];
    u8 slots[BATTLE_ENEMY_COUNT];
    u8 i;
    u8 partySlot;
    u8 paletteSlot;
    u32 enemyIdCount;
    u16 baseTile;
    const struct BattleEnemyInfo *enemyInfo;
    struct BattleCombatant *firstCharacter;

    if (!(Battle_GetControlFlags() & 1))
    {
        sBattleEncounter = &gBattleEncounters[D_08399B58[Battle_GetEncounterId()][0]
            + Battle_NextRandom() % (D_08399B58[Battle_GetEncounterId()][1]
                - D_08399B58[Battle_GetEncounterId()][0] + 1)];
    }

    Battle_ResetPartyPaletteIds();
    sBattleEnemyCount = sBattleEncounter->enemyCount;
    for (i = 0; i < BATTLE_ENEMY_COUNT; i++)
        enemyIds[i] = 0;
    for (i = 0; i < BATTLE_COMBATANT_COUNT; i++)
    {
        combatants[i].id |= BATTLE_NO_COMBATANT;
        combatants[i].status = BATTLE_STATUS_NORMAL;
    }

    baseTile = 9;
    partySlot = 0;
    for (i = 0, paletteSlot = 1; i < BATTLE_PARTY_COUNT; i++)
    {
        if (gPartyMembers[i] != PARTY_NO_MEMBER)
        {
            u8 characterId;

            combatants[partySlot].formationSlot = Battle_GetPartyMemberFormationSlot(i);
            combatants[partySlot].action = BATTLE_ACTION_NONE;
            combatants[partySlot].displayState = BATTLE_DISPLAY_GAUGE_DIRTY;
            combatants[partySlot].animation.priority = 3;
            characterId = gPartyMembers[i];
            combatants[partySlot].id = characterId;
            combatants[partySlot].homeX = gBattlePartyHomeX[combatants[partySlot].formationSlot];
            combatants[partySlot].homeY = gBattlePartyHomeY[combatants[partySlot].formationSlot];
            combatants[partySlot].verticalLift = 0;
            if (combatants[partySlot].id != BATTLE_NO_COMBATANT)
            {
                Battle_AppendPartyPaletteId(gPartyMembers[i]);
                if (characterId != 0)
                    characterId--;
                Battle_InitPartyCombatantStats(&combatants[partySlot], &gPartyCharacters[characterId],
                    D_08393B14[combatants[partySlot].formationSlot]);
            }
            Battle_SetInitialCombatantAnimation(&combatants[partySlot], combatants[partySlot].homeX,
                combatants[partySlot].homeY, baseTile, paletteSlot);
            Battle_LoadAnimationBlock(&combatants[partySlot].animation);
            combatants[partySlot].animation.flags |= BATTLE_ANIM_HIDDEN;
            partySlot++;
            if (combatants[partySlot].id == 0)
                firstCharacter = &combatants[partySlot];
        }
        paletteSlot++;
        baseTile += 22;
    }

    for (enemyIdCount = 0; i < sBattleEncounter->enemyCount + BATTLE_PARTY_COUNT; i++)
    {
        u8 j;

        combatants[i].formationSlot = i - BATTLE_PARTY_COUNT;
        combatants[i].displayState = 0;
        combatants[i].action = BATTLE_ACTION_ATTACK;
        combatants[i].id = sBattleEncounter->enemies[i - BATTLE_PARTY_COUNT].enemyId;
        combatants[i].animation.priority = 3;
        combatants[i].targetSlot = 0;
        combatants[i].homeX = sBattleEncounter->enemies[combatants[i].formationSlot].x;
        combatants[i].homeY = sBattleEncounter->enemies[combatants[i].formationSlot].y;
        combatants[i].verticalLift = 0;
        for (j = 0; j < sBattleEncounter->enemyCount; j++)
        {
            if (enemyIds[j] == combatants[i].id)
                break;
        }
        if (j >= sBattleEncounter->enemyCount || sBattleEncounter->enemies[0].enemyId == 119)
        {
            paletteSlot++;
            combatants[i].animation.paletteSlot = paletteSlot;
        }
        else
        {
            combatants[i].animation.paletteSlot = combatants[j + BATTLE_PARTY_COUNT].animation.paletteSlot;
        }
        Battle_InitEnemyCombatantStats((u32)firstCharacter, &combatants[i],
            sBattleEncounter->enemies[i - BATTLE_PARTY_COUNT].formationBits);
        if (combatants[i].id <= 112)
            enemyInfo = combatants[i].source.enemy;
        Battle_SetInitialCombatantAnimation(&combatants[i], combatants[i].homeX,
            combatants[i].homeY, baseTile, combatants[i].animation.paletteSlot);
        enemyIds[enemyIdCount] = combatants[i].id;
        enemyIdCount = (u8)(enemyIdCount + 1);
        Battle_LoadAnimationBlock(&combatants[i].animation);
        combatants[i].animation.flags |= BATTLE_ANIM_HIDDEN;
        if (combatants[i].id <= 112)
            baseTile += D_08393A44[enemyInfo->sizeClass];
        else if (combatants[i].id == 119)
            baseTile += 96;
    }

    if (sBattleEncounter->enemies[0].enemyId == 119)
    {
        BattlePalette_StartObjCycle(combatants[5].animation.paletteSlot, 1, 3, -11, 5);
        BattlePalette_StartObjCycle(combatants[6].animation.paletteSlot, 1, 3, -11, 5);
    }
    sBattleSharedPaletteSlot = paletteSlot + 1;
    BattlePalette_LoadObjBanks(D_08556DD4, sBattleSharedPaletteSlot, 1);
    DmaCopy(3, D_0861A7E4, (void *)(OBJ_MODE0_VRAM + 0x25C0), 0x2C0, 32);
    DmaWait(3);
    sBattleActionPhase = 0;
    sBattleFaintedCount = 0;
    Battle_InitDrawList(&sBattleFaintedList);

    if (SystemFlags_GetMask(0x6D) && !SystemFlags_GetMask(0x6E))
    {
        u8 count = Battle_ListCombatants(combatants, slots, BATTLE_SIDE_PARTY, 1);

        if (count > 1)
        {
            for (i = 0; i < 2; i++)
                Battle_ApplyStatusWithAlternateAnimation(&combatants[slots[Battle_NextRandom() % count]], 2);
        }
        else
        {
            Battle_ApplyStatusWithAlternateAnimation(&combatants[slots[0]], 2);
        }
    }
}

/**
 * @brief 推进普通行动队列的 11 个阶段，处理重复行动、击倒、复活和武器恢复。
 * @note 返回 0 表示进行中，1 为本轮结束，2 为敌人清空，3 为队伍失能，4 为撤离。
 *       保留结果 0 的分支和结果 2/3 的独立 case；队列耗尽时直接退出当前阶段，
 *       复现原跳转树及索引重读。未使用实参仍按原调用传递。
 *       普通 C 的完整 0x650 字节（含字面池）及已解析重定位匹配。
 * @address 0x0801BE34
 */
u8 Battle_UpdateActionQueue(struct BattleCombatant *combatants)
{
    u8 result = 0;

    switch (sBattleActionPhase)
    {
    case 0:
        Battle_AdvanceFaintedTurnCounts();
        Battle_BuildTurnOrder(combatants);
        sBattleActionPhase = 1;
        Battle_ResetCharacterMarks();
        Battle_ChooseEnemyCommands(&combatants[BATTLE_PARTY_COUNT], combatants);
        sBattleActionRepeatCount = 0;
        sBattleWeaponRecoveryValue = 0;
        break;
    case 1:
        BattleItem_ResetAutoRevive();
        while ((u8)Battle_CheckStatusMask(sBattleTurnOrder[sBattleTurnOrderIndex], 0x11C)
                != BATTLE_STATUS_MATCH_REJECTED
            && sBattleTurnOrderIndex < sBattleTurnOrderCount)
            sBattleTurnOrderIndex++;
        if (sBattleTurnOrderIndex >= sBattleTurnOrderCount)
        {
            sBattleActionPhase = 9;
            break;
        }
        Battle_PrepareCombatantAction(sBattleTurnOrder[sBattleTurnOrderIndex], combatants);
        sBattleDamageNumberFrame = 0;
        Battle_ResetDamageNumbers(0);
        switch (sBattleTurnOrder[sBattleTurnOrderIndex]->action)
        {
        case 0:
        case 1:
        case 3:
        case 4:
            BattleAction_ResetState(0);
            break;
        case 2:
            BattleItem_ResetEffect();
            break;
        }
        sBattleActionPhase = 2;
        Battle_ResetDefeatQueue();
        sBattleWeaponRecoveryState = 0;
        break;
    case 2:
        switch (sBattleTurnOrder[sBattleTurnOrderIndex]->action)
        {
        case 0:
        case 1:
            switch (BattleAction_Update(sBattleTurnOrder[sBattleTurnOrderIndex],
                &combatants[sBattleTurnOrder[sBattleTurnOrderIndex]->targetSlot]))
            {
            case 0:
                break;
            case 1:
                sBattleWeaponRecoveryValue += BattleAction_GetLastAttackDamage();
                sBattleActionPhase = 3;
                break;
            case 2:
                sBattleActionPhase = 9;
                sBattleActionUnknown0764 = 9;
                break;
            case 3:
                if (Battle_ApplyActionTargetResults(sBattleTurnOrder[sBattleTurnOrderIndex],
                    &combatants[sBattleTurnOrder[sBattleTurnOrderIndex]->targetSlot]))
                    sBattleActionPhase = 5;
                else
                    sBattleActionPhase = 9;
                break;
            case 4:
                Battle_ApplyActionTargetResults(sBattleTurnOrder[sBattleTurnOrderIndex],
                    &combatants[sBattleTurnOrder[sBattleTurnOrderIndex]->targetSlot]);
                sBattleActionPhase = 9;
                break;
            }
            break;
        case 4:
            if (BattleAction_UpdateCombatantEscape(sBattleTurnOrder[sBattleTurnOrderIndex]) == 1)
            {
                Battle_InitStatusTilemap();
                sBattleActionPhase = 9;
            }
            break;
        case 2:
            switch (BattleItem_UpdateAction(sBattleTurnOrder[sBattleTurnOrderIndex], combatants))
            {
            case 0:
                break;
            case 1:
                sBattleActionPhase = 9;
                break;
            case 2:
                sBattleActionPhase = 3;
                break;
            }
            break;
        case 3:
            sBattleActionPhase = 9;
            break;
        default:
            sBattleActionPhase = 9;
            break;
        }
        break;
    case 3:
        Battle_BuildActionDamageNumbers(sBattleTurnOrder[sBattleTurnOrderIndex],
            &combatants[sBattleTurnOrder[sBattleTurnOrderIndex]->targetSlot]);
        sBattleActionPhase = 4;
        break;
    case 4:
        if (Battle_UpdateDamageNumbers() == 1)
        {
            if (Battle_ApplyActionTargetResults(sBattleTurnOrder[sBattleTurnOrderIndex],
                &combatants[sBattleTurnOrder[sBattleTurnOrderIndex]->targetSlot]))
                sBattleActionPhase = 5;
            else
                sBattleActionPhase = 9;
        }
        break;
    case 5:
        switch (Battle_ProcessDefeatQueue())
        {
        case 0:
            break;
        case 1:
            if (BattleItem_GetAutoReviveCount() != 0)
                sBattleActionPhase = 7;
            else
                sBattleActionPhase = 9;
            break;
        case 2:
            sBattleActionPhase = 6;
            break;
        case 3:
            sBattleActionPhase = 6;
            break;
        }
        break;
    case 6:
        if (Battle_UpdateQueuedEnemyDefeat())
            sBattleActionPhase = 9;
        break;
    case 7:
        if (BattleItem_UpdateAutoRevive() == 1)
            sBattleActionPhase = 9;
        break;
    case 8:
        if ((u8)Battle_UpdateCombatantRecoveryEffect(sBattleTurnOrder[sBattleTurnOrderIndex],
            sBattleWeaponRecoveryType, sBattleWeaponRecoveryValue) == 1)
        {
            sBattleWeaponRecoveryState = 2;
            sBattleActionPhase = 9;
        }
        break;
    case 9:
        Battle_UpdateStatusPaletteOffset(&combatants[sBattleTurnOrder[sBattleTurnOrderIndex]->targetSlot], 0);
        sBattleActionRepeatCount++;
        if (sBattleTurnOrder[sBattleTurnOrderIndex]->id <= 10
            && sBattleTurnOrder[sBattleTurnOrderIndex]->action == BATTLE_ACTION_ATTACK)
        {
            if (sBattleActionRepeatCount >= sBattleTurnOrder[sBattleTurnOrderIndex]->noa
                || sBattleEnemyCount == 0)
            {
                if (sBattleWeaponRecoveryState == 0)
                {
                    switch (Battle_GetWeaponAttackEffect(sBattleTurnOrder[sBattleTurnOrderIndex]))
                    {
                    case 5:
                        Battle_ResetEffectPhase();
                        sBattleWeaponRecoveryState = 1;
                        sBattleWeaponRecoveryType = BATTLE_RECOVERY_HP;
                        if (sBattleWeaponRecoveryValue != 0)
                        {
                            sBattleWeaponRecoveryValue /= sBattleTurnOrder[sBattleTurnOrderIndex]->noa;
                            sBattleWeaponRecoveryValue /= 10;
                            if (sBattleWeaponRecoveryValue == 0)
                                sBattleWeaponRecoveryValue = 1;
                        }
                        break;
                    case 6:
                        Battle_ResetEffectPhase();
                        sBattleWeaponRecoveryState = 1;
                        sBattleWeaponRecoveryType = BATTLE_RECOVERY_MP;
                        if (sBattleWeaponRecoveryValue != 0)
                        {
                            sBattleWeaponRecoveryValue /= sBattleTurnOrder[sBattleTurnOrderIndex]->noa;
                            sBattleWeaponRecoveryValue /= 10;
                            if (sBattleWeaponRecoveryValue == 0)
                                sBattleWeaponRecoveryValue = 1;
                        }
                        break;
                    default:
                        Battle_AddAttackGauge(sBattleTurnOrder[sBattleTurnOrderIndex]);
                        do
                        {
                            sBattleTurnOrderIndex++;
                        } while ((u8)Battle_CheckStatusMask(sBattleTurnOrder[sBattleTurnOrderIndex], 0x11C)
                                != BATTLE_STATUS_MATCH_REJECTED
                            && sBattleTurnOrderIndex < sBattleTurnOrderCount);
                        sBattleActionRepeatCount = 0;
                        sBattleWeaponRecoveryValue = 0;
                        break;
                    }
                }
                else
                {
                    Battle_AddAttackGauge(sBattleTurnOrder[sBattleTurnOrderIndex]);
                    do
                    {
                        sBattleTurnOrderIndex++;
                    } while ((u8)Battle_CheckStatusMask(sBattleTurnOrder[sBattleTurnOrderIndex], 0x11C)
                            != BATTLE_STATUS_MATCH_REJECTED
                        && sBattleTurnOrderIndex < sBattleTurnOrderCount);
                    sBattleActionRepeatCount = 0;
                }
            }
        }
        else
        {
            do
            {
                sBattleTurnOrderIndex++;
            } while ((u8)Battle_CheckStatusMask(sBattleTurnOrder[sBattleTurnOrderIndex], 0x11C)
                    != BATTLE_STATUS_MATCH_REJECTED
                && sBattleTurnOrderIndex < sBattleTurnOrderCount);
            sBattleActionRepeatCount = 0;
        }

        if (sBattleWeaponRecoveryState == 1)
            sBattleActionPhase = 8;
        else if (sBattleEnemyCount == 0)
        {
            sBattleActionPhase = 0;
            result = 2;
        }
        else if (sBattleTurnOrderIndex < sBattleTurnOrderCount)
            sBattleActionPhase = 1;
        else
            sBattleActionPhase = 10;
        if (Battle_HasRemainingPartyAfterEscape(combatants) == 1)
        {
            if (Battle_ArePartyMembersDisabled(combatants))
            {
                sBattleActionPhase = 0;
                result = 3;
            }
        }
        else
        {
            sBattleActionPhase = 0;
            result = 4;
        }
        break;
    case 10:
        Battle_ResetCharacterMarks();
        sBattleActionPhase = 0;
        result = 1;
        break;
    }
    return result;
}

/** @brief 推进控制标志 0x20 选择的替代行动队列。
 *  @note 编号大于 112 的敌人也按 noa 重复行动；攻击结果 2/3 直接转入阶段 9。
 *  保留行动 case 3、结果 case 0 及击倒结果 2/3 的独立分支，复现原比较树。
 *  返回 0 表示进行中，1 为本轮结束，2 为敌人清空，3 为队伍失能。
 *  普通 C 的完整 0x584 字节（含字面池）及已解析重定位匹配。
 *  @address 0x0801C484
 */
u8 Battle_UpdateActionQueueAlternate(struct BattleCombatant *combatants)
{
    u8 result = 0;

    switch (sBattleActionPhase)
    {
    case 0:
        Battle_AdvanceFaintedTurnCounts();
        Battle_BuildTurnOrder(combatants);
        sBattleActionPhase = 1;
        Battle_ResetCharacterMarks();
        Battle_ChooseEnemyCommands(&combatants[BATTLE_PARTY_COUNT], combatants);
        sBattleActionRepeatCount = 0;
        sBattleWeaponRecoveryValue = 0;
        break;
    case 1:
        while ((u8)Battle_CheckStatusMask(sBattleTurnOrder[sBattleTurnOrderIndex], 0x11C)
                != BATTLE_STATUS_MATCH_REJECTED
            && sBattleTurnOrderIndex < sBattleTurnOrderCount)
            sBattleTurnOrderIndex++;
        if (sBattleTurnOrderIndex >= sBattleTurnOrderCount)
        {
            sBattleActionPhase = 9;
            break;
        }
        Battle_PrepareCombatantAction(sBattleTurnOrder[sBattleTurnOrderIndex], combatants);
        sBattleDamageNumberFrame = 0;
        Battle_ResetDamageNumbers(0);
        switch (sBattleTurnOrder[sBattleTurnOrderIndex]->action)
        {
        case 0:
        case 1:
        case 3:
        case 4:
            BattleAction_ResetState(0);
            break;
        case 2:
            BattleItem_ResetEffect();
            break;
        }
        sBattleActionPhase = 2;
        Battle_ResetDefeatQueue();
        sBattleWeaponRecoveryState = 0;
        break;
    case 2:
        switch (sBattleTurnOrder[sBattleTurnOrderIndex]->action)
        {
        case 0:
        case 1:
            switch (BattleAction_Update(sBattleTurnOrder[sBattleTurnOrderIndex],
                &combatants[sBattleTurnOrder[sBattleTurnOrderIndex]->targetSlot]))
            {
            case 0:
                break;
            case 1:
                sBattleWeaponRecoveryValue += BattleAction_GetLastAttackDamage();
                sBattleActionPhase = 3;
                break;
            case 2:
            case 3:
                sBattleActionPhase = 9;
                break;
            }
            break;
        case 2:
            switch (BattleItem_UpdateAction(sBattleTurnOrder[sBattleTurnOrderIndex], combatants))
            {
            case 0:
                break;
            case 1:
                sBattleActionPhase = 9;
                break;
            case 2:
                sBattleActionPhase = 3;
                break;
            }
            break;
        case 3:
            sBattleActionPhase = 9;
            break;
        default:
            sBattleActionPhase = 9;
            break;
        }
        break;
    case 3:
        Battle_BuildActionDamageNumbers(sBattleTurnOrder[sBattleTurnOrderIndex],
            &combatants[sBattleTurnOrder[sBattleTurnOrderIndex]->targetSlot]);
        sBattleActionPhase = 4;
        break;
    case 4:
        if (Battle_UpdateDamageNumbers() == 1)
        {
            if (Battle_ApplyActionTargetResults(sBattleTurnOrder[sBattleTurnOrderIndex],
                &combatants[sBattleTurnOrder[sBattleTurnOrderIndex]->targetSlot]))
                sBattleActionPhase = 5;
            else
                sBattleActionPhase = 9;
        }
        break;
    case 5:
        switch (Battle_ProcessDefeatQueue())
        {
        case 0:
            break;
        case 1:
            sBattleActionPhase = 9;
            break;
        case 2:
            sBattleActionPhase = 6;
            break;
        case 3:
            sBattleActionPhase = 6;
            break;
        }
        break;
    case 6:
        if (Battle_UpdateQueuedEnemyDefeat())
            sBattleActionPhase = 9;
        break;
    case 8:
        if ((u8)Battle_UpdateCombatantRecoveryEffect(sBattleTurnOrder[sBattleTurnOrderIndex],
            sBattleWeaponRecoveryType, sBattleWeaponRecoveryValue) == 1)
        {
            sBattleWeaponRecoveryState = 2;
            sBattleActionPhase = 9;
        }
        break;
    case 9:
        sBattleActionRepeatCount++;
        if (sBattleTurnOrder[sBattleTurnOrderIndex]->id <= 10
            && sBattleTurnOrder[sBattleTurnOrderIndex]->action == BATTLE_ACTION_ATTACK)
        {
            if (sBattleActionRepeatCount >= sBattleTurnOrder[sBattleTurnOrderIndex]->noa
                || sBattleEnemyCount == 0)
            {
                if (sBattleWeaponRecoveryState == 0)
                {
                    switch (Battle_GetWeaponAttackEffect(sBattleTurnOrder[sBattleTurnOrderIndex]))
                    {
                    case 5:
                        Battle_ResetEffectPhase();
                        sBattleWeaponRecoveryState = 1;
                        sBattleWeaponRecoveryType = BATTLE_RECOVERY_HP;
                        if (sBattleWeaponRecoveryValue != 0)
                        {
                            sBattleWeaponRecoveryValue /= sBattleTurnOrder[sBattleTurnOrderIndex]->noa;
                            sBattleWeaponRecoveryValue /= 10;
                            if (sBattleWeaponRecoveryValue == 0)
                                sBattleWeaponRecoveryValue = 1;
                        }
                        break;
                    case 6:
                        Battle_ResetEffectPhase();
                        sBattleWeaponRecoveryState = 1;
                        sBattleWeaponRecoveryType = BATTLE_RECOVERY_MP;
                        if (sBattleWeaponRecoveryValue != 0)
                        {
                            sBattleWeaponRecoveryValue /= sBattleTurnOrder[sBattleTurnOrderIndex]->noa;
                            sBattleWeaponRecoveryValue /= 10;
                            if (sBattleWeaponRecoveryValue == 0)
                                sBattleWeaponRecoveryValue = 1;
                        }
                        break;
                    default:
                        Battle_AddAttackGauge(sBattleTurnOrder[sBattleTurnOrderIndex]);
                        do
                        {
                            sBattleTurnOrderIndex++;
                        } while ((u8)Battle_CheckStatusMask(sBattleTurnOrder[sBattleTurnOrderIndex], 0x11C)
                                != BATTLE_STATUS_MATCH_REJECTED
                            && sBattleTurnOrderIndex < sBattleTurnOrderCount);
                        sBattleActionRepeatCount = 0;
                        sBattleWeaponRecoveryValue = 0;
                        break;
                    }
                }
                else
                {
                    Battle_AddAttackGauge(sBattleTurnOrder[sBattleTurnOrderIndex]);
                    do
                    {
                        sBattleTurnOrderIndex++;
                    } while ((u8)Battle_CheckStatusMask(sBattleTurnOrder[sBattleTurnOrderIndex], 0x11C)
                            != BATTLE_STATUS_MATCH_REJECTED
                        && sBattleTurnOrderIndex < sBattleTurnOrderCount);
                    sBattleActionRepeatCount = 0;
                }
            }
        }
        else if (sBattleTurnOrder[sBattleTurnOrderIndex]->id > 112)
        {
            if (sBattleActionRepeatCount >= sBattleTurnOrder[sBattleTurnOrderIndex]->noa)
            {
                do
                {
                    sBattleTurnOrderIndex++;
                } while ((u8)Battle_CheckStatusMask(sBattleTurnOrder[sBattleTurnOrderIndex], 0x11C)
                        != BATTLE_STATUS_MATCH_REJECTED
                    && sBattleTurnOrderIndex < sBattleTurnOrderCount);
                sBattleActionRepeatCount = 0;
            }
        }
        else
        {
            do
            {
                sBattleTurnOrderIndex++;
            } while ((u8)Battle_CheckStatusMask(sBattleTurnOrder[sBattleTurnOrderIndex], 0x11C)
                    != BATTLE_STATUS_MATCH_REJECTED
                && sBattleTurnOrderIndex < sBattleTurnOrderCount);
            sBattleActionRepeatCount = 0;
        }

        if (sBattleWeaponRecoveryState == 1)
            sBattleActionPhase = 8;
        else if (sBattleEnemyCount == 0)
        {
            sBattleActionPhase = 0;
            result = 2;
        }
        else if (sBattleTurnOrderIndex < sBattleTurnOrderCount)
            sBattleActionPhase = 1;
        else
            sBattleActionPhase = 10;
        if (Battle_ArePartyMembersDisabled(combatants))
        {
            sBattleActionPhase = 0;
            result = 3;
        }
        break;
    case 10:
        Battle_ResetCharacterMarks();
        sBattleActionPhase = 0;
        result = 1;
        break;
    }
    return result;
}

/** @brief 按模式选择普通敌人的动画，并设置攻击时序和接近宽度。
 *  @note 有效模式为 0..5；模式 0/5 在非零状态时改用基础动画加 2，状态 5 追加标志 0x20。
 *  模式 3/4 复制资源表的两项时序值，并分别使用攻击/技能接近宽度。
 *  直接访问全局资源表，保留各模式分支及字面池布局；普通 C 的完整 0x19C 字节及重定位匹配。
 *  @address 0x0801CA08
 */
void Battle_SetEnemyAnimation(struct BattleCombatant *combatant, u8 mode, u16 baseTile,
    u8 paletteSlot, u8 animationOffset)
{
    const struct BattleEnemyInfo *enemy = combatant->source.enemy;
    u16 animationId;
    u16 flags;

    switch (mode)
    {
    case 0:
        animationId = enemy->animationBaseId + mode;
        flags = 0x409;
        if (combatant->status != BATTLE_STATUS_NORMAL)
            animationId = enemy->animationBaseId + 2;
        break;
    case 5:
        animationId = enemy->animationBaseId;
        flags = 0x401;
        if (combatant->status != BATTLE_STATUS_NORMAL)
            animationId += 2;
        break;
    case 1:
        animationId = enemy->animationBaseId + mode;
        flags = 0x409;
        break;
    case 2:
        animationId = enemy->animationBaseId + mode;
        flags = 0x409;
        break;
    case 3:
        animationId = mode + animationOffset + enemy->animationBaseId;
        flags = 2;
        combatant->hitReactionStartFrame = gBattleAnimationConfigs[animationId].hitReactionStartFrame;
        combatant->hitEffectId = gBattleAnimationConfigs[animationId].hitEffectId;
        combatant->attackSpacingWidth = enemy->attackSpacingWidth;
        break;
    case 4:
        animationId = mode + animationOffset + enemy->animationBaseId;
        flags = 2;
        combatant->hitReactionStartFrame = gBattleAnimationConfigs[animationId].hitReactionStartFrame;
        combatant->hitEffectId = gBattleAnimationConfigs[animationId].hitEffectId;
        combatant->attackSpacingWidth = enemy->skillSpacingWidth;
        break;
    }
    if (combatant->status == BATTLE_STATUS_CONFUSED)
        flags |= 0x20;
    Battle_SetupAnimation(&combatant->animation, combatant->animation.x, combatant->animation.y,
        baseTile, paletteSlot, (const u16 *)gBattleAnimationConfigs[animationId].animationData,
        gBattleAnimationConfigs[animationId].palettes, gBattleAnimationConfigs[animationId].graphicsId,
        gBattleAnimationConfigs[animationId].graphicsBlockCount, flags);
}

/** @brief 按武器动作、状态和模式选择队伍成员的动画与调色板。
 *  @note 模式 0/4/7 共用标志赋值以保持原寄存器分配；模式 9 使用资源自带调色板。
 *  模式 4 保留角色标记查询和返回值截断，两个结果均选择状态动画；模式 5 未设置有效资源。
 *  普通 C 的完整 0x2DC 字节（含字面池）及已解析重定位匹配。
 *  @address 0x0801CBA4
 */
void Battle_SetPartyAnimation(struct BattleCombatant *combatant, u8 mode, u16 baseTile,
    u8 paletteSlot, u8 hitIndex)
{
    const struct BattleCharacterActionInfo *character = &gBattleCharacterActionInfo[combatant->id];
    const struct BattleAnimationResourceConfig *config;
    const u16 *palettes;
    u16 animationId;
    u16 flags;

    switch (mode)
    {
    case 0:
        if ((u8)(combatant->status - BATTLE_STATUS_PARALYZED) <= 2)
            animationId = character->statusAnimationId;
        else if (combatant->action == BATTLE_ACTION_DEFEND)
        {
            if (Battle_IsCharacterMarked(combatant) == 1)
                animationId = character->statusAnimationId;
            else
                animationId = character->defendAnimationId;
        }
        else if (combatant->status != BATTLE_STATUS_NORMAL)
            animationId = character->statusAnimationId;
        else
            animationId = character->idleAnimationIds[gBattleWeaponActionInfo[combatant->equipment[0]].actionVariant];
        config = &gBattleAnimationConfigs[animationId];
        goto setLoopingFlags;
    case 10:
        if (combatant->action == BATTLE_ACTION_DEFEND)
            animationId = character->defendAnimationId;
        else if (combatant->status != BATTLE_STATUS_NORMAL)
            animationId = character->statusAnimationId;
        else
            animationId = character->idleAnimationIds[gBattleWeaponActionInfo[combatant->equipment[0]].actionVariant];
        config = &gBattleAnimationConfigs[animationId];
        flags = 0x401;
        if (combatant->status == BATTLE_STATUS_CONFUSED)
            flags |= 0x20;
        break;
    case 1:
        config = &gBattleAnimationConfigs[character->attackAnimationBaseIds[
            gBattleWeaponActionInfo[combatant->equipment[0]].actionVariant] + hitIndex];
        flags = 2;
        combatant->hitReactionStartFrame = config->hitReactionStartFrame;
        combatant->hitEffectId = config->hitEffectId;
        if (combatant->status == BATTLE_STATUS_CONFUSED && combatant->targetSlot <= 10)
            flags = 0x22;
        break;
    case 2:
        config = &gBattleAnimationConfigs[character->animationId06];
        flags = 0x409;
        break;
    case 3:
        config = &gBattleAnimationConfigs[character->animationId07];
        flags = 0x409;
        break;
    case 4:
        if (combatant->action == BATTLE_ACTION_DEFEND
            && (u8)(combatant->status - BATTLE_STATUS_PARALYZED) > 2
            && Battle_IsCharacterMarked(combatant) != 1)
            animationId = character->statusAnimationId;
        else
            animationId = character->statusAnimationId;
        config = &gBattleAnimationConfigs[animationId];
        goto setLoopingFlags;
    case 6:
        animationId = character->animationId10;
        config = &gBattleAnimationConfigs[animationId];
        flags = 2;
        if (combatant->status == BATTLE_STATUS_CONFUSED)
            flags = 0x22;
        break;
    case 7:
        if ((u8)(combatant->status - BATTLE_STATUS_PARALYZED) <= 2 || Battle_IsCharacterMarked(combatant) == 1)
            animationId = character->statusAnimationId;
        else
            animationId = character->defendAnimationId;
        config = &gBattleAnimationConfigs[animationId];
    setLoopingFlags:
        flags = 0x409;
        if (combatant->status == BATTLE_STATUS_CONFUSED)
            flags |= 0x20;
        break;
    case 8:
        config = &gBattleAnimationConfigs[character->escapeAnimationId];
        flags = 0x402;
        break;
    case 9:
        config = &gBattleAnimationConfigs[character->animationId16];
        flags = 0x401;
        break;
    }

    if (combatant->status == BATTLE_STATUS_PETRIFIED && mode != 9)
    {
        palettes = gBattlePartyPetrifiedPalettes[combatant->id];
    }
    else
    {
        u8 variant = Battle_GetStatModifierPaletteVariant(combatant);

        if (variant == 0 || mode == 9)
        {
            palettes = config->palettes;
        }
        else
        {
            variant--;
            palettes = gBattlePartyStatModifierPalettes[combatant->id][variant];
        }
    }
    Battle_SetupAnimation(&combatant->animation, combatant->animation.x, combatant->animation.y,
        baseTile, paletteSlot, (const u16 *)config->animationData, palettes,
        config->graphicsId, config->graphicsBlockCount, flags);
}

/** @brief 从扩展敌人记录选择 Boss 动画，并设置攻击或技能时序。
 *  @note 模式 0/6 按状态选择动画，1/5 复制两项时序；编号 120 追加标志 0x20。
 *  模式 3/4 仅设置标志，保留原路径未初始化资源指针的行为。
 *  普通 C 的完整 0x110 字节（含字面池）及已解析重定位匹配。
 *  @address 0x0801CE80
 */
void Battle_SetBossAnimation(struct BattleCombatant *combatant, u8 mode, u16 baseTile,
    u8 paletteSlot, u8 skillIndex)
{
    const struct BattleBossInfo *boss = combatant->source.boss;
    const struct BattleAnimationResourceConfig *config;
    u16 animationId;
    u16 flags;

    switch (mode)
    {
    case 0:
        if (combatant->status != BATTLE_STATUS_NORMAL)
            animationId = boss->unknown04[1];
        else
            animationId = boss->idleAnimationId;
        config = &gBattleAnimationConfigs[animationId];
        flags = 0x409;
        break;
    case 6:
        if (combatant->status != BATTLE_STATUS_NORMAL)
            animationId = boss->unknown04[1];
        else
            animationId = boss->idleAnimationId;
        config = &gBattleAnimationConfigs[animationId];
        flags = 0x401;
        break;
    case 3:
    case 4:
        flags = 0x401;
        break;
    case 1:
        config = &gBattleAnimationConfigs[boss->attackAnimationId];
        flags = 2;
        combatant->hitReactionStartFrame = config->hitReactionStartFrame;
        combatant->hitEffectId = config->hitEffectId;
        break;
    case 2:
        animationId = boss->unknown04[0];
        config = &gBattleAnimationConfigs[animationId];
        flags = 0x409;
        break;
    case 5:
        config = &gBattleAnimationConfigs[boss->skillAnimationIds[skillIndex]];
        flags = 2;
        combatant->hitReactionStartFrame = config->hitReactionStartFrame;
        combatant->hitEffectId = config->hitEffectId;
        break;
    }
    if (combatant->id == 120)
        flags |= 0x20;
    Battle_SetupAnimation(&combatant->animation, combatant->animation.x, combatant->animation.y,
        baseTile, paletteSlot, (const u16 *)config->animationData, config->palettes,
        config->graphicsId, config->graphicsBlockCount, flags);
}

/** @brief 绘制队员 HP、MP 和名称，并刷新标记为脏的气槽图块。
 *  @note 每槽生成四个 4bpp 图块，共 0x80 字节；高、低半字节分别比较
 *        tile * 8 + pair * 2 与气槽值、气槽值加一，完成后清除显示掩码 0x2 和 0x1。
 *        像素合成保留 u32 临时量；缓冲区使用原 EWRAM 固定地址，保持寻址顺序。
 *        普通 C 的 0x19C 字节及已解析重定位与原 ROM 完全一致。
 *  @address 0x0801CF90
 */
void Battle_DrawPartyStatus(struct BattleCombatant *combatant, u8 slot)
{
    u8 colors[8];
    u8 labelPalette;
    u8 hpPalette;
    u8 mpPalette;
    u8 paletteBase;

    memcpy(colors, D_0839DF5C, sizeof(colors));
    if (combatant->displayState & BATTLE_DISPLAY_STATUS_HIGHLIGHT)
    {
        labelPalette = combatant->statusHighlightPalette;
        hpPalette = labelPalette;
        mpPalette = labelPalette;
        paletteBase = 0;
    }
    else
    {
        Battle_UpdateStatusPaletteOffset(combatant, 0);
        hpPalette = combatant->statusPaletteOffset;
        labelPalette = 0;
        if (hpPalette == 1)
        {
            labelPalette = 1;
            mpPalette = 1;
        }
        else if (hpPalette == 2)
        {
            labelPalette = 2;
            mpPalette = 2;
        }
        else
        {
            if (combatant->mp == combatant->maxMp)
                mpPalette = 3;
            else
                mpPalette = 0;
        }
        paletteBase = 3;
    }

    Battle_DrawStatusNumber(combatant->hp, slot * 5 + 5, 16, hpPalette, paletteBase);
    Battle_DrawStatusNumber(combatant->mp, slot * 5 + 5, 17, mpPalette, paletteBase);
    Battle_DrawStatusLabels(slot, slot * 5 + 4, 18,
        labelPalette, hpPalette, mpPalette, paletteBase);

    if (combatant->displayState & BATTLE_DISPLAY_GAUGE_DIRTY)
    {
        u8 *out = BATTLE_GAUGE_GRAPHICS + slot * 0x80;
        u16 tile;
        u8 row;
        u8 pair;
        u32 highNibble;
        u32 packedPixels;

        for (tile = 0; tile < 4; tile++)
        {
            for (row = 0; row < 8; row++)
            {
                for (pair = 0; pair < 4; pair++)
                {
                    if (tile * 8 + pair * 2 < combatant->source.party.specialGauge)
                        highNibble = colors[row] << 4;
                    else
                        highNibble = 0xF0;
                    if (tile * 8 + pair * 2 < combatant->source.party.specialGauge + 1)
                        packedPixels = highNibble + colors[row];
                    else
                        packedPixels = highNibble + 0xF;
                    *out++ = packedPixels;
                }
            }
        }
        combatant->displayState &= ~BATTLE_DISPLAY_GAUGE_DIRTY;
    }
    if (combatant->displayState & 1)
        combatant->displayState &= ~1;
}

/** @brief 根据队员状态和 HP 更新状态栏调色板偏移。
 *  @note 仅处理 id 0..10；模式 0..2 在状态 0 且 HP 全满时选 3，
 *        状态 1..7 选 1、状态 8 选 2。模式 5 只应用状态覆盖；其余模式原样写入。
 *        保留模式 4 及模式 5 下状态 0 的空 case；0x6E 字节匹配，末尾两字节零对齐不计。
 *  @address 0x0801D12C
 */
void Battle_UpdateStatusPaletteOffset(struct BattleCombatant *combatant, u8 mode)
{
    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
    {
        switch (mode)
        {
        case 0:
        case 1:
        case 2:
            switch (combatant->status)
            {
            case 0:
                if (combatant->hp == combatant->maxHp)
                    mode = 3;
                break;
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
                mode = 1;
                break;
            case 8:
                mode = 2;
                break;
            }
            break;
        case 4:
            break;
        case 5:
            switch (combatant->status)
            {
            case 0:
                break;
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
                mode = 1;
                break;
            case 8:
                mode = 2;
                break;
            }
            break;
        }
        combatant->statusPaletteOffset = mode;
    }
}

/** @brief 根据队员状态取得高亮效果使用的 BG 调色板 bank。
 *  @note 先将 mode 加三并截为 u8，再按结果分支；结果 0..2 时状态 0 且
 *        HP 全满选 6、状态 1..7 选 4、状态 8 选 5，结果 5 时状态 1..7 选 1。
 *        仅允许 id 0..10；保留原函数在敌人路径未定义返回值的行为。
 *        普通 C 的 0x76 字节匹配，末尾两字节零对齐不计。
 *  @address 0x0801D19C
 */
u8 Battle_GetStatusPaletteBank(const struct BattleCombatant *combatant, u8 mode)
{
    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
    {
        mode += 3;
        switch (mode)
        {
        case 0:
        case 1:
        case 2:
            switch (combatant->status)
            {
            case 0:
                if (combatant->hp == combatant->maxHp)
                    mode = 6;
                break;
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
                mode = 4;
                break;
            case 8:
                mode = 5;
                break;
            }
            break;
        case 4:
            break;
        case 5:
            switch (combatant->status)
            {
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
                mode = 1;
                break;
            }
            break;
        }
        return mode;
    }
}

/** @brief 绘制可见队员的计量条，上传共用图块及 OBJ 调色板，并返回递减的 OAM 游标。
 *  @note 跳过空槽和 displayState 的 0x20 位；坐标为 (32+40*slot, 144)，图块为 0x144+4*slot。
 *        32x8、16 色 OBJ 使用优先级 2、调色板 6；OamData 以两个字复制，AffineParam 未赋值。
 *        unusedStack 保留原 0x20 字节空栈帧；官方 DMA3 上传 0x280 字节并等待后加载调色板。
 *        普通 C 的完整 0x164 字节（含字面池）及已解析重定位 100% 匹配。
 *  @address 0x0801D214
 */
u8 Battle_DrawPartyGauges(struct BattleCombatant *combatants, u8 oamCursor)
{
    OamData sprite;
    u8 unusedStack[0x20];
    u8 slot = 0;
    const u16 *palettes = D_0861C744;

    for (; slot < BATTLE_PARTY_COUNT; slot++)
    {
        if (combatants[slot].id != BATTLE_NO_COMBATANT
            && !(combatants[slot].displayState & 0x20))
        {
            OamData *oam = (OamData *)&gSpriteOamBuffer[oamCursor];

            sprite.VPos = 144;
            sprite.AffineMode = ST_OAM_AFFINE_NONE;
            sprite.ObjMode = ST_OAM_OBJ_NORMAL;
            sprite.Mosaic = 0;
            sprite.ColorMode = ST_OAM_COLOR_16;
            sprite.Shape = ST_OAM_H_RECTANGLE;
            sprite.HPos = 32 + slot * 40;
            sprite.AffineParamNo_L = 0;
            sprite.HFlip = 0;
            sprite.VFlip = 0;
            sprite.Size = 1;
            sprite.CharNo = 0x144 + slot * 4;
            sprite.Priority = 2;
            sprite.Pltt = 6;
            *oam = sprite;
            oamCursor -= 1;
        }
    }

    DmaCopy(3, BATTLE_GAUGE_GRAPHICS, OBJ_GRAPHICS[0x144], 0x280, 32);
    DmaWait(3);
    BattlePalette_LoadObjBanks(palettes, 6, 1);
    return oamCursor;
}

/** @brief 在 homeX-16、homeY-5 写入地面精灵，并返回递减后的八位 OAM 游标。
 *  @note 使用 SDK OamData 设置 32x8、16 色、优先级 3 的非仿射 OBJ；id 0..10
 *  选择图块 0x140，其余选择 0x12E。主动画 0x8000 位选择覆盖调色板，0x10 位
 *  控制半透明；ObjMode 先转为 u8，保留原掩码指令。普通 C 的全部 0xF0 字节
 *  （含字面池）及 OAM 缓冲区重定位匹配。
 *  @address 0x0801D378
 */
u8 Battle_DrawCombatantGroundSprite(struct BattleCombatant *combatant, u8 oamCursor)
{
    u8 palette;
    OamData *oam;

    if (combatant->animation.flags & 0x8000)
        palette = combatant->animation.initialPaletteSlot;
    else
        palette = combatant->animation.paletteSlot;

    oam = (OamData *)&gSpriteOamBuffer[oamCursor];
    oam->VPos = combatant->homeY - 5;
    oam->AffineMode = ST_OAM_AFFINE_NONE;
    oam->ObjMode = (u8)((combatant->animation.flags >> 4) & 1);
    oam->Mosaic = 0;
    oam->ColorMode = ST_OAM_COLOR_16;
    oam->Shape = ST_OAM_H_RECTANGLE;
    oam->HPos = combatant->homeX - 16;
    oam->AffineParamNo_L = 0;
    oam->HFlip = 0;
    oam->VFlip = 0;
    oam->Size = 1;
    oam->CharNo = combatant->id < BATTLE_CHARACTER_ID_COUNT ? 0x140 : 0x12E;
    oam->Priority = 3;
    oam->Pltt = palette;
    oamCursor -= 1;
    return oamCursor;
}

/**
 * @brief 筛选两侧角色并按含修正的敏捷生成回合行动队列
 * @note 参数未使用；以 0xE3 筛选并分别排序，再合并排序；控制位 0x1000 置位时跳过队伍侧并清除此位。
 *       合并时先写槽位，再递增 u8 计数；最终只更新有效指针，行动索引清零。
 *       普通 C 的 0x100 字节（含字面池）及十一项已解析重定位 100% 匹配。
 * @address 0x0801D468
 */
void Battle_BuildTurnOrder(struct BattleCombatant *unused)
{
    u8 partySlots[BATTLE_PARTY_COUNT];
    u8 enemySlots[BATTLE_ENEMY_COUNT];
    u8 slots[BATTLE_COMBATANT_COUNT];
    u8 count = 0;
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 partyCount = Battle_ListCombatants(combatants, partySlots, BATTLE_SIDE_PARTY, 0xE3);
    u8 enemyCount = Battle_ListCombatants(combatants, enemySlots, BATTLE_SIDE_ENEMY, 0xE3);
    u8 i;

    Battle_SortCombatantSlots(partySlots, partyCount, BATTLE_SORT_AGL);
    Battle_SortCombatantSlots(enemySlots, enemyCount, BATTLE_SORT_AGL);
    if (Battle_GetControlFlags() & 0x1000)
        Battle_ClearControlFlags(0x1000);
    else
    {
        for (i = 0; i < partyCount; i++)
        {
            slots[count] = partySlots[i];
            count++;
        }
    }
    for (i = 0; i < enemyCount; i++)
    {
        slots[count] = enemySlots[i];
        count++;
    }
    Battle_SortCombatantSlots(slots, count, BATTLE_SORT_AGL);
    for (i = 0; i < count; i++)
        sBattleTurnOrder[i] = &combatants[slots[i]];
    sBattleTurnOrderIndex = 0;
    sBattleTurnOrderCount = count;
}

/** @brief 上传伤害数字图块并追加原伤害值及显示坐标。
 *  @note 显示值按无符号半字截到 999，前导零改为空白；记录保留原 pendingDamage。
 *        u16 数字数组配合 s16 强转，分支内复用循环下标 i，保留原读取及寄存器分配。
 *        数字图块使用首图块基址 -0x60；全部传输使用官方 DMA 宏。
 *        官方 DMA 宏的读回和等待顺序一致；普通 C 的完整 0x1A8 字节及重定位 100% 匹配。
 *  @address 0x0801D568
 */
void Battle_AddDamageNumber(struct BattleCombatant *combatant)
{
    u16 digits[3];
    u16 tile;
    u16 damage;
    u16 i;

    tile = sBattleDamageNumberCount * 4 + 0x158;
    damage = combatant->pendingDamage;
    if (damage > 999)
        damage = 999;
    digits[0] = damage / 100;
    digits[1] = (damage - (s16)digits[0] * 100) / 10;
    digits[2] = damage - ((s16)digits[0] * 100 + (s16)digits[1] * 10);

    DmaCopy(3, D_0861A004, OBJ_GRAPHICS[tile], 0x20, 32);
    DmaWait(3);
    tile++;
    for (i = 0; i <= 2; i++)
    {
        switch (i)
        {
        case 0:
            if ((s16)digits[i] == 0)
                digits[i] = 0xFFFF | digits[0];
            break;
        case 1:
            if ((s16)digits[i] == 0 && (s16)digits[0] <= 0)
                digits[i] = 0xFFFF | digits[i];
            break;
        case 2:
            break;
        }
        DmaCopy(3, D_0861A004 - 0x60 + ((s16)digits[i] + 40) * 32,
            OBJ_GRAPHICS[tile + i], 0x20, 32);
        DmaWait(3);
    }

    sBattleDamageNumbers[sBattleDamageNumberCount].value = combatant->pendingDamage;
    sBattleDamageNumbers[sBattleDamageNumberCount].x = combatant->homeX - 16;
    sBattleDamageNumbers[sBattleDamageNumberCount].y = combatant->homeY - 8;
    sBattleDamageNumberCount++;
}

/** @brief 按模式上传行动伤害数字或固定结果字形，并记录原伤害及坐标。
 *  @note 模式零显示截到 999 的三位数字并隐藏前导零；非零模式使用四块固定字形。
 *        记录保留原 pendingDamage；数字数组复用循环下标 i，以 s16 强转读取。
 *        目标采用整数地址表达式，保留官方 DMA 宏的地址计算、读回及等待顺序。
 *        普通 C 的完整 0x274 字节（含字面池）及解析后的重定位 100% 匹配。
 *  @address 0x0801D710
 */
void Battle_AddActionDamageNumber(struct BattleCombatant *combatant, u8 mode)
{
    u16 digits[3];
    u16 tile;
    u16 damage;
    u16 i;

    tile = sBattleDamageNumberCount * 4 + 0x158;
    damage = combatant->pendingDamage;
    if (mode == 0)
    {
        if (damage > 999)
            damage = 999;
        digits[0] = damage / 100;
        digits[1] = (damage - (s16)digits[0] * 100) / 10;
        digits[2] = damage - ((s16)digits[0] * 100 + (s16)digits[1] * 10);

        DmaCopy(3, D_0861A004, (void *)(OBJ_MODE0_VRAM + tile * 32), 0x20, 32);
        DmaWait(3);
        tile++;
        for (i = 0; i <= 2; i++)
        {
            switch (i)
            {
            case 0:
                if ((s16)digits[i] == 0)
                    digits[i] = 0xFFFF | digits[0];
                break;
            case 1:
                if ((s16)digits[i] == 0 && (s16)digits[0] <= 0)
                    digits[i] = 0xFFFF | digits[i];
                break;
            case 2:
                break;
            }
            DmaCopy(3, D_0861A004 - 0x60 + ((s16)digits[i] + 40) * 32,
                (void *)(OBJ_MODE0_VRAM + (tile + i) * 32), 0x20, 32);
            DmaWait(3);
        }
    }
    else
    {
        DmaCopy(3, D_0861A484, (void *)(OBJ_MODE0_VRAM + tile * 32), 0x20, 32);
        DmaWait(3);
        DmaCopy(3, D_0861A484, (void *)(OBJ_MODE0_VRAM + (tile + 1) * 32), 0x20, 32);
        DmaWait(3);
        DmaCopy(3, D_0861A484 - 0x360, (void *)(OBJ_MODE0_VRAM + (tile + 2) * 32), 0x20, 32);
        DmaWait(3);
        DmaCopy(3, D_08619FE4, (void *)(OBJ_MODE0_VRAM + (tile + 3) * 32), 0x20, 32);
        DmaWait(3);
    }

    sBattleDamageNumbers[sBattleDamageNumberCount].value = combatant->pendingDamage;
    sBattleDamageNumbers[sBattleDamageNumberCount].x = combatant->homeX - 16;
    sBattleDamageNumbers[sBattleDamageNumberCount].y = combatant->homeY - 8;
    sBattleDamageNumberCount++;
}

/** @brief 将已上传的伤害数字写入软件 OAM，返回递减后的八位游标。
 *  @note 反弹高度先截为 u16，再转为 s16 传入插值；u8 插值结果以 u32 保存，
 *  保留原 Y 加法指令。s8 索引为每条目生成 32x8、16 色 OBJ，图块为
 *  0x158+4*index，调色板 15、优先级 0。普通 C 的完整 0x11C 字节及重定位匹配。
 *  @address 0x0801D984
 */
u8 Battle_DrawDamageNumbers(u8 oamCursor)
{
    u32 yOffset;
    s8 index;

    if (sBattleDamageNumberCount != 0)
    {
        u8 bounce = sBattleDamageNumberBounce;
        u16 height = -((20 - bounce * 2) / bounce);

        if (bounce <= 2)
            yOffset = (u8)Battle_InterpolateS16(0, (s16)height,
                9 - bounce, sBattleDamageNumberFrame * 2, 3);
        else
            yOffset = 0;
    }

    for (index = 0; index < sBattleDamageNumberCount; index++)
    {
        OamData *oam = (OamData *)&gSpriteOamBuffer[oamCursor];

        oam->VPos = (u8)(yOffset + sBattleDamageNumbers[index].y);
        oam->AffineMode = ST_OAM_AFFINE_NONE;
        oam->ObjMode = ST_OAM_OBJ_NORMAL;
        oam->Mosaic = 0;
        oam->ColorMode = ST_OAM_COLOR_16;
        oam->Shape = ST_OAM_H_RECTANGLE;
        oam->HPos = sBattleDamageNumbers[index].x;
        oam->AffineParamNo_L = 0;
        oam->HFlip = 0;
        oam->VFlip = 0;
        oam->Size = 1;
        oam->CharNo = 0x158 + index * 4;
        oam->Priority = 0;
        oam->Pltt = 15;
        oamCursor -= 1;
    }

    return oamCursor;
}

/** @brief Advance damage-number bounce timing and clear completed presentations.
 *  @note Returns one on completion, clears seven records and releases the number palette.
 *  The bounce phase uses signed remainder over a ten-step cycle; palette table
 *  load and bank release share bank 15. Ordinary C matches all 0x9C bytes.
 *  @address 0x0801DAA0
 */
u8 Battle_UpdateDamageNumbers(void)
{
    u8 result;

    result = 0;
    if (sBattleDamageNumberBounce <= 2)
    {
        sBattleDamageNumberFrame = (sBattleDamageNumberFrame + 1) % 
            (10 - sBattleDamageNumberBounce);
        if (sBattleDamageNumberFrame >= 9 - sBattleDamageNumberBounce)
            sBattleDamageNumberBounce++;
    }
    else if (sBattleDamageNumberBounce <= 0x22)
        sBattleDamageNumberBounce++;
    else
    {
        u8 index;
        const u16 *palettes;

        result = 1;
        sBattleDamageNumberCount = 0;
        sBattleDamageNumberBounce = result;
        sBattleDamageNumberFrame = 0;
        index = 0;
        palettes = D_0861C664;
        for (; index < BATTLE_DAMAGE_NUMBER_COUNT; index++)
        {
            sBattleDamageNumbers[index].value = 0;
            sBattleDamageNumbers[index].x = 0;
            sBattleDamageNumbers[index].y = 0;
        }
        BattlePalette_LoadObjBanks(palettes, 15, 1);
        BattlePalette_ReleaseObjBanks(15, 1);
    }

    return result;
}

/** @brief Select and start a secondary combatant effect animation.
 *  @note Effect ids above two index the shared animation configuration table;
 *  ids 0..2 use the twelve-byte resource array at 0x0839B2B0 with a 0x543
 *  graphics base and a homeY offset (0x10, or half the combatant height above
 *  id 10). The palette variant advances 16 u16 entries, i.e. 32 bytes; the
 *  resource pointer is computed only after the height branch. Ordinary C
 *  matches all 0xE4 bytes.
 *  @address 0x0801DB3C
 */
void Battle_StartCombatantEffect(struct BattleCombatant *combatant, u8 timelineVariant, u16 effectId)
{
    if (effectId <= 2)
    {
        const struct BattleSharedEffectResources *resources;
        u8 offset;

        if (combatant->id <= 10)
            offset = 0x10;
        else
            offset = Battle_GetCombatantDimension(combatant, BATTLE_COMBATANT_HEIGHT) >> 1;
        resources = &D_0839B2B0[effectId];
        Battle_SetupAnimation(&combatant->effectAnimation, combatant->homeX,
            combatant->homeY - offset, 0x2B4, 0xE,
            resources->definition, resources->palettes + timelineVariant * 16,
            0x543 + resources->graphicsId, resources->graphicsBlockCount, 4);
    }
    else
    {
        const struct BattleAnimationResourceConfig *config = &gBattleAnimationConfigs[effectId];

        Battle_SetupAnimation(&combatant->effectAnimation, combatant->homeX,
            combatant->homeY, 0x300, 0xE,
            (const u16 *)config->animationData, config->palettes,
            config->graphicsId, config->graphicsBlockCount, 4);
    }

    combatant->effectAnimation.priority = 3;
    combatant->displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
}

/** @brief Queue a defeated combatant for the fainted draw list and clear its action state.
 *  @note The party stat modifiers are cleared first, then the record's character id
 *  is matched against the 0x7F-mask party list. The matching list entry supplies
 *  the fainted-node index through slots[index], not the list position. Party ids
 *  up to six also clear the special gauge and set display bit 2. Ordinary C
 *  matches all 0xE4 bytes.
 *  @address 0x0801DC20
 */
void Battle_AddFaintedCombatant(struct BattleCombatant *combatant, u8 priority)
{
    u8 slots[BATTLE_ENEMY_COUNT];
    struct BattleCombatant *combatants;
    u8 count;
    u8 index;

    Battle_ClearPartyStatModifiers(combatant);
    combatants = Battle_GetCombatants();
    count = Battle_ListCombatants(combatants, slots, BATTLE_SIDE_PARTY, 0x7F);

    for (index = 0; index < count; index++)
    {
        if (combatant->id == combatants[slots[index]].id)
            break;
    }

    sBattleFaintedNodes[slots[index]].combatant = combatant;
    Battle_InitDrawNode(&sBattleFaintedNodes[slots[index]], priority);
    Battle_InsertDrawNode(&sBattleFaintedList, &sBattleFaintedNodes[slots[index]]);
    Battle_ApplyStatus(combatant, 8);
    combatant->pendingDamage = 0;
    BattleItem_QueueAutoRevive(combatant);

    if (combatant->id <= 6)
    {
        combatant->source.party.specialGauge = 0;
        combatant->displayState |= 2;
    }

    sBattleFaintedCount++;
}

/** @brief 从倒地链表摘除指定槽，写入复活 HP 并恢复待机动画及状态调色板。
 *  @note 清状态和待结算伤害，行动设为普通攻击；HP 直接写入，倒地计数非零才递减。
 *  slot 同时决定图块和调色板编号；普通 C 的完整 0xAC 字节及已解析重定位 100% 匹配。
 *  @address 0x0801DD04
 */
void Battle_ReviveCombatant(struct BattleCombatant *combatant, u8 slot, u16 hp)
{
    u16 baseTile;
    u8 paletteSlot;

    sBattleFaintedNodes[slot].link.previous->next = sBattleFaintedNodes[slot].link.next;
    sBattleFaintedNodes[slot].link.next->previous = sBattleFaintedNodes[slot].link.previous;
    sBattleFaintedNodes[slot].link.previous = 0;
    sBattleFaintedNodes[slot].link.next = 0;
    combatant->status = BATTLE_STATUS_NORMAL;
    combatant->pendingDamage = 0;
    combatant->hp = hp;
    combatant->action = BATTLE_ACTION_ATTACK;
    if (sBattleFaintedCount != 0)
        sBattleFaintedCount--;

    baseTile = slot * 22 + 9;
    paletteSlot = slot + 1;
    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
        Battle_SetPartyAnimation(combatant, BATTLE_COMBATANT_ANIM_IDLE, baseTile, paletteSlot, 0);
    else if (combatant->id <= 112)
        Battle_SetEnemyAnimation(combatant, 0, baseTile, paletteSlot, 0);
    else if ((u8)(combatant->id - 113) <= 141)
        Battle_SetBossAnimation(combatant, BATTLE_COMBATANT_ANIM_IDLE, baseTile, paletteSlot, 0);
    Battle_UpdateStatusPaletteOffset(combatant, 0);
}

/** @brief Start a centered secondary animation and enable its OBJ-window display mode.
 *  @note Sets up the effect at (120,80), then modifies DISPCNT and WINOUT; this is an MMIO effect, not an ordinary sprite-only setup.
 *  The resource record supplies definition, variant palette, graphics id and
 *  block count; priority 3, animation flag 0x80 and display bit 0x2000 are set
 *  after setup. Ordinary C matches all 0x94 bytes.
 *  @address 0x0801DDB0
 */
void Battle_StartObjWindowEffect(struct BattleCombatant *combatant, u8 timelineVariant)
{
    const struct BattleSharedEffectResources *config = &D_0839B2D4;

    Battle_SetupAnimation(&combatant->effectAnimation, 120, 80, 0x1B4, 0xE,
        config->definition,
        config->palettes + timelineVariant * 16,
        0x549 + config->graphicsId,
        config->graphicsBlockCount, 0x402);
    combatant->effectAnimation.priority = 3;
    combatant->effectAnimation.flags |= 0x80;
    combatant->displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
    *(vu16 *)REG_DISPCNT |= DISP_OBJWIN_ON;
    *(vu16 *)REG_WINOUT |= 0x1400;
}

/** @brief 清空伤害数字槽并加载调色板，按行动目标列表追加数字或固定结果字形。
 *  @note 两个参数未使用；低四位选择战斗员，高四位全零时使用固定字形，否则显示伤害数字。
 *        重置计数、反弹阶段和动画帧；两个循环使用独立 u8 下标，保留递增截断。
 *        普通 C 的完整 0x98 字节（含字面池）及已解析重定位 100% 匹配。
 *  @address 0x0801DE44
 */
void Battle_BuildActionDamageNumbers(struct BattleCombatant *unusedActor, struct BattleCombatant *unusedTarget)
{
    u8 index;
    u8 targetIndex;
    const u16 *palettes;
    struct BattleCombatant *combatants;

    sBattleDamageNumberCount = 0;
    sBattleDamageNumberBounce = 1;
    sBattleDamageNumberFrame = 0;
    index = 0;
    palettes = D_0861C664;
    for (; index < BATTLE_DAMAGE_NUMBER_COUNT; index++)
    {
        sBattleDamageNumbers[index].value = 0;
        sBattleDamageNumbers[index].x = 0;
        sBattleDamageNumbers[index].y = 0;
    }
    BattlePalette_LoadObjBanks(palettes, 15, 1);

    combatants = Battle_GetCombatants();
    for (targetIndex = 0; targetIndex < sBattleActionTargetCount; targetIndex++)
    {
        u8 mode = 0;

        if ((sBattleActionTargets[targetIndex] & BATTLE_TARGET_FLAGS_MASK) == 0)
            mode = 1;
        Battle_AddActionDamageNumber(
            &combatants[sBattleActionTargets[targetIndex] & BATTLE_TARGET_SLOT_MASK], mode);
    }
}

/** @brief 按普通敌人攻击或技能的动画配置追加单目标或整侧伤害数字。
 *  @note 动画编号为基础编号加三，技能再加 alternateAnimationOffset，结果截为 u16。
 *        整侧按首槽 id 选择五/七槽，跳过空槽和状态八；两种模式均清零数字动画帧。
 *        u32 分步计算后赋给 u16 编号，保留原读取顺序；完整 0xB4 字节及重定位匹配。
 *  @address 0x0801DEDC
 */
void Battle_AddEnemyActionDamageNumbers(const struct BattleCombatant *actor, struct BattleCombatant *targets)
{
    const struct BattleAnimationResourceConfig *config;
    u32 animationValue;
    u16 animationId;
    u8 count;
    u8 i;

    switch (actor->action)
    {
    case BATTLE_ACTION_ATTACK:
        animationValue = actor->source.enemy->animationBaseId + 3;
        animationId = animationValue;
        config = &gBattleAnimationConfigs[animationId];
        break;
    case BATTLE_ACTION_SKILL:
        animationValue = actor->source.enemy->animationBaseId + 3;
        animationValue += actor->source.enemy->alternateAnimationOffset;
        animationId = animationValue;
        config = &gBattleAnimationConfigs[animationId];
        break;
    }

    switch (config->targetMode)
    {
    case 0:
        Battle_AddDamageNumber(targets);
        sBattleDamageNumberFrame = 0;
        break;
    case 1:
        if (targets->id < BATTLE_CHARACTER_ID_COUNT)
            count = BATTLE_PARTY_COUNT;
        else
            count = BATTLE_ENEMY_COUNT;
        for (i = 0; i < count; i++)
        {
            if (targets[i].id != BATTLE_NO_COMBATANT && targets[i].status != BATTLE_STATUS_DEFEATED)
                Battle_AddDamageNumber(&targets[i]);
        }
        sBattleDamageNumberFrame = 0;
        break;
    }
}

/** @brief 按扩展敌人攻击或所选技能的动画配置追加单目标或整侧伤害数字。
 *  @note 仅适用于攻击/技能；整侧模式按首槽 id 选择五/七槽，跳过空槽和状态八。
 *        显式 if/else 赋槽数，循环索引保留 u8 截断；两种模式均清零数字动画帧。
 *        普通 C 的完整 0xB0 字节（含字面池）及解析后的重定位 100% 匹配。
 *  @address 0x0801DF90
 */
void Battle_AddBossActionDamageNumbers(const struct BattleCombatant *actor, struct BattleCombatant *targets)
{
    const struct BattleAnimationResourceConfig *config;
    u8 count;
    u8 i;

    switch (actor->action)
    {
    case BATTLE_ACTION_ATTACK:
        config = &gBattleAnimationConfigs[actor->source.boss->attackAnimationId];
        break;
    case BATTLE_ACTION_SKILL:
        config = &gBattleAnimationConfigs[actor->source.boss->skillAnimationIds[actor->enemySkillIndex]];
        break;
    }

    switch (config->targetMode)
    {
    case 0:
        Battle_AddDamageNumber(targets);
        sBattleDamageNumberFrame = 0;
        break;
    case 1:
        if (targets->id < BATTLE_CHARACTER_ID_COUNT)
            count = BATTLE_PARTY_COUNT;
        else
            count = BATTLE_ENEMY_COUNT;
        for (i = 0; i < count; i++)
        {
            if (targets[i].id != BATTLE_NO_COMBATANT && targets[i].status != BATTLE_STATUS_DEFEATED)
                Battle_AddDamageNumber(&targets[i]);
        }
        sBattleDamageNumberFrame = 0;
        break;
    }
}

static inline void Battle_SetDefeatHitReactionInline(struct BattleCombatant *combatant)
{
    Battle_UpdateStatusPaletteOffset(combatant, 0);
    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
        Battle_SetPartyAnimation(combatant, 2, combatant->animation.baseTile,
            combatant->animation.paletteSlot, 0);
    else if (combatant->id <= 112)
        Battle_SetEnemyAnimation(combatant, 1, combatant->animation.baseTile,
            combatant->animation.paletteSlot, 0);
    else if ((u8)(combatant->id - 113) <= 141)
        Battle_SetBossAnimation(combatant, 2, combatant->animation.baseTile,
            combatant->animation.paletteSlot, 0);
}

/** @brief 每次处理一个击倒队列项，处理完后返回首项的角色类别。
 *  @note 每次处理一项返回 0；随后按首项 id 返回队员 1、普通敌人 2 或扩展敌人 3，
 *        并复位索引；空队列返回 0，扩展敌人同时复位移除特效。
 *        两条敌人路径分别内联受击动画、递减敌人数、隐藏状态图标并写入状态 8。
 *        保留调用后 id 重读与 u8 区间截断；普通 C 按原地址独立链接，
 *        完整 0x198 字节（含字面池）及解析后的重定位 100% 匹配。
 *  @address 0x0801E040
 */
u8 Battle_ProcessDefeatQueue(void)
{
    struct BattleCombatant *combatant;
    u8 result = 0;

    if (sBattleDefeatQueueIndex < sBattleDefeatQueueCount)
    {
        combatant = sBattleDefeatQueue[sBattleDefeatQueueIndex];
        if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
        {
            Battle_AddFaintedCombatant(combatant, 0);
            if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
                Battle_SetPartyAnimation(combatant, 3, combatant->animation.baseTile,
                    combatant->animation.paletteSlot, 0);
        }
        else if (combatant->id <= 112)
        {
            Battle_SetDefeatHitReactionInline(combatant);
            sBattleEnemyCount--;
            combatant->displayState |= BATTLE_DISPLAY_HIDE_STATUS_ICON;
            Battle_ApplyStatus(combatant, 8);
        }
        else
        {
            Battle_SetDefeatHitReactionInline(combatant);
            sBattleEnemyCount--;
            combatant->displayState |= BATTLE_DISPLAY_HIDE_STATUS_ICON;
            Battle_ApplyStatus(combatant, 8);
        }
        sBattleDefeatQueueIndex++;
    }
    else
    {
        if (sBattleDefeatQueueCount != 0)
        {
            if (sBattleDefeatQueue[0]->id < BATTLE_CHARACTER_ID_COUNT)
                result = 1;
            else if (sBattleDefeatQueue[0]->id <= 112)
                result = 2;
            else
            {
                BattleAction_ResetRemovalEffect();
                result = 3;
            }
        }
        sBattleDefeatQueueIndex = 0;
    }

    return result;
}

static inline u8 Battle_ApplyPendingDamageInline(struct BattleCombatant *combatant)
{
    if ((s16)combatant->hp - combatant->pendingDamage <= 0)
    {
        combatant->hp = 0;
        return 1;
    }

    combatant->hp -= combatant->pendingDamage;
    return 0;
}

static inline void Battle_QueueDefeatedCombatantInline(struct BattleCombatant *target)
{
    sBattleDefeatQueue[sBattleDefeatQueueCount] = target;
    sBattleDefeatQueueCount++;
    if (target->id > 11)
        sBattleLastDefeatedEnemy = target;
}

/** @brief 按编码目标列表结算伤害，登记击倒并向存活目标施加待处理状态。
 *  @note 两个参数未使用；目标项高半字节非零才处理，低半字节为角色槽号。
 *        HP 与待结算伤害按有符号半字判定；存活且状态非零时先标记再施加状态。
 *        保留 getter 调用和未访问的十二字节局部数组以复现原空栈帧；返回是否存在击倒。
 *        普通 C 的 0x132 字节及按原地址独立链接后的重定位 100% 匹配，末尾两字节零填充不计。
 *  @address 0x0801E1D8
 */
u8 Battle_ApplyActionTargetResults(struct BattleCombatant *unusedActor, struct BattleCombatant *unusedTarget)
{
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 unusedTargets[BATTLE_COMBATANT_COUNT];
    u8 defeatedCount;
    u8 i;

    BattleAction_GetPendingStatuses();
    defeatedCount = 0;
    for (i = 0; i < sBattleActionTargetCount; i++)
    {
        if (sBattleActionTargets[i] & BATTLE_TARGET_FLAGS_MASK)
        {
            if (Battle_ApplyPendingDamageInline(
                &combatants[sBattleActionTargets[i] & BATTLE_TARGET_SLOT_MASK]))
            {
                Battle_QueueDefeatedCombatantInline(
                    &combatants[sBattleActionTargets[i] & BATTLE_TARGET_SLOT_MASK]);
                defeatedCount++;
            }
            else if (gBattleActionPendingStatuses[sBattleActionTargets[i] & BATTLE_TARGET_SLOT_MASK] != 0)
            {
                Battle_MarkCharacter(&combatants[sBattleActionTargets[i] & BATTLE_TARGET_SLOT_MASK]);
                Battle_ApplyStatus(&combatants[sBattleActionTargets[i] & BATTLE_TARGET_SLOT_MASK],
                    gBattleActionPendingStatuses[sBattleActionTargets[i] & BATTLE_TARGET_SLOT_MASK]);
            }
        }
    }

    if (defeatedCount != 0)
        return 1;
    return 0;
}

/** @brief 按行动动画的目标模式结算伤害、登记击倒并返回是否存在击倒。
 *  @note 原入口未初始化所用表指针；C 中保留 character 的未初始化状态，
 *        当前编译结果使用进入函数时的 sb 值作为表基址。
 *        HP 判定使用有符号半字；击倒标志统一写入，队列助手内联以保留地址求值顺序。
 *        普通 C 的 0x1C6 字节及解析后的重定位按原地址独立链接 100% 匹配。
 *        末尾两字节零填充不计；实际函数地址和 RAM 布局已核对。
 *  @address 0x0801E30C
 */
u8 Battle_ApplyActionDamageFromCallerTable(const struct BattleCombatant *actor, struct BattleCombatant *targets)
{
    u8 defeated[BATTLE_ENEMY_COUNT];
    const struct BattleCharacterActionInfo *character;
    const struct BattleAnimationResourceConfig *config;
    u8 result = 0;
    u8 i;
    u8 count;
    u8 targetDefeated;

    for (i = 0; i < BATTLE_ENEMY_COUNT; i++)
        defeated[i] = 0;

    switch (actor->action)
    {
    case BATTLE_ACTION_ATTACK:
        config = &gBattleAnimationConfigs[character->attackAnimationBaseIds[
            gBattleWeaponActionInfo[actor->equipment[0]].actionVariant]];
        break;
    case BATTLE_ACTION_SKILL:
        config = &gBattleAnimationConfigs[character->attackAnimationBaseIds[
            actor->skills[actor->enemySkillIndex]]];
        break;
    }

    switch (config->targetMode)
    {
    case 0:
        if ((s16)targets->hp - targets->pendingDamage <= 0)
        {
            targets->hp = 0;
            targetDefeated = 1;
        }
        else
        {
            targets->hp -= targets->pendingDamage;
            targetDefeated = 0;
        }
        defeated[0] = targetDefeated;
        if (defeated[0] != 0)
        {
            Battle_QueueDefeatedCombatantInline(targets);
        }
        break;
    case 1:
        if (targets->id > 10)
            count = BATTLE_ENEMY_COUNT;
        else
            count = BATTLE_PARTY_COUNT;
        for (i = 0; i < count; i++)
        {
            if (targets[i].id != BATTLE_NO_COMBATANT && targets[i].status != BATTLE_STATUS_DEFEATED)
            {
                if ((s16)targets[i].hp - targets[i].pendingDamage <= 0)
                {
                    targets[i].hp = 0;
                    targetDefeated = 1;
                }
                else
                {
                    targets[i].hp -= targets[i].pendingDamage;
                    targetDefeated = 0;
                }
                defeated[i] = targetDefeated;
                if (defeated[i] != 0)
                {
                    Battle_QueueDefeatedCombatantInline(&targets[i]);
                }
            }
        }
        break;
    }
    for (i = 0; i < BATTLE_ENEMY_COUNT; i++)
    {
        if (defeated[i] == 1)
        {
            result = defeated[i];
            break;
        }
    }
    return result;
}

/** @brief 按普通敌人的动画目标模式结算伤害并登记击倒。
 *  @note 动画编号分步计算后截为 u16；HP 判定按有符号半字，队列写入保留内联顺序。
 *  普通 C 的 0x1BA 字节及已解析重定位 100% 匹配，末尾两字节零填充不计。
 *  @address 0x0801E4D4
 */
u8 Battle_ApplyEnemyActionDamage(const struct BattleCombatant *actor, struct BattleCombatant *targets)
{
    u8 defeated[BATTLE_ENEMY_COUNT];
    const struct BattleAnimationResourceConfig *config;
    u32 animationValue;
    u16 animationId;
    u8 result = 0;
    u8 i;
    u8 count;
    u8 targetDefeated;

    for (i = 0; i < BATTLE_ENEMY_COUNT; i++)
        defeated[i] = 0;

    switch (actor->action)
    {
    case BATTLE_ACTION_ATTACK:
        animationValue = actor->source.enemy->animationBaseId + 3;
        animationId = animationValue;
        config = &gBattleAnimationConfigs[animationId];
        break;
    case BATTLE_ACTION_SKILL:
        animationValue = actor->source.enemy->animationBaseId + 3;
        animationValue += actor->source.enemy->alternateAnimationOffset;
        animationId = animationValue;
        config = &gBattleAnimationConfigs[animationId];
        break;
    }
    switch (config->targetMode)
    {
    case 0:
        if ((s16)targets->hp - targets->pendingDamage <= 0)
        {
            targets->hp = 0;
            targetDefeated = 1;
        }
        else
        {
            targets->hp -= targets->pendingDamage;
            targetDefeated = 0;
        }
        defeated[0] = targetDefeated;
        if (defeated[0] != 0)
        {
            Battle_QueueDefeatedCombatantInline(targets);
        }
        break;
    case 1:
        if (targets->id > 10)
            count = BATTLE_ENEMY_COUNT;
        else
            count = BATTLE_PARTY_COUNT;
        for (i = 0; i < count; i++)
        {
            if (targets[i].id != BATTLE_NO_COMBATANT && targets[i].status != BATTLE_STATUS_DEFEATED)
            {
                if ((s16)targets[i].hp - targets[i].pendingDamage <= 0)
                {
                    targets[i].hp = 0;
                    targetDefeated = 1;
                }
                else
                {
                    targets[i].hp -= targets[i].pendingDamage;
                    targetDefeated = 0;
                }
                defeated[i] = targetDefeated;
                if (defeated[i] != 0)
                {
                    Battle_QueueDefeatedCombatantInline(&targets[i]);
                }
            }
        }
        break;
    }
    for (i = 0; i < BATTLE_ENEMY_COUNT; i++)
    {
        if (defeated[i] == 1)
        {
            result = defeated[i];
            break;
        }
    }
    return result;
}

/** @brief 按 Boss 攻击或所选技能的动画目标模式结算伤害并登记击倒。
 *  @note 整侧按首槽 id 选择五/七槽，跳过空槽和状态八；保留击倒标志数组及队列写入顺序。
 *  普通 C 的 0x1B6 字节及已解析重定位 100% 匹配，末尾两字节零填充不计。
 *  @address 0x0801E690
 */
u8 Battle_ApplyBossActionDamage(const struct BattleCombatant *actor, struct BattleCombatant *targets)
{
    u8 defeated[BATTLE_ENEMY_COUNT];
    const struct BattleAnimationResourceConfig *config;
    u8 result = 0;
    u8 i;
    u8 count;
    u8 targetDefeated;

    for (i = 0; i < BATTLE_ENEMY_COUNT; i++)
        defeated[i] = 0;

    switch (actor->action)
    {
    case BATTLE_ACTION_ATTACK:
        config = &gBattleAnimationConfigs[actor->source.boss->attackAnimationId];
        break;
    case BATTLE_ACTION_SKILL:
        config = &gBattleAnimationConfigs[actor->source.boss->skillAnimationIds[actor->enemySkillIndex]];
        break;
    }
    switch (config->targetMode)
    {
    case 0:
        if ((s16)targets->hp - targets->pendingDamage <= 0)
        {
            targets->hp = 0;
            targetDefeated = 1;
        }
        else
        {
            targets->hp -= targets->pendingDamage;
            targetDefeated = 0;
        }
        defeated[0] = targetDefeated;
        if (defeated[0] != 0)
        {
            Battle_QueueDefeatedCombatantInline(targets);
        }
        break;
    case 1:
        if (targets->id > 10)
            count = BATTLE_ENEMY_COUNT;
        else
            count = BATTLE_PARTY_COUNT;
        for (i = 0; i < count; i++)
        {
            if (targets[i].id != BATTLE_NO_COMBATANT && targets[i].status != BATTLE_STATUS_DEFEATED)
            {
                if ((s16)targets[i].hp - targets[i].pendingDamage <= 0)
                {
                    targets[i].hp = 0;
                    targetDefeated = 1;
                }
                else
                {
                    targets[i].hp -= targets[i].pendingDamage;
                    targetDefeated = 0;
                }
                defeated[i] = targetDefeated;
                if (defeated[i] != 0)
                {
                    Battle_QueueDefeatedCombatantInline(&targets[i]);
                }
            }
        }
        break;
    }
    for (i = 0; i < BATTLE_ENEMY_COUNT; i++)
    {
        if (defeated[i] == 1)
        {
            result = defeated[i];
            break;
        }
    }
    return result;
}

static inline void Battle_SetDefeatRemovalAnimationInline(struct BattleCombatant *combatant,
    u8 paletteVariant)
{
    const struct BattleSharedEffectResources *resources = &gBattleSharedEffectResources;

    Battle_SetupAnimation(&combatant->effectAnimation, combatant->homeX, combatant->homeY,
        0x1B4, 0xE, resources->definition, resources->palettes + paletteVariant * 16,
        0x541 + resources->graphicsId, resources->graphicsBlockCount, 2);
    combatant->effectAnimation.priority = 3;
    combatant->displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
}

/** @brief 推进击倒队列的移除动画、闪烁及调色板清理。
 *  @note 设置助手内联，动画记录用普通结构赋值复制；闪烁帧除六后的余数截为 u16。
 *  完成时 id 写 FF、推进队列索引并返回一；普通 C 的完整 0x228 字节及重定位 100% 匹配。
 *  @address 0x0801E848
 */
u8 Battle_UpdateDefeatRemovalEffects(void)
{
    u8 result = 0;
    u8 i;
    struct BattleCombatant *combatant;

    switch (sBattleDefeatPhase)
    {
    case 0:
    {
        combatant = sBattleDefeatQueue[0];
        Battle_SetDefeatRemovalAnimationInline(combatant, 0);
        sBattleDefeatPhase = 1;
        break;
    }
    case 1:
        if (!(sBattleDefeatQueue[0]->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            if (sBattleDefeatQueueCount > 1)
            {
                for (i = 1; i < sBattleDefeatQueueCount; i++)
                {
                    combatant = sBattleDefeatQueue[i];
                    combatant->effectAnimation = sBattleDefeatQueue[0]->effectAnimation;
                    combatant->displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
                    combatant->effectAnimation.x = combatant->homeX;
                    combatant->effectAnimation.y = combatant->homeY;
                }
            }
            Sound_PlaySfx(0x5A, 0, 0);
            sBattleDefeatPhase = 2;
        }
        break;
    case 2:
        for (i = 0; i < sBattleDefeatQueueCount; i++)
        {
            combatant = sBattleDefeatQueue[i];
            if (combatant->id != BATTLE_NO_COMBATANT)
            {
                if (combatant->effectAnimation.timelineFrame <= 30)
                {
                    if ((u16)(combatant->effectAnimation.timelineFrame % 6) == 0)
                        combatant->animation.flags |= BATTLE_ANIM_HIDDEN;
                    else
                        combatant->animation.flags &= ~BATTLE_ANIM_HIDDEN;
                }
                else if (combatant->effectAnimation.timelineFrame == 31)
                {
                    combatant->animation.flags |= BATTLE_ANIM_HIDDEN;
                }

                if (combatant->effectAnimation.flags & BATTLE_ANIM_FINISHED)
                {
                    BattlePalette_ReleaseObjBanks(combatant->effectAnimation.paletteSlot,
                        Battle_GetAnimationPaletteCount(&combatant->effectAnimation));
                    combatant->id = BATTLE_NO_COMBATANT;
                    combatant->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
                    sBattleDefeatQueueIndex++;
                }
            }
        }
        if (sBattleDefeatQueueIndex >= sBattleDefeatQueueCount)
        {
            Sound_StopSfx(0);
            sBattleDefeatPhase = 0;
            result = 1;
        }
        break;
    }

    return result;
}

/** @brief 按四种重定向类别修正队员行动目标，并选择普通敌人或 Boss 的指令。
 *  @note 类别 0/3 先缓存目标 id 再保存目标字段地址；状态 7 分支保留计数初始化，
 *  使类别 1/2 的判断保持独立。普通 C 的 0x1CA 字节及重定位 100% 匹配，不计末尾填充。
 *  @address 0x0801EA70
 */
void Battle_PrepareCombatantAction(struct BattleCombatant *actor, struct BattleCombatant *combatants)
{
    u8 slots[BATTLE_ENEMY_COUNT];
    u8 i;
    u32 count;
    u8 *targetSlot;

    if (actor->id < BATTLE_CHARACTER_ID_COUNT)
    {
        switch (Battle_GetActionRetargetClass(actor))
        {
        case 0:
        {
            u8 targetId = combatants[actor->targetSlot].id;

            targetSlot = &actor->targetSlot;
            if (targetId < BATTLE_CHARACTER_ID_COUNT)
            {
                if (combatants[actor->targetSlot].status == BATTLE_STATUS_DEFEATED)
                {
                    if (actor->status == BATTLE_STATUS_CONFUSED)
                    {
                        for (i = 0; i < BATTLE_PARTY_COUNT; i++)
                        {
                            if (combatants[i].id == actor->id)
                                goto confusedTarget;
                        }
                    }
                    else
                    {
                        goto partyPointerTarget;
                    }
                }
                else if (actor->status != BATTLE_STATUS_CONFUSED)
                {
                    count = (u8)Battle_ListCombatants(combatants, slots, BATTLE_SIDE_ENEMY, 0x7F);
                    goto randomPointerTarget;
                }
            }
            else if (targetId == BATTLE_NO_COMBATANT)
            {
                if (actor->status != BATTLE_STATUS_CONFUSED)
                {
                    count = (u8)Battle_ListCombatants(combatants, slots, BATTLE_SIDE_ENEMY, 0x7F);
                    goto randomPointerTarget;
                }
                else
                {
                    for (i = 0; i < BATTLE_PARTY_COUNT; i++)
                    {
                        if (combatants[i].id == actor->id)
                            goto confusedTarget;
                    }
                }
            }
            break;
        }
        case 1:
            if (Battle_GetControlFlags() & 0x4000)
            {
                targetSlot = &actor->targetSlot;
                if ((u8)(combatants[actor->targetSlot].status - BATTLE_STATUS_ESCAPED) > 1)
                    break;
                goto partyFieldTarget;
            }
            else
            {
                targetSlot = &actor->targetSlot;
                switch (combatants[actor->targetSlot].status)
                {
                default:
                    goto done;
                case 7:
                    count = 0;
                    break;
                }
                goto partyFieldTarget;
            }
            break;
        case 2:
            targetSlot = &actor->targetSlot;
            if (combatants[actor->targetSlot].status == BATTLE_STATUS_ESCAPED)
            {
                count = (u8)Battle_ListCombatants(combatants, slots, BATTLE_SIDE_PARTY, 0x7F);
                goto randomPointerTarget;
            }
            break;
        case 3:
        {
            u8 targetId = combatants[actor->targetSlot].id;

            targetSlot = &actor->targetSlot;
            if (targetId < BATTLE_CHARACTER_ID_COUNT)
            {
                if (combatants[actor->targetSlot].status == BATTLE_STATUS_DEFEATED
                    || targetId == BATTLE_NO_COMBATANT)
                {
                    if (actor->status == BATTLE_STATUS_CONFUSED)
                    {
                        for (i = 0; i < BATTLE_PARTY_COUNT; i++)
                        {
                            if (combatants[i].id == actor->id)
                                goto confusedTarget;
                        }
                    }
                    else
                    {
                        goto partyFieldTarget;
                    }
                }
            }
            else if (targetId == BATTLE_NO_COMBATANT)
            {
                goto partyFieldTarget;
            }
            break;
        }
        }
        goto done;

    partyFieldTarget:
        count = (u8)Battle_ListCombatants(combatants, slots, BATTLE_SIDE_PARTY, 0x7F);
    randomFieldTarget:
        *targetSlot = slots[(u8)((u8)Battle_NextRandom() % count)];
        goto done;
    partyPointerTarget:
        count = (u8)Battle_ListCombatants(combatants, slots, BATTLE_SIDE_PARTY, 0x7F);
    randomPointerTarget:
        *targetSlot = slots[(u8)((u8)Battle_NextRandom() % count)];
        goto done;
    }
    else if (actor->id <= 112)
    {
        BattleAi_ChooseNormalEnemyCommand(actor, combatants);
        goto done;
    }
    else
    {
        goto bossCommand;
    }

confusedTarget:
    *targetSlot = BattleAi_SelectConfusedPartyTarget(combatants, i);
    goto done;
bossCommand:
    BattleAi_ChooseBossCommand(actor, combatants);
done:
    ;
}

/** @brief 查询角色精灵的像素宽度或高度。
 *  @note 高度表指针先初始化再读取 sizeClass；保留 113..129 的完整跳转表及重复 case。
 *  队员、占位 id 和 id 113 保持默认 32 像素；普通 C 的完整 0x104 字节及重定位 100% 匹配。
 *  @address 0x0801EC3C
 */
u8 Battle_GetCombatantDimension(const struct BattleCombatant *combatant, u8 dimension)
{
    u8 size = 32;

    if ((u8)(combatant->id - 12) <= 100)
    {
        switch (dimension)
        {
        case BATTLE_COMBATANT_WIDTH:
            size = D_08393A3C[combatant->source.enemy->sizeClass] << 3;
            break;
        case BATTLE_COMBATANT_HEIGHT:
        {
            const u8 *heights = D_08393A3C + 4;

            size = heights[combatant->source.enemy->sizeClass] << 3;
            break;
        }
        }
    }
    else if (combatant->id > 113)
    {
        switch (combatant->id)
        {
        case 113:
            size = 6;
            break;
        case 114:
            size = dimension == BATTLE_COMBATANT_WIDTH ? 8 : 6;
            break;
        case 115:
            size = dimension == BATTLE_COMBATANT_WIDTH ? 6 : 8;
            break;
        case 116:
            size = 8;
            break;
        case 117:
            size = dimension == BATTLE_COMBATANT_WIDTH ? 11 : 14;
            break;
        case 118:
            size = dimension == BATTLE_COMBATANT_WIDTH ? 11 : 13;
            break;
        case 119:
            size = 8;
            break;
        case 120:
            size = 4;
            break;
        case 121:
            size = dimension == BATTLE_COMBATANT_WIDTH ? 11 : 13;
            break;
        case 122:
        case 123:
            size = dimension == BATTLE_COMBATANT_WIDTH ? 10 : 8;
            break;
        case 124:
            size = 6;
            break;
        case 125:
        case 126:
        case 127:
            size = 8;
            break;
        case 128:
            size = 9;
            break;
        case 129:
        default:
            size = 6;
            break;
        }
        size <<= 3;
    }

    return size;
}

/** @brief 启动角色的 OBJ 调色板高亮，并为敌人设置调色板覆盖。
 *  @note RGB 使用八位位域，普通敌人状态四使用共用 bank；分配失败时回退到原 bank。
 *  起始 bank 按 s8 传参；普通 C 的 0x12A 字节及已解析重定位 100% 匹配，末尾零填充不计。
 *  @address 0x0801ED40
 */
void Battle_StartCombatantHighlight(struct BattleCombatant *combatant, u8 duration)
{
    struct
    {
        s8 red : 8;
        s8 green : 8;
        s8 blue : 8;
    } offsets;
    u8 paletteSlot;
    s8 outputBank;

    offsets.red = 31;
    offsets.green = 31;
    offsets.blue = 31;
    sBattleActionUnknown0765 = combatant->animation.paletteSlot + 1;

    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
    {
        BattlePalette_StartObjPulse(combatant->animation.paletteSlot,
            Battle_GetAnimationPaletteCount(&combatant->animation), (s8 *)&offsets,
            duration, -1, BATTLE_PALETTE_USE_SOURCE_BANK);
    }
    else if (combatant->id <= 112)
    {
        if (combatant->status == BATTLE_STATUS_PETRIFIED)
            paletteSlot = sBattleSharedPaletteSlot;
        else
            paletteSlot = combatant->animation.paletteSlot;
        outputBank = BattlePalette_StartObjPulse(paletteSlot,
            Battle_GetAnimationPaletteCount(&combatant->animation), (s8 *)&offsets,
            duration, (s8)(paletteSlot + 1), BATTLE_PALETTE_ALLOCATE_BANK);
        if (outputBank < 0)
        {
            BattlePalette_StartObjPulse(paletteSlot,
                Battle_GetAnimationPaletteCount(&combatant->animation), (s8 *)&offsets,
                duration, -1, BATTLE_PALETTE_USE_SOURCE_BANK);
            combatant->animation.initialPaletteSlot = paletteSlot;
        }
        else
        {
            combatant->animation.initialPaletteSlot = outputBank;
        }
        combatant->animation.flags |= 0x8000;
    }
    else
    {
        outputBank = BattlePalette_StartObjPulse(combatant->animation.paletteSlot,
            Battle_GetAnimationPaletteCount(&combatant->animation), (s8 *)&offsets,
            duration, 8, BATTLE_PALETTE_ALLOCATE_BANK);
        if (outputBank >= 0)
        {
            combatant->animation.initialPaletteSlot = outputBank;
            combatant->animation.flags |= 0x8000;
        }
    }
}

/** @brief Stop a combatant highlight and restore its normal palette handling.
 *  @note Clears animation flag 0x8000 when set and restarts id 119's color cycle.
 *  Enemy status four uses the shared effect bank instead of its own animation
 *  palette slot. Ordinary C matches all 0x78 bytes; the first argument is
 *  evaluated before the palette-count call so the selected slot stays in r5.
 *  @address 0x0801EE6C
 */
void Battle_StopCombatantHighlight(struct BattleCombatant *combatant)
{
    u8 paletteSlot;

    if (combatant->id > 11 && combatant->status == BATTLE_STATUS_PETRIFIED)
        paletteSlot = sBattleSharedPaletteSlot;
    else
        paletteSlot = combatant->animation.paletteSlot;

    BattlePalette_StopObjPulse(paletteSlot,
        Battle_GetAnimationPaletteCount(&combatant->animation));

    if (combatant->animation.flags & 0x8000)
        combatant->animation.flags &= 0x7FFF;

    if (combatant->id == 119)
        BattlePalette_StartObjCycle(combatant->animation.paletteSlot, 1, 3, -11, 5);
}

static inline void Battle_SetRecoveryEffectAnimationInline(struct BattleCombatant *combatant,
    u8 x, u8 y, u16 baseTile, u8 paletteSlot, u16 animationId, u16 flags)
{
    Battle_SetupAnimation(&combatant->effectAnimation, x, y, baseTile, paletteSlot,
        (const u16 *)gBattleAnimationConfigs[animationId].animationData,
        gBattleAnimationConfigs[animationId].palettes,
        gBattleAnimationConfigs[animationId].graphicsId,
        gBattleAnimationConfigs[animationId].graphicsBlockCount, flags);
}

static inline u8 Battle_ListRecoveryTargetsInline(struct BattleCombatant *combatants, s16 mask, u8 side)
{
    return Battle_ListCombatants(combatants, sBattleEffectTargets, side, mask);
}

/** @brief 推进目标列表的恢复动画，并结算 HP、MP、复活和状态解除。
 *  @note 保留 targetSlot <= 10 的整侧判断；建表助手以 s16 接收掩码，保持字面加载顺序。
 *  动画完成计数先清零，再初始化循环索引；完成返回一。
 *  普通 C 的 0x516 字节及已解析重定位 100% 匹配，末尾两字节零填充不计。
 *  @address 0x0801EEE4
 */
u32 Battle_UpdateTargetRecoveryEffect(struct BattleCombatant *selection, struct BattleCombatant *combatants,
    u8 targetMode, u8 effect, u16 amount)
{
    u32 result = 0;
    u8 i;
    struct BattleCombatant *target;

    switch (sBattleEffectPhase)
    {
    case 0:
        sBattleEffectTargetCount = 0;
        switch (targetMode)
        {
        case BATTLE_RECOVERY_SELECTED_TARGET:
            sBattleEffectTargetCount = 1;
            sBattleEffectTargets[0] = selection->targetSlot;
            break;
        case BATTLE_RECOVERY_SIDE_TARGETS:
        {
            u8 side;

            if (selection->targetSlot <= 10)
                side = BATTLE_SIDE_PARTY;
            else
                side = BATTLE_SIDE_ENEMY;
            sBattleEffectTargetCount = Battle_ListRecoveryTargetsInline(combatants, 0x1FF, side);
            break;
        }
        }
        sBattleEffectPhase = 1;
        break;
    case 1:
        Battle_SetupAnimation(&combatants[sBattleEffectTargets[0]].effectAnimation,
            combatants[sBattleEffectTargets[0]].animation.x,
            combatants[sBattleEffectTargets[0]].animation.y, 0x2EA, 14,
            (const u16 *)gBattleAnimationConfigs[0xA6].animationData,
            gBattleAnimationConfigs[0xA6].palettes,
            gBattleAnimationConfigs[0xA6].graphicsId,
            gBattleAnimationConfigs[0xA6].graphicsBlockCount, 4);
        combatants[sBattleEffectTargets[0]].displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
        combatants[sBattleEffectTargets[0]].effectAnimation.flags |= 0x100;
        combatants[sBattleEffectTargets[0]].effectAnimation.priority = 3;
        sBattleEffectPhase = 2;
        break;
    case 2:
        if (!(combatants[sBattleEffectTargets[0]].effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            combatants[sBattleEffectTargets[0]].effectAnimation.flags &= 0xFEFF;
            if (sBattleEffectTargetCount > 1)
            {
                for (i = 0; i < sBattleEffectTargetCount; i++)
                {
                    target = &combatants[sBattleEffectTargets[i]];
                    target->effectAnimation = combatants[sBattleEffectTargets[0]].effectAnimation;
                    target->displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
                    target->effectAnimation.x = target->animation.x;
                    target->effectAnimation.y = target->animation.y;
                }
            }
            Sound_PlaySfx(0x16, 0, 0);
            sBattleEffectPhase = 3;
        }
        break;
    case 3:
    {
        u8 completedCount;

        completedCount = 0;
        i = 0;
        for (; i < sBattleEffectTargetCount; i++)
        {
            target = &combatants[sBattleEffectTargets[i]];
            if (target->effectAnimation.flags & BATTLE_ANIM_FINISHED)
            {
                BattlePalette_ReleaseObjBanks(target->effectAnimation.paletteSlot,
                    Battle_GetAnimationPaletteCount(&target->effectAnimation));
                target->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
                target->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
                completedCount++;
            }
                }
        if (completedCount >= sBattleEffectTargetCount)
            sBattleEffectPhase = 7;
        break;
    }
    case 7:
        if (effect <= BATTLE_RECOVERY_MP || effect == BATTLE_RECOVERY_REVIVE)
            sBattleEffectPhase = 8;
        else
            sBattleEffectPhase = 10;
        break;
    case 8:
    {
        u8 index;
        const u16 *palettes;

        sBattleDamageNumberCount = 0;
        sBattleDamageNumberBounce = 1;
        sBattleDamageNumberFrame = 0;
        index = 0;
        palettes = D_0861C664;
        for (; index < BATTLE_DAMAGE_NUMBER_COUNT; index++)
        {
            sBattleDamageNumbers[index].value = 0;
            sBattleDamageNumbers[index].x = 0;
            sBattleDamageNumbers[index].y = 0;
        }
        BattlePalette_LoadObjBanks(palettes + 3 * BATTLE_PALETTE_COLORS_PER_BANK, 15, 1);

        for (i = 0; i < sBattleEffectTargetCount; i++)
        {
            target = &combatants[sBattleEffectTargets[i]];
            switch (effect)
            {
            case BATTLE_RECOVERY_HP:
                if ((u8)Battle_CheckStatusMask(target, 0x110) == BATTLE_STATUS_MATCH_REJECTED)
                    target->pendingDamage = Battle_LimitHpRecovery(target, amount);
                else
                    target->pendingDamage = 0;
                break;
            case BATTLE_RECOVERY_REVIVE:
                if ((u8)Battle_CheckStatusMask(target, 0x100) == BATTLE_STATUS_MATCH_ACCEPTED)
                    target->pendingDamage = Battle_LimitHpRecovery(target, amount);
                else
                    target->pendingDamage = 0;
                break;
            case BATTLE_RECOVERY_CURE_STATUS:
                target->pendingDamage = Battle_LimitHpRecovery(target, amount);
                break;
            case BATTLE_RECOVERY_MP:
                if ((u8)Battle_CheckStatusMask(target, 0x110) == BATTLE_STATUS_MATCH_REJECTED)
                    target->pendingDamage = Battle_LimitMpRecovery(target, amount);
                else
                    target->pendingDamage = 0;
                break;
            }
            Battle_AddDamageNumber(target);
        }
        sBattleEffectPhase = 9;
        break;
    }
    case 9:
        if ((u8)Battle_UpdateDamageNumbers() == 1)
            sBattleEffectPhase = 10;
        break;
    case 10:
        for (i = 0; i < sBattleEffectTargetCount; i++)
        {
            target = &combatants[sBattleEffectTargets[i]];
            switch (effect)
            {
            case BATTLE_RECOVERY_HP:
                target->hp += (u16)target->pendingDamage;
                break;
            case BATTLE_RECOVERY_MP:
                target->mp += (u16)target->pendingDamage;
                break;
            case BATTLE_RECOVERY_HP_AND_MP:
                if ((u8)Battle_CheckStatusMask(target, 0x110) == BATTLE_STATUS_MATCH_REJECTED)
                {
                    target->hp += Battle_LimitHpRecovery(target, amount);
                    target->mp += Battle_LimitMpRecovery(target, amount);
                }
                break;
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
            case BATTLE_RECOVERY_REVIVE:
            case BATTLE_RECOVERY_CURE_STATUS:
                Battle_CureStatusOrRevive(target, effect, target->pendingDamage);
                break;
            case BATTLE_RECOVERY_TURN_END_HP:
                target->turnEndEffects |= BATTLE_TURN_END_HP_RECOVERY;
                break;
            }
        }
        sBattleEffectPhase = 11;
        break;
    case 11:
        result = 1;
        sBattleEffectPhase = 0;
        break;
    }

    return result;
}
/** @brief 推进指定角色的 HP/MP 恢复、复活及状态解除流程。
 *  @note 共用 sBattleEffectPhase，正常入口由阶段 0 直接跳到 7；保留阶段 1..3
 *  的动画路径。状态检查取低 u8，HP 限幅结果按 u32 接收。
 *  动画设置用私有内联助手保留实参求值与资源取址顺序。
 *  普通 C 的 0x36E 字节（含字面池）及已解析重定位 100% 匹配；末尾两字节零填充不计。
 *  @address 0x0801F3FC
 */
u32 Battle_UpdateCombatantRecoveryEffect(struct BattleCombatant *combatant, u8 effect, u16 amount)
{
    u32 result = 0;

    switch (sBattleEffectPhase)
    {
    case 0:
        sBattleEffectTargetCount = 0;
        sBattleEffectPhase = 7;
        break;
    case 1:
    {
        Battle_SetRecoveryEffectAnimationInline(combatant,
            combatant->animation.x, combatant->animation.y, 0x2EA, 0xE, 0xA6, 0x104);
        combatant->displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
        combatant->effectAnimation.priority = 3;
        sBattleEffectPhase = 2;
        break;
    }
    case 2:
        if (!(combatant->effectAnimation.flags & BATTLE_ANIM_LOADING))
        {
            combatant->effectAnimation.flags &= 0xFEFF;
            Sound_PlaySfx(0x16, 0, 0);
            sBattleEffectPhase = 3;
        }
        break;
    case 3:
        if (combatant->effectAnimation.flags & BATTLE_ANIM_FINISHED)
        {
            BattlePalette_ReleaseObjBanks(combatant->effectAnimation.paletteSlot,
                Battle_GetAnimationPaletteCount(&combatant->animation));
            combatant->effectAnimation.flags &= ~BATTLE_ANIM_FINISHED;
            combatant->displayState &= ~BATTLE_DISPLAY_EFFECT_ANIMATION;
            sBattleEffectPhase = 7;
        }
        break;
    case 7:
        if (effect <= BATTLE_RECOVERY_MP || effect == BATTLE_RECOVERY_REVIVE)
            sBattleEffectPhase = 8;
        else
            sBattleEffectPhase = 10;
        break;
    case 8:
    {
        u8 index;
        const u16 *palettes;

        sBattleDamageNumberCount = 0;
        sBattleDamageNumberBounce = 1;
        sBattleDamageNumberFrame = 0;
        index = 0;
        palettes = D_0861C664;
        for (; index < BATTLE_DAMAGE_NUMBER_COUNT; index++)
        {
            sBattleDamageNumbers[index].value = 0;
            sBattleDamageNumbers[index].x = 0;
            sBattleDamageNumbers[index].y = 0;
        }
        BattlePalette_LoadObjBanks(palettes + 3 * BATTLE_PALETTE_COLORS_PER_BANK, 15, 1);

        switch (effect)
        {
        case BATTLE_RECOVERY_HP:
            if ((u8)Battle_CheckStatusMask(combatant, 0x110) == BATTLE_STATUS_MATCH_REJECTED)
                combatant->pendingDamage = Battle_LimitHpRecovery(combatant, amount);
            else
                combatant->pendingDamage = 0;
            break;
        case BATTLE_RECOVERY_REVIVE:
            if ((u8)Battle_CheckStatusMask(combatant, 0x100) == BATTLE_STATUS_MATCH_ACCEPTED)
                combatant->pendingDamage = Battle_LimitHpRecovery(combatant, amount);
            else
                combatant->pendingDamage = 0;
            break;
        case BATTLE_RECOVERY_CURE_STATUS:
            combatant->pendingDamage = Battle_LimitHpRecovery(combatant, amount);
            break;
        case BATTLE_RECOVERY_MP:
            if ((u8)Battle_CheckStatusMask(combatant, 0x110) == BATTLE_STATUS_MATCH_REJECTED)
                combatant->pendingDamage = Battle_LimitMpRecovery(combatant, amount);
            else
                combatant->pendingDamage = 0;
            break;
        }

        Battle_AddDamageNumber(combatant);
        sBattleEffectPhase = 9;
        break;
    }
    case 9:
        if ((u8)Battle_UpdateDamageNumbers() == 1)
            sBattleEffectPhase = 10;
        break;
    case 10:
        switch (effect)
        {
        case BATTLE_RECOVERY_HP:
            combatant->hp += (u16)combatant->pendingDamage;
            break;
        case BATTLE_RECOVERY_MP:
            combatant->mp += (u16)combatant->pendingDamage;
            break;
        case BATTLE_RECOVERY_HP_AND_MP:
            if ((u8)Battle_CheckStatusMask(combatant, 0x110) == BATTLE_STATUS_MATCH_REJECTED)
            {
                combatant->hp += Battle_LimitHpRecovery(combatant, amount);
                combatant->mp += Battle_LimitMpRecovery(combatant, amount);
            }
            break;
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case BATTLE_RECOVERY_REVIVE:
        case BATTLE_RECOVERY_CURE_STATUS:
            Battle_CureStatusOrRevive(combatant, effect, combatant->pendingDamage);
            break;
        case BATTLE_RECOVERY_TURN_END_HP:
            combatant->turnEndEffects |= BATTLE_TURN_END_HP_RECOVERY;
            break;
        }
        sBattleEffectPhase = 11;
        break;
    case 11:
        result = 1;
        sBattleEffectPhase = 0;
        break;
    }

    return result;
}

/** @brief Classify action/skill/item ids for the target-correction routine.
 *  @note Returns a class 0..3, not a status mask or skill target-mode nibble.
 *  A learned-skill slot is resolved to its zero-based id before classification.
 *  @address 0x0801F76C
 */
u8 Battle_GetActionRetargetClass(const struct BattleCombatant *combatant)
{
    u8 result = 0;
    u8 actionId;

    switch (combatant->action)
    {
    case BATTLE_ACTION_ATTACK:
        break;
    case BATTLE_ACTION_SKILL:
        actionId = combatant->skillSelection < BATTLE_SKILL_COUNT
            ? combatant->skills[combatant->skillSelection]
            : combatant->skillSelection;
        switch (actionId)
        {
        case 6:
        case 8:
        case 9:
        case 12:
        case 13:
        case 30:
        case 31:
        case 32:
        case 33:
        case 35:
        case 49:
        case 51:
            result = 1;
            break;
        case 10:
        case 25:
        case 29:
            result = 3;
            break;
        case 34:
            result = 2;
            break;
        }
        break;
    case BATTLE_ACTION_ITEM:
        if (combatant->itemId == 0xE1)
            result = 2;
        else if (combatant->itemId > 0xDC)
            result = 1;
        break;
    }

    return result;
}

/** @brief 查询当前行动用于筛选目标的阵形条件，0 表示不限制。
 *  @note 敌方攻击／技能分支先取得动画配置，再读取行列筛选值；保留字节返回值截断。
 *  普通 C 的完整 0x18A 字节及重定位 100% 匹配。
 *  @address 0x0801F884
 */
u8 Battle_GetActionFormationFilter(const struct BattleCombatant *combatant)
{
    u8 formation = 0;
    struct BattleCombatant *combatants = Battle_GetCombatants();
    const struct BattleAnimationResourceConfig *config;
    u32 animationValue;
    u16 animationId;

    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
    {
        switch (combatant->action)
        {
        case BATTLE_ACTION_ATTACK:
            formation = combatants[combatant->targetSlot].formationBits;
            break;
        case BATTLE_ACTION_SKILL:
            switch (Battle_GetSkillTargetMode(combatant, combatant->skillSelection))
            {
            case 0:
            case 4:
                formation = combatants[combatant->targetSlot].formationBits;
                break;
            case 1:
                formation = combatants[combatant->targetSlot].formationBits & 0xF;
                break;
            case 2:
                formation = combatants[combatant->targetSlot].formationBits & 0xF0;
                if (formation == 0x20)
                    formation = 0x30;
                break;
            case 3:
            case 5:
                formation = 0;
                break;
            }
            break;
        case BATTLE_ACTION_ITEM:
            formation = combatants[combatant->targetSlot].formationBits;
            break;
        }
    }
    else
    {
        if (combatant->id <= 112)
        {
            switch (combatant->action)
            {
            case BATTLE_ACTION_ATTACK:
                animationValue = combatant->source.enemy->animationBaseId + 3;
                animationId = animationValue;
                config = &gBattleAnimationConfigs[animationId];
                break;
            case BATTLE_ACTION_SKILL:
                animationValue = combatant->source.enemy->animationBaseId + 3;
                animationValue += combatant->source.enemy->alternateAnimationOffset;
                animationId = animationValue;
                config = &gBattleAnimationConfigs[animationId];
                break;
            }
        }
        else
        {
            switch (combatant->action)
            {
            case BATTLE_ACTION_ATTACK:
                config = &gBattleAnimationConfigs[combatant->source.boss->attackAnimationId];
                break;
            case BATTLE_ACTION_SKILL:
                config = &gBattleAnimationConfigs[combatant->source.boss->skillAnimationIds[combatant->enemySkillIndex]];
                break;
            }
        }

        switch (config->targetMode)
        {
        case 0:
            formation = combatants[combatant->targetSlot].formationBits;
            break;
        case 1:
            formation = 0;
            break;
        case 2:
            formation = combatants[combatant->targetSlot].formationBits & 0xF;
            if (formation == 2)
                formation = 1;
            break;
        case 3:
            formation = combatants[combatant->targetSlot].formationBits & 0xF0;
            if (formation == 0x20)
                formation = 0x10;
            break;
        }
    }

    return formation;
}

/**
 * @brief 更新光标显示状态的低四位并选择关联动画
 * @note 保留高十二位；状态 1/2 分别使用图形编号 0/1，共用调色板、单块图形和标志 0x403；其他状态不重建动画。
 *       高、低状态分别保存为 u16 局部量以保持掩码和 OR 寄存器分配；普通 C 的 0xA8 字节（含字面池）及六项已解析重定位 100% 匹配。
 * @address 0x0801FA10
 */
void Battle_SetCursorAnimation(struct BattleCombatant *cursor, u16 state)
{
    u16 highState;
    u16 animationState;

    highState = cursor->displayState & 0xFFF0;
    cursor->displayState = highState;
    animationState = state & 0xF;
    cursor->displayState = highState | animationState;

    switch (cursor->displayState & 0xF)
    {
    case 1:
        Battle_SetupAnimation(&cursor->animation, cursor->animation.x, cursor->animation.y,
            0, 0, D_0856B440, gBattleSpritePalettes[0], 0, 1, 0x403);
        break;
    case 2:
        Battle_SetupAnimation(&cursor->animation, cursor->animation.x, cursor->animation.y,
            0, 0, D_0856B494, gBattleSpritePalettes[0], 1, 1, 0x403);
        break;
    }
}

/**
 * @brief 推进光标移动、往返浮动及目标复活流程
 * @note 状态 0x60 捕获目标 homeX/homeY-48 后进行 100 帧插值；帧数和时长按 s16 比较。
 *       switch 保留 0x30、0x90 空分支；0x90 与默认出口同义，用于维持原比较树，无需 goto。
 *       恢复结果取低 u8，候选编号按 s8 判断。
 *       普通 C 的 0x402 字节（含字面池）及 46 项已解析重定位 100% 匹配；末尾两字节零填充不计。
 * @address 0x0801FAB8
 */
void Battle_UpdateCursor(struct BattleCombatant *cursor, struct BattleCombatant *combatants)
{
    switch (cursor->displayState & BATTLE_CURSOR_MOTION_MASK)
    {
    case 0x10:
        cursor->animation.x = Battle_InterpolateS16(sBattleCursorStartX, sBattleCursorDeltaX,
            (s16)sBattleCursorMoveDuration, (s16)sBattleCursorMoveFrame, (s8)sBattleCursorMoveCurve);
        cursor->animation.y = Battle_InterpolateS16(sBattleCursorStartY, sBattleCursorDeltaY,
            (s16)sBattleCursorMoveDuration, (s16)sBattleCursorMoveFrame, (s8)sBattleCursorMoveCurve);
        sBattleCursorMoveFrame++;
        if ((s16)sBattleCursorMoveFrame > (s16)sBattleCursorMoveDuration)
            cursor->displayState &= ~BATTLE_CURSOR_MOTION_MASK;
        break;
    case 0x20:
        cursor->animation.x = Battle_InterpolateS16(sBattleCursorStartX, sBattleCursorDeltaX,
            (s16)sBattleCursorMoveDuration, (s16)sBattleCursorMoveFrame, (s8)sBattleCursorMoveCurve);
        cursor->animation.y = Battle_InterpolateS16(sBattleCursorStartY, sBattleCursorDeltaY,
            (s16)sBattleCursorMoveDuration, (s16)sBattleCursorMoveFrame, (s8)sBattleCursorMoveCurve);
        sBattleCursorMoveFrame++;
        if ((s16)sBattleCursorMoveFrame > (s16)sBattleCursorMoveDuration)
        {
            cursor->displayState &= ~BATTLE_CURSOR_MOTION_MASK;
            cursor->displayState |= 0x50;
            if (cursor->animation.flags & BATTLE_ANIM_FLIP_X)
                cursor->animation.flags &= ~BATTLE_ANIM_FLIP_X;
            sBattleCursorMoveFrame = 0;
        }
        break;
    case 0x30:
        break;
    case 0x40:
        cursor->animation.x = Battle_InterpolateS16(170, 10, 70, (s16)sBattleCursorMoveFrame, 2);
        cursor->animation.y = Battle_InterpolateS16(20, -5, 70, (s16)sBattleCursorMoveFrame, 2);
        sBattleCursorMoveFrame++;
        if ((s16)sBattleCursorMoveFrame > 70)
        {
            cursor->displayState &= ~BATTLE_CURSOR_MOTION_MASK;
            cursor->displayState |= 0x50;
            sBattleCursorMoveFrame = 0;
        }
        break;
    case 0x50:
        cursor->animation.x = Battle_InterpolateS16(180, -10, 70, (s16)sBattleCursorMoveFrame, 2);
        cursor->animation.y = Battle_InterpolateS16(15, 5, 70, (s16)sBattleCursorMoveFrame, 2);
        sBattleCursorMoveFrame++;
        if ((s16)sBattleCursorMoveFrame > 70)
        {
            cursor->displayState &= ~BATTLE_CURSOR_MOTION_MASK;
            cursor->displayState |= 0x40;
            sBattleCursorMoveFrame = 0;
        }
        break;
    case 0x60:
        sBattleCursorMoveDuration = 100;
        sBattleCursorMoveFrame = 0;
        sBattleCursorStartX = cursor->animation.x;
        sBattleCursorStartY = cursor->animation.y;
        sBattleCursorDeltaX = combatants[cursor->targetSlot].homeX - cursor->animation.x;
        sBattleCursorDeltaY = combatants[cursor->targetSlot].homeY - 48 - cursor->animation.y;
        sBattleCursorMoveCurve = 0;
        if (sBattleCursorDeltaX > 0)
            cursor->animation.flags |= BATTLE_ANIM_FLIP_X;
        cursor->displayState &= ~BATTLE_CURSOR_MOTION_MASK;
        cursor->displayState |= 0x70;
        break;
    case 0x70:
        cursor->animation.x = Battle_InterpolateS16(sBattleCursorStartX, sBattleCursorDeltaX,
            (s16)sBattleCursorMoveDuration, (s16)sBattleCursorMoveFrame, (s8)sBattleCursorMoveCurve);
        cursor->animation.y = Battle_InterpolateS16(sBattleCursorStartY, sBattleCursorDeltaY,
            (s16)sBattleCursorMoveDuration, (s16)sBattleCursorMoveFrame, (s8)sBattleCursorMoveCurve);
        sBattleCursorMoveFrame++;
        if ((s16)sBattleCursorMoveFrame > (s16)sBattleCursorMoveDuration)
        {
            cursor->displayState &= ~BATTLE_CURSOR_MOTION_MASK;
            cursor->displayState |= 0x80;
            sBattleEffectPhase = 0;
            sBattleCursorMoveFrame = 0;
        }
        break;
    case 0x80:
        if ((u8)Battle_UpdateTargetRecoveryEffect(cursor, combatants, 0, BATTLE_RECOVERY_REVIVE, 1) == 1)
        {
            if (cursor->displayState & 0x200)
            {
                u8 targetSlot = Battle_SelectRevivalCandidate(1);

                if ((s8)targetSlot >= 0)
                    Battle_SetCursorTarget(cursor, targetSlot, 0);
                else
                {
                    cursor->displayState &= ~0x200;
                    Battle_ReturnCursorToRest(cursor, 100, 0);
                }
            }
            else
                Battle_ReturnCursorToRest(cursor, 100, 0);
        }
        break;
    case 0x90:
        break;
    }
}

/**
 * @brief 启动光标向休息位置 (180,15) 的插值移动
 * @note 移动子字段设为 0x20，保存时长、起点、位移和曲线，帧数清零。
 *       X 位移大于零时先置横向翻转位，随后动画 1 初始化会重写标志；保留原写入顺序。
 *       普通 C 按原地址独立链接，0x84 字节（含字面池）及八项已解析重定位 100% 匹配。
 * @address 0x0801FEBC
 */
void Battle_ReturnCursorToRest(struct BattleCombatant *cursor, u16 duration, u8 curve)
{
    cursor->displayState &= ~BATTLE_CURSOR_MOTION_MASK;
    cursor->displayState |= 0x20;
    sBattleCursorMoveDuration = duration;
    sBattleCursorMoveFrame = 0;
    sBattleCursorStartX = cursor->animation.x;
    sBattleCursorStartY = cursor->animation.y;
    sBattleCursorDeltaX = 180 - cursor->animation.x;
    sBattleCursorDeltaY = 15 - cursor->animation.y;
    sBattleCursorMoveCurve = curve;
    if (sBattleCursorDeltaX > 0)
        cursor->animation.flags |= BATTLE_ANIM_FLIP_X;
    Battle_SetCursorAnimation(cursor, 1);
}

/** @brief 更新倒地等待次数，筛选复活候选并转换为战斗槽编号。
 *  @note mode 1 直接选中；遍历下标沿用清零循环结束值 5，保留 s8 截断。
 *  早期映射读取未初始化的 count，编译后保留入口 r8 的值；普通 C 的完整
 *  0x1A8 字节（含字面池）及已解析重定位 100% 匹配。
 *  @address 0x0801FF40
 */
s8 Battle_SelectRevivalCandidate(u8 mode)
{
    u8 slots[BATTLE_PARTY_COUNT];
    s8 selected = -1;
    s8 i;
    u8 candidateCount;
    u8 count;
    struct BattleFaintedNode *node;
    struct BattleCombatant *combatants;

    for (i = 0; i < BATTLE_PARTY_COUNT; i++)
        slots[i] = 0;
    Battle_SeedRandom(Battle_NextRandom());
    Battle_NextRandom();

    node = (struct BattleFaintedNode *)sBattleFaintedList.next;
    candidateCount = 0;
    while (node->link.sortKey < 0xFF)
    {
        node->combatant->pendingDamage++;
        i++;
        if (mode == 1)
        {
            selected = i;
            break;
        }
        if ((u8)(Battle_NextRandom() % 101)
            <= ((u16)node->combatant->pendingDamage + 1) * 40)
        {
            selected = i;
            break;
        }
        if ((u16)node->combatant->pendingDamage > 4)
        {
            slots[candidateCount] = node->combatant->formationSlot;
            candidateCount++;
        }
        node = (struct BattleFaintedNode *)node->link.next;
    }

    if (candidateCount != 0)
    {
        selected = slots[Battle_NextRandom() % candidateCount];
        for (i = 0; i < count; i++)
        {
            if (sBattleFaintedNodes[i].combatant->formationSlot == selected)
            {
                selected = i;
                break;
            }
        }
    }
    else if (selected >= 0)
    {
        for (i = 0; i < count; i++)
        {
            if (node->combatant->formationSlot == sBattleFaintedNodes[i].combatant->formationSlot)
            {
                selected = i;
                break;
            }
        }
    }

    combatants = Battle_GetCombatants();
    count = Battle_ListCombatants(combatants, slots, BATTLE_SIDE_PARTY, 0x100);
    for (i = 0; i < count; i++)
    {
        if (selected == combatants[slots[i]].formationSlot)
        {
            selected = slots[i];
            break;
        }
    }
    return selected;
}

/** @brief 将队伍角色的属性、六槽装备、计量槽和八槽技能导入战斗记录。
 *  @note 五项属性取基础字节与装备加成之和；技能 FF 和 0x26 写为 FF，其他编号减一。
 *  保留特殊技能、装备效果和状态调色板的调用顺序；普通 C 主体的 0x13E 字节
 *  及已解析重定位 100% 匹配，末尾两字节零填充不计。
 *  @address 0x080200E8
 */
void Battle_InitPartyCombatantStats(struct BattleCombatant *combatant,
    const struct PartyCharacter *character, u8 formationBits)
{
    u8 i;

    combatant->maxHp = character->maxHp;
    combatant->maxMp = character->maxMp;
    combatant->hp = character->hp;
    combatant->mp = character->mp;
    combatant->atc = character->baseAtc + character->equipAtc;
    combatant->def = character->baseDef + character->equipDef;
    combatant->agl = character->baseAgl + character->equipAgl;
    combatant->men = character->baseMen + character->equipMen;
    combatant->res = character->baseRes + character->equipRes;
    combatant->noa = character->noa;
    combatant->level = character->level;
    combatant->status = BATTLE_STATUS_NORMAL;
    combatant->atcModifier = 0;
    combatant->defModifier = 0;
    combatant->aglModifier = 0;
    combatant->menModifier = 0;
    combatant->resModifier = 0;
    combatant->equipment[0] = character->equipSlot1;
    combatant->equipment[1] = character->equipSlot2;
    combatant->equipment[2] = character->equipSlot3;
    combatant->equipment[3] = character->equipSlot4;
    combatant->equipment[4] = character->equipSlot5;
    combatant->equipment[5] = character->equipSlot6;
    Battle_SetSpecialGauge(combatant, character->specialGauge);

    for (i = 0; i < BATTLE_SKILL_COUNT; i++)
    {
        if (character->skills[i] != 0xFF && character->skills[i] != 0x26)
            combatant->skills[i] = character->skills[i] - 1;
        else
            combatant->skills[i] |= 0xFF;
    }
    Battle_SetPartySpecialSkill(combatant);
    combatant->formationBits = formationBits;
    Battle_AddEquipmentTurnEndEffects(combatant);
    Battle_UpdateStatusPaletteOffset(combatant, 0);
    combatant->attackSpacingWidth = 16;
    combatant->unknownC4 = 16;
}

/** @brief 从普通敌人表或扩展初始化器载入属性，最后写入阵型。
 *  @note 普通记录步长为 0x2C；遭遇等级修正为 s8，加法结果截为 u8。
 *  保留 HP/MP 的重复读取及五项修正量清零顺序；首参数仅转交 Boss 分支。
 *  普通 C 的完整 0xF4 字节（含字面池）及已解析重定位 100% 匹配。
 *  @address 0x08020228
 */
void Battle_InitEnemyCombatantStats(u32 unused, struct BattleCombatant *combatant, u8 formationBits)
{
    if (combatant->id <= 112)
    {
        const struct BattleEnemyInfo *enemy = &gActorPreviewConfigs[combatant->id - 12];

        combatant->source.enemy = enemy;
        combatant->level = enemy->level;
        combatant->status = BATTLE_STATUS_NORMAL;
        combatant->maxHp = enemy->hp;
        combatant->maxMp = enemy->mp;
        combatant->hp = enemy->hp;
        combatant->mp = enemy->mp;
        combatant->atc = enemy->atc;
        combatant->def = enemy->def;
        combatant->agl = enemy->agl;
        combatant->men = enemy->men;
        combatant->res = enemy->res;
        combatant->noa = enemy->noa;
        combatant->atcModifier = 0;
        combatant->defModifier = 0;
        combatant->aglModifier = 0;
        combatant->menModifier = 0;
        combatant->resModifier = 0;
        combatant->attackSpacingWidth = enemy->attackSpacingWidth;
        combatant->unknownC4 = D_08393A3C[enemy->sizeClass] << 3;
        combatant->level += Battle_GetEncounterLevelAdjustment(Battle_GetEncounterId());
    }
    else
    {
        combatant->atcModifier = 0;
        combatant->defModifier = 0;
        combatant->aglModifier = 0;
        combatant->menModifier = 0;
        combatant->resModifier = 0;
        Battle_InitBossCombatantStats(unused, combatant);
    }
    combatant->formationBits = formationBits;
}

/** @brief 导入 0x40 字节 Boss 属性记录，并按 ID 113..129 执行等级缩放。
 *  @note 首参数未使用；逐 case 查询队伍等级，保留 17 项跳转表和六处缩放调用。
 *  ID 114/116/117/118/119/121 设置隐藏状态图标位；普通 C 主体的 0x32A 字节
 *  （含字面池和跳转表）及已解析重定位 100% 匹配，末尾两字节零填充不计。
 *  @address 0x0802031C
 */
void Battle_InitBossCombatantStats(u32 unused, struct BattleCombatant *combatant)
{
    const struct BattleBossInfo *boss = &gExtendedActorPreviewConfigs[combatant->id - 113];

    combatant->source.boss = boss;
    combatant->level = 0;
    combatant->status = BATTLE_STATUS_NORMAL;
    combatant->maxHp = boss->hp;
    combatant->maxMp = boss->mp;
    combatant->hp = boss->hp;
    combatant->mp = boss->mp;
    combatant->atc = boss->atc;
    combatant->def = boss->def;
    combatant->agl = boss->agl;
    combatant->men = boss->men;
    combatant->res = boss->res;
    combatant->noa = boss->noa;
    combatant->attackSpacingWidth = boss->unknown3B;
    combatant->unknownC4 = boss->unknown3C;

    switch (combatant->id)
    {
    case 113:
        Battle_ScaleBossStats(Battle_GetProtagonistBattleLevel(), combatant, 80, 2, 1, 1, 1, 1, 4);
        break;
    case 114:
        Battle_ScaleBossStats(Battle_GetProtagonistBattleLevel(), combatant, 70, 2, 1, 1, 4, 1, 7);
        combatant->displayState |= BATTLE_DISPLAY_HIDE_STATUS_ICON;
        break;
    case 115:
        Battle_ScaleBossStats(Battle_GetProtagonistBattleLevel(), combatant, 30, 3, 2, 1, 1, 1, 10);
        break;
    case 116:
        Battle_ScaleBossStats(Battle_GetProtagonistBattleLevel(), combatant, 70, 3, 1, 1, 4, 2, 13);
        combatant->displayState |= BATTLE_DISPLAY_HIDE_STATUS_ICON;
        break;
    case 117:
        Battle_ScaleBossStats(Battle_GetProtagonistBattleLevel(), combatant, 130, 2, 2, 1, 1, 2, 14);
        combatant->displayState |= BATTLE_DISPLAY_HIDE_STATUS_ICON;
        break;
    case 118:
        Battle_ScaleBossStats(Battle_GetProtagonistBattleLevel(), combatant, 110, 2, 2, 1, 3, 2, 16);
        combatant->displayState |= BATTLE_DISPLAY_HIDE_STATUS_ICON;
        break;
    case 119:
        Battle_ScaleBossStats(Battle_GetProtagonistBattleLevel(), combatant, 80, 4, 1, 1, 2, 1, 22);
        combatant->displayState |= BATTLE_DISPLAY_HIDE_STATUS_ICON;
        break;
    case 120:
        Battle_ScaleBossStats(Battle_GetProtagonistBattleLevel(), combatant, 10, 2, 2, 1, 2, 1, 25);
        break;
    case 121:
        Battle_ScaleBossStats(Battle_GetProtagonistBattleLevel(), combatant, 150, 4, 1, 1, 5, 2, 30);
        combatant->displayState |= BATTLE_DISPLAY_HIDE_STATUS_ICON;
        break;
    case 122:
        Battle_ScaleBossStats(Battle_GetProtagonistBattleLevel(), combatant, 150, 3, 2, 1, 3, 2, 32);
        break;
    case 123:
        Battle_ScaleBossStats(Battle_GetProtagonistBattleLevel(), combatant, 120, 3, 2, 1, 3, 2, 34);
        break;
    case 124:
        Battle_ScaleBossStats(Battle_GetProtagonistBattleLevel(), combatant, 450, 5, 2, 2, 5, 3, 43);
        break;
    case 125:
        Battle_ScaleBossStats(Battle_GetProtagonistBattleLevel(), combatant, 100, 4, 1, 1, 4, 2, 38);
        break;
    case 126:
        Battle_ScaleBossStats(Battle_GetProtagonistBattleLevel(), combatant, 110, 4, 1, 1, 4, 2, 40);
        break;
    case 127:
        Battle_ScaleBossStats(Battle_GetProtagonistBattleLevel(), combatant, 150, 5, 2, 1, 3, 2, 40);
        break;
    case 128:
        Battle_ScaleBossStats(Battle_GetProtagonistBattleLevel(), combatant, 150, 5, 1, 1, 3, 2, 40);
        break;
    case 129:
        Battle_ScaleBossStats(Battle_GetProtagonistBattleLevel(), combatant, 100, 3, 1, 1, 3, 2, 30);
        break;
    }
}

/** @brief 按 min(max(partyLevel, minimumLevel), 50) 增加 Boss 的 HP 和五项属性。
 *  @note 六项乘积先截为 u16，加法超过 0xFFFF 时饱和；HP 用 maxHp 判断溢出，
 *  未溢出时累加 hp 并同步 maxHp。普通 C 的 0x14E 字节及字面池 100% 匹配。
 *  @address 0x08020648
 */
void Battle_ScaleBossStats(u8 partyLevel, struct BattleCombatant *combatant,
    u16 hpPerLevel, u16 atcPerLevel, u16 defPerLevel, u16 aglPerLevel,
    u16 menPerLevel, u16 resPerLevel, u16 minimumLevel)
{
    if (minimumLevel < partyLevel)
        minimumLevel = partyLevel;
    if (minimumLevel > 50)
        minimumLevel = 50;

    hpPerLevel *= minimumLevel;
    atcPerLevel *= minimumLevel;
    defPerLevel *= minimumLevel;
    aglPerLevel *= minimumLevel;
    menPerLevel *= minimumLevel;
    resPerLevel *= minimumLevel;
    combatant->level = minimumLevel;

    if (combatant->maxHp + hpPerLevel > 0xFFFF)
    {
        combatant->maxHp |= 0xFFFF;
        combatant->hp |= 0xFFFF;
    }
    else
    {
        combatant->hp += hpPerLevel;
        combatant->maxHp = combatant->hp;
    }

    if (combatant->atc + atcPerLevel > 0xFFFF)
        combatant->atc |= 0xFFFF;
    else
        combatant->atc += atcPerLevel;

    if (combatant->def + defPerLevel > 0xFFFF)
        combatant->def |= 0xFFFF;
    else
        combatant->def += defPerLevel;

    if (combatant->agl + aglPerLevel > 0xFFFF)
        combatant->agl |= 0xFFFF;
    else
        combatant->agl += aglPerLevel;

    if (combatant->men + menPerLevel > 0xFFFF)
        combatant->men |= 0xFFFF;
    else
        combatant->men += menPerLevel;

    if (combatant->res + resPerLevel > 0xFFFF)
        combatant->res |= 0xFFFF;
    else
        combatant->res += resPerLevel;
}

/** @brief Return the palette bank reserved after the combatant palettes.
 *  @note Reads the private byte selected during encounter initialization.
 *  @address 0x08020798
 */
u8 Battle_GetSharedEffectPaletteSlot(void)
{
    return sBattleSharedPaletteSlot;
}

/** @brief Consume one living-enemy count entry during defeat processing.
 *  @note The byte decrement wraps; callers must not consume an empty count.
 *  @address 0x080207A4
 */
void Battle_DecrementEnemyCount(void)
{
    sBattleEnemyCount--;
}

/** @brief Dispatch the current turn to the ordinary or special encounter updater.
 *  @note The scene control flag selects the record/AI path, not a combatant id.
 *  @address 0x080207B4
 */
u8 Battle_UpdateTurnActions(struct BattleCombatant *combatants)
{
    u8 result;

    if (!(Battle_GetControlFlags() & BATTLE_CONTROL_SPECIAL_ENCOUNTER))
        result = Battle_UpdateActionQueue(combatants);
    else
        result = Battle_UpdateActionQueueAlternate(combatants);
    return result;
}

/** @brief Restore the idle animation selected by the combatant's record class.
 *  @note Position arguments are unused. Empty id FF performs no animation setup.
 *  @address 0x080207DC
 */
void Battle_RestoreIdleAnimation(struct BattleCombatant *combatant, u8 unusedX,
    u8 unusedY, u16 baseTile, u8 paletteSlot)
{
    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
        Battle_SetPartyAnimation(combatant, BATTLE_COMBATANT_ANIM_IDLE, baseTile, paletteSlot, 0);
    else if (combatant->id <= 112)
        Battle_SetEnemyAnimation(combatant, 0, baseTile, paletteSlot, 0);
    else if ((u8)(combatant->id - 113) <= 141)
        Battle_SetBossAnimation(combatant, BATTLE_COMBATANT_ANIM_IDLE, baseTile, paletteSlot, 0);
}

/** @brief Select the initial party/enemy animation used during encounter setup.
 *  @note Positions are unused; party uses mode ten, ordinary enemies mode five,
 *  and extended enemies idle mode. Empty id FF leaves animation state unchanged.
 *  @address 0x08020840
 */
void Battle_SetInitialCombatantAnimation(struct BattleCombatant *combatant, u8 unusedX, u8 unusedY,
    u16 baseTile, u8 paletteSlot)
{
    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
        Battle_SetPartyAnimation(combatant, 10, baseTile, paletteSlot, 0);
    else if (combatant->id <= 112)
        Battle_SetEnemyAnimation(combatant, 5, baseTile, paletteSlot, 0);
    else if ((u8)(combatant->id - 113) <= 141)
        Battle_SetBossAnimation(combatant, 0, baseTile, paletteSlot, 0);
}

/** @brief Select the hit-reaction animation for the combatant's record class.
 *  @note Refreshes the status palette first and retains the current tile/palette slots.
 *  @address 0x080208A4
 */
void Battle_SetHitReactionAnimation(struct BattleCombatant *combatant)
{
    Battle_UpdateStatusPaletteOffset(combatant, 0);
    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
        Battle_SetPartyAnimation(combatant, 2, combatant->animation.baseTile,
            combatant->animation.paletteSlot, 0);
    else if (combatant->id <= 112)
        Battle_SetEnemyAnimation(combatant, 1, combatant->animation.baseTile,
            combatant->animation.paletteSlot, 0);
    else if ((u8)(combatant->id - 113) <= 141)
        Battle_SetBossAnimation(combatant, 2, combatant->animation.baseTile,
            combatant->animation.paletteSlot, 0);
}

/** @brief Select party animation mode three using the existing display slots.
 *  @note This reads action-info animationId07; its gameplay role is unconfirmed.
 *  @address 0x08020914
 */
void Battle_SetPartyAnimationMode3(struct BattleCombatant *combatant)
{
    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
        Battle_SetPartyAnimation(combatant, 3, combatant->animation.baseTile,
            combatant->animation.paletteSlot, 0);
}

/** @brief Select the ordinary enemy's approach width for its queued action.
 *  @note Callers must supply the normal enemy-record view; id > 11 alone does
 *  not exclude boss records. Other actions leave the existing width unchanged.
 *  @address 0x0802093C
 */
void Battle_UpdateEnemyAttackSpacingWidth(struct BattleCombatant *combatant)
{
    if (combatant->id > 11)
    {
        const struct BattleEnemyInfo *enemy = combatant->source.enemy;

        switch (combatant->action)
        {
        case BATTLE_ACTION_ATTACK:
            combatant->attackSpacingWidth = enemy->attackSpacingWidth;
            break;
        case BATTLE_ACTION_SKILL:
            combatant->attackSpacingWidth = enemy->skillSpacingWidth;
            break;
        }
    }
}

/** @brief Rebind an animation from the shared resource table at its current position.
 *  @note The argument is a BattleAnimation, not a whole BattleCombatant.
 *  @address 0x08020974
 */
void Battle_SetAnimationById(struct BattleAnimation *animation, u16 animationId, u16 baseTile,
    u8 paletteSlot, u16 flags)
{
    const struct BattleAnimationResourceConfig *config = &gBattleAnimationConfigs[animationId];

    Battle_SetupAnimation(animation, animation->x, animation->y, baseTile,
        paletteSlot, (const u16 *)config->animationData, config->palettes,
        config->graphicsId, config->graphicsBlockCount, flags);
}

/** @brief Add four gauge units to eligible party ids and request a display refresh.
 *  @note Tests the old value against 32; values 29..31 become 33..35, not 32.
 *  @address 0x080209C8
 */
void Battle_AddAttackGauge(struct BattleCombatant *combatant)
{
    if (combatant->id <= 6 && combatant->source.party.specialGauge < 32)
    {
        combatant->source.party.specialGauge += 4;
        combatant->displayState |= BATTLE_DISPLAY_GAUGE_DIRTY;
    }
}

/** @brief Clear the attack gauge for party ids zero through six.
 *  @note Marks the gauge display dirty even when the gauge was already zero.
 *  @address 0x080209EC
 */
void Battle_ResetAttackGauge(struct BattleCombatant *combatant)
{
    if (combatant->id <= 6)
    {
        combatant->source.party.specialGauge = 0;
        combatant->displayState |= BATTLE_DISPLAY_GAUGE_DIRTY;
    }
}

/** @brief Start the shared secondary effect with the selected palette variant.
 *  @note Uses the 0x0839B2A4 resource record: tile 0x1B4, palette bank 14,
 *  home coordinates, graphics base 0x541 and flag 2. The palette variant
 *  advances 16 u16 entries (32 bytes). Ordinary C matches all 0x70 bytes.
 *  @address 0x08020A0C
 */
void Battle_SetSharedEffectAnimation(struct BattleCombatant *combatant, u8 paletteVariant)
{
    const struct BattleSharedEffectResources *resources = &gBattleSharedEffectResources;

    Battle_SetupAnimation(&combatant->effectAnimation, combatant->homeX, combatant->homeY,
        0x1B4, 0xE, resources->definition, resources->palettes + paletteVariant * 16,
        0x541 + resources->graphicsId, resources->graphicsBlockCount, 2);
    combatant->effectAnimation.priority = 3;
    combatant->displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
}

/** @brief Test whether every occupied party slot has status two, four or eight.
 *  @note Empty slots are accepted; the original visits all five slots, including
 *  after a rejected status. A completely empty party therefore returns one.
 *  @address 0x08020A7C
 */
u8 Battle_ArePartyMembersDisabled(struct BattleCombatant *combatants)
{
    u8 allDisabled = 1;
    u8 slot;

    for (slot = 0; slot < BATTLE_PARTY_COUNT; slot++)
    {
        if ((u8)Battle_CheckStatusMask(&combatants[slot], 0x114) == BATTLE_STATUS_MATCH_REJECTED)
            allDisabled = 0;
    }
    return allDisabled;
}

/** @brief Check that escape has not removed every eligible party member.
 *  @note With no recorded escapes this returns one even for an empty party;
 *  otherwise at least one slot must satisfy the original 0x6B status mask.
 *  The supplied combatant pointer is unused; the getter provides the array.
 *  @address 0x08020AB0
 */
u8 Battle_HasRemainingPartyAfterEscape(struct BattleCombatant *unused)
{
    u8 targets[8];
    u8 count = Battle_ListCombatants(Battle_GetCombatants(), targets, BATTLE_SIDE_PARTY, 0x6B);
    u8 result;

    if (BattleAction_GetEscapedCombatantCount() != 0)
        result = count != 0;
    else
        result = 1;
    return result;
}

/** @brief Advance the turn counters of members waiting in the revival list.
 *  @note The sentinel has sort key FF and does not contain a combatant pointer.
 *  @address 0x08020AE4
 */
void Battle_AdvanceFaintedTurnCounts(void)
{
    struct BattleFaintedNode *node = (struct BattleFaintedNode *)sBattleFaintedList.next;

    while (node->link.sortKey <= 0xFE)
    {
        node->combatant->pendingDamage++;
        node = (struct BattleFaintedNode *)node->link.next;
    }
}

/** @brief Queue damage numbers for the current formation-filtered target list.
 *  @note The rules helper fills slot ids, not encoded action-target entries.
 *  @address 0x08020B04
 */
void Battle_AddTargetDamageNumbers(struct BattleCombatant *combatant)
{
    u8 targets[BATTLE_COMBATANT_COUNT];
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 count = Battle_ListActionTargetSlots(combatant, targets, 0x7F);
    u8 index;

    for (index = 0; index < count; index++)
        Battle_AddDamageNumber(&combatants[targets[index]]);
}

/** @brief Return the last queued enemy whose id is above the placeholder id 11.
 *  @note This is the remembered pointer, not a scan of the current queue.
 *  @address 0x08020B48
 */
struct BattleCombatant *Battle_GetLastDefeatedEnemy(void)
{
    return sBattleLastDefeatedEnemy;
}

/** @brief Clear all seven defeat slots and restart their processing cursor.
 *  @note The last-defeated-enemy pointer is deliberately not cleared here.
 *  @address 0x08020B54
 */
void Battle_ResetDefeatQueue(void)
{
    u8 index = 0;
    u8 *phase;
    u8 *count = &sBattleDefeatQueueCount;
    u8 *cursor = &sBattleDefeatQueueIndex;
    struct BattleCombatant **queue;

    phase = &sBattleDefeatPhase;
    queue = sBattleDefeatQueue;

    for (; index < BATTLE_ENEMY_COUNT; index++)
        queue[index] = NULL;
    *count = 0;
    *cursor = 0;
    *phase = 0;
}

/** @brief Append a defeat target and remember the last ordinary enemy.
 *  @note The action dispatcher owns the seven-entry capacity limit.
 *  @address 0x08020B90
 */
void Battle_QueueDefeatedCombatant(struct BattleCombatant *combatant)
{
    sBattleDefeatQueue[sBattleDefeatQueueCount] = combatant;
    sBattleDefeatQueueCount++;
    if (combatant->id > 11)
        sBattleLastDefeatedEnemy = combatant;
}

/** @brief Apply pending damage, returning 1 when HP is reduced to zero.
 *  @note The defeat check reads HP as s16; the surviving path stores a u16 difference.
 *  @address 0x08020BC0
 */
u8 Battle_ApplyPendingDamage(struct BattleCombatant *combatant)
{
    if ((s16)combatant->hp - combatant->pendingDamage <= 0)
    {
        combatant->hp = 0;
        return 1;
    }

    combatant->hp -= combatant->pendingDamage;
    return 0;
}

/** @brief Dispatch removal processing according to the first queued enemy's id class.
 *  @note Requires a nonempty queue containing an enemy; invalid id classes retain the original incoming r0 value.
 *  Ids 11..112 use the ordinary removal updater, 113..254 the extended-enemy handler.
 *  Ordinary C matches all 0x3C bytes.
 *  @address 0x08020BF0
 */
u8 Battle_UpdateQueuedEnemyDefeat(void)
{
    struct BattleCombatant *combatant = sBattleDefeatQueue[0];

    if ((u8)(combatant->id - 11) <= 0x65)
        return Battle_UpdateDefeatRemovalEffects();
    if ((u8)(combatant->id - 113) <= 0x8D)
        return Battle_UpdateBossDefeat();
}

/** @brief Call the extended enemy defeat handler for the first queued record.
 *  @note Requires a nonempty queue and a supported id at least 113; the handler receives that combatant.
 *  The seventeen-entry ROM table is indexed by id minus 113. Ordinary C matches
 *  all 0x2C bytes; the indirect call uses the automatic r1 helper entry.
 *  @address 0x08020C2C
 */
u8 Battle_UpdateBossDefeat(void)
{
    return gBattleBossDefeatHandlers[sBattleDefeatQueue[0]->id - 113](sBattleDefeatQueue[0]);
}

/** @brief Choose commands for the current encounter's enemy records.
 *  @note enemies starts at the enemy subarray. The original compares the entire
 *  displayState halfword with 8 and 5, not its low nibble or the status byte.
 *  @address 0x08020C58
 */
void Battle_ChooseEnemyCommands(struct BattleCombatant *enemies, struct BattleCombatant *combatants)
{
    u8 count = sBattleEncounter->enemyCount;
    u8 index;

    for (index = 0; index < count; index++)
    {
        struct BattleCombatant *enemy = (struct BattleCombatant *)(
            index * sizeof(struct BattleCombatant) + (u32)enemies);

        if (enemy->id != BATTLE_NO_COMBATANT && enemy->displayState != 8
            && enemy->displayState != 5)
        {
            if (!(Battle_GetControlFlags() & BATTLE_CONTROL_SPECIAL_ENCOUNTER))
                BattleAi_ChooseNormalEnemyCommand(enemy, combatants);
            else
                BattleAi_ChooseBossCommand(enemy, combatants);
        }
    }
}

/** @brief Set up the secondary animation and enable its combatant display bit.
 *  @note Resource pointers and sizes come from the shared 0x14-byte ROM records.
 *  @address 0x08020CC4
 */
void Battle_SetEffectAnimation(struct BattleCombatant *combatant, u8 x, u8 y, u16 baseTile,
    u8 paletteSlot, u16 animationId, u16 flags)
{
    Battle_SetupAnimation(&combatant->effectAnimation, x, y, baseTile, paletteSlot,
        (const u16 *)gBattleAnimationConfigs[animationId].animationData,
        gBattleAnimationConfigs[animationId].palettes,
        gBattleAnimationConfigs[animationId].graphicsId,
        gBattleAnimationConfigs[animationId].graphicsBlockCount, flags);
    combatant->displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
}

/** @brief Highlight a party status display with a newly allocated white BG pulse.
 *  @note The period is fixed at 16; the third argument is ignored.
 *  @address 0x08020D50
 */
void Battle_StartStatusHighlight(struct BattleCombatant *combatant, u8 mode, u8 unused)
{
    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
    {
        combatant->statusHighlightPalette = BattlePalette_StartBgPulse(
            Battle_GetStatusPaletteBank(combatant, mode), 1, 31, 31, 31, 16, 0,
            BATTLE_PALETTE_ALLOCATE_BANK);
        combatant->displayState |= BATTLE_DISPLAY_STATUS_HIGHLIGHT;
    }
}

/** @brief Restore the party status palette and clear its highlight flag.
 *  @note The source bank is recomputed from mode; it is not the allocated bank.
 *  @address 0x08020DA0
 */
void Battle_StopStatusHighlight(struct BattleCombatant *combatant, u8 mode)
{
    if (combatant->id < BATTLE_CHARACTER_ID_COUNT
        && combatant->displayState & BATTLE_DISPLAY_STATUS_HIGHLIGHT)
    {
        BattlePalette_StopBgPulse(Battle_GetStatusPaletteBank(combatant, mode), 1);
        combatant->displayState &= ~BATTLE_DISPLAY_STATUS_HIGHLIGHT;
    }
}

/** @brief Restart the shared combatant effect state machine.
 *  @note Only the phase byte is reset, not the effect target buffer.
 *  @address 0x08020DE4
 */
void Battle_ResetEffectPhase(void)
{
    sBattleEffectPhase = 0;
}

/**
 * @brief 生成本次行动的目标列表并统计有效项
 * @note 将本模块 03000730 缓冲传给 rules 的 08046480，列表项含目标编号和标志。
 * @address 0x08020DF0
 */
void Battle_BuildActionTargets(struct BattleCombatant *combatant)
{
    u8 targetSlot = combatant->targetSlot;
    u8 side = BATTLE_SIDE_ENEMY;
    u8 *targets;
    u8 index;

    if (targetSlot < BATTLE_PARTY_COUNT)
        side = BATTLE_SIDE_PARTY;
    targets = sBattleActionTargets;
    sBattleActionTargetCount = Battle_SelectActionTargets(combatant, targets, side);
    sBattleValidTargetCount = 0;
    for (index = 0; index < sBattleActionTargetCount; index++)
    {
        if (sBattleActionTargets[index] & BATTLE_TARGET_FLAGS_MASK)
            sBattleValidTargetCount++;
    }
}

/**
 * @brief 返回行动目标列表缓冲地址
 * @note 返回 03000730；actions 经此接口访问，不能只按直接 RAM 字面地址判断共享。
 * @address 0x08020E54
 */
u8 * Battle_GetActionTargets(void)
{
    return sBattleActionTargets;
}

/** @brief Return the number of encoded entries in the current action target list.
 *  @note Includes entries without a valid-target flag; not sBattleValidTargetCount.
 *  @address 0x08020E5C
 */
u8 Battle_GetActionTargetCount(void)
{
    return sBattleActionTargetCount;
}

/** @brief Return the encounter variant selected while the combatants were built.
 *  @note Returns the stored ROM pointer without copying the encounter record.
 *  @address 0x08020E68
 */
const struct BattleEncounter *Battle_GetEncounter(void)
{
    return sBattleEncounter;
}

/** @brief Clear all eleven party-character marks.
 *  @note Character ids zero and one have separate entries, unlike persistent party records.
 *  @address 0x08020E74
 */
void Battle_ResetCharacterMarks(void)
{
    u8 characterId;

    for (characterId = 0; characterId < BATTLE_CHARACTER_ID_COUNT; characterId++)
        sBattleCharacterMarks[characterId] = 0;
}

/** @brief Set a party character mark; enemy and empty record ids are ignored.
 *  @note Marks are indexed by character id, not by the five party slots.
 *  @address 0x08020E90
 */
void Battle_MarkCharacter(struct BattleCombatant *combatant)
{
    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
        sBattleCharacterMarks[combatant->id] = 1;
}

/** @brief Read the party character mark, or zero for an enemy or empty record.
 *  @note The query does not change the private mark table.
 *  @address 0x08020EAC
 */
u8 Battle_IsCharacterMarked(const struct BattleCombatant *combatant)
{
    u8 marked = 0;

    if (combatant->id < BATTLE_CHARACTER_ID_COUNT)
        marked = sBattleCharacterMarks[combatant->id];
    return marked;
}

/** @brief Reset the palette id list populated while the party records are built.
 *  @note Clears both the count and all eleven character-id entries.
 *  @address 0x08020EC8
 */
void Battle_ResetPartyPaletteIds(void)
{
    u8 index;

    sBattlePartyPaletteCount = 0;
    for (index = 0; index < BATTLE_CHARACTER_ID_COUNT; index++)
        sBattlePartyPaletteIds[index] = 0;
}

/** @brief Append one party palette id; the encounter builder controls list capacity.
 *  @note Stores a character id, not a hardware palette-bank number.
 *  @address 0x08020EEC
 */
void Battle_AppendPartyPaletteId(u8 characterId)
{
    sBattlePartyPaletteIds[sBattlePartyPaletteCount] = characterId;
    sBattlePartyPaletteCount++;
}

/** @brief Upload the collected party palettes to OBJ banks starting at one.
 *  @note The encounter builder populated the private character-id list.
 *  @address 0x08020F08
 */
void Battle_LoadPartyPalettes(void)
{
    u8 index;

    for (index = 0; index < sBattlePartyPaletteCount; index++)
        BattlePalette_LoadObjBanks(gBattlePartySpritePalettes[sBattlePartyPaletteIds[index]], index + 1, 1);
}

/** @brief Initialize the scene's cursor record and its shared movement workspace.
 *  @note Id 11 is a cursor placeholder; only the low state nibble of 0x31 is
 *  consumed by cursor animation setup. No party/enemy slot is allocated here.
 *  @address 0x08020F4C
 */
void Battle_InitCursor(struct BattleCombatant *cursor)
{
    cursor->formationSlot = 0;
    cursor->action = BATTLE_ACTION_NONE;
    cursor->displayState = 0;
    cursor->id = 11;
    cursor->animation.priority = 0;
    sBattleCursorMoveDuration = 0;
    sBattleCursorMoveFrame = 0;
    sBattleCursorStartX = 0;
    sBattleCursorStartY = 0;
    sBattleCursorDeltaX = 0;
    sBattleCursorDeltaY = 0;
    sBattleCursorMoveCurve = 0;
    Battle_SetCursorAnimation(cursor, 0x31);
}

/** @brief Start cursor interpolation from its current byte-sized screen position.
 *  @note The shared movement workspace belongs to this module; signed coordinate
 *  differences are stored as halfwords and the current frame restarts at zero.
 *  @address 0x08020FB8
 */
void Battle_MoveCursor(struct BattleCombatant *cursor, s16 x, s16 y, u16 duration, u8 curve)
{
    cursor->displayState &= ~BATTLE_CURSOR_MOTION_MASK;
    cursor->displayState |= BATTLE_CURSOR_MOVING;
    sBattleCursorMoveDuration = duration;
    sBattleCursorMoveFrame = 0;
    sBattleCursorStartX = cursor->animation.x;
    sBattleCursorStartY = cursor->animation.y;
    sBattleCursorDeltaX = x - cursor->animation.x;
    sBattleCursorDeltaY = y - cursor->animation.y;
    sBattleCursorMoveCurve = curve;
}

/** @brief Queue cursor movement toward a combatant's home position.
 *  @note The updater captures homeX/homeY-48 on its next frame. Only the
 *  movement nibble is replaced before caller flags are ORed in.
 *  @address 0x0802103C
 */
void Battle_SetCursorTarget(struct BattleCombatant *cursor, u8 targetSlot, u16 flags)
{
    cursor->displayState &= ~BATTLE_CURSOR_MOTION_MASK;
    cursor->displayState |= BATTLE_CURSOR_BEGIN_TARGET_MOVE;
    cursor->displayState |= flags;
    cursor->targetSlot = targetSlot;
}

/** @brief Clear damage-number slots and load their selected palette into OBJ bank 15.
 *  @note The palette table is shared with scene setup and declared by battle.h.
 *  @address 0x08021064
 */
void Battle_ResetDamageNumbers(u8 paletteId)
{
    u8 index;
    const u16 *palettes;

    sBattleDamageNumberCount = 0;
    sBattleDamageNumberBounce = 1;
    sBattleDamageNumberFrame = 0;
    index = 0;
    palettes = D_0861C664;
    for (; index < BATTLE_DAMAGE_NUMBER_COUNT; index++)
    {
        sBattleDamageNumbers[index].value = 0;
        sBattleDamageNumbers[index].x = 0;
        sBattleDamageNumbers[index].y = 0;
    }
    BattlePalette_LoadObjBanks((const u16 *)(paletteId * BATTLE_PALETTE_COLORS_PER_BANK
        * sizeof(u16) + (u32)palettes), 15, 1);
}

/** @brief Alternate original entry for the shared secondary effect.
 *  @note Behavior and encoding are identical to Battle_SetSharedEffectAnimation;
 *  no second resource is allocated. Ordinary C matches all 0x70 bytes.
 *  @address 0x080210C0
 */
void Battle_SetSharedEffectAnimationAlternate(struct BattleCombatant *combatant, u8 paletteVariant)
{
    const struct BattleSharedEffectResources *resources = &gBattleSharedEffectResources;

    Battle_SetupAnimation(&combatant->effectAnimation, combatant->homeX, combatant->homeY,
        0x1B4, 0xE, resources->definition, resources->palettes + paletteVariant * 16,
        0x541 + resources->graphicsId, resources->graphicsBlockCount, 2);
    combatant->effectAnimation.priority = 3;
    combatant->displayState |= BATTLE_DISPLAY_EFFECT_ANIMATION;
}

/* Split read-only data, in ROM address order. */

/* ROM 0x083939F8..0x08393A05; .rodata. */
const u8 D_083939F8[13] __attribute__((aligned(1))) =
{ 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B };

/* ROM 0x08393A18..0x08393A3C; .rodata.00393A18. */
const u8 D_08393A18[12] __attribute__((section(".rodata.00393A18"), aligned(1))) =
{ 0x01, 0x02, 0x04, 0x08, 0x02, 0x04, 0x04, 0x08, 0x01, 0x01, 0x02, 0x04 };

const u8 gBattleObjHeightTiles[12] __attribute__((section(".rodata.00393A18"), aligned(1))) =
{ 0x01, 0x02, 0x04, 0x08, 0x01, 0x01, 0x02, 0x04, 0x02, 0x04, 0x04, 0x08 };

const u8 gBattleObjTileCounts[12] __attribute__((section(".rodata.00393A18"), aligned(1))) =
{ 0x01, 0x04, 0x10, 0x40, 0x02, 0x04, 0x08, 0x20, 0x02, 0x04, 0x08, 0x20 };

/* ROM 0x08393A3C..0x08393A40; .rodata.00393A3C. */
const u8 D_08393A3C[] __attribute__((section(".rodata.00393A3C"), aligned(1))) =
{ 0x04, 0x06, 0x08, 0x00 };

/* ROM 0x08393A44..0x08393A52; .rodata.00393A44. */
const u8 D_08393A44[] __attribute__((section(".rodata.00393A44"), aligned(1))) =
{ 0x10, 0x24, 0x40, 0x00 };

const u8 gBattlePartyHomeX[BATTLE_PARTY_COUNT] __attribute__((section(".rodata.00393A44"), aligned(1))) =
{ 0x9A, 0xAE, 0xBA, 0xD6, 0xDA };

const u8 gBattlePartyHomeY[BATTLE_PARTY_COUNT] __attribute__((section(".rodata.00393A44"), aligned(1))) =
{ 0x4A, 0x74, 0x58, 0x50, 0x7A };

/* ROM 0x08393B14..0x08393B19; .rodata.00393B14. */
const u8 D_08393B14[] __attribute__((section(".rodata.00393B14"), aligned(1))) =
{ 0x31, 0x11, 0x22, 0x33, 0x13 };

/* ROM 0x083988A8..0x083989B0; .rodata.003988A8. */
const struct BattleCharacterActionInfo gBattleCharacterActionInfo[BATTLE_CHARACTER_ID_COUNT] __attribute__((section(".rodata.003988A8"), aligned(1))) =
{
    {
        { 0x00, 0x01, 0x01 },
        { 0x02, 0x05, 0x05 },
        0x08,
        0x09,
        0x0A,
        { 0x02, 0x03, 0x01 },
        { 0x1E, 0x0A, 0x05 },
        0x00,
        0x000B,
        0x000C,
        0x000D,
        0x000E,
    },
    {
        { 0x0F, 0x10, 0x10 },
        { 0x11, 0x13, 0x13 },
        0x16,
        0x17,
        0x18,
        { 0x02, 0x03, 0x03 },
        { 0x1E, 0x0F, 0x0A },
        0x00,
        0x0019,
        0x001A,
        0x001B,
        0x001C,
    },
    {
        { 0x1D, 0x1E, 0x1E },
        { 0x1F, 0x22, 0x22 },
        0x25,
        0x26,
        0x27,
        { 0x03, 0x03, 0x03 },
        { 0x1E, 0x0A, 0x05 },
        0x00,
        0x0028,
        0x0029,
        0x002A,
        0x002B,
    },
    {
        { 0x2C, 0x2D, 0x2D },
        { 0x2E, 0x31, 0x31 },
        0x33,
        0x34,
        0x35,
        { 0x02, 0x02, 0x02 },
        { 0x1E, 0x0A, 0x05 },
        0x00,
        0x0036,
        0x0037,
        0x0038,
        0x0039,
    },
    {
        { 0x3A, 0x3B, 0x3C },
        { 0x3D, 0x3E, 0x3F },
        0x46,
        0x47,
        0x48,
        { 0x01, 0x01, 0x01 },
        { 0x1E, 0x0A, 0x05 },
        0x00,
        0x0049,
        0x004A,
        0x004B,
        0x004C,
    },
    {
        { 0x4D, 0x4E, 0x4F },
        { 0x50, 0x53, 0x56 },
        0x59,
        0x5A,
        0x5B,
        { 0x03, 0x03, 0x03 },
        { 0x1E, 0x0A, 0x05 },
        0x00,
        0x005C,
        0x005D,
        0x005E,
        0x005F,
    },
    {
        { 0x60, 0x61, 0x62 },
        { 0x63, 0x66, 0x69 },
        0x6B,
        0x6C,
        0x6D,
        { 0x03, 0x03, 0x02 },
        { 0x1E, 0x1E, 0x0A },
        0x00,
        0x006E,
        0x006F,
        0x0070,
        0x0071,
    },
    {
        { 0x80, 0x81, 0x81 },
        { 0x82, 0x83, 0x83 },
        0x86,
        0x87,
        0x88,
        { 0x01, 0x03, 0x03 },
        { 0x1E, 0x0A, 0x05 },
        0x00,
        0x0089,
        0x008A,
        0x008B,
        0x008E,
    },
    {
        { 0x72, 0x73, 0x73 },
        { 0x74, 0x76, 0x76 },
        0x79,
        0x7A,
        0x7B,
        { 0x02, 0x03, 0x03 },
        { 0x1E, 0x0A, 0x05 },
        0x00,
        0x007C,
        0x007D,
        0x007E,
        0x007F,
    },
    {
        { 0x8F, 0x90, 0x90 },
        { 0x91, 0x91, 0x91 },
        0x93,
        0x94,
        0x95,
        { 0x03, 0x02, 0x03 },
        { 0x1E, 0x0A, 0x05 },
        0x00,
        0x0096,
        0x0097,
        0x0098,
        0x0099,
    },
    {
        { 0x9A, 0x9B, 0x9B },
        { 0x9C, 0x9C, 0x9C },
        0x9F,
        0xA0,
        0xA1,
        { 0x03, 0x03, 0x03 },
        { 0x1E, 0x0A, 0x05 },
        0x00,
        0x00A2,
        0x00A3,
        0x00A4,
        0x00A5,
    },
};

/* ROM 0x083989B0..0x083989FC; .rodata.003989B0. */
const u8 D_083989B0[] __attribute__((section(".rodata.003989B0"), aligned(1))) =
{
    0x00, 0x04, 0x05, 0x06, 0x07, 0x09, 0x0A, 0x21, 0x0B, 0x01, 0x0C, 0x0E, 0x0D, 0x08, 0x0F, 0x22, 0x11, 0x14,
    0x20, 0x15, 0x16, 0x17, 0x18, 0x03, 0x22, 0x23, 0x05,
};

const u8 D_083989CB[] __attribute__((section(".rodata.003989B0"), aligned(1))) =
{ 0x04, 0x1E, 0x1F, 0x06, 0x09, 0x19, 0x0C, 0x1A, 0x11, 0x08, 0x1B, 0x1B, 0x17, 0x1C, 0x1D, 0x23, 0x14 };

const u8 D_083989DC[] __attribute__((section(".rodata.003989B0"), aligned(1))) =
{
    0x04, 0x04, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x13, 0x12, 0x08, 0x00, 0x01, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
    0x0D, 0x0D, 0x0D, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x14, 0x00, 0x00,
};

/* ROM 0x08399B58..0x0839B2E0; .rodata.00399B58. */
const u16 D_08399B58[][2] __attribute__((section(".rodata.00399B58"), aligned(1))) =
{
    { 0x0000, 0x0000 },
    { 0x0001, 0x0006 },
    { 0x0007, 0x0008 },
    { 0x0009, 0x000E },
    { 0x000F, 0x0016 },
    { 0x0017, 0x001E },
    { 0x001F, 0x0026 },
    { 0x0027, 0x002E },
    { 0x002F, 0x0036 },
    { 0x0037, 0x003D },
    { 0x003E, 0x0042 },
    { 0x0043, 0x0046 },
    { 0x0047, 0x004B },
    { 0x004C, 0x0053 },
    { 0x0054, 0x005A },
    { 0x005B, 0x005F },
    { 0x0060, 0x0066 },
    { 0x0067, 0x006C },
    { 0x006D, 0x0074 },
    { 0x0075, 0x007B },
    { 0x007C, 0x0081 },
    { 0x0082, 0x0089 },
    { 0x008A, 0x008E },
    { 0x008F, 0x0092 },
    { 0x0093, 0x0096 },
    { 0x0097, 0x009A },
    { 0x0097, 0x009A },
    { 0x0082, 0x0089 },
    { 0x0007, 0x0007 },
    { 0x0008, 0x0008 },
    { 0x001F, 0x001F },
    { 0x0020, 0x0020 },
    { 0x0021, 0x0021 },
    { 0x0022, 0x0022 },
    { 0x0023, 0x0023 },
    { 0x0024, 0x0024 },
    { 0x009B, 0x009B },
    { 0x009C, 0x009C },
    { 0x009D, 0x009D },
    { 0x009E, 0x009E },
    { 0x0042, 0x0042 },
    { 0x004C, 0x004C },
    { 0x004D, 0x004D },
    { 0x004E, 0x004E },
    { 0x004F, 0x004F },
    { 0x0050, 0x0050 },
    { 0x0051, 0x0051 },
    { 0x0052, 0x0052 },
    { 0x0053, 0x0053 },
    { 0x002F, 0x002F },
    { 0x0030, 0x0030 },
    { 0x0031, 0x0031 },
    { 0x0032, 0x0032 },
    { 0x0033, 0x0033 },
    { 0x0034, 0x0034 },
    { 0x0035, 0x0035 },
    { 0x0036, 0x0036 },
    { 0x009F, 0x009F },
    { 0x00A0, 0x00A0 },
    { 0x00A1, 0x00A1 },
    { 0x00A2, 0x00A2 },
    { 0x00A3, 0x00A3 },
    { 0x00A4, 0x00A4 },
    { 0x00A5, 0x00A5 },
    { 0x00A6, 0x00A6 },
    { 0x00A7, 0x00A7 },
    { 0x00A8, 0x00A8 },
    { 0x00A9, 0x00A9 },
    { 0x00AA, 0x00AA },
    { 0x00AB, 0x00AB },
    { 0x00AC, 0x00AC },
    { 0x00AD, 0x00AD },
    { 0x00AE, 0x00AE },
    { 0x00AF, 0x00AF },
    { 0x00B0, 0x00B0 },
};

const struct BattleEncounter gBattleEncounters[] __attribute__((section(".rodata.00399B58"), aligned(1))) =
{
    {
        0x01,
        {
            { 0x0D, 0x38, 0x70, 0x22 },
            { 0x0E, 0x60, 0x78, 0x11 },
            { 0x0E, 0x40, 0x50, 0x32 },
            { 0x0E, 0x30, 0x70, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x0E, 0x60, 0x48, 0x31 },
            { 0x0E, 0x60, 0x78, 0x11 },
            { 0x0E, 0x40, 0x50, 0x32 },
            { 0x0E, 0x30, 0x70, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x0E, 0x58, 0x58, 0x31 },
            { 0x0E, 0x40, 0x78, 0x11 },
            { 0x0D, 0x20, 0x60, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x05,
        {
            { 0x0E, 0x60, 0x48, 0x31 },
            { 0x0E, 0x58, 0x60, 0x21 },
            { 0x0E, 0x60, 0x80, 0x11 },
            { 0x0D, 0x20, 0x58, 0x32 },
            { 0x0D, 0x28, 0x80, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x0E, 0x58, 0x50, 0x31 },
            { 0x11, 0x30, 0x78, 0x12 },
            { 0x11, 0x28, 0x58, 0x33 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x11, 0x58, 0x58, 0x31 },
            { 0x0E, 0x60, 0x78, 0x11 },
            { 0x0E, 0x28, 0x50, 0x32 },
            { 0x0E, 0x38, 0x70, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x11, 0x48, 0x58, 0x31 },
            { 0x0D, 0x38, 0x80, 0x12 },
            { 0x11, 0x20, 0x60, 0x33 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x0F, 0x60, 0x48, 0x31 },
            { 0x0F, 0x60, 0x78, 0x11 },
            { 0x0F, 0x40, 0x50, 0x32 },
            { 0x0F, 0x30, 0x70, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x05,
        {
            { 0x0F, 0x48, 0x48, 0x31 },
            { 0x0F, 0x50, 0x80, 0x21 },
            { 0x0F, 0x30, 0x68, 0x22 },
            { 0x10, 0x20, 0x50, 0x32 },
            { 0x10, 0x18, 0x78, 0x13 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x05,
        {
            { 0x14, 0x50, 0x58, 0x31 },
            { 0x14, 0x58, 0x70, 0x21 },
            { 0x14, 0x48, 0x80, 0x11 },
            { 0x14, 0x18, 0x80, 0x32 },
            { 0x14, 0x18, 0x60, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x13, 0x58, 0x58, 0x31 },
            { 0x13, 0x50, 0x78, 0x11 },
            { 0x13, 0x20, 0x60, 0x32 },
            { 0x13, 0x18, 0x80, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x15, 0x58, 0x70, 0x21 },
            { 0x15, 0x38, 0x58, 0x32 },
            { 0x15, 0x20, 0x78, 0x13 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x05,
        {
            { 0x15, 0x50, 0x58, 0x31 },
            { 0x15, 0x58, 0x70, 0x21 },
            { 0x15, 0x48, 0x80, 0x11 },
            { 0x15, 0x18, 0x80, 0x32 },
            { 0x15, 0x18, 0x60, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x05,
        {
            { 0x15, 0x58, 0x70, 0x21 },
            { 0x12, 0x38, 0x50, 0x32 },
            { 0x12, 0x38, 0x68, 0x22 },
            { 0x12, 0x38, 0x80, 0x12 },
            { 0x15, 0x20, 0x70, 0x23 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x12, 0x58, 0x58, 0x31 },
            { 0x12, 0x58, 0x80, 0x11 },
            { 0x12, 0x30, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x16, 0x60, 0x60, 0x21 },
            { 0x16, 0x38, 0x50, 0x32 },
            { 0x16, 0x30, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x16, 0x60, 0x48, 0x31 },
            { 0x16, 0x60, 0x78, 0x11 },
            { 0x16, 0x30, 0x58, 0x32 },
            { 0x16, 0x38, 0x70, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x19, 0x60, 0x60, 0x21 },
            { 0x19, 0x40, 0x50, 0x32 },
            { 0x19, 0x40, 0x78, 0x12 },
            { 0x19, 0x20, 0x68, 0x23 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x05,
        {
            { 0x19, 0x60, 0x48, 0x31 },
            { 0x19, 0x60, 0x78, 0x21 },
            { 0x19, 0x50, 0x60, 0x11 },
            { 0x19, 0x28, 0x58, 0x32 },
            { 0x19, 0x30, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x19, 0x50, 0x48, 0x31 },
            { 0x19, 0x60, 0x60, 0x21 },
            { 0x19, 0x48, 0x78, 0x11 },
            { 0x17, 0x20, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x16, 0x60, 0x48, 0x31 },
            { 0x19, 0x60, 0x78, 0x11 },
            { 0x16, 0x40, 0x50, 0x32 },
            { 0x16, 0x28, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x16, 0x60, 0x50, 0x31 },
            { 0x16, 0x60, 0x78, 0x11 },
            { 0x18, 0x40, 0x68, 0x22 },
            { 0x16, 0x20, 0x58, 0x23 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x05,
        {
            { 0x16, 0x50, 0x48, 0x31 },
            { 0x16, 0x60, 0x60, 0x21 },
            { 0x16, 0x50, 0x78, 0x11 },
            { 0x18, 0x30, 0x50, 0x32 },
            { 0x18, 0x30, 0x70, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x1A, 0x60, 0x48, 0x31 },
            { 0x1A, 0x58, 0x70, 0x11 },
            { 0x1A, 0x38, 0x50, 0x32 },
            { 0x1A, 0x20, 0x70, 0x23 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x06,
        {
            { 0x1A, 0x60, 0x48, 0x31 },
            { 0x1A, 0x58, 0x60, 0x21 },
            { 0x1A, 0x60, 0x78, 0x11 },
            { 0x1A, 0x38, 0x68, 0x22 },
            { 0x1A, 0x18, 0x50, 0x33 },
            { 0x1A, 0x18, 0x78, 0x13 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x1D, 0x60, 0x48, 0x31 },
            { 0x1D, 0x50, 0x60, 0x21 },
            { 0x1D, 0x58, 0x78, 0x11 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x1D, 0x58, 0x50, 0x31 },
            { 0x1D, 0x50, 0x78, 0x11 },
            { 0x1D, 0x30, 0x58, 0x32 },
            { 0x1D, 0x28, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x1A, 0x60, 0x48, 0x31 },
            { 0x1A, 0x60, 0x78, 0x11 },
            { 0x1B, 0x30, 0x58, 0x32 },
            { 0x1B, 0x28, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x05,
        {
            { 0x1D, 0x60, 0x48, 0x31 },
            { 0x1D, 0x58, 0x60, 0x21 },
            { 0x1D, 0x60, 0x78, 0x11 },
            { 0x1B, 0x30, 0x58, 0x32 },
            { 0x1B, 0x28, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x1C, 0x58, 0x58, 0x31 },
            { 0x1C, 0x38, 0x78, 0x12 },
            { 0x1C, 0x20, 0x60, 0x23 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x1D, 0x60, 0x48, 0x31 },
            { 0x1D, 0x60, 0x78, 0x11 },
            { 0x1C, 0x38, 0x58, 0x32 },
            { 0x1C, 0x28, 0x78, 0x13 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x1E, 0x58, 0x58, 0x31 },
            { 0x1E, 0x50, 0x78, 0x11 },
            { 0x1E, 0x28, 0x60, 0x32 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x21, 0x58, 0x58, 0x31 },
            { 0x21, 0x30, 0x78, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x21, 0x58, 0x58, 0x31 },
            { 0x21, 0x50, 0x78, 0x11 },
            { 0x21, 0x28, 0x58, 0x32 },
            { 0x21, 0x20, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x1F, 0x50, 0x50, 0x31 },
            { 0x1F, 0x28, 0x70, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x1F, 0x58, 0x50, 0x31 },
            { 0x1F, 0x50, 0x78, 0x11 },
            { 0x1F, 0x30, 0x48, 0x32 },
            { 0x1F, 0x28, 0x70, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x05,
        {
            { 0x1F, 0x60, 0x48, 0x31 },
            { 0x1F, 0x50, 0x68, 0x21 },
            { 0x1F, 0x60, 0x80, 0x11 },
            { 0x1F, 0x30, 0x58, 0x32 },
            { 0x1F, 0x30, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x1E, 0x58, 0x58, 0x31 },
            { 0x1E, 0x50, 0x78, 0x11 },
            { 0x20, 0x30, 0x70, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x20, 0x50, 0x68, 0x31 },
            { 0x20, 0x28, 0x78, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x22, 0x48, 0x50, 0x32 },
            { 0x22, 0x60, 0x78, 0x11 },
            { 0x22, 0x20, 0x58, 0x33 },
            { 0x23, 0x30, 0x80, 0x13 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x05,
        {
            { 0x23, 0x60, 0x50, 0x31 },
            { 0x23, 0x60, 0x78, 0x11 },
            { 0x22, 0x38, 0x48, 0x32 },
            { 0x22, 0x38, 0x80, 0x12 },
            { 0x22, 0x18, 0x60, 0x23 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x22, 0x60, 0x50, 0x31 },
            { 0x25, 0x60, 0x78, 0x11 },
            { 0x22, 0x38, 0x50, 0x32 },
            { 0x25, 0x38, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x05,
        {
            { 0x22, 0x60, 0x48, 0x31 },
            { 0x22, 0x60, 0x78, 0x11 },
            { 0x25, 0x40, 0x60, 0x22 },
            { 0x25, 0x28, 0x48, 0x33 },
            { 0x25, 0x28, 0x78, 0x13 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x22, 0x60, 0x48, 0x31 },
            { 0x22, 0x60, 0x78, 0x11 },
            { 0x24, 0x38, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x22, 0x60, 0x48, 0x31 },
            { 0x24, 0x30, 0x58, 0x32 },
            { 0x24, 0x30, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x22, 0x60, 0x48, 0x31 },
            { 0x22, 0x60, 0x70, 0x11 },
            { 0x22, 0x30, 0x50, 0x32 },
            { 0x22, 0x38, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x05,
        {
            { 0x22, 0x60, 0x48, 0x31 },
            { 0x22, 0x60, 0x78, 0x11 },
            { 0x22, 0x48, 0x60, 0x22 },
            { 0x22, 0x20, 0x50, 0x33 },
            { 0x22, 0x30, 0x78, 0x13 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x27, 0x58, 0x58, 0x31 },
            { 0x27, 0x48, 0x78, 0x11 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x27, 0x58, 0x58, 0x31 },
            { 0x27, 0x50, 0x78, 0x11 },
            { 0x27, 0x28, 0x60, 0x32 },
            { 0x27, 0x30, 0x80, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x29, 0x58, 0x58, 0x31 },
            { 0x29, 0x48, 0x78, 0x11 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x29, 0x58, 0x68, 0x21 },
            { 0x29, 0x30, 0x58, 0x32 },
            { 0x29, 0x38, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x26, 0x58, 0x58, 0x31 },
            { 0x26, 0x50, 0x78, 0x11 },
            { 0x26, 0x28, 0x60, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x26, 0x58, 0x58, 0x31 },
            { 0x26, 0x48, 0x78, 0x11 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x28, 0x58, 0x58, 0x31 },
            { 0x28, 0x48, 0x78, 0x11 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x28, 0x58, 0x58, 0x31 },
            { 0x28, 0x50, 0x78, 0x11 },
            { 0x28, 0x28, 0x60, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x2B, 0x60, 0x48, 0x31 },
            { 0x2A, 0x60, 0x78, 0x11 },
            { 0x2A, 0x28, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x2D, 0x50, 0x70, 0x21 },
            { 0x2D, 0x30, 0x78, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x2C, 0x50, 0x70, 0x21 },
            { 0x2C, 0x28, 0x78, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x2A, 0x60, 0x50, 0x31 },
            { 0x2A, 0x60, 0x78, 0x11 },
            { 0x2C, 0x30, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x2A, 0x60, 0x60, 0x21 },
            { 0x2B, 0x48, 0x50, 0x32 },
            { 0x2B, 0x50, 0x78, 0x12 },
            { 0x2B, 0x30, 0x68, 0x23 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x2A, 0x60, 0x78, 0x11 },
            { 0x2A, 0x48, 0x50, 0x32 },
            { 0x2A, 0x30, 0x68, 0x23 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x2B, 0x60, 0x50, 0x31 },
            { 0x2B, 0x60, 0x78, 0x11 },
            { 0x2B, 0x40, 0x58, 0x32 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x30, 0x58, 0x60, 0x31 },
            { 0x30, 0x48, 0x78, 0x12 },
            { 0x30, 0x28, 0x68, 0x23 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x30, 0x50, 0x60, 0x31 },
            { 0x30, 0x58, 0x78, 0x11 },
            { 0x30, 0x20, 0x58, 0x32 },
            { 0x30, 0x30, 0x80, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x31, 0x60, 0x48, 0x31 },
            { 0x31, 0x60, 0x70, 0x11 },
            { 0x31, 0x38, 0x50, 0x32 },
            { 0x31, 0x30, 0x68, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x2F, 0x60, 0x50, 0x31 },
            { 0x2F, 0x58, 0x78, 0x11 },
            { 0x2F, 0x40, 0x48, 0x32 },
            { 0x2F, 0x38, 0x70, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x2F, 0x60, 0x50, 0x31 },
            { 0x2F, 0x58, 0x78, 0x11 },
            { 0x2E, 0x28, 0x58, 0x33 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x34, 0x58, 0x58, 0x31 },
            { 0x34, 0x50, 0x70, 0x11 },
            { 0x34, 0x28, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x32, 0x60, 0x58, 0x21 },
            { 0x32, 0x48, 0x50, 0x32 },
            { 0x32, 0x50, 0x78, 0x12 },
            { 0x35, 0x28, 0x70, 0x23 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x32, 0x58, 0x50, 0x31 },
            { 0x32, 0x60, 0x70, 0x11 },
            { 0x33, 0x28, 0x58, 0x32 },
            { 0x33, 0x38, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x32, 0x60, 0x58, 0x31 },
            { 0x32, 0x60, 0x78, 0x11 },
            { 0x32, 0x40, 0x50, 0x32 },
            { 0x32, 0x38, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x37, 0x50, 0x70, 0x21 },
            { 0x37, 0x30, 0x78, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x38, 0x60, 0x50, 0x31 },
            { 0x38, 0x58, 0x70, 0x11 },
            { 0x38, 0x38, 0x48, 0x32 },
            { 0x38, 0x30, 0x70, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x38, 0x60, 0x50, 0x31 },
            { 0x38, 0x58, 0x78, 0x11 },
            { 0x38, 0x38, 0x58, 0x32 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x36, 0x58, 0x78, 0x11 },
            { 0x36, 0x48, 0x60, 0x32 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x36, 0x58, 0x60, 0x31 },
            { 0x36, 0x48, 0x78, 0x12 },
            { 0x36, 0x30, 0x58, 0x33 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x3C, 0x60, 0x50, 0x31 },
            { 0x3C, 0x58, 0x68, 0x21 },
            { 0x3C, 0x60, 0x78, 0x11 },
            { 0x3A, 0x38, 0x70, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x3A, 0x50, 0x58, 0x31 },
            { 0x3A, 0x58, 0x78, 0x11 },
            { 0x3A, 0x30, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x39, 0x50, 0x78, 0x11 },
            { 0x39, 0x30, 0x60, 0x32 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x39, 0x50, 0x58, 0x31 },
            { 0x39, 0x58, 0x78, 0x11 },
            { 0x39, 0x30, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x3C, 0x60, 0x50, 0x31 },
            { 0x3C, 0x58, 0x78, 0x11 },
            { 0x3C, 0x38, 0x58, 0x32 },
            { 0x3C, 0x28, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x3C, 0x60, 0x58, 0x31 },
            { 0x3C, 0x58, 0x78, 0x11 },
            { 0x3C, 0x38, 0x60, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x3B, 0x58, 0x60, 0x31 },
            { 0x3B, 0x48, 0x78, 0x11 },
            { 0x3B, 0x30, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x3B, 0x58, 0x60, 0x31 },
            { 0x3B, 0x30, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x3D, 0x58, 0x60, 0x31 },
            { 0x3D, 0x48, 0x78, 0x11 },
            { 0x3E, 0x28, 0x58, 0x32 },
            { 0x3E, 0x20, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x3F, 0x60, 0x60, 0x21 },
            { 0x3F, 0x48, 0x50, 0x32 },
            { 0x3F, 0x40, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x3F, 0x60, 0x50, 0x31 },
            { 0x3F, 0x60, 0x78, 0x11 },
            { 0x3F, 0x40, 0x58, 0x32 },
            { 0x3F, 0x38, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x40, 0x60, 0x60, 0x21 },
            { 0x40, 0x48, 0x50, 0x32 },
            { 0x40, 0x40, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x40, 0x60, 0x50, 0x31 },
            { 0x40, 0x60, 0x78, 0x11 },
            { 0x40, 0x40, 0x58, 0x32 },
            { 0x40, 0x38, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x3D, 0x58, 0x60, 0x31 },
            { 0x3D, 0x38, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x41, 0x50, 0x60, 0x31 },
            { 0x42, 0x58, 0x78, 0x11 },
            { 0x42, 0x30, 0x70, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x47, 0x60, 0x50, 0x31 },
            { 0x47, 0x58, 0x60, 0x21 },
            { 0x47, 0x60, 0x78, 0x11 },
            { 0x44, 0x30, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x44, 0x58, 0x78, 0x11 },
            { 0x44, 0x48, 0x60, 0x32 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x05,
        {
            { 0x47, 0x60, 0x50, 0x31 },
            { 0x47, 0x50, 0x60, 0x21 },
            { 0x47, 0x58, 0x78, 0x11 },
            { 0x45, 0x30, 0x58, 0x32 },
            { 0x45, 0x28, 0x70, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x05,
        {
            { 0x47, 0x60, 0x50, 0x31 },
            { 0x47, 0x58, 0x60, 0x21 },
            { 0x47, 0x50, 0x78, 0x11 },
            { 0x46, 0x38, 0x50, 0x32 },
            { 0x46, 0x28, 0x70, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x47, 0x60, 0x50, 0x31 },
            { 0x47, 0x58, 0x60, 0x21 },
            { 0x47, 0x48, 0x78, 0x11 },
            { 0x47, 0x38, 0x50, 0x32 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x4B, 0x60, 0x50, 0x31 },
            { 0x4B, 0x58, 0x78, 0x11 },
            { 0x4B, 0x38, 0x58, 0x32 },
            { 0x4B, 0x30, 0x70, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x05,
        {
            { 0x4B, 0x60, 0x50, 0x31 },
            { 0x4B, 0x60, 0x78, 0x11 },
            { 0x4B, 0x48, 0x60, 0x32 },
            { 0x4B, 0x30, 0x60, 0x33 },
            { 0x4B, 0x28, 0x70, 0x13 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x4A, 0x60, 0x50, 0x31 },
            { 0x4A, 0x58, 0x70, 0x11 },
            { 0x49, 0x30, 0x60, 0x32 },
            { 0x49, 0x28, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x4A, 0x60, 0x50, 0x31 },
            { 0x4A, 0x58, 0x78, 0x11 },
            { 0x4A, 0x38, 0x58, 0x32 },
            { 0x49, 0x28, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x48, 0x60, 0x50, 0x31 },
            { 0x48, 0x58, 0x70, 0x11 },
            { 0x48, 0x30, 0x50, 0x32 },
            { 0x48, 0x38, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x49, 0x58, 0x60, 0x31 },
            { 0x49, 0x50, 0x78, 0x11 },
            { 0x49, 0x30, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x48, 0x60, 0x50, 0x31 },
            { 0x48, 0x58, 0x78, 0x11 },
            { 0x49, 0x30, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x4D, 0x58, 0x60, 0x31 },
            { 0x4D, 0x50, 0x78, 0x11 },
            { 0x4D, 0x30, 0x60, 0x32 },
            { 0x4E, 0x28, 0x70, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x4C, 0x50, 0x70, 0x21 },
            { 0x4C, 0x30, 0x80, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x4D, 0x58, 0x60, 0x31 },
            { 0x4D, 0x50, 0x78, 0x11 },
            { 0x4F, 0x28, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x4D, 0x50, 0x60, 0x31 },
            { 0x4D, 0x58, 0x78, 0x11 },
            { 0x4F, 0x28, 0x58, 0x32 },
            { 0x4F, 0x28, 0x80, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x4D, 0x58, 0x60, 0x31 },
            { 0x4D, 0x38, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x4D, 0x48, 0x60, 0x31 },
            { 0x4D, 0x58, 0x80, 0x11 },
            { 0x4D, 0x28, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x55, 0x58, 0x60, 0x31 },
            { 0x55, 0x50, 0x78, 0x11 },
            { 0x52, 0x28, 0x78, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x55, 0x58, 0x60, 0x31 },
            { 0x52, 0x40, 0x78, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x53, 0x48, 0x60, 0x31 },
            { 0x53, 0x58, 0x78, 0x11 },
            { 0x53, 0x30, 0x70, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x53, 0x58, 0x60, 0x31 },
            { 0x53, 0x50, 0x78, 0x11 },
            { 0x53, 0x20, 0x60, 0x32 },
            { 0x53, 0x20, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x54, 0x58, 0x60, 0x31 },
            { 0x54, 0x50, 0x78, 0x11 },
            { 0x54, 0x30, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x54, 0x50, 0x60, 0x31 },
            { 0x54, 0x58, 0x78, 0x11 },
            { 0x54, 0x28, 0x68, 0x32 },
            { 0x54, 0x30, 0x80, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x55, 0x58, 0x60, 0x31 },
            { 0x55, 0x40, 0x78, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x55, 0x58, 0x60, 0x31 },
            { 0x55, 0x50, 0x80, 0x11 },
            { 0x55, 0x28, 0x58, 0x32 },
            { 0x55, 0x28, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x56, 0x50, 0x70, 0x31 },
            { 0x57, 0x20, 0x58, 0x32 },
            { 0x57, 0x28, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x59, 0x60, 0x50, 0x31 },
            { 0x59, 0x58, 0x78, 0x11 },
            { 0x58, 0x30, 0x60, 0x32 },
            { 0x58, 0x28, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x59, 0x60, 0x50, 0x31 },
            { 0x59, 0x60, 0x78, 0x11 },
            { 0x59, 0x48, 0x60, 0x22 },
            { 0x58, 0x28, 0x68, 0x23 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x59, 0x60, 0x68, 0x21 },
            { 0x59, 0x50, 0x50, 0x32 },
            { 0x59, 0x48, 0x78, 0x12 },
            { 0x59, 0x30, 0x60, 0x23 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x05,
        {
            { 0x59, 0x60, 0x50, 0x31 },
            { 0x59, 0x60, 0x78, 0x11 },
            { 0x59, 0x40, 0x58, 0x32 },
            { 0x59, 0x38, 0x70, 0x12 },
            { 0x59, 0x20, 0x60, 0x23 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x57, 0x58, 0x60, 0x31 },
            { 0x57, 0x38, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x57, 0x50, 0x60, 0x31 },
            { 0x57, 0x58, 0x80, 0x11 },
            { 0x57, 0x28, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x5D, 0x58, 0x60, 0x31 },
            { 0x5D, 0x50, 0x78, 0x11 },
            { 0x5A, 0x30, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x5D, 0x58, 0x60, 0x31 },
            { 0x5D, 0x50, 0x78, 0x11 },
            { 0x5B, 0x30, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x5C, 0x58, 0x60, 0x31 },
            { 0x5C, 0x50, 0x78, 0x11 },
            { 0x5C, 0x30, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x5C, 0x50, 0x60, 0x31 },
            { 0x5C, 0x58, 0x78, 0x11 },
            { 0x5C, 0x28, 0x58, 0x32 },
            { 0x5C, 0x20, 0x80, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x5D, 0x58, 0x60, 0x31 },
            { 0x5D, 0x50, 0x78, 0x11 },
            { 0x5D, 0x30, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x5D, 0x50, 0x60, 0x31 },
            { 0x5D, 0x58, 0x78, 0x11 },
            { 0x5D, 0x28, 0x58, 0x32 },
            { 0x5D, 0x20, 0x80, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x5F, 0x50, 0x60, 0x31 },
            { 0x5F, 0x58, 0x78, 0x11 },
            { 0x5F, 0x28, 0x58, 0x32 },
            { 0x5F, 0x20, 0x80, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x5F, 0x58, 0x60, 0x31 },
            { 0x5F, 0x50, 0x78, 0x11 },
            { 0x5F, 0x30, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x60, 0x58, 0x60, 0x21 },
            { 0x61, 0x28, 0x58, 0x32 },
            { 0x61, 0x30, 0x70, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x60, 0x58, 0x60, 0x31 },
            { 0x60, 0x50, 0x78, 0x11 },
            { 0x61, 0x28, 0x58, 0x32 },
            { 0x61, 0x20, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x60, 0x58, 0x60, 0x31 },
            { 0x60, 0x50, 0x78, 0x11 },
            { 0x5E, 0x28, 0x58, 0x32 },
            { 0x5E, 0x20, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x60, 0x58, 0x60, 0x21 },
            { 0x5E, 0x28, 0x58, 0x32 },
            { 0x5E, 0x30, 0x70, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x60, 0x50, 0x60, 0x31 },
            { 0x60, 0x58, 0x78, 0x11 },
            { 0x60, 0x28, 0x58, 0x32 },
            { 0x60, 0x20, 0x80, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x60, 0x58, 0x60, 0x31 },
            { 0x60, 0x50, 0x78, 0x11 },
            { 0x60, 0x30, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x62, 0x48, 0x78, 0x11 },
            { 0x64, 0x28, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x63, 0x58, 0x60, 0x31 },
            { 0x63, 0x50, 0x78, 0x11 },
            { 0x63, 0x30, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x63, 0x50, 0x60, 0x31 },
            { 0x63, 0x58, 0x78, 0x11 },
            { 0x63, 0x28, 0x58, 0x32 },
            { 0x63, 0x20, 0x80, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x62, 0x50, 0x60, 0x31 },
            { 0x62, 0x58, 0x78, 0x11 },
            { 0x62, 0x28, 0x58, 0x32 },
            { 0x62, 0x20, 0x80, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x01,
        {
            { 0x62, 0x58, 0x60, 0x31 },
            { 0x62, 0x50, 0x78, 0x11 },
            { 0x62, 0x30, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x67, 0x50, 0x70, 0x21 },
            { 0x65, 0x30, 0x78, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x66, 0x50, 0x70, 0x21 },
            { 0x65, 0x30, 0x78, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x65, 0x50, 0x70, 0x21 },
            { 0x65, 0x30, 0x78, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x68, 0x58, 0x60, 0x31 },
            { 0x68, 0x50, 0x78, 0x11 },
            { 0x65, 0x28, 0x68, 0x22 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x69, 0x58, 0x58, 0x31 },
            { 0x69, 0x58, 0x80, 0x11 },
            { 0x69, 0x30, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x6A, 0x58, 0x70, 0x21 },
            { 0x6A, 0x38, 0x58, 0x32 },
            { 0x6A, 0x20, 0x78, 0x13 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x6B, 0x60, 0x48, 0x31 },
            { 0x6B, 0x60, 0x78, 0x11 },
            { 0x6B, 0x40, 0x50, 0x32 },
            { 0x6B, 0x30, 0x70, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x6B, 0x60, 0x50, 0x31 },
            { 0x6B, 0x60, 0x78, 0x11 },
            { 0x6C, 0x30, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x70, 0x60, 0x48, 0x31 },
            { 0x70, 0x60, 0x78, 0x11 },
            { 0x70, 0x40, 0x50, 0x32 },
            { 0x70, 0x30, 0x70, 0x13 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x6D, 0x58, 0x58, 0x31 },
            { 0x6D, 0x58, 0x80, 0x11 },
            { 0x6D, 0x30, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x6E, 0x58, 0x58, 0x31 },
            { 0x6E, 0x58, 0x80, 0x11 },
            { 0x6E, 0x30, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x6F, 0x58, 0x70, 0x21 },
            { 0x6F, 0x38, 0x58, 0x32 },
            { 0x6F, 0x20, 0x78, 0x13 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x51, 0x50, 0x68, 0x11 },
            { 0x51, 0x30, 0x78, 0x32 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x03,
        {
            { 0x50, 0x58, 0x60, 0x31 },
            { 0x50, 0x48, 0x78, 0x11 },
            { 0x50, 0x28, 0x68, 0x32 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x43, 0x50, 0x68, 0x11 },
            { 0x43, 0x30, 0x78, 0x32 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x04,
        {
            { 0x0D, 0x58, 0x58, 0x31 },
            { 0x0D, 0x50, 0x78, 0x11 },
            { 0x0D, 0x30, 0x58, 0x32 },
            { 0x0D, 0x28, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x01,
        {
            { 0x53, 0x38, 0x78, 0x11 },
            { 0x0D, 0x50, 0x78, 0x11 },
            { 0x0D, 0x30, 0x58, 0x32 },
            { 0x0D, 0x28, 0x78, 0x12 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x18, 0x58, 0x00 },
            { 0x14, 0x20, 0x70, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x01,
        {
            { 0x71, 0x42, 0x64, 0x22 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x01,
        {
            { 0x72, 0x37, 0x78, 0x22 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x01,
        {
            { 0x73, 0x3C, 0x64, 0x22 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x01,
        {
            { 0x74, 0x3C, 0x78, 0x22 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x01,
        {
            { 0x75, 0x3C, 0x78, 0x22 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x01,
        {
            { 0x76, 0x4C, 0x78, 0x22 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x02,
        {
            { 0x77, 0x58, 0x60, 0x31 },
            { 0x77, 0x2E, 0x80, 0x12 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x01,
        {
            { 0x78, 0x60, 0x50, 0x22 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x01,
        {
            { 0x79, 0x20, 0x86, 0x22 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x01,
        {
            { 0x7A, 0x3C, 0x78, 0x22 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x01,
        {
            { 0x7B, 0x3C, 0x78, 0x22 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x01,
        {
            { 0x7C, 0x3C, 0x78, 0x22 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x01,
        {
            { 0x7D, 0x3C, 0x78, 0x22 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x01,
        {
            { 0x7E, 0x3C, 0x78, 0x22 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x01,
        {
            { 0x7F, 0x3C, 0x78, 0x22 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x01,
        {
            { 0x80, 0x3C, 0x78, 0x22 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
    {
        0x01,
        {
            { 0x81, 0x3C, 0x78, 0x22 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00 },
        },
        { 0x00, 0x00, 0x00 },
    },
};

const struct BattleSharedEffectResources gBattleSharedEffectResources __attribute__((section(".rodata.00399B58"), aligned(1))) =
{ (const u16 *)0x086197E0, (const u16 *)0x086193B0, 0x0000, 0x0002 };

const struct BattleSharedEffectResources D_0839B2B0[] __attribute__((section(".rodata.00399B58"), aligned(1))) =
{
    { (const u16 *)0x08619450, (const u16 *)0x086192F0, 0x0000, 0x0002 },
    { (const u16 *)0x08619580, (const u16 *)0x086192F0, 0x0002, 0x0002 },
    { (const u16 *)0x086196B0, (const u16 *)0x086192F0, 0x0004, 0x0002 },
};

const struct BattleSharedEffectResources D_0839B2D4 __attribute__((section(".rodata.00399B58"), aligned(1))) =
{ (const u16 *)0x0857FAA8, gBattleSpritePalettes[209], 0x0000, 0x0001 };

/* ROM 0x0839CE7C..0x0839CEC0; .rodata.0039CE7C. */
BattleBossDefeatHandler const gBattleBossDefeatHandlers[BATTLE_BOSS_DEFEAT_HANDLER_COUNT] __attribute__((section(".rodata.0039CE7C"), aligned(1))) =
{
    (BattleBossDefeatHandler)BattleAction_UpdateBoss113Defeat,
    (BattleBossDefeatHandler)BattleAction_UpdateBoss114Defeat,
    (BattleBossDefeatHandler)BattleAction_UpdateBoss115Defeat,
    (BattleBossDefeatHandler)BattleAction_UpdateBoss116Defeat,
    (BattleBossDefeatHandler)BattleAction_UpdateBoss117Defeat,
    (BattleBossDefeatHandler)BattleAction_UpdateBoss118Defeat,
    (BattleBossDefeatHandler)BattleAction_UpdateBoss119Defeat,
    (BattleBossDefeatHandler)BattleAction_UpdateBoss120Defeat,
    (BattleBossDefeatHandler)BattleAction_UpdateBoss121Defeat,
    (BattleBossDefeatHandler)BattleAction_UpdateBoss122Defeat,
    (BattleBossDefeatHandler)BattleAction_UpdateBoss123And124Defeat,
    (BattleBossDefeatHandler)BattleAction_UpdateBoss123And124Defeat,
    (BattleBossDefeatHandler)BattleAction_UpdateBoss125Defeat,
    (BattleBossDefeatHandler)BattleAction_UpdateBoss126Defeat,
    (BattleBossDefeatHandler)BattleAction_UpdateBoss127Defeat,
    (BattleBossDefeatHandler)BattleAction_UpdateBoss128Defeat,
    (BattleBossDefeatHandler)BattleAction_UpdateBoss129Defeat,
};

/* ROM 0x0839D8EC..0x0839D9B8; .rodata.0039D8EC. */
const struct BattleBackgroundLoaderResources D_0839D8EC[] __attribute__((section(".rodata.0039D8EC"), aligned(1))) =
{
    { (const u16 *)0x085545D4, (const void *)0x086144AC, 0x0000, 0x0003 },
    { (const u16 *)0x08554654, (const void *)0x086147B0, 0x0003, 0x0002 },
    { (const u16 *)0x085546D4, (const void *)0x08614A5C, 0x0005, 0x0003 },
    { (const u16 *)0x08554754, (const void *)0x08614D94, 0x0008, 0x0002 },
    { (const u16 *)0x08554D14, (const void *)0x08615034, 0x000A, 0x0001 },
    { (const u16 *)0x08554F94, (const void *)0x08615220, 0x000B, 0x0002 },
    { (const u16 *)0x08554FD4, (const void *)0x08615508, 0x000D, 0x0003 },
    { (const u16 *)0x08555014, (const void *)0x08615860, 0x0010, 0x0003 },
    { (const u16 *)0x08555054, (const void *)0x08615B6C, 0x0013, 0x0002 },
    { (const u16 *)0x08555094, (const void *)0x08615E58, 0x0015, 0x0002 },
    { (const u16 *)0x085550D4, (const void *)0x0861615C, 0x0017, 0x0003 },
    { (const u16 *)0x08555114, (const void *)0x086164E4, 0x001A, 0x0003 },
    { (const u16 *)0x085536B4, (const void *)0x08614118, 0x001D, 0x0001 },
    { (const u16 *)0x08551974, (const void *)0x08613FDC, 0x001E, 0x0001 },
    { (const u16 *)0x08554D14, (const void *)0x08616850, 0x001F, 0x0001 },
    { (const u16 *)0x08554754, (const void *)0x08613D3C, 0x0020, 0x0002 },
    { (const u16 *)0x08555134, (const void *)0x08616850, 0x001F, 0x0001 },
};

/* ROM 0x0839DF5C..0x0839DF64; .rodata.0039DF5C. */
const u8 D_0839DF5C[8] __attribute__((section(".rodata.0039DF5C"), aligned(1))) =
{ 0x01, 0x02, 0x03, 0x04, 0x04, 0x03, 0x02, 0x01 };

/* ROM 0x08553734..0x08556DF4; .rodata.00553734. */
const u16 gBattleSpritePalettes[437][16] __attribute__((section(".rodata.00553734"), aligned(1))) =
{
    {
        0x83E0, 0x0022, 0x08CF, 0x0CB4, 0x014D, 0x316B, 0x01B3, 0xCA31, 0x39F7, 0x829A, 0xD694, 0xAEFC, 0xE739,
        0xFBDE, 0x0000, 0x0000,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0xE739,
    },
    {
        0x46BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0x3A9C, 0x5B5F, 0x058F, 0x0678, 0x033E, 0x09C5, 0x0E89, 0x2B70,
        0x539A, 0x56B5, 0x7FFF,
    },
    {
        0x46BB, 0x0245, 0x02E6, 0x0B48, 0x23CD, 0x37F3, 0x2FF9, 0x43FF, 0x63FE, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FE3, 0x7FEF, 0x7FFB,
    },
    {
        0x46BB, 0x640E, 0x7011, 0x7076, 0x7879, 0x79DE, 0x7EFF, 0x7F9F, 0x7FFF, 0x1095, 0x0C1A, 0x00BE, 0x01BF,
        0x1E9F, 0x371F, 0x03DE,
    },
    {
        0x0000, 0x2529, 0x318C, 0x3DEF, 0x4E73, 0x6318, 0x7FFF, 0x4A52, 0x5AD6, 0x6739, 0x35AD, 0x4210, 0x5AD6,
        0x6B5A, 0x56B5, 0x7FFF,
    },
    {
        0x46BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0x3A9C, 0x5B5F, 0x058F, 0x0678, 0x033E, 0x09C5, 0x0E89, 0x2B70,
        0x539A, 0x56B5, 0x7FFF,
    },
    {
        0x46BB, 0x640E, 0x7011, 0x7076, 0x7879, 0x79DE, 0x7EFF, 0x7F9F, 0x7FFF, 0x1095, 0x0C1A, 0x00BE, 0x01BF,
        0x1E9F, 0x371F, 0x03DE,
    },
    {
        0x0000, 0x2529, 0x318C, 0x3DEF, 0x4E73, 0x6318, 0x7FFF, 0x4A52, 0x5AD6, 0x6739, 0x35AD, 0x4210, 0x5AD6,
        0x6B5A, 0x56B5, 0x7FFF,
    },
    {
        0x46BB, 0x0C86, 0x0C14, 0x007B, 0x15DF, 0x4ABF, 0x635F, 0x0593, 0x067C, 0x031E, 0x0C13, 0x015A, 0x01FE,
        0x1F1F, 0x56B5, 0x679F,
    },
    {
        0x46BB, 0x0C86, 0x0C14, 0x007B, 0x15DF, 0x4ABF, 0x635F, 0x0593, 0x067C, 0x031E, 0x0C13, 0x015A, 0x01FE,
        0x1F1F, 0x56B5, 0x679F,
    },
    {
        0x46BB, 0x0245, 0x02E6, 0x0B48, 0x23CD, 0x37F3, 0x2FF9, 0x43FF, 0x63FE, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FE3, 0x7FEF, 0x7FFB,
    },
    {
        0x46BB, 0x0C86, 0x24AD, 0x3531, 0x41D7, 0x5A9C, 0x7B5F, 0x6189, 0x7672, 0x7338, 0x5DC5, 0x6289, 0x7F10,
        0x7F57, 0x56B5, 0x7FFF,
    },
    {
        0x46BB, 0x0C86, 0x24AD, 0x3531, 0x41D7, 0x5A9C, 0x7B5F, 0x6189, 0x7672, 0x7338, 0x5DC5, 0x6289, 0x7F10,
        0x7F57, 0x56B5, 0x7FFF,
    },
    {
        0x46BB, 0x0245, 0x02E6, 0x0B48, 0x23CD, 0x37F3, 0x2FF9, 0x43FF, 0x63FE, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FE3, 0x7FEF, 0x7FFB,
    },
    {
        0x3A96, 0x0022, 0x21D7, 0x3A9C, 0x5F5F, 0x420F, 0x4889, 0x5D8E, 0x7295, 0x0458, 0x39BC, 0x0170, 0x067B,
        0x033F, 0x56B5, 0x7BDE,
    },
    {
        0x0000, 0x54AE, 0x5D11, 0x6975, 0x75D9, 0x7E3D, 0x7EDF, 0x7FBF, 0x013A, 0x01DC, 0x027D, 0x037F, 0x03FF,
        0x57FF, 0x77FF, 0x7FFF,
    },
    {
        0x3A96, 0x2529, 0x4E73, 0x6318, 0x7FFF, 0x35AD, 0x3DEF, 0x4E73, 0x737B, 0x5AD6, 0x739C, 0x35AD, 0x4A31,
        0x56B5, 0x6719, 0x7FFF,
    },
    {
        0x3A96, 0x0022, 0x31B9, 0x4ABF, 0x635F, 0x4212, 0x1D72, 0x219A, 0x323F, 0x3C58, 0x5A5E, 0x15B7, 0x2E9D,
        0x473F, 0x56B8, 0x7BDE,
    },
    {
        0x3A96, 0x0022, 0x31B9, 0x4ABF, 0x635F, 0x4212, 0x1D72, 0x219A, 0x323F, 0x3C58, 0x5A5E, 0x15B7, 0x2E9D,
        0x473F, 0x56B8, 0x7BDE,
    },
    {
        0x0000, 0x54AE, 0x5D11, 0x6975, 0x75D9, 0x7E3D, 0x7EDF, 0x7FBF, 0x013A, 0x01DC, 0x027D, 0x037F, 0x03FF,
        0x57FF, 0x77FF, 0x7FFF,
    },
    {
        0x3A96, 0x0022, 0x11D5, 0x4A9C, 0x737F, 0x4212, 0x5123, 0x7E08, 0x7EED, 0x61B4, 0x6E55, 0x1530, 0x4E39,
        0x5B3F, 0x56B8, 0x7BDE,
    },
    {
        0x3A96, 0x0022, 0x11D5, 0x4A9C, 0x737F, 0x4212, 0x5123, 0x7E08, 0x7EED, 0x61B4, 0x6E55, 0x1530, 0x4E39,
        0x5B3F, 0x56B8, 0x7BDE,
    },
    {
        0x0000, 0x54AE, 0x5D11, 0x6975, 0x75D9, 0x7E3D, 0x7EDF, 0x7FBF, 0x013A, 0x01DC, 0x027D, 0x037F, 0x03FF,
        0x57FF, 0x77FF, 0x7FFF,
    },
    {
        0x3B56, 0x1086, 0x04AC, 0x1551, 0x01D5, 0x3A9C, 0x5F7F, 0x007C, 0x4D47, 0x7666, 0x7B4E, 0x027A, 0x037F,
        0x2929, 0x5273, 0x7FFF,
    },
    {
        0x3B56, 0x013A, 0x01DC, 0x027D, 0x037F, 0x03FF, 0x57FF, 0x77FF, 0x013A, 0x01DC, 0x027D, 0x037F, 0x03FF,
        0x57FF, 0x77FF, 0x7FFF,
    },
    {
        0x3B56, 0x2529, 0x294A, 0x35AD, 0x4210, 0x6318, 0x7FFF, 0x4E73, 0x3DEF, 0x5294, 0x6318, 0x4E73, 0x5EF7,
        0x2929, 0x35AD, 0x7FFF,
    },
    {
        0x3B56, 0x0C86, 0x188F, 0x2915, 0x2DBB, 0x42BF, 0x635F, 0x1C72, 0x21BA, 0x369F, 0x3F1F, 0x021A, 0x031F,
        0x2970, 0x4675, 0x77DF,
    },
    {
        0x3B56, 0x0C86, 0x188F, 0x2915, 0x2DBB, 0x42BF, 0x635F, 0x1C72, 0x21BA, 0x369F, 0x3F1F, 0x021A, 0x031F,
        0x2970, 0x4675, 0x77DF,
    },
    {
        0x3B56, 0x013A, 0x01DC, 0x027D, 0x037F, 0x03FF, 0x57FF, 0x77FF, 0x013A, 0x01DC, 0x027D, 0x037F, 0x03FF,
        0x57FF, 0x77FF, 0x7FFF,
    },
    {
        0x3B56, 0x0C86, 0x24AD, 0x512F, 0x65F5, 0x5A9C, 0x7B5F, 0x4478, 0x554C, 0x75F1, 0x7A53, 0x6ED5, 0x7B9B,
        0x2929, 0x5273, 0x7FDE,
    },
    {
        0x3B56, 0x0C86, 0x24AD, 0x512F, 0x65F5, 0x5A9C, 0x7B5F, 0x4478, 0x554C, 0x75F1, 0x7A53, 0x6ED5, 0x7B9B,
        0x2929, 0x5273, 0x7FDE,
    },
    {
        0x3B56, 0x013A, 0x01DC, 0x027D, 0x037F, 0x03FF, 0x57FF, 0x77FF, 0x013A, 0x01DC, 0x027D, 0x037F, 0x03FF,
        0x57FF, 0x77FF, 0x7FFF,
    },
    {
        0x3B56, 0x0022, 0x01D5, 0x3A9C, 0x5F7F, 0x28A4, 0x4483, 0x59CC, 0x6A94, 0x0239, 0x033F, 0x1C56, 0x39FB,
        0x5B1F, 0x56B5, 0x7FFF,
    },
    {
        0x3B56, 0x34D7, 0x451A, 0x517C, 0x61FD, 0x6E7E, 0x7EFF, 0x7FBF, 0x013A, 0x01DC, 0x027D, 0x037F, 0x03FF,
        0x57FF, 0x77FF, 0x7FFF,
    },
    {
        0x3B56, 0x0022, 0x00AF, 0x0133, 0x013A, 0x2A1D, 0x32BD, 0x479F, 0x6A94, 0x0239, 0x2529, 0x35AD, 0x4A31,
        0x56B5, 0x6719, 0x7FFF,
    },
    {
        0x3B56, 0x34D7, 0x451A, 0x517C, 0x61FD, 0x6E7E, 0x7EFF, 0x7FBF, 0x013A, 0x01DC, 0x027D, 0x037F, 0x03FF,
        0x57FF, 0x77FF, 0x7FFF,
    },
    {
        0x3B56, 0x2529, 0x4E73, 0x6318, 0x7FFF, 0x2529, 0x318C, 0x4631, 0x5AD6, 0x5AD6, 0x739C, 0x35AD, 0x4A31,
        0x5EF7, 0x6B5A, 0x7FFF,
    },
    {
        0x3B56, 0x0022, 0x35FC, 0x4ABF, 0x635F, 0x1C0A, 0x38D0, 0x59BA, 0x627F, 0x267B, 0x2F1F, 0x1C17, 0x39BD,
        0x527F, 0x5675, 0x779F,
    },
    {
        0x3B56, 0x0022, 0x35FC, 0x4ABF, 0x635F, 0x1C0A, 0x38D0, 0x59BA, 0x627F, 0x267B, 0x2F1F, 0x1C17, 0x39BD,
        0x527F, 0x5675, 0x779F,
    },
    {
        0x3B56, 0x34D7, 0x451A, 0x517C, 0x61FD, 0x6E7E, 0x7EFF, 0x7FBF, 0x013A, 0x01DC, 0x027D, 0x037F, 0x03FF,
        0x57FF, 0x77FF, 0x7FFF,
    },
    {
        0x3B56, 0x0022, 0x11D5, 0x4A9C, 0x737F, 0x28A5, 0x5126, 0x7E0D, 0x7EF1, 0x367B, 0x4F5F, 0x6051, 0x7216,
        0x7B1A, 0x5673, 0x7FDD,
    },
    {
        0x3B56, 0x0022, 0x11D5, 0x4A9C, 0x737F, 0x28A5, 0x5126, 0x7E0D, 0x7EF1, 0x367B, 0x4F5F, 0x6051, 0x7216,
        0x7B1A, 0x5673, 0x7FDD,
    },
    {
        0x3B56, 0x34D7, 0x451A, 0x517C, 0x61FD, 0x6E7E, 0x7EFF, 0x7FBF, 0x013A, 0x01DC, 0x027D, 0x037F, 0x03FF,
        0x57FF, 0x77FF, 0x7FFF,
    },
    {
        0x3B56, 0x1025, 0x01D5, 0x3A9C, 0x5F7F, 0x394A, 0x4DF0, 0x733A, 0x7FFF, 0x0992, 0x1E7A, 0x1F3F, 0x013D,
        0x5D27, 0x7E2B, 0x0131,
    },
    {
        0x0000, 0x7E07, 0x7E8B, 0x7F29, 0x7FB1, 0x7FF6, 0x7FFA, 0x7FFF, 0x25EF, 0x2E52, 0x3694, 0x3EF7, 0x475A,
        0x4FBD, 0x57FF, 0x7FFF,
    },
    {
        0x3B56, 0x2529, 0x4631, 0x6318, 0x7FFF, 0x39CE, 0x5294, 0x6739, 0x7FFF, 0x35AD, 0x4210, 0x6F7B, 0x6739,
        0x39CE, 0x4E73, 0x4A31,
    },
    {
        0x3B56, 0x0022, 0x31B9, 0x4ADF, 0x637F, 0x19D7, 0x3A5A, 0x5B5F, 0x7FFF, 0x0952, 0x11BA, 0x1F3F, 0x3C5B,
        0x5531, 0x6E33, 0x28F3,
    },
    {
        0x3B56, 0x0022, 0x31B9, 0x4ADF, 0x637F, 0x19D7, 0x3A5A, 0x5B5F, 0x7FFF, 0x0952, 0x11BA, 0x1F3F, 0x3C5B,
        0x5531, 0x6E33, 0x28F3,
    },
    {
        0x0000, 0x7E07, 0x7E8B, 0x7F29, 0x7FB1, 0x7FF6, 0x7FFA, 0x7FFF, 0x25EF, 0x2E52, 0x3694, 0x3EF7, 0x475A,
        0x4FBD, 0x57FF, 0x7FFF,
    },
    {
        0x3B56, 0x0022, 0x11D5, 0x4A9C, 0x737F, 0x38EA, 0x4D90, 0x7295, 0x7F5B, 0x3D92, 0x467A, 0x533F, 0x6534,
        0x41C3, 0x5E67, 0x412E,
    },
    {
        0x3B56, 0x0022, 0x11D5, 0x4A9C, 0x737F, 0x38EA, 0x4D90, 0x7295, 0x7F5B, 0x3D92, 0x467A, 0x533F, 0x6534,
        0x41C3, 0x5E67, 0x412E,
    },
    {
        0x0000, 0x7E07, 0x7E8B, 0x7F29, 0x7FB1, 0x7FF6, 0x7FFA, 0x7FFF, 0x25EF, 0x2E52, 0x3694, 0x3EF7, 0x475A,
        0x4FBD, 0x57FF, 0x7FFF,
    },
    {
        0x3B56, 0x0421, 0x110B, 0x1572, 0x2617, 0x46BB, 0x2108, 0x294A, 0x4630, 0x5A47, 0x7769, 0x18F5, 0x255E,
        0x4ED8, 0x63DF, 0x7BDE,
    },
    {
        0x3B56, 0x0018, 0x007D, 0x195E, 0x325F, 0x367F, 0x42DF, 0x537F, 0x63FE, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FA3, 0x7FF2, 0x7FFB,
    },
    {
        0x3B56, 0x2529, 0x3DEF, 0x4E73, 0x6318, 0x739C, 0x2D6B, 0x39CE, 0x4E73, 0x4210, 0x6739, 0x7FFF, 0x6739,
        0x4E73, 0x5AD6, 0x7FFF,
    },
    {
        0x3B56, 0x0421, 0x1111, 0x1576, 0x261D, 0x469E, 0x18CE, 0x2573, 0x4218, 0x4DB3, 0x5A58, 0x18F5, 0x255E,
        0x4E98, 0x637F, 0x7BDE,
    },
    {
        0x3B56, 0x0421, 0x1111, 0x1576, 0x261D, 0x469E, 0x18CE, 0x2573, 0x4218, 0x4DB3, 0x5A58, 0x18F5, 0x255E,
        0x4E98, 0x637F, 0x7BDE,
    },
    {
        0x3B56, 0x0018, 0x007D, 0x195E, 0x325F, 0x367F, 0x42DF, 0x537F, 0x63FE, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FA3, 0x7FF2, 0x7FFB,
    },
    {
        0x3B56, 0x0421, 0x210B, 0x2572, 0x3617, 0x56BB, 0x4108, 0x596B, 0x6E31, 0x69E9, 0x768F, 0x40F5, 0x59B8,
        0x5A72, 0x7738, 0x7B9B,
    },
    {
        0x3B56, 0x0421, 0x210B, 0x2572, 0x3617, 0x56BB, 0x4108, 0x596B, 0x6E31, 0x69E9, 0x768F, 0x40F5, 0x59B8,
        0x5A72, 0x7738, 0x7B9B,
    },
    {
        0x3B56, 0x0018, 0x007D, 0x195E, 0x325F, 0x367F, 0x42DF, 0x537F, 0x63FE, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FA3, 0x7FF2, 0x7FFB,
    },
    {
        0x46BB, 0x0C86, 0x04AC, 0x1551, 0x01D5, 0x3A9E, 0x5F5F, 0x01F2, 0x06B8, 0x037E, 0x35E6, 0x4AA9, 0x636D,
        0x358D, 0x56B5, 0x7FFF,
    },
    {
        0x46BB, 0x0245, 0x02E6, 0x0B48, 0x23CD, 0x37F3, 0x2FF9, 0x43FF, 0x63FE, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FE3, 0x7FEF, 0x7FFB,
    },
    {
        0x46BB, 0x2529, 0x318C, 0x39CE, 0x4631, 0x6318, 0x7FFF, 0x4631, 0x5AD6, 0x6739, 0x2D6B, 0x4210, 0x5294,
        0x4210, 0x56B5, 0x7FFF,
    },
    {
        0x46BB, 0x0C86, 0x0C13, 0x0078, 0x31DB, 0x42BF, 0x635F, 0x0172, 0x0638, 0x02FE, 0x35EC, 0x4AB1, 0x6376,
        0x3590, 0x56B8, 0x77BF,
    },
    {
        0x46BB, 0x0C86, 0x0C13, 0x0078, 0x31DB, 0x42BF, 0x635F, 0x0172, 0x0638, 0x02FE, 0x35EC, 0x4AB1, 0x6376,
        0x3590, 0x56B8, 0x77BF,
    },
    {
        0x46BB, 0x0245, 0x02E6, 0x0B48, 0x23CD, 0x37F3, 0x2FF9, 0x43FF, 0x63FE, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FE3, 0x7FEF, 0x7FFB,
    },
    {
        0x46BB, 0x0C86, 0x24AD, 0x3531, 0x41D7, 0x5A9C, 0x6B5F, 0x3DF2, 0x52B8, 0x5B7E, 0x4DE6, 0x62A9, 0x736D,
        0x4D8D, 0x66B5, 0x7FDE,
    },
    {
        0x46BB, 0x0C86, 0x24AD, 0x3531, 0x41D7, 0x5A9C, 0x6B5F, 0x3DF2, 0x52B8, 0x5B7E, 0x4DE6, 0x62A9, 0x736D,
        0x4D8D, 0x66B5, 0x7FDE,
    },
    {
        0x46BB, 0x0245, 0x02E6, 0x0B48, 0x23CD, 0x37F3, 0x2FF9, 0x43FF, 0x63FE, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FE3, 0x7FEF, 0x7FFB,
    },
    {
        0x3B56, 0x0C86, 0x048C, 0x194F, 0x21D6, 0x3A9E, 0x5F5F, 0x05F3, 0x06BB, 0x037E, 0x5146, 0x662C, 0x76D3,
        0x3DEF, 0x56B5, 0x7FFF,
    },
    {
        0x0000, 0x0179, 0x09DA, 0x165B, 0x22BC, 0x2F3D, 0x379E, 0x43FF, 0x63FE, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FA3, 0x7FCF, 0x7FFB,
    },
    {
        0x3B56, 0x2529, 0x318C, 0x3DEF, 0x4E73, 0x6318, 0x7FFF, 0x4A52, 0x5AD6, 0x6739, 0x35AD, 0x4210, 0x5AD6,
        0x6B5A, 0x56B5, 0x7FFF,
    },
    {
        0x3B56, 0x0C86, 0x0C13, 0x0078, 0x31DB, 0x42BF, 0x635F, 0x05F7, 0x2AFD, 0x3FBF, 0x514D, 0x6633, 0x76D9,
        0x3DF2, 0x56B8, 0x7BDF,
    },
    {
        0x3B56, 0x0C86, 0x0C13, 0x0078, 0x31DB, 0x42BF, 0x635F, 0x05F7, 0x2AFD, 0x3FBF, 0x514D, 0x6633, 0x76D9,
        0x3DF2, 0x56B8, 0x7BDF,
    },
    {
        0x0000, 0x0179, 0x09DA, 0x165B, 0x22BC, 0x2F3D, 0x379E, 0x43FF, 0x63FE, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FA3, 0x7FCF, 0x7FFB,
    },
    {
        0x3B56, 0x0C86, 0x24AD, 0x3531, 0x41D7, 0x5A9C, 0x6B5F, 0x39F3, 0x4ABB, 0x577E, 0x512A, 0x6591, 0x7636,
        0x458C, 0x6274, 0x7FDE,
    },
    {
        0x3B56, 0x0C86, 0x24AD, 0x3531, 0x41D7, 0x5A9C, 0x6B5F, 0x39F3, 0x4ABB, 0x577E, 0x512A, 0x6591, 0x7636,
        0x458C, 0x6274, 0x7FDE,
    },
    {
        0x0000, 0x0179, 0x09DA, 0x165B, 0x22BC, 0x2F3D, 0x379E, 0x43FF, 0x63FE, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FA3, 0x7FCF, 0x7FFB,
    },
    {
        0x46BB, 0x0022, 0x133E, 0x3A38, 0x56FE, 0x6B9F, 0x59F5, 0x6EBA, 0x20F5, 0x317E, 0x3509, 0x494B, 0x65EF,
        0x5A74, 0x6F18, 0x7BDE,
    },
    {
        0x46BB, 0x5406, 0x6409, 0x780D, 0x78B1, 0x7998, 0x7A3A, 0x7AFE, 0x7B9F, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FA3, 0x7FF2, 0x7FFB,
    },
    {
        0x46BB, 0x2529, 0x7BDE, 0x4E73, 0x6318, 0x7FFF, 0x4A52, 0x5AD6, 0x4210, 0x6739, 0x35AD, 0x4210, 0x5AD6,
        0x6B5A, 0x739C, 0x7FFF,
    },
    {
        0x46BB, 0x0022, 0x271E, 0x31DB, 0x42BF, 0x635F, 0x49F8, 0x5ABC, 0x1D58, 0x2E1D, 0x350F, 0x4951, 0x65F6,
        0x5A76, 0x6F1B, 0x7BDF,
    },
    {
        0x46BB, 0x0022, 0x271E, 0x31DB, 0x42BF, 0x635F, 0x49F8, 0x5ABC, 0x1D58, 0x2E1D, 0x350F, 0x4951, 0x65F6,
        0x5A76, 0x6F1B, 0x7BDF,
    },
    {
        0x46BB, 0x5406, 0x6409, 0x780D, 0x78B1, 0x7998, 0x7A3A, 0x7AFE, 0x7B9F, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FA3, 0x7FF2, 0x7FFB,
    },
    {
        0x46BB, 0x0022, 0x473E, 0x5A38, 0x62FE, 0x6F9F, 0x69F2, 0x76B7, 0x4CF5, 0x557E, 0x4107, 0x4D49, 0x6DEC,
        0x6274, 0x7318, 0x7FDE,
    },
    {
        0x46BB, 0x0022, 0x473E, 0x5A38, 0x62FE, 0x6F9F, 0x69F2, 0x76B7, 0x4CF5, 0x557E, 0x4107, 0x4D49, 0x6DEC,
        0x6274, 0x7318, 0x7FDE,
    },
    {
        0x46BB, 0x5406, 0x6409, 0x780D, 0x78B1, 0x7998, 0x7A3A, 0x7AFE, 0x7B9F, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FA3, 0x7FF2, 0x7FFB,
    },
    {
        0x3B56, 0x0022, 0x112C, 0x21D6, 0x3A9E, 0x533F, 0x294A, 0x3DEE, 0x5A47, 0x7769, 0x18F5, 0x255E, 0x3A33,
        0x4ED8, 0x63DF, 0x7BDE,
    },
    {
        0x46BB, 0x0018, 0x007D, 0x195E, 0x2E3F, 0x42DF, 0x537F, 0x63FE, 0x7BFE, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FA3, 0x7FCF, 0x7FFB,
    },
    {
        0x3B56, 0x2529, 0x3DEF, 0x4E73, 0x6318, 0x739C, 0x2D6B, 0x39CE, 0x4E73, 0x4210, 0x39CE, 0x4E73, 0x4E73,
        0x5AD6, 0x7FFF, 0x7FFF,
    },
    {
        0x3B56, 0x0022, 0x0078, 0x31DB, 0x42BF, 0x635F, 0x294E, 0x35B3, 0x464E, 0x6371, 0x1957, 0x261F, 0x39F6,
        0x4E9A, 0x635F, 0x77BE,
    },
    {
        0x3B56, 0x0022, 0x0078, 0x31DB, 0x42BF, 0x635F, 0x294E, 0x35B3, 0x464E, 0x6371, 0x1957, 0x261F, 0x39F6,
        0x4E9A, 0x635F, 0x77BE,
    },
    {
        0x46BB, 0x0018, 0x007D, 0x195E, 0x2E3F, 0x42DF, 0x537F, 0x63FE, 0x7BFE, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FA3, 0x7FCF, 0x7FFB,
    },
    {
        0x3B56, 0x0022, 0x3531, 0x41D7, 0x5A9C, 0x6B5F, 0x394A, 0x4DEE, 0x6A4C, 0x7ECD, 0x44F0, 0x5157, 0x4E33,
        0x62D8, 0x739D, 0x7BBD,
    },
    {
        0x3B56, 0x0022, 0x3531, 0x41D7, 0x5A9C, 0x6B5F, 0x394A, 0x4DEE, 0x6A4C, 0x7ECD, 0x44F0, 0x5157, 0x4E33,
        0x62D8, 0x739D, 0x7BBD,
    },
    {
        0x46BB, 0x0018, 0x007D, 0x195E, 0x2E3F, 0x42DF, 0x537F, 0x63FE, 0x7BFE, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FA3, 0x7FCF, 0x7FFB,
    },
    {
        0x46BB, 0x0C86, 0x1110, 0x1195, 0x1D5C, 0x3A9E, 0x5F5F, 0x2D6B, 0x3DEE, 0x5694, 0x0212, 0x471A, 0x5BDE,
        0x09C5, 0x0EE9, 0x7FFF,
    },
    {
        0x0000, 0x0245, 0x02E6, 0x0B48, 0x23CD, 0x37F3, 0x2FF9, 0x43FF, 0x63FE, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FE3, 0x7FEF, 0x7FFB,
    },
    {
        0x46BB, 0x640E, 0x7011, 0x7076, 0x7879, 0x79DE, 0x7EFF, 0x7F9F, 0x7FFF, 0x1095, 0x0C1A, 0x00BE, 0x01BF,
        0x1E9F, 0x371F, 0x03DE,
    },
    {
        0x3B56, 0x2529, 0x318C, 0x3DEF, 0x4E73, 0x6318, 0x7FFF, 0x2D6B, 0x3DEE, 0x5694, 0x4210, 0x5AD6, 0x7FFF,
        0x3DEE, 0x56B5, 0x7FFF,
    },
    {
        0x46BB, 0x0C86, 0x1110, 0x1195, 0x1D5C, 0x3A9E, 0x5F5F, 0x2D6B, 0x3DEE, 0x5694, 0x0212, 0x471A, 0x5BDE,
        0x09C5, 0x0EE9, 0x7FFF,
    },
    {
        0x46BB, 0x640E, 0x7011, 0x7076, 0x7879, 0x79DE, 0x7EFF, 0x7F9F, 0x7FFF, 0x1095, 0x0C1A, 0x00BE, 0x01BF,
        0x1E9F, 0x371F, 0x03DE,
    },
    {
        0x3B56, 0x2529, 0x318C, 0x3DEF, 0x4E73, 0x6318, 0x7FFF, 0x2D6B, 0x3DEE, 0x5694, 0x4210, 0x5AD6, 0x7FFF,
        0x3DEE, 0x56B5, 0x7FFF,
    },
    {
        0x3B56, 0x0C86, 0x0C13, 0x0078, 0x31DB, 0x46DF, 0x637F, 0x294D, 0x3DD1, 0x4279, 0x2DB7, 0x469D, 0x5B3E,
        0x19EA, 0x264D, 0x6BDF,
    },
    {
        0x3B56, 0x0C86, 0x0C13, 0x0078, 0x31DB, 0x46DF, 0x637F, 0x294D, 0x3DD1, 0x4279, 0x2DB7, 0x469D, 0x5B3E,
        0x19EA, 0x264D, 0x6BDF,
    },
    {
        0x0000, 0x0245, 0x02E6, 0x0B48, 0x23CD, 0x37F3, 0x2FF9, 0x43FF, 0x63FE, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FE3, 0x7FEF, 0x7FFB,
    },
    {
        0x3B56, 0x0C86, 0x44AD, 0x4970, 0x51D5, 0x5A9B, 0x6B5F, 0x3129, 0x418C, 0x5652, 0x4DEF, 0x6AF7, 0x777B,
        0x4165, 0x5269, 0x7FDE,
    },
    {
        0x3B56, 0x0C86, 0x44AD, 0x4970, 0x51D5, 0x5A9B, 0x6B5F, 0x3129, 0x418C, 0x5652, 0x4DEF, 0x6AF7, 0x777B,
        0x4165, 0x5269, 0x7FDE,
    },
    {
        0x0000, 0x0245, 0x02E6, 0x0B48, 0x23CD, 0x37F3, 0x2FF9, 0x43FF, 0x63FE, 0x5D00, 0x71E9, 0x7687, 0x7B25,
        0x7FE3, 0x7FEF, 0x7FFB,
    },
    {
        0x8240, 0x25B2, 0x1A56, 0x8EDB, 0x837F, 0x83DE, 0xAFFF, 0xDFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0xC6BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0xBA9C, 0xDB5F, 0x058F, 0x8678, 0x833E, 0x09C5, 0x8E89, 0xAB70,
        0xD39A, 0xD6B5, 0xFFFF,
    },
    {
        0x0B00, 0x0014, 0x0076, 0x00D9, 0x011A, 0x015C, 0x019D, 0x01FE, 0x025E, 0x02BE, 0x031F, 0x037F, 0x07DF,
        0x2FFF, 0x57FF, 0x7FFF,
    },
    {
        0x46BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0x3A9C, 0x5B5F, 0x058F, 0x0678, 0x033E, 0x09C5, 0x0E89, 0x2B70,
        0x539A, 0x56B5, 0x7FFF,
    },
    {
        0x0B00, 0x0014, 0x0076, 0x00D9, 0x011A, 0x015C, 0x019D, 0x01FE, 0x025E, 0x02BE, 0x031F, 0x037F, 0x07DF,
        0x2FFF, 0x57FF, 0x7FFF,
    },
    {
        0x46BB, 0x0C86, 0x1110, 0x1195, 0x1D5C, 0x3A9E, 0x5F5F, 0x2D6B, 0x3DEE, 0x5694, 0x0212, 0x471A, 0x5BDE,
        0x09C5, 0x0EE9, 0x7FFF,
    },
    {
        0x2E88, 0x0400, 0x1844, 0x28C7, 0x392A, 0x256C, 0x458D, 0x4DAE, 0x31EF, 0x5612, 0x3A73, 0x6654, 0x6AB7,
        0x7339, 0x7B9C, 0x7FFF,
    },
    {
        0x46BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0x3A9C, 0x5B5F, 0x058F, 0x0678, 0x033E, 0x09C5, 0x0E89, 0x2B70,
        0x539A, 0x56B5, 0x7FFF,
    },
    {
        0x0B00, 0x6654, 0x6A75, 0x6EB7, 0x72F8, 0x7319, 0x775A, 0x7B9C, 0x7FBD, 0x7FFE, 0x7FBD, 0x7B7C, 0x773A,
        0x72F9, 0x6ED7, 0x6A96,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x7C04, 0x0000, 0x0008, 0x0844, 0x200D, 0x0030, 0x2413, 0x04B4, 0x2897, 0x291A, 0x25BC, 0x265E, 0x2AFD,
        0x2B3F, 0x37DF, 0x5BFF,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x0B00, 0x291A, 0x295B, 0x2DBC, 0x2E1C, 0x2E7D, 0x32DD, 0x331E, 0x339F, 0x37FF, 0x377F, 0x331E, 0x32BD,
        0x2E3D, 0x2DDC, 0x297B,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x2E88, 0x0822, 0x2C40, 0x3C81, 0x10A7, 0x4CC4, 0x1C90, 0x5D06, 0x6149, 0x1DD4, 0x2235, 0x6A11, 0x72B5,
        0x26F9, 0x379D, 0x77BD,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x0B00, 0x4D40, 0x5180, 0x55A0, 0x59E0, 0x5E20, 0x6260, 0x66A0, 0x6AC0, 0x6F00, 0x6AC0, 0x6680, 0x6240,
        0x5E00, 0x55C0, 0x5180,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x7E40, 0x0821, 0x18A6, 0x1CEA, 0x294B, 0x0533, 0x318E, 0x0978, 0x39D0, 0x15D9, 0x4232, 0x263B, 0x4E74,
        0x52D7, 0x5F3B, 0x6FBE,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x0B00, 0x2823, 0x2C44, 0x3065, 0x3886, 0x3CA7, 0x40C8, 0x48EA, 0x4D2B, 0x514C, 0x4D2B, 0x44E9, 0x40C8,
        0x38A7, 0x3465, 0x2C44,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x02E0, 0x7FEF, 0x7FF1, 0x7FF4, 0x7FF6, 0x7FF8, 0x7FFB, 0x7FFD, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x0243, 0x02B7, 0x02F9, 0x033A, 0x035B, 0x039D, 0x03DE, 0x03FF, 0x0FFF, 0x1FFF, 0x2FFF, 0x3FFF, 0x4FFF,
        0x5FFF, 0x6FFF, 0x7FFF,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x0B00, 0x7159, 0x719A, 0x71BA, 0x71DB, 0x75FB, 0x761B, 0x765C, 0x7A7C, 0x7A9D, 0x7ABD, 0x7ADE, 0x7F1E,
        0x7F3F, 0x7F5F, 0x7F7F,
    },
    {
        0x46BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0x3A9C, 0x5B5F, 0x058F, 0x0678, 0x033E, 0x09C5, 0x0E89, 0x2B70,
        0x539A, 0x56B5, 0x7FFF,
    },
    {
        0x1BE1, 0x4A0D, 0x3E71, 0x2EF5, 0x2359, 0x4A51, 0x4A76, 0x4ABA, 0x4ADE, 0x17BD, 0x5B5F, 0x2FFF, 0x53FF,
        0x6FBF, 0x63FF, 0x7FFF,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x8240, 0x25B2, 0x1A56, 0x8EDB, 0x837F, 0x83DE, 0xAFFF, 0xDFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0xC6BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0xBA9C, 0xDB5F, 0x058F, 0x8678, 0x833E, 0x09C5, 0x8E89, 0xAB70,
        0xD39A, 0xD6B5, 0xFFFF,
    },
    {
        0x8240, 0x25B2, 0x1A56, 0x8EDB, 0x837F, 0x83DE, 0xAFFF, 0xDFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0xC6BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0xBA9C, 0xDB5F, 0x058F, 0x8678, 0x833E, 0x09C5, 0x8E89, 0xAB70,
        0xD39A, 0xD6B5, 0xFFFF,
    },
    {
        0x8240, 0x25B2, 0x1A56, 0x8EDB, 0x837F, 0x83DE, 0xAFFF, 0xDFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0xC6BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0xBA9C, 0xDB5F, 0x058F, 0x8678, 0x833E, 0x09C5, 0x8E89, 0xAB70,
        0xD39A, 0xD6B5, 0xFFFF,
    },
    {
        0x23E5, 0x02DA, 0x02FB, 0x031C, 0x035C, 0x037D, 0x039E, 0x03BF, 0x03DF, 0x13FF, 0x27FF, 0x37FF, 0x4BFF,
        0x5BFF, 0x6FFF, 0x7FFF,
    },
    {
        0x46BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0x3A9C, 0x5B5F, 0x058F, 0x0678, 0x033E, 0x09C5, 0x0E89, 0x2B70,
        0x539A, 0x56B5, 0x7FFF,
    },
    {
        0x23E5, 0x7265, 0x7686, 0x76C7, 0x7AE8, 0x7B09, 0x7F4A, 0x7F6B, 0x7F8C, 0x7FAF, 0x7FB1, 0x7FB4, 0x7FD7,
        0x7FDA, 0x7FFD, 0x7FFF,
    },
    {
        0x46BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0x3A9C, 0x5B5F, 0x058F, 0x0678, 0x033E, 0x09C5, 0x0E89, 0x2B70,
        0x539A, 0x56B5, 0x7FFF,
    },
    {
        0x23E5, 0x149C, 0x18DD, 0x1CFD, 0x253E, 0x297E, 0x2DBF, 0x35FF, 0x3A3F, 0x427F, 0x4EBF, 0x56FF, 0x633F,
        0x6B7F, 0x77BF, 0x7FFF,
    },
    {
        0x46BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0x3A9C, 0x5B5F, 0x058F, 0x0678, 0x033E, 0x09C5, 0x0E89, 0x2B70,
        0x539A, 0x56B5, 0x7FFF,
    },
    {
        0x23E5, 0x6894, 0x6CB5, 0x70F6, 0x7138, 0x7579, 0x799A, 0x7DDB, 0x7E1C, 0x7E5D, 0x7E9D, 0x7EDE, 0x7F3E,
        0x7F7F, 0x7FBF, 0x7FFF,
    },
    {
        0x46BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0x3A9C, 0x5B5F, 0x058F, 0x0678, 0x033E, 0x09C5, 0x0E89, 0x2B70,
        0x539A, 0x56B5, 0x7FFF,
    },
    {
        0x23E5, 0x4631, 0x4A52, 0x4E73, 0x5294, 0x56B5, 0x5AD6, 0x5EF7, 0x6318, 0x6739, 0x6B5A, 0x6F7B, 0x739C,
        0x77BD, 0x7BDE, 0x7FFF,
    },
    {
        0x46BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0x3A9C, 0x5B5F, 0x058F, 0x0678, 0x033E, 0x09C5, 0x0E89, 0x2B70,
        0x539A, 0x56B5, 0x7FFF,
    },
    {
        0x8240, 0x25B2, 0x1A56, 0x8EDB, 0x837F, 0x83DE, 0xAFFF, 0xDFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0xC6BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0xBA9C, 0xDB5F, 0x058F, 0x8678, 0x833E, 0x09C5, 0x8E89, 0xAB70,
        0xD39A, 0xD6B5, 0xFFFF,
    },
    {
        0x0240, 0x25B2, 0x25B2, 0x25B2, 0x25B2, 0x25B2, 0x1DF4, 0x1A56, 0x0EDB, 0x037F, 0x03DE, 0x2FFF, 0x5FFF,
        0x7BDE, 0x0000, 0x0000,
    },
    {
        0x0000, 0x25B2, 0x1A56, 0x0EDB, 0x037F, 0x03DE, 0x2FFF, 0x5FFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x0B00, 0x061E, 0x065E, 0x029E, 0x02DE, 0x06FE, 0x0B1E, 0x0F3F, 0x135F, 0x177F, 0x279F, 0x339F, 0x43BF,
        0x4FDF, 0x5FDF, 0x6BFF,
    },
    {
        0x3B56, 0x1086, 0x04AC, 0x1551, 0x01D5, 0x3A9C, 0x5F7F, 0x007C, 0x4D47, 0x7666, 0x7B4E, 0x027A, 0x037F,
        0x2929, 0x5273, 0x7FFF,
    },
    {
        0x0240, 0x0016, 0x809C, 0x813E, 0x01DF, 0x967F, 0x2F3F, 0xC3DF, 0xF7FF, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0xC6BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0xBA9C, 0xDB5F, 0x058F, 0x8678, 0x833E, 0x09C5, 0x8E89, 0xAB70,
        0xD39A, 0xD6B5, 0xFFFF,
    },
    {
        0x82C0, 0x001B, 0x007D, 0x80DE, 0x015F, 0x81BF, 0x023F, 0x029F, 0x02FF, 0x035F, 0x839F, 0x87FF, 0x23DF,
        0xC3DF, 0x63FF, 0xFFFF,
    },
    {
        0xC6BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0xBA9C, 0xDB5F, 0x058F, 0x8678, 0x833E, 0x09C5, 0x8E89, 0xAB70,
        0xD39A, 0xD6B5, 0xFFFF,
    },
    {
        0x0240, 0x0016, 0x809C, 0x813E, 0x01DF, 0x967F, 0x2F3F, 0xC3DF, 0xF7FF, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0xC6BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0xBA9C, 0xDB5F, 0x058F, 0x8678, 0x833E, 0x09C5, 0x8E89, 0xAB70,
        0xD39A, 0xD6B5, 0xFFFF,
    },
    {
        0x0B00, 0x0014, 0x0076, 0x00D9, 0x011A, 0x015C, 0x019D, 0x01FE, 0x025E, 0x02BE, 0x031F, 0x037F, 0x07DF,
        0x2FFF, 0x57FF, 0x7FFF,
    },
    {
        0x46BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0x3A9C, 0x5B5F, 0x058F, 0x0678, 0x033E, 0x09C5, 0x0E89, 0x2B70,
        0x539A, 0x56B5, 0x7FFF,
    },
    {
        0x8360, 0x7DE0, 0xFE63, 0xFEE7, 0xFF6B, 0xFFEF, 0xFFF8, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x001F,
    },
    {
        0xC6BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0xBA9C, 0xDB5F, 0x058F, 0x8678, 0x833E, 0x09C5, 0x8E89, 0xAB70,
        0xD39A, 0xD6B5, 0xFFFF,
    },
    {
        0x8B00, 0x7DE0, 0x7E21, 0xFE42, 0x7E83, 0xFEA5, 0xFEE6, 0x7F27, 0xFF48, 0x7F8A, 0x7FCB, 0xFFEF, 0xFFF3,
        0xFFF8, 0xFFFB, 0xFFFE,
    },
    {
        0xC6BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0xBA9C, 0xDB5F, 0x058F, 0x8678, 0x833E, 0x09C5, 0x8E89, 0xAB70,
        0xD39A, 0xD6B5, 0xFFFF,
    },
    {
        0x8B00, 0x7DE0, 0xFE43, 0xFEA6, 0xFF29, 0x7FCC, 0xFFEF, 0xFFF8, 0xFFFB, 0xFFFE, 0xFFF9, 0xFFF4, 0xFFEF,
        0xFF6B, 0xFEE7, 0xFE63,
    },
    {
        0xC6BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0xBA9C, 0xDB5F, 0x058F, 0x8678, 0x833E, 0x09C5, 0x8E89, 0xAB70,
        0xD39A, 0xD6B5, 0xFFFF,
    },
    {
        0x0B00, 0x6940, 0x6D60, 0x71A1, 0x75C1, 0x7A02, 0x7E22, 0x7E63, 0x7EA5, 0x7F08, 0x7F4A, 0x7FAD, 0x7FB2,
        0x7FD6, 0x7FDB, 0x7FFF,
    },
    {
        0x46BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0x3A9C, 0x5B5F, 0x058F, 0x0678, 0x033E, 0x09C5, 0x0E89, 0x2B70,
        0x539A, 0x56B5, 0x7FFF,
    },
    {
        0x3DEF, 0x6A4A, 0x6E6B, 0x728C, 0x72CE, 0x76EF, 0x7B10, 0x7F32, 0x7F53, 0x7F75, 0x7F97, 0x7F98, 0x7FBA,
        0x7FDC, 0x7FFE, 0x7FFF,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x0B00, 0x061E, 0x065E, 0x029E, 0x02DE, 0x06FE, 0x0B1E, 0x0F3F, 0x135F, 0x177F, 0x279F, 0x339F, 0x43BF,
        0x4FDF, 0x5FDF, 0x6BFF,
    },
    {
        0x3B56, 0x0022, 0x01D5, 0x3A9C, 0x5F7F, 0x28A4, 0x4483, 0x59CC, 0x6A94, 0x0239, 0x033F, 0x1C56, 0x39FB,
        0x5B1F, 0x56B5, 0x7FFF,
    },
    {
        0x02C0, 0x628E, 0x66AF, 0x6ED0, 0x72F1, 0x7712, 0x7B33, 0x7F54, 0x7F54, 0x7F76, 0x7F97, 0x7F99, 0x7FBB,
        0x7FDC, 0x7FFE, 0x7FFF,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x02B5, 0x3DFF, 0x4A5F, 0x529F, 0x5EFF, 0x6B5F, 0x77BF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x5986, 0x16C0, 0x16E2, 0x1B05, 0x1F28, 0x1F4B, 0x236E, 0x23B0, 0x27D3, 0x2BF6, 0x2BF6, 0x37F8, 0x47F9,
        0x57DB, 0x67DD, 0x73DE,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x0B00, 0x0014, 0x0076, 0x00D9, 0x011A, 0x015C, 0x019D, 0x01FE, 0x025E, 0x02BE, 0x031F, 0x037F, 0x07DF,
        0x2FFF, 0x57FF, 0x7FFF,
    },
    {
        0x3B56, 0x0421, 0x110B, 0x1572, 0x2617, 0x46BB, 0x2108, 0x294A, 0x4630, 0x5A47, 0x7769, 0x18F5, 0x255E,
        0x4ED8, 0x63DF, 0x7BDE,
    },
    {
        0x02C0, 0x0018, 0x009A, 0x013C, 0x05DE, 0x0ADE, 0x13BE, 0x3FDE, 0x67DE, 0x67DE, 0x3FDE, 0x13BE, 0x0ADE,
        0x05DE, 0x013C, 0x009A,
    },
    {
        0x46BB, 0x0C86, 0x04AD, 0x1531, 0x21D7, 0x3A9C, 0x5B5F, 0x058F, 0x0678, 0x033E, 0x09C5, 0x0E89, 0x2B70,
        0x539A, 0x56B5, 0x7FFF,
    },
    {
        0x29ED, 0x0C86, 0x7BFF, 0x7FFA, 0x7FD2, 0x7FAD, 0x5A66, 0x41C4, 0x2902, 0x7FFD, 0x66C7, 0x7328, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x0B00, 0x3BE0, 0x43E3, 0x4BE7, 0x53EB, 0x5FEF, 0x67F3, 0x6FF7, 0x77FB, 0x7FFF, 0x77FB, 0x6BF6, 0x63F2,
        0x57ED, 0x4FE9, 0x43E4,
    },
    {
        0x7FC7, 0x1185, 0x0465, 0x0889, 0x1225, 0x08CC, 0x08ED, 0x18B5, 0x1591, 0x0E56, 0x22DC, 0x2B1D, 0x233C,
        0x4F5F, 0x635A, 0x7FFE,
    },
    {
        0x2F20, 0x3740, 0x3B60, 0x4380, 0x4B80, 0x53A0, 0x5BC0, 0x63E0, 0x6BE1, 0x63E0, 0x57C0, 0x4FA0, 0x4780,
        0x3F60, 0x3740, 0x2F20,
    },
    {
        0x03C0, 0x0015, 0x086C, 0x10F0, 0x14BC, 0x2697, 0x2DAE, 0x6E68, 0x4B6D, 0x229B, 0x4652, 0x4F5B, 0x42FB,
        0x4F5F, 0x6F7B, 0x7FFF,
    },
    {
        0x2F20, 0x3740, 0x3B60, 0x4380, 0x4B80, 0x53A0, 0x5BC0, 0x63E0, 0x6BE1, 0x63E0, 0x57C0, 0x4FA0, 0x4780,
        0x3F60, 0x3740, 0x2F20,
    },
    {
        0x2E59, 0x0026, 0x2083, 0x1F7C, 0x592A, 0x08F6, 0x658B, 0x6E0D, 0x2E59, 0x76B2, 0x2E9E, 0x435F, 0x7F9A,
        0x5F9F, 0x3BFF, 0x7BDE,
    },
    {
        0x7E35, 0x7E76, 0x7E97, 0x7ED9, 0x7EFA, 0x7F3B, 0x7F5C, 0x7F9D, 0x7FBE, 0x7F9D, 0x7F5C, 0x7F1B, 0x7ED9,
        0x7EB8, 0x7E77, 0x7E35,
    },
    {
        0x83C0, 0x0444, 0x1C63, 0x0069, 0x2CC4, 0x14B5, 0x6580, 0x4D87, 0x1171, 0xF244, 0x29F3, 0xAA76, 0xC2FB,
        0xD77D, 0xBF9F, 0xFFFE,
    },
    {
        0x02D9, 0x0EFA, 0x1B1B, 0x2B3C, 0x377C, 0x479D, 0x53BE, 0x63DF, 0x6FFF, 0x5FDF, 0x4FBE, 0x3F7D, 0x335C,
        0x233B, 0x12FA, 0x02D9,
    },
    {
        0x03E0, 0x1C82, 0x14C7, 0x4948, 0x55EB, 0x18B8, 0x3E11, 0x623C, 0x6318, 0x5B1B, 0x3B9C, 0x72FD, 0x6B9B,
        0x737B, 0x6FBD, 0x7FFE,
    },
    {
        0x39DF, 0x41FF, 0x4A3F, 0x4E7F, 0x56BF, 0x5EFF, 0x673F, 0x6F7F, 0x739F, 0x6B5F, 0x631F, 0x5ADF, 0x529F,
        0x4A5F, 0x421F, 0x39DF,
    },
    {
        0x03E0, 0x10A5, 0x58A0, 0x1D29, 0x158E, 0x14B5, 0x6A26, 0x0318, 0x32FB, 0x2B9D, 0x3DF0, 0x6F39, 0x379D,
        0x479F, 0x5FDF, 0x7FFF,
    },
    {
        0x7FF1, 0x7FF2, 0x7FF4, 0x7FF5, 0x7FF7, 0x7FF8, 0x7FFA, 0x7FFB, 0x7FFC, 0x7FFB, 0x7FF9, 0x7FF8, 0x7FF6,
        0x7FF4, 0x7FF3, 0x7FF1,
    },
    {
        0x03E0, 0x0821, 0x0C63, 0x0049, 0x24E6, 0x08E8, 0x14C8, 0x1593, 0x14B5, 0x49EC, 0x2DF4, 0x2EBA, 0x4252,
        0x579B, 0x6B7B, 0x7FFE,
    },
    {
        0x0077, 0x0498, 0x0CD9, 0x151A, 0x1D5B, 0x219C, 0x29BD, 0x31FE, 0x3A3F, 0x31FE, 0x29BD, 0x217C, 0x193B,
        0x10FA, 0x08B8, 0x0077,
    },
    {
        0xFFFF, 0x0000, 0x5400, 0x7C00, 0x8080, 0xA880, 0xD480, 0xFC80, 0x0120, 0x2920, 0x5520, 0x7D20, 0x81A0,
        0xA9A0, 0xD5A0, 0xFDA0,
    },
    {
        0x0240, 0x2A40, 0x5640, 0x7E40, 0x82C0, 0xAAC0, 0xD6C0, 0xFEC0, 0x0360, 0x2B60, 0x5760, 0x7F60, 0x83E0,
        0xABE0, 0xD7E0, 0xFFE0,
    },
    {
        0x0000, 0x7E6A, 0xFECD, 0x7F32, 0x7F76, 0xFFBB, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFFB, 0x7FF2, 0xFFA3, 0xFE6D, 0x5D00, 0xFFBE, 0xFF1B, 0xFE78, 0x7DD5, 0x7110,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFFB, 0xFFF6, 0xFFCD, 0xFB82, 0x6F42, 0xFF1B, 0xFE17, 0x7D32, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x8360, 0x7DE0, 0xFE63, 0xFEE7, 0xFF6B, 0xFFEF, 0xFFF8, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x001F,
    },
    {
        0x7C00, 0x826F, 0x82F1, 0x8373, 0x83F5, 0xA7F9, 0xCBFC, 0xEFFF, 0xEFF8, 0xF3F2, 0xEBE0, 0xCB80, 0xA6A0,
        0x01E0, 0x0000, 0x001F,
    },
    {
        0x01A0, 0x7DE0, 0xFE63, 0xFEE7, 0xFF6B, 0xFFEF, 0xFFF8, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x001F,
    },
    {
        0x0000, 0x0016, 0x809C, 0x813E, 0x01DF, 0x967F, 0x2F3F, 0xC3DF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x83E0, 0x7DE0, 0xFE63, 0xFEE7, 0xFF6B, 0xFFEF, 0xFFF8, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x001F,
    },
    {
        0x82C0, 0x001B, 0x007D, 0x80DE, 0x015F, 0x81BF, 0x023F, 0x029F, 0x02FF, 0x035F, 0x839F, 0x87FF, 0x23DF,
        0xC3DF, 0x63FF, 0xFFFF,
    },
    {
        0x0000, 0x0016, 0x809C, 0x813E, 0x01DF, 0x967F, 0x2F3F, 0xC3DF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xF39C, 0xDEF7, 0xD294, 0xCA52, 0x3DEF, 0x25FF, 0xA11C, 0xEBFF, 0xD3FF, 0xBBFF,
        0xA77F, 0xA6FF, 0x20A5,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFFA, 0xFFE9, 0xD286, 0x39C4, 0x2522, 0x25FF, 0xA11C, 0xEBBF, 0xD37F, 0xB71F,
        0x9ABF, 0x825F, 0x20A5,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFF5D, 0xEE16, 0x58EF, 0x3C69, 0x2425, 0x25FF, 0xA11C, 0xF7BF, 0xE31F, 0xBDFF,
        0x253F, 0x001F, 0x20A5,
    },
    {
        0xF39F, 0xEB5F, 0xE31F, 0xDADF, 0xD29F, 0xCA5F, 0xC21F, 0xBDFF, 0xB5BF, 0x2D7F, 0x253F, 0x1CFF, 0x14BF,
        0x0C7F, 0x045F, 0x001F,
    },
    {
        0xCE72, 0x0C86, 0x0C4F, 0x89EA, 0x1492, 0x014D, 0x2515, 0x92AD, 0x01B3, 0x0526, 0x35F0, 0x2F13, 0x829A,
        0xCA95, 0x8F3F, 0xEFDC,
    },
    {
        0x3916, 0x0C86, 0xEFDC, 0xE357, 0xD2D3, 0x6AD6, 0x6A94, 0x75EF, 0xE929, 0x29ED, 0x35F0, 0xAA90, 0x829A,
        0xCA95, 0x8F3F, 0xEFDC,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xE739, 0xF2D4, 0x6E30, 0x596A, 0x3CC3, 0x25BE, 0x3156, 0xDF3F, 0x9DF3, 0xB61B,
        0x2EDB, 0xC631, 0x9CCE,
    },
    {
        0x0000, 0x0C86, 0xFBFF, 0xF39C, 0xDEF7, 0xD294, 0xCA52, 0x3DEF, 0x25FF, 0xA11C, 0xEBFF, 0xD3FF, 0xBBFF,
        0xA77F, 0xA6FF, 0x20A5,
    },
    {
        0xCE72, 0x0C86, 0xFBFF, 0x7E5F, 0x70FD, 0x5CB8, 0x4091, 0xBB71, 0xAEAD, 0x1A27, 0x8D03, 0xD7BE, 0x4739,
        0xAA53, 0x21CE, 0x20A5,
    },
    {
        0xBE89, 0x0C86, 0xFBFF, 0x6F9D, 0x675A, 0x5F18, 0x56D6, 0x0F7B, 0xC7BE, 0x8E7B, 0xEF7F, 0xFFFF, 0x11AD,
        0x114A, 0x3156, 0xB61B,
    },
    {
        0xF295, 0x0C86, 0xFFFF, 0xD6B5, 0xC631, 0x35AD, 0xDB5F, 0xBA9C, 0x21D7, 0x833E, 0x8678, 0x058F, 0xBB73,
        0x96CB, 0x8A06, 0x1531,
    },
    {
        0x0000, 0x0C86, 0xFFFF, 0xDF1E, 0xBE3E, 0x9D1B, 0xFFF5, 0x7F40, 0xFE80, 0x7620, 0xF160, 0xE840, 0x83FF,
        0x837F, 0x827D, 0x01DC,
    },
    {
        0x82C9, 0x0C86, 0xFFFF, 0xD6B5, 0xC631, 0x35AD, 0xDB5F, 0xBA9C, 0x21D7, 0x39FC, 0x9D1A, 0x0033, 0x5F0A,
        0xDA67, 0xBD62, 0x1531,
    },
    {
        0x0C86, 0x0C86, 0xFFFF, 0x7FF4, 0xFF46, 0xFEC6, 0x7E46, 0x7DA4, 0x21D7, 0x39FC, 0x9D1A, 0x0033, 0xDF0A,
        0xDA67, 0x3D62, 0x1531,
    },
    {
        0xCE72, 0x0C86, 0x0C4F, 0x2F13, 0x1492, 0x8F3F, 0x2515, 0x92AD, 0x829A, 0x89EA, 0x35F0, 0x0526, 0x01B3,
        0xCA95, 0x014D, 0xEFDC,
    },
    {
        0xFBFE, 0x0C86, 0xAFF9, 0xB7F3, 0xA3CD, 0x8B48, 0x82E6, 0x8245, 0xE929, 0x29ED, 0x35F0, 0xAA90, 0x6AD6,
        0x6A94, 0x75EF, 0xE929,
    },
    {
        0xCE72, 0x0C86, 0xFBFF, 0x7E5F, 0x70FD, 0x5CB8, 0x4091, 0xD7BE, 0x4739, 0xAA53, 0x21CE, 0xA17B, 0x809B,
        0x0016, 0x0012, 0x20A5,
    },
    {
        0x7E44, 0x7E5F, 0x7E5F, 0x7E5F, 0xEFFF, 0x53FF, 0x2FDF, 0xA71F, 0x9E5E, 0x117D, 0x2FDF, 0xA71F, 0x9E5E,
        0x117D, 0x08DD, 0x001C,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xE6DF, 0x521E, 0xE739, 0xF39C, 0x7339, 0x72B3, 0x6A0C, 0xDAD6, 0xB4FB, 0xC927,
        0xC210, 0x1C73, 0xA076,
    },
    {
        0x0000, 0x0C86, 0xFBFF, 0xF39C, 0xDEF7, 0xD294, 0xCA52, 0x3DEF, 0x25FF, 0xA11C, 0xEBFF, 0xD3FF, 0xBBFF,
        0xA77F, 0xA6FF, 0x20A5,
    },
    {
        0x2A44, 0x0C86, 0xFFFF, 0xEF76, 0x66ED, 0xDA29, 0xC186, 0x87BE, 0x075A, 0x82B5, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x83E4,
    },
    {
        0xDBDE, 0xFFFF, 0xFFFB, 0xFFCF, 0xFFA3, 0x7B25, 0x7687, 0x71E9, 0x5D00, 0xDBDE, 0x87BE, 0x075A, 0x82B5,
        0x833E, 0x8678, 0x058F,
    },
    {
        0x7E49, 0x0C86, 0xFFFF, 0x5BDF, 0xBF5F, 0xA69E, 0x99DD, 0x08DA, 0x8014, 0x000D, 0x2924, 0x81A0, 0x81A0,
        0xA9A0, 0xD5A0, 0xFDA0,
    },
    {
        0x0000, 0x0C86, 0xFBFF, 0xF39C, 0xDEF7, 0xD294, 0xCA52, 0x3DEF, 0x25FF, 0xA11C, 0xEBFF, 0xD3FF, 0xBBFF,
        0xA77F, 0xA6FF, 0x20A5,
    },
    {
        0xCE72, 0x0C86, 0xFBFF, 0xEF31, 0xFAAC, 0x6609, 0xCCC6, 0xD27E, 0x317A, 0x9CB5, 0x0413, 0x4BBF, 0x9F1D,
        0x9678, 0x1214, 0x20A5,
    },
    {
        0x09C5, 0x2A40, 0x5640, 0xBB73, 0x172C, 0x8A87, 0x09C5, 0xFEC0, 0x0360, 0x2B60, 0x5760, 0x7F60, 0x83E0,
        0xABE0, 0xD7E0, 0xFFE0,
    },
    {
        0xB20D, 0x0C86, 0xFBFF, 0xFB92, 0xFECC, 0xF24A, 0x6107, 0x47DF, 0xAF7F, 0x9EFC, 0x9257, 0x8D93, 0x88ED,
        0xEB5A, 0xD294, 0x39CE,
    },
    {
        0x7E44, 0x0C86, 0xFBFF, 0x6107, 0x69EA, 0xF2CE, 0xFB92, 0xFBFF, 0x2B60, 0x2B60, 0x5760, 0x7F60, 0x83E0,
        0xABE0, 0xD7E0, 0xFFE0,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xF37C, 0x6719, 0x5A95, 0x4642, 0x5AE2, 0x6F84, 0x7BC9, 0xF1BF, 0x39FC, 0x9D1A,
        0x0033, 0x002D, 0xC1D0,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x82C9, 0x0C86, 0xFFFF, 0x16B5, 0x9F39, 0x939E, 0xE739, 0xD6B5, 0xC631, 0x423F, 0x9D1A, 0x0033, 0x6F8B,
        0xE6C7, 0xC5A2, 0x19EE,
    },
    {
        0x5A40, 0x0C86, 0xFFFF, 0x83FF, 0xD3FF, 0x83BF, 0xE739, 0xD6B5, 0xC631, 0x423F, 0x9D1A, 0x0033, 0x6F8B,
        0xE6C7, 0xC5A2, 0x19EE,
    },
    {
        0x82C9, 0x0C86, 0xFFFF, 0x2C76, 0x459A, 0xE2BD, 0xE739, 0xD6B5, 0xC631, 0xFFF5, 0xDB6A, 0xBEE0, 0x137D,
        0x8A99, 0x85D6, 0x00F2,
    },
    {
        0x5A40, 0x0C86, 0xFFFF, 0x83FF, 0xD3FF, 0x83BF, 0xE739, 0xD6B5, 0xC631, 0x423F, 0x9D1A, 0x0033, 0x6F8B,
        0xE6C7, 0xC5A2, 0x19EE,
    },
    {
        0x82C9, 0x0C86, 0xFFFF, 0xFF2C, 0xCE7F, 0xA778, 0xE739, 0xC631, 0x9715, 0xB1FB, 0x0054, 0x92B3, 0x7620,
        0xE520, 0x894B, 0x1A10,
    },
    {
        0x5A40, 0x0C86, 0xFFFF, 0x83FF, 0xD3FF, 0x83BF, 0xE739, 0xD6B5, 0xC631, 0x423F, 0x9D1A, 0x0033, 0xFF2C,
        0xEA47, 0x5100, 0x19EE,
    },
    {
        0x82C9, 0x0C86, 0xFFFF, 0xC7DE, 0xC3BF, 0x3B5D, 0x96FA, 0x1237, 0x0DD5, 0x8951, 0x08AD, 0x0469, 0xBE3F,
        0x151A, 0x0000, 0xFFFF,
    },
    {
        0x5A40, 0x0C86, 0xFFFF, 0xEFFF, 0xDFFF, 0xCBFF, 0x97DE, 0x17DE, 0x977E, 0x175E, 0x96FE, 0x96BE, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x82C9, 0x0C86, 0xFFFF, 0xE739, 0xD294, 0xFF30, 0xEECE, 0xD9C8, 0x761F, 0xE51D, 0x4817, 0x3811, 0xC631,
        0x93FE, 0x8F59, 0x8A51,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xDB9C, 0xB6D4, 0xE739, 0xF39C, 0xD2FF, 0xB23E, 0x9DBC, 0xDAD6, 0x9A2E, 0x851B,
        0xC210, 0x114A, 0x01A7,
    },
    {
        0x0000, 0x0C86, 0xFBFF, 0xF39C, 0xDEF7, 0xD294, 0xCA52, 0x3DEF, 0x25FF, 0xA11C, 0xEBFF, 0xD3FF, 0xBBFF,
        0xA77F, 0xA6FF, 0x20A5,
    },
    {
        0x29ED, 0x0C86, 0x7BFF, 0x6FDF, 0x57BF, 0x739C, 0x233F, 0x7B31, 0x6318, 0x027A, 0x726B, 0x01B2, 0x5965,
        0x012C, 0x4631, 0x13A4,
    },
    {
        0x29ED, 0x0C86, 0x7BFF, 0x7F5F, 0x7EBF, 0x797B, 0x7D35, 0x44AC, 0x2045, 0x7B31, 0x726B, 0x5965, 0x739C,
        0x6318, 0x4631, 0x13A4,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0x4E07, 0x575C, 0xBD86, 0xCED8, 0xD6D6, 0xBA11, 0x4A79, 0x3145, 0xB293, 0x2509,
        0x01A9, 0x1021, 0x0082,
    },
    {
        0x0000, 0x0000, 0xF3FF, 0xCBFF, 0xA7BD, 0x837B, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x2A44, 0x0C86, 0xFFFF, 0xB3FF, 0xA759, 0x9AB3, 0x11EE, 0xCA5F, 0x001D, 0x000E, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x83E4,
    },
    {
        0xDBDE, 0xFFFF, 0xFFFB, 0xFFCF, 0xFFA3, 0x7B25, 0x7687, 0x71E9, 0x5D00, 0xDBDE, 0x87BE, 0x075A, 0x82B5,
        0x833E, 0x8678, 0x058F,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0x7FFA, 0x7FD2, 0xFFAD, 0xDA66, 0x41C4, 0xA902, 0xFFFD, 0xE6C7, 0x7328, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xF39C, 0xDEF7, 0xD294, 0xCA52, 0x3DEF, 0x25FF, 0xA11C, 0xEBFF, 0xD3FF, 0xBBFF,
        0xA77F, 0xA6FF, 0x20A5,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xE6DF, 0x521E, 0xB4FB, 0xA477, 0x984F, 0x1067, 0xF37F, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xEBDF, 0x5BDF, 0xD35F, 0xBEDE, 0xAA3D, 0x25FF, 0xA11C, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xEBDF, 0x5BDF, 0xD35F, 0xBEDE, 0xAA3D, 0x25FF, 0xA11C, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0x037B, 0x7F78, 0xE6DF, 0xFA8E, 0x521E, 0xF1A6, 0xCE94, 0x5922, 0xB4FB, 0x38C1,
        0xA076, 0x1C40, 0xD7FF,
    },
    {
        0x29ED, 0x2A40, 0x5640, 0x7E40, 0x82C0, 0xAAC0, 0xD6C0, 0xFEC0, 0x0360, 0x2B60, 0x5760, 0x7F60, 0x83E0,
        0xABE0, 0xD7E0, 0xFFE0,
    },
    {
        0xA913, 0x0C86, 0xFBFF, 0xD3FF, 0xEBFF, 0xFFB2, 0xD75A, 0xFE8E, 0xBEB5, 0xA6FF, 0x25EF, 0x6D4A, 0x114A,
        0x5407, 0x08C6, 0xCE94,
    },
    {
        0x0000, 0x0C86, 0xFBFF, 0xF39C, 0xDEF7, 0xD294, 0xCA52, 0x3DEF, 0x25FF, 0xA11C, 0xEBFF, 0xD3FF, 0xBBFF,
        0xA77F, 0xA6FF, 0x20A5,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFFB, 0xFBFF, 0x0000, 0xFFF5, 0xF7DE, 0x0000, 0x7F66, 0xDEF7, 0x0000, 0xEA26,
        0xC231, 0x0000, 0xCCC0,
    },
    {
        0x55E0, 0x0000, 0xFBFF, 0xFF5F, 0xFA9E, 0x71FB, 0x5CF6, 0x388E, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0xA591, 0x0C86, 0xFBFF, 0xE339, 0xE31F, 0xCF36, 0xEA94, 0xCE5E, 0xBAAF, 0x4D8A, 0x7F5A, 0x3179, 0x25C4,
        0x2513, 0x1101, 0x3C62,
    },
    {
        0x29ED, 0x0000, 0xFF9D, 0xFF5F, 0xFA9E, 0x6DBA, 0x5CF6, 0x388E, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x28A4, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xD7D6, 0x53DF, 0xBF51, 0xE339, 0x1B9F, 0x9A89, 0x5192, 0x8299, 0x01A2, 0x3DEF,
        0x40AE, 0x012B, 0x0080,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xE7FE, 0xCBFC, 0x83F6, 0x0394, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xF6FB, 0xCBFF, 0xEF9C, 0xE235, 0xD6D6, 0x8F9C, 0x4D30, 0x3DEF, 0x82B5, 0x294A,
        0x384C, 0x016B, 0x1405,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xF3FF, 0xE6DF, 0x521E, 0xB4FB, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0xA913, 0x0C86, 0xFBFF, 0xFFF3, 0x7FFF, 0xFADB, 0x6B5A, 0x6532, 0x5AD6, 0xFE4D, 0xCA52, 0x502C, 0x3DEF,
        0x2C27, 0x20A5, 0xCE94,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xEF9C, 0xE37F, 0xFFCF, 0xDEF7, 0xAE9F, 0xCA52, 0x15DA, 0x7B25, 0x0157, 0x35AD,
        0x00CE, 0x71E9, 0x0045,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xCE5E, 0x4EFB, 0x3179, 0x265C, 0xD6D6, 0x017A, 0x4889, 0x1CEF, 0xB293, 0x00CE,
        0x01A9, 0x0023, 0x0082,
    },
    {
        0x0000, 0x0000, 0xFF9D, 0xFA98, 0x75B3, 0x6CCE, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFF5D, 0xDB9B, 0xEEFF, 0xFEDB, 0xBF18, 0x69D3, 0x55FC, 0x8630, 0x48EB, 0x3CF9,
        0x00C5, 0x2465, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFF7D, 0xFF1A, 0xF615, 0x6D51, 0x688C, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0xCE72, 0x0C86, 0xFBFF, 0x7B7F, 0x669C, 0x59BB, 0x4D77, 0x677F, 0xC6DF, 0xAA1B, 0x1578, 0x28CD, 0x0000,
        0x0000, 0x0000, 0x6F9D,
    },
    {
        0xBE89, 0x0C86, 0xFBFF, 0x6F9D, 0x675A, 0x5F18, 0x56D6, 0x0F7B, 0xC7BE, 0x8E7B, 0xEF7F, 0xFFFF, 0x11AD,
        0x114A, 0x3156, 0xB61B,
    },
    {
        0xCE72, 0x0C86, 0xFBFF, 0xFF9A, 0xFF35, 0x7665, 0x59E4, 0xE7FF, 0xC3FF, 0xAB5B, 0x9A31, 0x3D42, 0x0000,
        0x0000, 0x56D6, 0x6F9D,
    },
    {
        0xCE72, 0x0C86, 0xFBFF, 0xFF7F, 0xFB3F, 0xEEBC, 0x49B2, 0xE7FF, 0xC3FF, 0xAB5B, 0x9A31, 0x3D42, 0x11AD,
        0x114A, 0x3156, 0xB61B,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFF7F, 0xDF7F, 0x5AD6, 0xF71D, 0x4AB8, 0xFE17, 0xE65B, 0x91F5, 0x6D51, 0x4974,
        0x1551, 0x24CB, 0x04AC,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0x7F9F, 0xEEBE, 0x61DD, 0x553B, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0xCA52, 0x0C86, 0xFBFF, 0xDFF9, 0xA788, 0x9AE4, 0x0DA2, 0xFF5F, 0xFE5F, 0x59B5, 0x34EC, 0x2087, 0xF39C,
        0xE739, 0xDAD6, 0x39CE,
    },
    {
        0xCA52, 0x0C86, 0xDFF9, 0xC3B0, 0xA788, 0xD294, 0xCA52, 0x3DEF, 0x25FF, 0xA11C, 0xEBFF, 0xD3FF, 0xBBFF,
        0xA77F, 0xA6FF, 0x20A5,
    },
    {
        0xCE72, 0x0C86, 0xFBFF, 0xF71F, 0xEE1E, 0x59B9, 0x4553, 0x20AA, 0x3910, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x20A5,
    },
    {
        0xBE89, 0x0C86, 0xFBFF, 0x6F9D, 0x675A, 0x5F18, 0x56D6, 0x0F7B, 0xC7BE, 0x8E7B, 0xEF7F, 0xFFFF, 0x11AD,
        0x114A, 0x3156, 0xB61B,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xDF5F, 0xFF5E, 0x46BF, 0x7EBB, 0xDEF7, 0x0D1C, 0x71D7, 0x4FDF, 0x0074, 0x58D0,
        0x037F, 0x0006, 0x2046,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x82C9, 0x0C86, 0xFFFF, 0xE77F, 0xD31F, 0xB61D, 0xA99D, 0x045C, 0x0019, 0x0013, 0x000D, 0x0007, 0xADDD,
        0x0C9C, 0x0000, 0xFFFF,
    },
    {
        0x5A40, 0x0C86, 0xFFFF, 0xFFFE, 0xFFFC, 0xFFFA, 0x7FF7, 0x7FF5, 0x7FF3, 0x7FD1, 0x7FCF, 0x7FCD, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xD6D6, 0x6F7E, 0xE29C, 0xD29C, 0x59FC, 0xD955, 0xD6D6, 0xB5BC, 0xB293, 0x2133,
        0x01A9, 0x108A, 0x0082,
    },
    {
        0x29ED, 0x0000, 0xFF9D, 0xE73C, 0xE25C, 0x59BC, 0x4D19, 0x2CB1, 0x0427, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFFC, 0xFF57, 0xFBFF, 0x73D7, 0xFDE6, 0xDB9B, 0xDAEF, 0xD484, 0xA294, 0x35C3,
        0xD6D6, 0x05AD, 0x0C80,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFDA, 0xFB56, 0xF6F3, 0x69ED, 0x5D07, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFDA, 0xFB56, 0xF6F3, 0x69ED, 0x5D07, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xF2FB, 0xFF1F, 0xDF5F, 0x71F8, 0xFA1E, 0xADFD, 0x7114, 0x64BA, 0x009B, 0x0000,
        0x386F, 0x0074, 0x0C04,
    },
    {
        0x29ED, 0x0C86, 0xFF9D, 0xFA98, 0x75B3, 0x6CCE, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x82C9, 0x0C86, 0xFFFF, 0xE739, 0xD294, 0xFEF8, 0x658E, 0x38E8, 0xDBF6, 0xAFE9, 0x9F25, 0x11E3, 0xC631,
        0x93FE, 0x8F59, 0x8A51,
    },
    {
        0xE3F9, 0xDBF6, 0xCFF4, 0xCBF1, 0xBFEF, 0xB7EC, 0xAFE9, 0xA7E7, 0xA7A6, 0xA366, 0x9F25, 0x9EE5, 0x9AA4,
        0x9664, 0x9623, 0x11E3,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFB5E, 0xFFB9, 0x7F7F, 0xF259, 0x7FAF, 0x7E9F, 0x6513, 0x6F06, 0xEDD9, 0x6739,
        0x48AD, 0x2CCA, 0x3DA6,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xF7FB, 0xE7F3, 0xD7ED, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0x0000, 0xFBD7, 0xDEF7, 0xF370, 0xCB7E, 0xE6C4, 0xAE96, 0x55A0, 0x096C, 0x48C0,
        0x0483, 0x3080, 0x0000,
    },
    {
        0x29ED, 0x0000, 0xFBFF, 0xE37F, 0xC6FF, 0xB2BF, 0x9A3F, 0x81BF, 0x6BFF, 0x4BDF, 0x2FBF, 0x0F7F, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x82C9, 0x0C86, 0xFFFF, 0xFFFC, 0xFFD9, 0x7FB5, 0xFF51, 0xFAA9, 0x7622, 0xCD41, 0x4100, 0x28A0, 0xFE42,
        0x4100, 0x0000, 0xFFFF,
    },
    {
        0x5A40, 0x0C86, 0xFFFF, 0xFFFE, 0xFFBD, 0xFF7C, 0xFF1B, 0xFEDA, 0xFE99, 0xFE58, 0x7DF7, 0x7DB6, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0xCE72, 0x0C86, 0xFBFF, 0xFFF1, 0xD782, 0x5AE2, 0xD5E1, 0x1D40, 0x2DE1, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x20A5,
    },
    {
        0xFFB9, 0xFFB9, 0x7FF8, 0x7FF7, 0x7FF7, 0xFFF6, 0xFFF5, 0xD782, 0xD722, 0xD6C2, 0xD662, 0xDA01, 0xD9A1,
        0x5941, 0x5901, 0x5C81,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFFB, 0xD71A, 0x5AD6, 0xF3D6, 0xC697, 0x7573, 0xDB88, 0xA5B0, 0x48EB, 0xC6E2,
        0x110B, 0x2581, 0x0CC8,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0x7F9F, 0xEEBE, 0x61DD, 0x553B, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xF37C, 0x6719, 0x5A95, 0x187A, 0x353C, 0xD65D, 0xFF7F, 0xF1BF, 0x39FC, 0x9D1A,
        0x0033, 0x002D, 0xC1D0,
    },
    {
        0xFF7F, 0xF75F, 0xF31F, 0xEAFE, 0xE2BE, 0xDE7E, 0xD65D, 0xCE1D, 0x49DD, 0x41BC, 0x3D7C, 0x353C, 0x2D1B,
        0x28DB, 0x20BA, 0x187A,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFCF, 0xFA9E, 0xDFFF, 0xD73F, 0x7687, 0x835B, 0x85DF, 0x49E3, 0x8252, 0x0112,
        0x5D00, 0x0068, 0x1840,
    },
    {
        0x29ED, 0x0000, 0xFF9D, 0xFF5F, 0xFA9E, 0x6DBA, 0x5CF6, 0x388E, 0x1846, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0xCE72, 0x0C86, 0xFBFF, 0xEF5F, 0xD67C, 0x3DB8, 0x3156, 0xFB55, 0xF24F, 0x6907, 0x5800, 0xBB3F, 0xAA7C,
        0x1579, 0x08D8, 0x20A5,
    },
    {
        0xBE89, 0x0C86, 0xFBFF, 0x6F9D, 0x675A, 0x5F18, 0x56D6, 0x0F7B, 0xC7BE, 0x8E7B, 0xEF7F, 0xFFFF, 0x11AD,
        0x114A, 0x3156, 0xB61B,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xE39D, 0xE39D, 0xDB5F, 0xD6F7, 0xCEF9, 0xBE9E, 0xBE10, 0xBA74, 0x29BA, 0x2D8C,
        0x110B, 0x004F, 0x20E8,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFDA, 0xFB56, 0xF6F3, 0x69ED, 0x5D07, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFB3D, 0xCBFF, 0xE2B8, 0xE339, 0xB3DE, 0x41D1, 0x5192, 0x8318, 0x210B, 0x3DEF,
        0x40AE, 0x016B, 0x0C86,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xE7FE, 0xCBFC, 0x83F6, 0x0394, 0xFFBE, 0xFF1B, 0xFE17, 0x7D32, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xCBBB, 0xFF7F, 0xE6DF, 0xEEB9, 0x521E, 0x5151, 0xB333, 0x9AAB, 0x391E, 0x384C,
        0xA076, 0x1003, 0x0181,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xF3FF, 0xE6DF, 0x521E, 0xB4FB, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFF76, 0xCEFE, 0xFEDB, 0x31FD, 0xFAB0, 0x7DB5, 0x18FC, 0xF5CA, 0x5D2F, 0x0015,
        0x34A3, 0x2465, 0x000E,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0x7B25, 0x7687, 0x5D00, 0xE3DF, 0xAADF, 0x2A5F, 0x001B, 0x6EFF, 0xD5BF, 0x2E2C,
        0x675A, 0x837F, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0x809B, 0xFB7F, 0xCF3F, 0xFE7E, 0xF6DF, 0xCE94, 0x6556, 0xE63D, 0xC2DE, 0x4D10,
        0x55BB, 0xAE3C, 0x1C66,
    },
    {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFF6, 0x7391, 0x5EF0, 0x4A4D, 0x2D6B, 0x14A5, 0xFFFB, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFDD, 0xFF79, 0xFED4, 0xFDEC, 0x7D26, 0x7D26, 0x7C81, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFDD, 0xFF79, 0xFED4, 0xFDEC, 0x7D26, 0x7D26, 0x7C81, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFFB, 0xEBF1, 0xDBE7, 0xAE22, 0x1D61, 0x0CC0, 0xFFFD, 0xBEE3, 0xCFA3, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFFA, 0xFFE9, 0xD286, 0x39C4, 0x2522, 0x25FF, 0xA11C, 0xEBBF, 0xD37F, 0xB71F,
        0x9ABF, 0x825F, 0x20A5,
    },
    {
        0xCE72, 0x0C86, 0xFBFF, 0x4FFF, 0xAF5A, 0x9295, 0x0230, 0xCBF8, 0xAAD0, 0x11E9, 0x0124, 0xBEBF, 0x1DFD,
        0x9578, 0x90F1, 0x20A5,
    },
    {
        0x4FFF, 0x4BDE, 0xC3BD, 0xBF9C, 0xBB7C, 0xB77B, 0xAF5A, 0xAB39, 0xA739, 0x9F18, 0x9AF7, 0x96D6, 0x8ED6,
        0x8AB5, 0x8694, 0x8293,
    },
    {
        0xCE72, 0x0C86, 0xFE26, 0xD2FD, 0xFECD, 0xFF76, 0xFF53, 0xB638, 0xFE70, 0x1952, 0x35F0, 0x008D, 0x7D69,
        0xCA95, 0x7C43, 0xEFDC,
    },
    {
        0xFBFE, 0x0C86, 0xAFF9, 0xB7F3, 0xA3CD, 0x8B48, 0x82E6, 0x8245, 0xE929, 0x29ED, 0x35F0, 0xAA90, 0x6AD6,
        0x6A94, 0x75EF, 0xE929,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFBF, 0xFFB8, 0xEFBF, 0xF2F9, 0xFAF2, 0x575F, 0x55F1, 0xF60C, 0xB69B, 0x412A,
        0x5D26, 0x1D6F, 0x30A5,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFFB, 0xF3D6, 0xE3AE, 0xD766, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xDEF7, 0xF3E3, 0x4BDF, 0xCEF8, 0x1CFB, 0x0F7F, 0xBA53, 0xCA52, 0x8257, 0x152A,
        0x0014, 0x0085, 0x0003,
    },
    {
        0x29ED, 0x0000, 0xFF9D, 0xFF5F, 0xFA9E, 0x6DBA, 0x5CF6, 0x388E, 0x1846, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0x739F, 0xFEFB, 0xFE17, 0xDAFF, 0x5D2F, 0xAB9F, 0x39FD, 0x9255, 0x0018, 0x3198,
        0x0011, 0x18ED, 0x0C67,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFFB, 0xF3D6, 0xE3AE, 0xD766, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xEB5F, 0xDB5F, 0xC23C, 0xE339, 0xA5DD, 0xAD99, 0x024D, 0x0098, 0x2134, 0x3DEF,
        0x81A4, 0x002C, 0x0C68,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xDB5F, 0xBE9E, 0xA5DD, 0x009B, 0x0052, 0x0003, 0x0B9F, 0x8E7F, 0x197F, 0xC75F,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xEFBF, 0xDBFF, 0xD71A, 0xA7FF, 0xBE75, 0xEB1F, 0x8B19, 0xA5B0, 0x4DDD, 0x858D,
        0x110B, 0x2C7B, 0x0885,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xEFBF, 0xEB1F, 0x4DDD, 0x2C7B, 0xCEFE, 0xBA3D, 0x257D, 0x001B, 0x0015, 0xDB7E,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x2954, 0x0C86, 0xFBFF, 0xE79C, 0xFBD8, 0xDF5A, 0xEB51, 0x0000, 0xC6B4, 0xD6AB, 0x6F7B, 0x29CC, 0x35A1,
        0x0000, 0x00C2, 0x1480,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xEFBF, 0xEB1F, 0x4DDD, 0x2C7B, 0xCEFE, 0xBA3D, 0x257D, 0x001B, 0x0015, 0xDB7E,
        0x0000, 0x0000, 0x0000,
    },
    {
        0xA591, 0x0C86, 0xFBFF, 0xE339, 0xFBFF, 0xEB5F, 0x3FBF, 0xE339, 0xCE5E, 0x82FC, 0x6BFF, 0xCA52, 0x3179,
        0x35AD, 0x1CEF, 0x8235,
    },
    {
        0xA591, 0x0C86, 0xFBFF, 0xE339, 0xE31F, 0xFFFB, 0x3FBF, 0xCE5E, 0xFFA3, 0x82FC, 0x6BFF, 0x3179, 0x7687,
        0x2513, 0x71E9, 0x8235,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xD69F, 0xF7DE, 0xF295, 0xEF9C, 0x5D8E, 0xD6B3, 0x20FD, 0x0016, 0x4889, 0x3DEB,
        0x30C5, 0x14C0, 0x0006,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFF7F, 0x7F3D, 0x7298, 0x69B6, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xE79E, 0xCFFF, 0xD71A, 0xE31F, 0x9BFF, 0xB633, 0xCA5F, 0x8AB6, 0x216E, 0x3198,
        0x858D, 0x0CC8, 0x2C7B,
    },
    {
        0x29ED, 0x2C7B, 0xEF7F, 0xE73F, 0xDAFF, 0xD29F, 0xCA5F, 0x35DB, 0x2C7B, 0xDB7E, 0x2F5F, 0x82DF, 0x01FC,
        0x001B, 0x0015, 0xFFFF,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xF338, 0xF3D8, 0xFB58, 0xDA52, 0xDF31, 0xEE6F, 0x3D4A, 0xCA6A, 0x5D00, 0x24C6,
        0x14C0, 0x3480, 0x9463,
    },
    {
        0xFF9B, 0xFB59, 0xF338, 0xEEF6, 0xE6B5, 0xE293, 0xDA52, 0xD610, 0x4DEF, 0x49AD, 0x416B, 0x3D4A, 0x3108,
        0x28C6, 0x1CA5, 0x0821,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xE39A, 0xFFD9, 0xEF9C, 0xCAB0, 0xD6D6, 0xF734, 0xB609, 0x3DEF, 0xE64D, 0x294A,
        0x2143, 0xDD88, 0x0440,
    },
    {
        0xEFFF, 0xEBDC, 0xE39A, 0xDF57, 0xD735, 0xD2F3, 0xCAB0, 0xC28E, 0xBE4B, 0xB609, 0x31E7, 0x29A4, 0x2143,
        0x18E2, 0x0C81, 0x0440,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xE339, 0x6B3C, 0x9E1B, 0xFA54, 0x0133, 0x0046, 0xE339, 0x64EA, 0xE339, 0x4C03,
        0xE339, 0x2001, 0xE339,
    },
    {
        0x29ED, 0x0000, 0xFF9D, 0xE73C, 0xE25C, 0x59BC, 0x4D19, 0x2CB1, 0x0427, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xD7DD, 0xF73F, 0xB77B, 0xE339, 0xF2BC, 0x8AB6, 0xCE94, 0x5975, 0x85F0, 0x0000,
        0xB84D, 0x00C6, 0x0000,
    },
    {
        0x29ED, 0xC1E2, 0xFFFA, 0xFFFD, 0xF7B6, 0xEB51, 0xDEED, 0xD289, 0xC1E2, 0xE7FF, 0xC3FF, 0x037F, 0x82DF,
        0x81BB, 0x8156, 0xFFFF,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xEBBF, 0xC21F, 0xCAFD, 0xF37B, 0x9EFB, 0xBA59, 0x66B3, 0x0019, 0x1D54, 0x5589,
        0x0010, 0x18CD, 0x2083,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xEFFF, 0xD7FF, 0xBFFF, 0x9BFF, 0x839C, 0x82D6, 0xE37E, 0xC71F, 0x9EBF, 0x825F,
        0x85FB, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFDE, 0xC6BB, 0xF77C, 0xC71F, 0xA617, 0xE6D7, 0x1A7C, 0x1572, 0xD212, 0x85D9,
        0x110B, 0x354B, 0x1CED,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFFD, 0xFFFA, 0xFFF4, 0x7FEE, 0x7FE6, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xEB5F, 0xCA5F, 0x0C7F, 0x0012, 0x000D, 0x0006, 0xE31F, 0x0015, 0x001F, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFF5D, 0xEE16, 0x58EF, 0x3C69, 0x2425, 0x25FF, 0xA11C, 0xF7BF, 0xE31F, 0xBDFF,
        0x253F, 0x001F, 0x20A5,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFBD9, 0xEB7B, 0xCF7E, 0xEB70, 0xA2BB, 0x5708, 0xCE94, 0x81F6, 0xC263, 0x3DEF,
        0x0170, 0x2DA2, 0xFC9B,
    },
    {
        0x29ED, 0x0015, 0xFBFF, 0xE31F, 0xD7FF, 0xD37F, 0xCA5F, 0xB2DF, 0x001F, 0xA7FF, 0x965F, 0x001C, 0x8339,
        0x8597, 0x0015, 0x8273,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFB7F, 0xF3DF, 0xEAFF, 0xEF9C, 0x1FBF, 0xD65F, 0x0000, 0x82B9, 0x41B7, 0xCA52,
        0x8190, 0x24EC, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xF77F, 0xEB1F, 0xE29E, 0x4DDD, 0x38FB, 0x7B7F, 0xFEBF, 0xFE1F, 0x6CFB, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x55E0, 0x0C86, 0xFBFF, 0xEFBD, 0xD31F, 0x0000, 0xE35A, 0xAA1F, 0x0000, 0xCAB6, 0x1579, 0x0000, 0x29AD,
        0x00B2, 0x0000, 0x18E7,
    },
    {
        0x55E0, 0x0000, 0xFBFF, 0xEFFF, 0x8318, 0xCFFF, 0x83FF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFBFE, 0xFBBD, 0xFBD7, 0xE339, 0xEF3A, 0xEF4E, 0xAADF, 0x4DF1, 0xE609, 0x3DEF,
        0x0256, 0x2486, 0x5905,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFFB, 0x7FF2, 0xFFA3, 0xFE6D, 0x5D00, 0xFFBE, 0xFF1B, 0xFE78, 0x7DD5, 0x7110,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xE7BF, 0xE7BD, 0xC73F, 0xCF7E, 0x7E5F, 0xAABF, 0xAEFC, 0xCEF7, 0x89DB, 0x8239,
        0xDD3A, 0x00EE, 0x01B3,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xDFFF, 0xBBFF, 0x3BDF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0x0000, 0xCF3F, 0xDEF7, 0xC6DF, 0xFF12, 0xA19D, 0xFDE9, 0x1539, 0x50C2, 0x8470,
        0x2461, 0x0025, 0x0000,
    },
    {
        0x29ED, 0x0000, 0xFBFF, 0xFFFB, 0xF7B3, 0xEB08, 0xDE60, 0x55A0, 0xE37F, 0xC2FF, 0xA67F, 0x8DFF, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xDBFF, 0xCB94, 0xA39C, 0xE339, 0xBF51, 0x9318, 0x5192, 0x9A89, 0x09EF, 0x3DEF,
        0x40AE, 0x0121, 0x04C6,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xE7FE, 0xCBFC, 0x83F6, 0x0394, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFBF, 0x57FF, 0xFF7F, 0x373B, 0xFF37, 0xF2DC, 0x0000, 0xFEB1, 0xEA19, 0x8256,
        0x65CA, 0x5133, 0x3D06,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFFB, 0xFFF6, 0xFFCF, 0xFFA3, 0x7F45, 0xFF9F, 0x7F1F, 0xFE7F, 0x79BF, 0x7687,
        0x71E9, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0x0360, 0xDBFF, 0x7F3B, 0xB37B, 0xE5F3, 0xAAD7, 0xCE94, 0x0DCE, 0xE0D0, 0x0108,
        0x30A7, 0x0084, 0xABF2,
    },
    {
        0x29ED, 0x2A40, 0x5640, 0x7E40, 0x82C0, 0xAAC0, 0xD6C0, 0xFEC0, 0x0360, 0x2B60, 0x5760, 0x7F60, 0x83E0,
        0xABE0, 0xD7E0, 0xFFE0,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFFF, 0xFF59, 0xFB9C, 0xE339, 0xFE0D, 0xEB17, 0xFE17, 0x7168, 0x51EE, 0x3DEF,
        0x50AC, 0x40C4, 0x3082,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFFB, 0xFFF6, 0xFFCD, 0xFB82, 0x6F42, 0xFF1B, 0xFE17, 0x7D32, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0x7BFF, 0x679E, 0x63BD, 0x7B7A, 0x4ED9, 0x5359, 0x6ED5, 0x3633, 0x3693, 0x59EE, 0x1D6E,
        0x1DED, 0x4549, 0x0CA8,
    },
    {
        0x29ED, 0x0C86, 0x7BFF, 0x63FF, 0x4B5F, 0x2E7F, 0x1D9F, 0x08BF, 0x001C, 0x0015, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0x7BFF, 0x63BD, 0x0000, 0x5B5A, 0x0000, 0x4EF7, 0x0000, 0x3A73, 0x0000, 0x0000, 0x1D8D,
        0x0000, 0x0000, 0x0908,
    },
    {
        0x29ED, 0x0C86, 0x7BFF, 0x7FBF, 0x771F, 0x6E9E, 0x61BD, 0x6539, 0x68B6, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0x7BFF, 0x0000, 0x7FFD, 0x0000, 0x7F55, 0x0000, 0x0000, 0x7A6F, 0x0000, 0x0000, 0x75A9,
        0x0000, 0x0000, 0x48E4,
    },
    {
        0x29ED, 0x0C86, 0x7BFF, 0x6FFF, 0x4F7F, 0x22DF, 0x025F, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0x7BFF, 0x771A, 0x2F79, 0x53BF, 0x6E75, 0x364D, 0x2EFE, 0x61B0, 0x024F, 0x021C, 0x50EA,
        0x19A1, 0x01B6, 0x34A6,
    },
    {
        0x29ED, 0x0273, 0x7BFF, 0x5BFF, 0x43FF, 0x1BFF, 0x0B7C, 0x02D3, 0x0AF7, 0x0000, 0x0AB6, 0x0000, 0x0000,
        0x020E, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFFD, 0xD7FF, 0xFBD9, 0x373B, 0xC21D, 0xEB10, 0x0000, 0x001A, 0xDE8A, 0x8256,
        0x0012, 0xCDE3, 0x000D,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFBF, 0xFB3E, 0xF29B, 0x65D8, 0xFFFB, 0xFBD5, 0xEB8F, 0xDF49, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xE31F, 0xE714, 0xD250, 0x317B, 0xC63F, 0xFF5F, 0xEE99, 0x59B3, 0x20F9, 0x356A, 0x48CE,
        0x0415, 0x20A5, 0x20A5,
    },
    {
        0x0000, 0x0C86, 0xFBFF, 0xF39C, 0xDEF7, 0xD294, 0xCA52, 0x3DEF, 0x25FF, 0xA11C, 0xEBFF, 0xD3FF, 0xBBFF,
        0xA77F, 0xA6FF, 0x20A5,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFBF, 0xFFFB, 0x7F5D, 0xE6BE, 0x52D0, 0x7236, 0x455A, 0x31E4, 0x5CAF, 0x2012,
        0xD6D6, 0x30A6, 0x0C07,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFFB, 0xE353, 0xD6EF, 0xBE47, 0x25A0, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xFFFB, 0xE353, 0xD6EF, 0xBE47, 0x25A0, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x61CA, 0x0C86, 0xFBFF, 0xFF7F, 0xFEDF, 0xE3B1, 0xDE7E, 0x5DFC, 0x413C, 0x4977, 0xC707, 0x2416, 0x28CD,
        0xAA20, 0x1810, 0x0C24,
    },
    {
        0x29ED, 0x0000, 0x0000, 0x809B, 0xFB7F, 0xCF3F, 0xFE7E, 0xF6DF, 0xCE94, 0x6556, 0xE63D, 0xC2DE, 0x4D10,
        0x55BB, 0xAE3C, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0x7778, 0x57FF, 0xF735, 0x175B, 0xEE91, 0x8275, 0x5A69, 0xC1A6, 0x5D27, 0x018D,
        0x30A3, 0x0043, 0x2804,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xF3FF, 0xCBFF, 0xA7BD, 0x837B, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0xCE72, 0x0C86, 0x818F, 0xF2F7, 0x9696, 0xDADF, 0xB75D, 0xDE10, 0xB9DC, 0x4909, 0xCA95, 0x3443, 0x1CF9,
        0x35F0, 0x0016, 0xEFDC,
    },
    {
        0xFBFE, 0x0C86, 0xAFF9, 0xB7F3, 0xA3CD, 0x8B48, 0x82E6, 0x8245, 0xE929, 0x29ED, 0x35F0, 0xAA90, 0x6AD6,
        0x6A94, 0x75EF, 0xE929,
    },
    {
        0xB20D, 0x0C86, 0xFBFF, 0xF2FF, 0x5DBD, 0x48F9, 0x34B3, 0xFFF7, 0xEBEF, 0xDBE7, 0xCFA3, 0xC303, 0xB682,
        0xEB5A, 0xD294, 0x39CE,
    },
    {
        0x7E44, 0x0C86, 0xFBFF, 0x6107, 0x69EA, 0xF2CE, 0xFB92, 0xFBFF, 0x2B60, 0x2B60, 0x5760, 0x7F60, 0x83E0,
        0xABE0, 0xD7E0, 0xFFE0,
    },
    {
        0x82C9, 0x0C86, 0xFFFF, 0xD3FF, 0x7E9B, 0xDB1F, 0xE739, 0xC631, 0x2E1C, 0x64F3, 0x542F, 0x1536, 0x835B,
        0x8253, 0x088A, 0x0CCF,
    },
    {
        0x5A40, 0x0C86, 0xFFFF, 0x83FF, 0xD3FF, 0x83BF, 0xE739, 0xD6B5, 0xC631, 0x423F, 0x9D1A, 0x0033, 0xFF2C,
        0xEA47, 0x5100, 0x19EE,
    },
    {
        0xCE72, 0x0C86, 0xFBFF, 0xFB92, 0xFB92, 0xF24A, 0x6107, 0x47DF, 0x2B3D, 0x9257, 0x8D93, 0xEFFF, 0xDBE8,
        0x3682, 0x21A1, 0x20A5,
    },
    {
        0x7E44, 0x7E5F, 0x7E5F, 0x7E5F, 0xEFFF, 0x53FF, 0x2FDF, 0xA71F, 0x9E5E, 0x117D, 0x2FDF, 0xA71F, 0x9E5E,
        0x117D, 0x08DD, 0x001C,
    },
    {
        0x29ED, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xEBDF, 0x5BDF, 0xD35F, 0xBEDE, 0xAA3D, 0x25FF, 0xA11C, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x48E6, 0x0C86, 0x7BDE, 0x57FF, 0x2FFF, 0x06FF, 0x021F, 0x011F, 0x00DA, 0x00B5, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000,
    },
    {
        0x29ED, 0x0C86, 0xFBFF, 0xDB5F, 0xBE9E, 0xA5DD, 0x009B, 0x0052, 0x0003, 0x0B9F, 0x8E7F, 0x197F, 0xC75F,
        0x0000, 0x0000, 0x0000,
    },
    {
        0xCE72, 0x0C86, 0xFBFF, 0x4FFF, 0xAF5A, 0x9295, 0x0230, 0xCBF8, 0xAAD0, 0x11E9, 0x0124, 0xFF7F, 0xFB3F,
        0xEEBC, 0x49B2, 0x20A5,
    },
    {
        0xCE72, 0x0C86, 0xFBFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFF7F, 0xFB3F,
        0xEEBC, 0x49B2, 0x0000,
    },
};

const u16 D_08556DD4[] __attribute__((section(".rodata.00553734"), aligned(1))) =
{
    0x3D4A, 0x0C86, 0xFBFF, 0xF7DE, 0xEF9C, 0xEB7B, 0xE339, 0xDEF7, 0xD6D6, 0xCE94, 0xCA52, 0xC231, 0x3DEF,
    0x35AD, 0x2D8C, 0x294A,
};

/* ROM 0x0856B440..0x0856B4E8; .rodata.0056B440. */
const u16 D_0856B440[] __attribute__((section(".rodata.0056B440"), aligned(1))) =
{
    0x0004, 0x0044, 0x0002, 0x0001, 0x0008, 0x0024, 0x0000, 0x0004, 0x00F8, 0x0004, 0x0000, 0x40F8, 0x01F4,
    0x0001, 0x80E8, 0x0004, 0x0003, 0x00E8, 0x41F4, 0x0005, 0x0000, 0x0004, 0x00F8, 0x0004, 0x0009, 0x40F8,
    0x01F4, 0x000A, 0x80E8, 0x0004, 0x000C, 0x00E8, 0x41F4, 0x000E, 0x0001, 0x0008, 0x0006, 0x0002, 0x0000,
    0x0004, 0x0001, 0x0008,
};

const u16 D_0856B494[] __attribute__((section(".rodata.0056B440"), aligned(1))) =
{
    0x0004, 0x0044, 0x0002, 0x0001, 0x0008, 0x0024, 0x0000, 0x0004, 0x00F8, 0x0004, 0x0000, 0x40F8, 0x01F4,
    0x0001, 0x80E8, 0x0004, 0x0003, 0x00E8, 0x41F4, 0x0005, 0x0000, 0x0004, 0x00F8, 0x0004, 0x0009, 0x40F8,
    0x01F4, 0x000A, 0x80E8, 0x0004, 0x000C, 0x00E8, 0x41F4, 0x000E, 0x0001, 0x0008, 0x0006, 0x0002, 0x0000,
    0x0004, 0x0001, 0x0008,
};

/* ROM 0x08619AA4..0x08619FE4; .rodata.00619AA4. */
const u8 D_08619AA4[] __attribute__((section(".rodata.00619AA4"), aligned(1))) =
{
    0x66, 0x06, 0x00, 0x00, 0xF6, 0x6F, 0x00, 0x00, 0x86, 0xF7, 0x06, 0x00, 0x60, 0x78, 0x6F, 0x00, 0x00, 0x86,
    0xF7, 0x06, 0x00, 0x60, 0x78, 0x6F, 0x00, 0x00, 0x86, 0xF7, 0x00, 0x00, 0x60, 0x78, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86, 0x00, 0x00, 0x00, 0x60,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF7, 0x06, 0x30, 0x03, 0x78, 0x6F, 0x53, 0x34, 0x86, 0xF7, 0x45, 0x03,
    0x60, 0x98, 0x34, 0x00, 0x30, 0x45, 0x78, 0x06, 0x53, 0x34, 0x87, 0x67, 0x43, 0x03, 0x76, 0x68, 0x30, 0x00,
    0x60, 0x66, 0x90, 0x09, 0x00, 0x99, 0xA9, 0x9A, 0x99, 0xAA, 0xA9, 0xAB, 0xAA, 0xFB, 0x90, 0xFA, 0xFB, 0xBF,
    0x90, 0xBA, 0xBF, 0xBA, 0x90, 0xFA, 0xFB, 0xFB, 0xA9, 0xFB, 0xBA, 0xFF, 0xA9, 0xBF, 0xFB, 0xFF, 0x99, 0x00,
    0x90, 0x09, 0xAA, 0x99, 0xA9, 0x9A, 0xBF, 0xAA, 0xBA, 0x9A, 0xFB, 0xBF, 0xAF, 0x09, 0xAB, 0xFB, 0xAB, 0x09,
    0xBF, 0xBF, 0xAF, 0x09, 0xFF, 0xAB, 0xBF, 0x9A, 0xFF, 0xBF, 0xFB, 0x9A, 0xA9, 0xAB, 0xFB, 0xFF, 0xA9, 0xFB,
    0xBA, 0xFF, 0x90, 0xFA, 0xFB, 0xFB, 0x90, 0xBA, 0xBF, 0xBA, 0x90, 0xFA, 0xFB, 0xBF, 0xA9, 0xAB, 0xAA, 0xFB,
    0xA9, 0x9A, 0x99, 0xAA, 0x90, 0x09, 0x00, 0x99, 0xFF, 0xBF, 0xFB, 0x9A, 0xFF, 0xAB, 0xBF, 0x9A, 0xBF, 0xBF,
    0xAF, 0x09, 0xAB, 0xFB, 0xAB, 0x09, 0xFB, 0xBF, 0xAF, 0x09, 0xBF, 0xAA, 0xBA, 0x9A, 0xAA, 0x99, 0xA9, 0x9A,
    0x99, 0x00, 0x90, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30,
    0x33, 0x33, 0x30, 0x43, 0x55, 0x55, 0x30, 0x55, 0xFF, 0x4F, 0x43, 0xF5, 0x5F, 0x55, 0x53, 0xF5, 0x55, 0xFF,
    0x00, 0x30, 0x03, 0x00, 0x00, 0x53, 0x35, 0x00, 0x00, 0x43, 0x55, 0x03, 0x13, 0x31, 0x55, 0x34, 0x14, 0x12,
    0x43, 0x34, 0x44, 0x21, 0x31, 0x03, 0x55, 0x14, 0x11, 0x00, 0x5F, 0x44, 0x13, 0x01, 0x53, 0x55, 0xF5, 0xFF,
    0x53, 0x55, 0x55, 0x5F, 0x43, 0x45, 0x55, 0x55, 0x30, 0x54, 0x45, 0x45, 0x30, 0x44, 0x55, 0x54, 0x00, 0x43,
    0x44, 0x44, 0x00, 0x30, 0x43, 0x44, 0x00, 0x00, 0x30, 0x33, 0x45, 0x44, 0x03, 0x21, 0x44, 0x45, 0x03, 0x12,
    0x55, 0x45, 0x03, 0x10, 0x45, 0x44, 0x03, 0x20, 0x44, 0x44, 0x03, 0x00, 0x44, 0x34, 0x00, 0x00, 0x44, 0x33,
    0x00, 0x00, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x66, 0x66, 0x66, 0x60, 0x77, 0x88, 0x88,
    0x60, 0x67, 0x66, 0x66, 0x60, 0x67, 0xBA, 0xBB, 0x60, 0x67, 0xAA, 0xAA, 0x60, 0x67, 0xAA, 0xAA, 0x60, 0x67,
    0xA9, 0x9A, 0x00, 0x00, 0x00, 0x00, 0x66, 0x66, 0x66, 0x06, 0xFF, 0xFF, 0xFF, 0x06, 0x66, 0x66, 0xF6, 0x06,
    0xFF, 0xFF, 0x86, 0x06, 0xBF, 0xBB, 0x86, 0x06, 0xBF, 0xBB, 0x86, 0x06, 0xBF, 0xBB, 0x86, 0x06, 0x60, 0x67,
    0x99, 0x9A, 0x60, 0x67, 0x99, 0x99, 0x00, 0x76, 0x96, 0x99, 0x00, 0x76, 0x67, 0x99, 0x00, 0x60, 0x77, 0x96,
    0x00, 0x00, 0x76, 0x67, 0x00, 0x00, 0x60, 0x77, 0x00, 0x00, 0x00, 0x66, 0xBF, 0xBB, 0x86, 0x06, 0xBF, 0xAB,
    0x86, 0x06, 0xBB, 0x6A, 0x68, 0x00, 0xAB, 0x86, 0x67, 0x00, 0x6A, 0x78, 0x06, 0x00, 0x86, 0x67, 0x00, 0x00,
    0x78, 0x06, 0x00, 0x00, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x88, 0x00, 0x00,
    0x70, 0xF8, 0x00, 0x00, 0x87, 0xFF, 0x00, 0x00, 0x76, 0xF8, 0x00, 0x66, 0x69, 0x87, 0x60, 0xA9, 0xAA, 0x76,
    0x60, 0xA9, 0xAA, 0x6A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x8F, 0x00,
    0x00, 0x00, 0x8F, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x60, 0x99, 0xAA, 0xAA, 0x00, 0x66, 0x96, 0xA9, 0x00, 0x00, 0x60, 0x99, 0x00, 0x00, 0x00, 0x96, 0x00, 0x00,
    0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x06, 0x00, 0x00,
    0xBB, 0x6A, 0x06, 0x00, 0xAA, 0xBB, 0x6A, 0x00, 0xA9, 0xBA, 0x6B, 0x00, 0x99, 0xA9, 0x6B, 0x00, 0x66, 0x66,
    0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x7F, 0x77, 0x77,
    0x80, 0xF7, 0xFF, 0x3F, 0x00, 0x78, 0x9F, 0x39, 0x00, 0x86, 0xF7, 0xAA, 0x00, 0x86, 0x78, 0x3F, 0x00, 0xF6,
    0x89, 0x47, 0x00, 0xF6, 0xA9, 0x54, 0x30, 0x33, 0x00, 0x00, 0x43, 0x44, 0x03, 0x00, 0x54, 0x55, 0x34, 0x00,
    0x55, 0x43, 0x35, 0x00, 0x33, 0x54, 0x35, 0x00, 0x54, 0x45, 0x63, 0x00, 0x45, 0x33, 0x68, 0x00, 0x33, 0x9A,
    0x6F, 0x00, 0x00, 0xF6, 0x49, 0x35, 0x00, 0x86, 0x59, 0xA3, 0x00, 0x86, 0x45, 0xA3, 0x00, 0x50, 0x34, 0xA9,
    0x00, 0x44, 0x83, 0x98, 0x40, 0x33, 0x60, 0x87, 0x33, 0x00, 0x00, 0x76, 0x00, 0x00, 0x00, 0x60, 0xF7, 0x9A,
    0x68, 0x00, 0x78, 0x9F, 0x58, 0x05, 0x8A, 0xF7, 0x45, 0x04, 0x9A, 0x48, 0x34, 0x00, 0x89, 0x43, 0x73, 0x08,
    0x78, 0x33, 0x60, 0x87, 0x67, 0x00, 0x00, 0x76, 0x06, 0x00, 0x00, 0x00, 0xC0, 0xDD, 0x0C, 0x00, 0xDC, 0xFE,
    0xCD, 0xDD, 0xDC, 0xFE, 0xCD, 0xED, 0xDC, 0xFE, 0xCD, 0xED, 0xDC, 0xFE, 0xCD, 0xDC, 0xDC, 0xFE, 0xCD, 0xED,
    0xDC, 0xFE, 0xCD, 0xED, 0xDC, 0xFE, 0xCD, 0xCC, 0x00, 0x00, 0x00, 0x00, 0x0D, 0x00, 0x00, 0x00, 0xDD, 0x0D,
    0x00, 0x00, 0xEE, 0xEE, 0xFE, 0xDF, 0xDC, 0xEE, 0xEE, 0xDF, 0xCE, 0xDC, 0xED, 0xDF, 0xEE, 0xEE, 0xDD, 0xDF,
    0xDD, 0xEE, 0xEE, 0xDF, 0xDC, 0xFE, 0xCD, 0xED, 0xDC, 0xFE, 0xCD, 0xED, 0xEC, 0xDD, 0xCF, 0xCC, 0xDC, 0xCC,
    0xCD, 0xED, 0xDC, 0xCC, 0xCD, 0xED, 0xC0, 0xDD, 0xCC, 0xCC, 0x00, 0xCC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xCE, 0xDD, 0xEE, 0xDF, 0xEE, 0xEE, 0xCD, 0xDF, 0xDD, 0xEE, 0xEE, 0xDF, 0xDE, 0xCC, 0xED, 0xDF, 0xEE, 0xEE,
    0xCD, 0xDF, 0xDD, 0xEE, 0xEE, 0xDF, 0xCC, 0xDC, 0xDD, 0xDD, 0x00, 0xC0, 0xCC, 0xCC, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x88,
    0x88, 0x88, 0xF8, 0xFF, 0xFF, 0xFF, 0x87, 0x88, 0x88, 0x88, 0xF0, 0x08, 0x00, 0x00, 0xF8, 0x8F, 0x00, 0x00,
    0xF7, 0x8F, 0x00, 0x00, 0x87, 0xFF, 0x08, 0x00, 0x70, 0xFF, 0x08, 0x00, 0x77, 0xF8, 0x8F, 0x80, 0x78, 0xF7,
    0x8F, 0x88, 0x88, 0xF8, 0xFF, 0xF8, 0x70, 0x77, 0x77, 0x77, 0x00, 0x00, 0x87, 0x8F, 0x00, 0x00, 0x87, 0xFF,
    0x00, 0x00, 0x76, 0x88, 0x00, 0x00, 0x86, 0xFF, 0x00, 0x00, 0x60, 0x88, 0x00, 0x00, 0x00, 0x66, 0x00, 0x00,
    0x00, 0x00, 0x87, 0xF7, 0xFF, 0xF8, 0x77, 0x87, 0x8F, 0xF8, 0x68, 0x77, 0x78, 0xF8, 0x67, 0x77, 0x67, 0x77,
    0x68, 0x67, 0x06, 0x66, 0x67, 0x66, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
};

const u8 D_08619FA4[] __attribute__((section(".rodata.00619AA4"), aligned(1))) =
{
    0x10, 0x11, 0x11, 0x11, 0x91, 0x99, 0x99, 0x19, 0x10, 0x11, 0x51, 0x19, 0x00, 0x91, 0x95, 0x01, 0x00, 0x91,
    0x19, 0x00, 0x00, 0x91, 0x01, 0x00, 0x10, 0x59, 0x01, 0x00, 0x00, 0x11, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x91, 0x01, 0x00, 0x00, 0x91, 0x01, 0x00, 0x00, 0x91, 0x01, 0x10, 0x01, 0x91, 0x11, 0x31, 0x19, 0x91, 0x51,
    0x93, 0x01, 0x91, 0x99, 0x19, 0x00, 0x10, 0x11, 0x01, 0x00,
};

/* ROM 0x08619FE4..0x0861A4A4; .rodata.00619FE4. */
const u8 D_08619FE4[] __attribute__((section(".rodata.00619FE4"), aligned(1))) =
{
    0x10, 0x11, 0x11, 0x00, 0x91, 0x99, 0x99, 0x01, 0x10, 0x11, 0x91, 0x01, 0x00, 0x10, 0x19, 0x00, 0x00, 0x91,
    0x13, 0x00, 0x10, 0x19, 0x59, 0x01, 0x91, 0x01, 0x91, 0x15, 0x11, 0x00, 0x10, 0x11,
};

const u8 D_0861A004[] __attribute__((section(".rodata.00619FE4"), aligned(1))) =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x11, 0x19, 0x00, 0x10, 0x19, 0x19, 0x00, 0x10, 0x19, 0x19, 0x01, 0x10, 0x19, 0x19, 0x19, 0x10, 0x19,
    0x19, 0x19, 0x91, 0x15, 0x99, 0x14, 0x10, 0x01, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x11, 0x11, 0x01, 0x91, 0x99, 0x99, 0x19, 0x10, 0x11, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x10, 0x11, 0x19, 0x01, 0x91, 0x99, 0x99, 0x19,
    0x10, 0x11, 0x19, 0x01, 0x00, 0x41, 0x19, 0x00, 0x10, 0x94, 0x13, 0x00, 0x91, 0x39, 0x01, 0x00, 0x10, 0x11,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x10, 0x11, 0x19, 0x01, 0x91, 0x99, 0x99, 0x19, 0x10, 0x11, 0x19, 0x01, 0x00, 0x41, 0x19, 0x00,
    0x10, 0x94, 0x13, 0x00, 0x91, 0x39, 0x01, 0x00, 0x10, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x10, 0x19, 0x10, 0x00, 0x91, 0x91, 0x91, 0x01, 0x10, 0x19, 0x91, 0x01, 0x00, 0x11, 0x59, 0x01,
    0x10, 0x99, 0x15, 0x00, 0x00, 0x11, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x10, 0x19, 0x00, 0x00, 0x10, 0x95,
    0x01, 0x01, 0x91, 0x11, 0x10, 0x19, 0x10, 0x19, 0x91, 0x14, 0x10, 0x11, 0x49, 0x01, 0x91, 0x99, 0x14, 0x00,
    0x10, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x11, 0x01, 0x00, 0x91, 0x99,
    0x19, 0x00, 0x10, 0x11, 0x19, 0x00, 0x10, 0x11, 0x19, 0x00, 0x91, 0x99, 0x99, 0x01, 0x10, 0x11, 0x11, 0x00,
    0x10, 0x11, 0x01, 0x00, 0x91, 0x99, 0x15, 0x00, 0x10, 0x11, 0x99, 0x01, 0x91, 0x99, 0x11, 0x00, 0x10, 0x51,
    0x99, 0x01, 0x91, 0x19, 0x11, 0x01, 0x10, 0x95, 0x99, 0x19, 0x00, 0x11, 0x11, 0x01, 0x10, 0x11, 0x11, 0x11,
    0x91, 0x99, 0x99, 0x19, 0x10, 0x11, 0x51, 0x19, 0x00, 0x91, 0x95, 0x01, 0x00, 0x91, 0x19, 0x00, 0x00, 0x91,
    0x01, 0x00, 0x10, 0x59, 0x01, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x10, 0x10, 0x10, 0x19, 0x91, 0x91, 0x10, 0x95, 0x11, 0x11, 0x91, 0x11, 0x10, 0x19,
    0x10, 0x19, 0x91, 0x14, 0x10, 0x11, 0x49, 0x01, 0x91, 0x99, 0x14, 0x00, 0x10, 0x11, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x11, 0x11, 0x00, 0x91, 0x99, 0x99, 0x01, 0x10, 0x91, 0x11, 0x00,
    0x10, 0x91, 0x11, 0x00, 0x91, 0x99, 0x99, 0x01, 0x10, 0x11, 0x11, 0x00, 0x00, 0x01, 0x00, 0x00, 0x10, 0x19,
    0x00, 0x00, 0x10, 0x95, 0x01, 0x01, 0x91, 0x11, 0x10, 0x19, 0x10, 0x19, 0x91, 0x14, 0x10, 0x11, 0x49, 0x01,
    0x91, 0x99, 0x14, 0x00, 0x10, 0x11, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x10, 0x91, 0x11, 0x01, 0x91, 0x99,
    0x99, 0x15, 0x10, 0x91, 0x11, 0x19, 0x10, 0x95, 0x11, 0x19, 0x10, 0x19, 0x10, 0x19, 0x91, 0x15, 0x91, 0x15,
    0x10, 0x01, 0x10, 0x01, 0x00, 0x10, 0x00, 0x00, 0x10, 0x91, 0x11, 0x00, 0x91, 0x99, 0x99, 0x01, 0x10, 0x91,
    0x11, 0x11, 0x91, 0x99, 0x99, 0x09, 0x10, 0x11, 0x19, 0x01, 0x00, 0x10, 0x19, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x10, 0x00, 0x10, 0x00, 0x91, 0x01, 0x91, 0x01, 0x91, 0x01, 0x91, 0x01, 0x91, 0x01, 0x91, 0x01, 0x91, 0x01,
    0x91, 0x01, 0x10, 0x11, 0x55, 0x01, 0x10, 0x99, 0x15, 0x00, 0x00, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x10, 0x11, 0x11, 0x01, 0x91, 0x99, 0x99, 0x19, 0x10, 0x11, 0x11, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x11, 0x00, 0x10, 0x99, 0x99, 0x01, 0x10, 0x11, 0x11, 0x01,
    0x91, 0x99, 0x99, 0x19, 0x10, 0x11, 0x19, 0x01, 0x10, 0x91, 0x13, 0x00, 0x91, 0x33, 0x01, 0x00, 0x10, 0x11,
    0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x91, 0x01, 0x00, 0x10, 0x94, 0x01, 0x00, 0x10, 0x19, 0x11, 0x00,
    0x91, 0x19, 0x49, 0x01, 0x91, 0x11, 0x91, 0x14, 0x91, 0x99, 0x99, 0x19, 0x10, 0x11, 0x11, 0x19, 0x00, 0x01,
    0x10, 0x10, 0x10, 0x19, 0x91, 0x91, 0x10, 0x95, 0x11, 0x11, 0x91, 0x11, 0x10, 0x19, 0x10, 0x19, 0x91, 0x14,
    0x10, 0x11, 0x49, 0x01, 0x91, 0x99, 0x14, 0x00, 0x10, 0x11, 0x01, 0x00, 0x10, 0x01, 0x00, 0x00, 0x41, 0x19,
    0x00, 0x00, 0x10, 0x94, 0x01, 0x01, 0x00, 0x11, 0x10, 0x19, 0x00, 0x00, 0x91, 0x14, 0x10, 0x11, 0x49, 0x01,
    0x91, 0x99, 0x14, 0x00, 0x10, 0x11, 0x01, 0x00, 0x00, 0x11, 0x11, 0x00, 0x10, 0x99, 0x99, 0x01, 0x10, 0x11,
    0x11, 0x01, 0x91, 0x99, 0x99, 0x19, 0x10, 0x11, 0x11, 0x19, 0x00, 0x11, 0x99, 0x14, 0x10, 0x99, 0x49, 0x01,
    0x00, 0x11, 0x11, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x91, 0x01, 0x00, 0x10, 0x94, 0x01, 0x00, 0x10, 0x19,
    0x11, 0x00, 0x91, 0x19, 0x49, 0x01, 0x91, 0x11, 0x91, 0x14, 0x91, 0x99, 0x99, 0x19, 0x10, 0x11, 0x11, 0x19,
    0x10, 0x11, 0x11, 0x00, 0x91, 0x99, 0x99, 0x01, 0x10, 0x11, 0x91, 0x01, 0x00, 0x10, 0x19, 0x00, 0x00, 0x91,
    0x13, 0x00, 0x10, 0x19, 0x59, 0x01, 0x91, 0x01, 0x91, 0x15, 0x11, 0x00, 0x10, 0x11, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x11, 0x11, 0x10, 0x91, 0x91, 0x91,
    0x91, 0x99, 0x99, 0x14, 0x10, 0x91, 0x11, 0x19, 0x00, 0x91, 0x11, 0x19, 0x10, 0x49, 0x11, 0x19, 0x91, 0x14,
    0x91, 0x14, 0x10, 0x01, 0x10, 0x01, 0x10, 0x00, 0x00, 0x00, 0x91, 0x01, 0x00, 0x00, 0x91, 0x01, 0x00, 0x00,
    0x91, 0x01, 0x10, 0x01, 0x91, 0x11, 0x31, 0x19, 0x91, 0x51, 0x93, 0x01, 0x91, 0x99, 0x19, 0x00, 0x10, 0x11,
    0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x11, 0x91, 0x11, 0x91, 0x99, 0x99, 0x19, 0x10, 0x11, 0x99, 0x01,
    0x10, 0x91, 0x91, 0x01, 0x41, 0x19, 0x91, 0x01, 0x10, 0x11, 0x99, 0x01, 0x00, 0x00, 0x11, 0x00, 0x10, 0x01,
    0x00, 0x00, 0x41, 0x19, 0x00, 0x00, 0x10, 0x94, 0x01, 0x01, 0x00, 0x11, 0x10, 0x19, 0x00, 0x00, 0x91, 0x14,
    0x10, 0x11, 0x49, 0x01, 0x91, 0x99, 0x14, 0x00, 0x10, 0x11, 0x01, 0x00, 0x10, 0x00, 0x00, 0x00, 0x91, 0x01,
    0x00, 0x00, 0x91, 0x01, 0x00, 0x00, 0x91, 0x01, 0x10, 0x01, 0x91, 0x11, 0x31, 0x19, 0x91, 0x51, 0x93, 0x01,
    0x91, 0x99, 0x19, 0x00, 0x10, 0x11, 0x01, 0x00, 0x00, 0x00, 0x10, 0x01, 0x00, 0x10, 0x41, 0x19, 0x10, 0x41,
    0x95, 0x01, 0x31, 0x99, 0x19, 0x00, 0x10, 0x11, 0x19, 0x00, 0x00, 0x10, 0x19, 0x00, 0x00, 0x10, 0x19, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x91, 0x11, 0x01, 0x10, 0x94, 0x99, 0x19, 0x41, 0x19,
    0x11, 0x19, 0x91, 0x11, 0x91, 0x19, 0x10, 0x11, 0x99, 0x14, 0x10, 0x99, 0x49, 0x01, 0x00, 0x11, 0x11, 0x00,
};

const u8 D_0861A484[] __attribute__((section(".rodata.00619FE4"), aligned(1))) =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* ROM 0x0861A4A4..0x0861A7A4; .rodata.0061A4A4. */
const u32 D_0861A4A4[0x50] __attribute__((section(".rodata.0061A4A4"), aligned(1))) =
{
    0x00111100, 0x01599510, 0x16911961, 0x17911971, 0x17911971, 0x16911961, 0x01599510, 0x00111100, 0x00011000,
    0x00199100, 0x00199100, 0x00199910, 0x00199100, 0x00199100, 0x01999910, 0x00111100, 0x00111110, 0x01699991,
    0x16911110, 0x19911100, 0x19999610, 0x01111961, 0x19999991, 0x01111110, 0x00111110, 0x01599991, 0x15951110,
    0x13999910, 0x15951100, 0x15975110, 0x01599991, 0x00111110, 0x00000110, 0x00111931, 0x01991951, 0x01991991,
    0x01991991, 0x19999991, 0x01991110, 0x00110000, 0x01111110, 0x19999991, 0x01111991, 0x01699971, 0x16911110,
    0x16911110, 0x01699991, 0x00111110, 0x00111100, 0x01999510, 0x19111951, 0x01799971, 0x17911971, 0x15911951,
    0x01599510, 0x00111100, 0x00111100, 0x01699610, 0x16911961, 0x19911991, 0x19911991, 0x19910110, 0x19910000,
    0x01100000, 0x00111100, 0x01599510, 0x17911971, 0x01999910, 0x17911971, 0x17911971, 0x01599510, 0x00111100,
    0x00111100, 0x01599510, 0x15911971, 0x17911971, 0x17999710, 0x15911151, 0x01599710, 0x00111100,
};

const u8 D_0861A5E4[] __attribute__((section(".rodata.0061A4A4"), aligned(1))) =
{
    0x11, 0x10, 0x11, 0x00, 0x19, 0x11, 0x19, 0x00, 0x99, 0x99, 0x19, 0x00, 0x19, 0x11, 0x19, 0x01, 0x19, 0x91,
    0x99, 0x14, 0x01, 0x91, 0x11, 0x19, 0x00, 0x91, 0x99, 0x14, 0x01, 0x91, 0x11, 0x01, 0x11, 0x11, 0x11, 0x00,
    0x19, 0x11, 0x19, 0x00, 0x99, 0x91, 0x19, 0x00, 0x49, 0x49, 0x19, 0x01, 0x19, 0x94, 0x99, 0x14, 0x01, 0x91,
    0x11, 0x19, 0x00, 0x91, 0x99, 0x14, 0x00, 0x91, 0x11, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10,
    0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* ROM 0x0861A7E4..0x0861AAA4; .rodata.0061A7E4. */
const u32 D_0861A7E4[0xB0] __attribute__((section(".rodata.0061A7E4"), aligned(1))) =
{
    0x00000000, 0x11110000, 0x11111100, 0x11111110, 0x11111110, 0x11111100, 0x11110000, 0x00000000, 0x11111111,
    0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111,
    0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x00000000, 0x00001111, 0x00111111,
    0x01111111, 0x01111111, 0x00111111, 0x00001111, 0x00000000, 0x00000000, 0x11110000, 0x11111100, 0x11111110,
    0x11111110, 0x11111100, 0x11110000, 0x00000000, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111,
    0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111,
    0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111,
    0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111,
    0x00000000, 0x00001111, 0x00111111, 0x01111111, 0x01111111, 0x00111111, 0x00001111, 0x00000000, 0x10000000,
    0x11111000, 0x11111110, 0x11111111, 0x11111111, 0x11111110, 0x11111000, 0x10000000, 0x11111111, 0x11111111,
    0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111,
    0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111,
    0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111,
    0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111,
    0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111,
    0x11111111, 0x00000001, 0x00011111, 0x01111111, 0x11111111, 0x11111111, 0x01111111, 0x00011111, 0x00000001,
    0x00000000, 0x00000000, 0x11000000, 0x11100000, 0x11100000, 0x11000000, 0x00000000, 0x00000000, 0x00000000,
    0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x00000000, 0x00000000, 0x11111111,
    0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x00000000, 0x00000000, 0x00000000, 0x00000011,
    0x00000111, 0x00000111, 0x00000011, 0x00000000, 0x00000000,
};

/* ROM 0x0861C664..0x0861C744; .rodata.0061C664. */
const u16 D_0861C664[] __attribute__((section(".rodata.0061C664"), aligned(1))) =
{
    0xFE69, 0x0C21, 0x94A5, 0xA94A, 0xBDEF, 0xD294, 0x5EF7, 0x6B5A, 0xF39C, 0xFFFF, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0xFE69, 0x0C21, 0x1C23, 0x3026, 0x4028, 0x542B, 0xE06E, 0x68D0, 0xF513, 0x7D76,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFE69, 0x0C21, 0x8806, 0x040B, 0x000F, 0x0011, 0x0014,
    0x0016, 0x0018, 0x001A, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFE69, 0x0C21, 0x08A0, 0x0520,
    0x01A0, 0x0220, 0x8280, 0x0300, 0x0380, 0x83E0, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFE69,
    0x0C21, 0x1463, 0x9CA5, 0x2508, 0xAD4A, 0x39AD, 0xC1EF, 0x4A52, 0xD294, 0x5529, 0x7D29, 0x81A9, 0xA9A9,
    0xD5A9, 0xFDA9, 0xFE69, 0x0C21, 0x88A6, 0x894B, 0x05F0, 0x0695, 0x033A, 0x83BF, 0x93DF, 0xA3FF, 0x5769,
    0x7F69, 0x83E9, 0xABE9, 0xD7E9, 0xFFE9, 0x25AD, 0x0421, 0x94A5, 0xA94A, 0xBDEF, 0xD294, 0x5EF7, 0x6B5A,
    0xF39C, 0xFFFF, 0x0C7F, 0x0C7F, 0x0C7F, 0x0C7F, 0x0C7F, 0x0C7F,
};

/* ROM 0x0861C744..0x0861C764; .rodata.0061C744. */
const u16 D_0861C744[] __attribute__((section(".rodata.0061C744"), aligned(1))) =
{
    0x0000, 0x001F, 0x825F, 0x837F, 0x83FF, 0x825F, 0x837F, 0x83FF, 0xEBFF, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000,
};

/* ROM 0x087EBDF0..0x087EBE00; .rodata.007EBDF0. */
const void *const D_087EBDF0[4] __attribute__((section(".rodata.007EBDF0"), aligned(1))) =
{ (const void *)0x0861AD64, (const void *)0x0861B36C, (const void *)0x0861B8D8, (const void *)0x0861BEA8 };

/* ROM 0x087ED394..0x087ED5CC; .rodata.007ED394. */
const struct BattleBackgroundResources D_087ED394[] __attribute__((section(".rodata.007ED394"), aligned(1))) =
{
    { (const void *)0x08598EA4, (const u16 *)0x08608648, (const void *)0x08609368 },
    { (const void *)0x0859B600, (const u16 *)0x086086A8, (const void *)0x0860994C },
    { (const void *)0x08598EA4, (const u16 *)0x08608708, (const void *)0x08609368 },
    { (const void *)0x08598EA4, (const u16 *)0x08608768, (const void *)0x08609368 },
    { (const void *)0x085A1AB4, (const u16 *)0x086087C8, (const void *)0x0860A4B4 },
    { (const void *)0x085A40F8, (const u16 *)0x08608828, (const void *)0x0860AA2C },
    { (const void *)0x085A82DC, (const u16 *)0x08608888, (const void *)0x0860AFEC },
    { (const void *)0x085AB56C, (const u16 *)0x086088E8, (const void *)0x0860B570 },
    { (const void *)0x085AB56C, (const u16 *)0x08608948, (const void *)0x0860B570 },
    { (const void *)0x085AFA30, (const u16 *)0x086089A8, (const void *)0x0860BB30 },
    { (const void *)0x085B3038, (const u16 *)0x08608A08, (const void *)0x0860C0E0 },
    { (const void *)0x085B9154, (const u16 *)0x08608AC8, (const void *)0x0860CC60 },
    { (const void *)0x085BBF2C, (const u16 *)0x08608B28, (const void *)0x0860D214 },
    { (const void *)0x085BBF2C, (const u16 *)0x08608B88, (const void *)0x0860D214 },
    { (const void *)0x085BFB94, (const u16 *)0x08608BE8, (const void *)0x0860D7D4 },
    { (const void *)0x085C3C90, (const u16 *)0x08608C48, (const void *)0x0860DD94 },
    { (const void *)0x08598EA4, (const u16 *)0x08608CA8, (const void *)0x08609368 },
    { (const void *)0x085C7648, (const u16 *)0x08608D08, (const void *)0x0860E34C },
    { (const void *)0x085CB6F4, (const u16 *)0x08608D68, (const void *)0x0860E910 },
    { (const void *)0x085CE86C, (const u16 *)0x08608DC8, (const void *)0x0860EEC0 },
    { (const void *)0x085D1368, (const u16 *)0x08608E28, (const void *)0x0860F43C },
    { (const void *)0x085D4BCC, (const u16 *)0x08608E88, (const void *)0x0860F9FC },
    { (const void *)0x085D782C, (const u16 *)0x08608EE8, (const void *)0x0860FFB4 },
    { (const void *)0x085DA614, (const u16 *)0x08608F48, (const void *)0x08610560 },
    { (const void *)0x085DD7C8, (const u16 *)0x08608FA8, (const void *)0x08610B14 },
    { (const void *)0x085E1464, (const u16 *)0x08609008, (const void *)0x086110C4 },
    { (const void *)0x085E43F4, (const u16 *)0x08609068, (const void *)0x08611654 },
    { (const void *)0x085E7494, (const u16 *)0x086090C8, (const void *)0x08611C14 },
    { (const void *)0x085E9D58, (const u16 *)0x08609128, (const void *)0x0861219C },
    { (const void *)0x085EB6E4, (const u16 *)0x08609188, (const void *)0x08612678 },
    { (const void *)0x085ED724, (const u16 *)0x086091E8, (const void *)0x08612C28 },
    { (const void *)0x085F0F20, (const u16 *)0x08609248, (const void *)0x086131E4 },
    { (const void *)0x085F42A8, (const u16 *)0x086092A8, (const void *)0x08613778 },
    { (const void *)0x085B6204, (const u16 *)0x08608A68, (const void *)0x0860C68C },
    { (const void *)0x0859F090, (const u16 *)0x08608CA8, (const void *)0x08609F04 },
    { (const void *)0x085BFB94, (const u16 *)0x08609308, (const void *)0x0860D7D4 },
};

const void *const D_087ED544[] __attribute__((section(".rodata.007ED394"), aligned(1))) =
{
    (const void *)0x085F91F0, (const void *)0x085F9F1C, (const void *)0x085FAD30, (const void *)0x085FAD54,
    (const void *)0x085FBB80, (const void *)0x085FC434, (const void *)0x085FD2EC, (const void *)0x085FDFE4,
    (const void *)0x085FE17C, (const void *)0x085FEF38, (const void *)0x085FF768, (const void *)0x085FFD40,
    (const void *)0x08600670, (const void *)0x08600D4C, (const void *)0x086016EC, (const void *)0x08601E80,
    (const void *)0x086020B8, (const void *)0x08602B38, (const void *)0x086034A4, (const void *)0x08603520,
    (const void *)0x08603D20, (const void *)0x08604454, (const void *)0x08604D3C, (const void *)0x08605644,
    (const void *)0x08605F90, (const void *)0x0860693C, (const void *)0x08606D50, (const void *)0x08607654,
    (const void *)0x08607F50, (const void *)0x085F9018, (const void *)0x085F8F60, (const void *)0x08608260,
    (const void *)0x085F7960, (const void *)0x085F86F8,
};

/* End of split read-only data. */

/* ROM 0x087ED5CC..0x087ED6D4; .rodata.007ED5CC. */
const u16 *const gBattlePartyPetrifiedPalettes[BATTLE_CHARACTER_ID_COUNT] __attribute__((section(".rodata.007ED5CC"), aligned(1))) =
{
    gBattleSpritePalettes[8], gBattleSpritePalettes[104], gBattleSpritePalettes[17], gBattleSpritePalettes[26],
    gBattleSpritePalettes[37], gBattleSpritePalettes[46], gBattleSpritePalettes[55], gBattleSpritePalettes[64],
    gBattleSpritePalettes[73], gBattleSpritePalettes[82], gBattleSpritePalettes[91],
};

const u16 *const gBattlePartyStatModifierPalettes[BATTLE_CHARACTER_ID_COUNT][4] __attribute__((section(".rodata.007ED5CC"), aligned(1))) =
{
    {
        gBattleSpritePalettes[9], gBattleSpritePalettes[12], gBattleSpritePalettes[10],
        gBattleSpritePalettes[13],
    },
    {
        gBattleSpritePalettes[105], gBattleSpritePalettes[108], gBattleSpritePalettes[106],
        gBattleSpritePalettes[109],
    },
    {
        gBattleSpritePalettes[18], gBattleSpritePalettes[21], gBattleSpritePalettes[19],
        gBattleSpritePalettes[22],
    },
    {
        gBattleSpritePalettes[27], gBattleSpritePalettes[30], gBattleSpritePalettes[28],
        gBattleSpritePalettes[31],
    },
    {
        gBattleSpritePalettes[38], gBattleSpritePalettes[41], gBattleSpritePalettes[39],
        gBattleSpritePalettes[42],
    },
    {
        gBattleSpritePalettes[47], gBattleSpritePalettes[50], gBattleSpritePalettes[48],
        gBattleSpritePalettes[51],
    },
    {
        gBattleSpritePalettes[56], gBattleSpritePalettes[59], gBattleSpritePalettes[57],
        gBattleSpritePalettes[60],
    },
    {
        gBattleSpritePalettes[65], gBattleSpritePalettes[68], gBattleSpritePalettes[66],
        gBattleSpritePalettes[69],
    },
    {
        gBattleSpritePalettes[74], gBattleSpritePalettes[77], gBattleSpritePalettes[75],
        gBattleSpritePalettes[78],
    },
    {
        gBattleSpritePalettes[83], gBattleSpritePalettes[86], gBattleSpritePalettes[84],
        gBattleSpritePalettes[87],
    },
    {
        gBattleSpritePalettes[92], gBattleSpritePalettes[95], gBattleSpritePalettes[93],
        gBattleSpritePalettes[96],
    },
};

const u16 *const gBattlePartySpritePalettes[BATTLE_CHARACTER_ID_COUNT] __attribute__((section(".rodata.007ED5CC"), aligned(1))) =
{
    gBattleSpritePalettes[2], gBattleSpritePalettes[98], gBattleSpritePalettes[15], gBattleSpritePalettes[24],
    gBattleSpritePalettes[33], gBattleSpritePalettes[44], gBattleSpritePalettes[53], gBattleSpritePalettes[62],
    gBattleSpritePalettes[71], gBattleSpritePalettes[80], gBattleSpritePalettes[89],
};

/* End of split read-only data. */
