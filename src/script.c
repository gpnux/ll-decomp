#include "battle.h"
#include "script.h"
#include "gba.h"
#include "sound.h"
#include "party.h"
#include "menu.h"
#include "include_asm.h"

/* DrawText's fixed address view; gScriptTextTilemap owns the storage. */
#define SCRIPT_TEXT_TILEMAP_ADDR 0x02005800




/* Text handlers use a byte offset; the choices input phase reuses it as an index. */
union ScriptTextPosition
{
    u16 byteOffset;
    u16 choiceIndex;
} __attribute__((packed, aligned(2)));

typedef char ScriptTextPositionCheck[(sizeof(union ScriptTextPosition) == 2
    && __alignof__(union ScriptTextPosition) == 2) ? 1 : -1];

/* Script-owned input .bss; declaration order preserves the IWRAM layout. */
static u8 gScriptBankId;                                     /* 0x03000E68 */
static u8 gScriptPendingEntryId;                             /* 0x03000E69 */
static const u8 *gScriptPc;                                  /* 0x03000E6C */
static u16 gScriptFlags;                                     /* 0x03000E70 */
static u8 gScriptTextPhase;                                  /* 0x03000E72; enum ScriptTextPhase */
static u8 gScriptTextCounter;                                /* 0x03000E73; also inline wait duration */
static u8 gScriptWaitCounter;                                /* 0x03000E74; shared delay counter */
static union ScriptTextPosition gScriptTextPosition;         /* 0x03000E76; phase-dependent view */
static u8 gScriptCallStackDepth;                             /* 0x03000E78 */
static const u8 *gScriptReturnPcs[SCRIPT_CALL_STACK_DEPTH];  /* 0x03000E80 */
static const u8 *gScriptBankReturnPcs[SCRIPT_BANK_STACK_DEPTH]; /* 0x03000EA0 */
static u8 gScriptReturnBankIds[SCRIPT_BANK_STACK_DEPTH];     /* 0x03000EC0 */
static u8 gScriptUnknownEC8;                                 /* 0x03000EC8; incoming r6 saved by CallBank */
static u8 gScriptUnknownEC9;                                 /* 0x03000EC9; incoming r5 saved by CallBank */
static u8 gScriptBankStackDepth;                             /* 0x03000ECA */
static u8 gScriptWindowTileX;                                /* 0x03000ECB */
static u8 gScriptWindowTileY;                                /* 0x03000ECC */
static u8 gScriptTextTileX;                                  /* 0x03000ECD */
static u8 gScriptTextTileY;                                  /* 0x03000ECE; tile rows, advances by 2 */
/* Unrecovered bytes; these reservations do not assign an original meaning. */
static u8 sScriptReservedECF;                                /* 0x03000ECF */
static u16 sScriptReservedED0;                               /* 0x03000ED0 */
static u8 gScriptPortraitSlot;                               /* 0x03000ED2; 0..3 */
static u16 gScriptUnusedInputScratch[8];                     /* 0x03000ED8; initialized to 0xFFFF */
static u16 gScriptCachedGlyphIds[SCRIPT_GLYPH_CACHE_COUNT];  /* 0x03000EE8 */
static u16 gScriptCachedGlyphCount;                          /* 0x03000F24 */
static u16 gScriptLastCachedGlyphIndex;                      /* 0x03000F26; count - 1, or 0xFFFF */
static u16 gScriptDialogueWordCount;                         /* 0x03000F28 */
static u8 gScriptNameGlyphIndex;                             /* 0x03000F2A */
static u8 gScriptNameId;                                     /* 0x03000F2B */
static u16 gScriptKeysHeld;                                  /* 0x03000F2C; inverted KEYINPUT */
static u16 gScriptKeysPressed;                               /* 0x03000F2E; newly held bits */
static u8 gScriptLastReturnedBankId;                         /* 0x03000F30 */

/* Tentative external definition: agbcc emits COMMON instead of .lcomm. */
u16 gScriptChoiceUnknown06;              /* 0x03004F9C; header +6, meaning unknown */

/* Actual shared EWRAM storage; the same tilemap is also used by menu/system. */
u16 gScriptTextTilemap[SCRIPT_TILEMAP_WIDTH * SCRIPT_TILEMAP_HEIGHT]; /* 0x02005800 */
struct ScriptGlyphTiles gScriptGlyphCacheTiles[SCRIPT_GLYPH_CACHE_COUNT]; /* 0x0203DE00 */

typedef char ScriptTextTilemapSizeCheck[(sizeof(gScriptTextTilemap) == 0x800) ? 1 : -1];
typedef char ScriptGlyphCacheTilesSizeCheck[(sizeof(gScriptGlyphCacheTiles) == 0x780) ? 1 : -1];


/* Early returns preserve the shared result comparison after inlining. */
static inline u8 Script_CacheChoiceNameInline(u8 nameId)
{
    Script_CacheGlyphsFromWords(gScriptNames[nameId].glyphs + gScriptNameGlyphIndex,
                              SCRIPT_GLYPH_SCAN_NAME);
    if (gScriptCachedGlyphCount != 0)
    {
        gScriptFlags |= SCRIPT_FLAG_GLYPH_UPLOAD_PENDING;
        return 1;
    }
    return 0;
}

/* Reassemble the byte pair while preserving its halfword stack accesses. */
static inline u16 Script_ReadHeaderPartsInline(const u16 *parts)
{
    return (parts[1] << 8) | parts[0];
}

/* The value-parameter boundary preserves End's inlined stop sequence. */
static inline void Script_StopInline(u8 mode)
{
    switch (mode)
    {
    case SCRIPT_STOP_RESET:
    default:
        gScriptPc = (const u8 *)SCRIPT_BYTECODE;
        break;
    case SCRIPT_STOP_KEEP_CURRENT:
        break;
    }
    gScriptFlags &= ~SCRIPT_FLAG_RUNNING;
}

/* The value parameter preserves the branch handler's entry-address sequence. */
static inline const u8 *Script_ResolveEntryInline(u8 entryId)
{
    return SCRIPT_ENTRY_OFFSETS[entryId] + (const u8 *)SCRIPT_BYTECODE;
}

/**
 * @brief 按子命令更新角色位置、运动、显示或命令流
 *
 * @note 操作码 04；头部长度是后续载荷的字节数；子命令长度不同，处理后返回 1。
 * @address 0x0804F280
 */
u16 ScriptCmd_RunActorCommands(const u8 **pc)
{
    const u8 *payload = *pc;
    u8 payloadSize = payload[1];
    u8 offset;
    struct ActorSpawnConfig config;

    payload += 2;
    for (offset = 0; offset < payloadSize; offset++)
    {
        /* Normalize the opcode before keeping its parameter pointer live. */
        s32 opcode = payload[offset] - 'F';
        const u8 *command = payload + offset;

        switch (opcode)
        {
        case 'F' - 'F':
            gActorStates[command[1]].verticalTarget = command[2];
            gActorStates[command[1]].verticalTimer = command[3];
            gActorStates[command[1]].verticalStep =
                (gActorStates[command[1]].verticalTarget
                 - gActorStates[command[1]].verticalOffset) * 16 / command[3];
            gActorStates[command[1]].verticalFixed =
                gActorStates[command[1]].verticalOffset * 16;
            offset += 4;
            break;
        case 'H' - 'F':
            if (command[1] >= 100)
            {
                if (command[1] >= 200)
                {
                    if (command[2] != 0)
                        gActorStates[command[1] - 200].stateFlags &= ~4;
                    else
                        gActorStates[command[1] - 200].stateFlags |= 4;
                }
                else
                {
                    if (command[2] == 0)
                        gActorStates[command[1] - 100].stateFlags &= ~CORE_ACTOR_STATE_SCENE_RELATIVE;
                    else
                        gActorStates[command[1] - 100].stateFlags |= CORE_ACTOR_STATE_SCENE_RELATIVE;
                }
            }
            else
                gActorStates[command[1]].verticalOffset = command[2];
            offset += 2;
            break;
        case 't' - 'F':
            Actor_SetGraphicsSlots(command[1], command[2], command[3]);
            offset += 3;
            break;
        case 'd' - 'F':
            Actor_FreeSprites(command[1]);
            offset++;
            break;
        case 'h' - 'F':
            Actor_BuildMoveCommands(command[1], command[2], command[3]);
            Actor_StartCommands(command[1]);
            offset += 3;
            break;
        case 'N' - 'F':
            Player_ResetFollowing();
            break;
        case 'n' - 'F':
            Player_FreezePositionHistory();
            break;
        case 'S' - 'F':
            config.flags = command[2];
            config.graphicsSlot = command[3];
            config.paletteSlot = command[4];
            config.direction = command[5];
            config.tileX = command[6];
            config.tileY = command[7];
            config.minTileX = command[8];
            config.minTileY = command[9];
            config.maxTileX = command[10];
            config.maxTileY = command[11];
            config.moveSpeed = command[12];
            config.scriptEntryId = command[13];
            config.commandData = NULL;
            Actor_Init(command[1], &config);
            offset += 13;
            break;
        case 'L' - 'F':
            if (command[1] >= 100)
            {
                if (command[1] >= 200)
                {
                    if (command[2] != 0)
                        gActorStates[command[1] - 200].flags &= ~CORE_ACTOR_FLAG_NO_SHADOW;
                    else
                        gActorStates[command[1] - 200].flags |= CORE_ACTOR_FLAG_NO_SHADOW;
                }
                else if (command[1] >= 120)
                    Actor_OffsetPosition(command[1] - 120, command[2], command[3], command[4]);
                else
                    Actor_SetAxisPosition(command[1] - 100, command[2], command[3], command[4]);
            }
            else
            {
                Actor_SetPosition(command[1], command[2], command[3], command[4]);
                if (command[1] == 0
                    && (!(gSceneType & CORE_SCENE_TYPE_FLAG_CUSTOM_ACTORS)
                        || gSceneType > CORE_SCENE_TYPE_SCRIPT_BACKGROUND))
                {
                    gPlayerDirection = gActorStates[0].direction;
                    gPlayerX = gActorStates[0].x;
                    gPlayerY = gActorStates[0].y - CORE_ACTOR_GRID_PIXELS;
                    Player_ResetPositionHistory();
                }
            }
            offset += 4;
            break;
        case 'M' - 'F':
        {
            u8 actorId = command[1];
            const u8 *commands;
            u8 length;

            if (actorId >= 200)
            {
                actorId -= 200;
                gActorStates[actorId].stateFlags |= 4;
            }
            commands = payload + (offset + 2);
            Actor_SetCommandData(actorId, commands);
            Actor_StartCommands(actorId);
            length = 2;
            if (*commands >= CORE_ACTOR_CMD_STOP)
                length = 3;
            else
            {
                do
                {
                    if (payload[offset + length] == CORE_ACTOR_CMD_MOVE
                        || payload[offset + length] == CORE_ACTOR_CMD_MOVE_KEEP_FACING)
                        length += 4;
                    else if (payload[offset + length] == CORE_ACTOR_CMD_WAIT)
                        length += 3;
                } while (payload[offset + length] < CORE_ACTOR_CMD_STOP);
                length++;
            }
            offset += length;
            break;
        }
        }
    }
    *pc += payloadSize + 2;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 记录镜头或背景的平移起点、目标和时长
 *
 * @note 操作码 1A；起点取决于背景滚动模式；时长只写低字节，目标为两个小端半字；PC += 6，返回 1。
 * @address 0x0804F64C
 */
u16 ScriptCmd_PanCamera(const u8 **pc)
{
    const u8 *command = *pc;

    /* This command changes only the low byte of each shared halfword. */
    *(u8 *)&gBackgroundMoveDuration = command[1];
    *(u8 *)&gBackgroundMoveFrame = 0;

    switch (gBackgroundScrollMode)
    {
    case 2:
        gBackgroundMoveStartX = gBackgroundXState;
        gBackgroundMoveStartY = 0;
        break;
    case 5:
        if (gBackgroundMotionState == 0)
            gBackgroundMoveStartX = gCameraX;
        else
            gBackgroundMoveStartX = gBackgroundXState;
        gBackgroundMoveStartY = gCameraY;
        break;
    case 8:
        if (gBackgroundMotionState == 0)
            gBackgroundMoveStartY = gCameraY;
        else
            gBackgroundMoveStartY = gBackgroundYState;
        gBackgroundMoveStartX = gCameraX;
        break;
    default:
        gBackgroundMoveStartX = gCameraX;
        gBackgroundMoveStartY = gCameraY;
        break;
    }

    gBackgroundMoveTargetX = command[2] + (command[3] << 8);
    gBackgroundMoveTargetY = command[4] + (command[5] << 8);
    *pc += 6;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 从队伍及角色站位表移除成员
 *
 * @note 操作码 1C；队伍表向前压缩并补 FF，站位表只清对应槽；PC += 2，返回 1。
 * @address 0x0804F768
 */
u16 ScriptCmd_RemovePartyMember(const u8 **pc)
{
    const u8 *command = *pc;
    u16 index;
    u8 member;

    for (index = 0; index < PARTY_MAX_MEMBERS; index++)
    {
        if (gPartyMembers[index] == command[1])
        {
            gPartyMembers[index] = PARTY_NO_MEMBER;
            for (; index < PARTY_MAX_MEMBERS; index++)
            {
                member = gPartyMembers[index];
                gPartyMembers[index] = gPartyMembers[index + 1];
                gPartyMembers[index + 1] = member;
            }
            break;
        }
    }

    for (index = 0; index < PARTY_MAX_MEMBERS; index++)
    {
        if (gPartyCharacterSlots[index] == command[1])
        {
            gPartyCharacterSlots[index] = PARTY_NO_MEMBER;
            break;
        }
    }

    *pc += 2;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 按编号顺序把成员插入队伍并恢复其站位槽
 *
 * @note 操作码 1D；保留队伍末尾 FF 哨兵，并按角色记录的站位或空槽安排位置；PC += 2，返回 1。
 * @address 0x0804F7F8
 */
u16 ScriptCmd_AddPartyMember(const u8 **pc)
{
    const u8 *command = *pc;
    u8 index;
    u8 member;
    u8 characterId;
    u8 characterIndex;

    for (index = 0; index < PARTY_MAX_MEMBERS
        && gPartyMembers[index] != command[1]; index++)
    {
        if (gPartyMembers[index] > command[1])
        {
            member = command[1];
            for (; index < PARTY_MAX_MEMBERS; index++)
            {
                u8 previous = gPartyMembers[index];

                gPartyMembers[index] = member;
                member = previous;
            }
            break;
        }
    }

    Party_RefreshFirstCharacterSkills(command[1]);
    gPartyMembers[PARTY_MAX_MEMBERS] = PARTY_NO_MEMBER;
    characterId = command[1];
    characterIndex = characterId;
    if (characterId != 0)
        characterIndex = characterId - 1;

    if (gPartyCharacterSlots[gPartyCharacters[characterIndex].characterSlot] == PARTY_NO_MEMBER)
    {
        gPartyCharacterSlots[gPartyCharacters[characterIndex].characterSlot] = characterId;
    }
    else
    {
        u8 slotMember;

        index = 0;
        slotMember = characterId;
        for (; index < PARTY_MAX_MEMBERS; index++)
        {
            if (gPartyCharacterSlots[index] == characterId)
            {
                gPartyCharacters[characterIndex].characterSlot = index;
                *pc += 2;
                return SCRIPT_RESULT_CONTINUE;
            }
        }
        for (index = 0; index < PARTY_MAX_MEMBERS; index++)
        {
            if (gPartyCharacterSlots[index] == PARTY_NO_MEMBER)
            {
                gPartyCharacterSlots[index] = slotMember;
                gPartyCharacters[characterIndex].characterSlot = index;
                break;
            }
        }
    }

    *pc += 2;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 请求脚本战斗，等待返回后选择后续入口
 *
 * @note 操作码 22；请求期间保持 PC；返回状态 3 时依据结果 bit6 和入口参数选择分支，不据此命名胜负含义。
 * @address 0x0804F8D8
 */
u16 ScriptCmd_StartBattle(const u8 **pc)
{
    const u8 *command = *pc;

    switch (gScriptedBattleState)
    {
    case 3:
        if ((Battle_GetControlFlags() & 0x40) || command[1] == 0)
            *pc += 4;
        else
            *pc = SCRIPT_ENTRY_OFFSETS[command[1]] + (const u8 *)SCRIPT_BYTECODE;
        gScriptedBattleState = 0;
        return SCRIPT_RESULT_CONTINUE;
    case 0:
        gScriptedBattleState = 1;
        gBattleFieldReturnMode = command[3];
        if ((s8)command[2] < 0)
            gScriptedEncounterId = command[2] + 0xBA;
        else
            gScriptedEncounterId = command[2] + 0x1C;
        gSceneState = 5;
        break;
    }
    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 列表中的标志全部置位时跳转
 *
 * @note 操作码 45；剧情和系统编号可混用；空列表路径依赖传入 r1，不能定义为空集恒真；返回 1。
 * @address 0x0804F974
 */
u16 ScriptCmd_JumpIfAllFlagsSet(const u8 **pc)
{
    const u8 *command = *pc;
    u8 payloadBytes = command[1];
    u16 index;
    u16 flagId;
    /* Original zero-pair path leaves r1 uninitialized; do not default to zero. */
    u8 mask;

    for (index = 0; payloadBytes / 2 > index; index++)
    {
        flagId = command[index * 2 + 3] | (command[index * 2 + 4] << 8);
        if (flagId >= 512)
            mask = SystemFlags_GetMask(flagId - 512);
        else
            mask = EventFlags_GetMask(flagId);
        if (mask == 0)
            break;
    }

    if (mask != 0)
        *pc = Script_ResolveEntryInline(command[2]);
    else
        *pc += payloadBytes + 3;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 列表中的标志全部清零时跳转
 *
 * @note 操作码 46；剧情和系统编号可混用；空列表的 r1 未初始化，保留原行为；返回 1。
 * @address 0x0804FA04
 */
u16 ScriptCmd_JumpIfAllFlagsClear(const u8 **pc)
{
    const u8 *command = *pc;
    u8 payloadBytes = command[1];
    u16 index;
    u16 flagId;
    /* Original zero-pair path leaves r1 uninitialized; do not default to zero. */
    u8 mask;

    for (index = 0; payloadBytes / 2 > index; index++)
    {
        flagId = command[index * 2 + 3] | (command[index * 2 + 4] << 8);
        if (flagId >= 512)
            mask = SystemFlags_GetMask(flagId - 512);
        else
            mask = EventFlags_GetMask(flagId);
        if (mask != 0)
            break;
    }

    if (mask == 0)
        *pc = Script_ResolveEntryInline(command[2]);
    else
        *pc += payloadBytes + 3;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 列表中任意一个标志置位时跳转
 *
 * @note 操作码 47；未满足条件时跳过三字节头及完整载荷；空列表同样依赖传入 r1，返回 1。
 * @address 0x0804FA94
 */
u16 ScriptCmd_JumpIfAnyFlagSet(const u8 **pc)
{
    const u8 *command = *pc;
    u8 payloadBytes = command[1];
    u16 index;
    u16 flagId;
    /* Original zero-pair path leaves r1 uninitialized; do not default to zero. */
    u8 mask;

    for (index = 0; payloadBytes / 2 > index; index++)
    {
        flagId = command[index * 2 + 3] | (command[index * 2 + 4] << 8);
        if (flagId >= 512)
            mask = SystemFlags_GetMask(flagId - 512);
        else
            mask = EventFlags_GetMask(flagId);
        if (mask != 0)
            break;
    }

    if (mask != 0)
        *pc = Script_ResolveEntryInline(command[2]);
    else
        *pc += payloadBytes + 3;
    return SCRIPT_RESULT_CONTINUE;
}

#if 0
/*
 * Candidate only: full-module integration changes ShowDialogue's code.
 * Wider helper interfaces are also required; see docs/SCRIPT-MATCHING.md.
 */
#include "menu.h"

/* Palette clearing reuses the existing animation graphics workspace. */
#define SCRIPT_EFFECT_PALETTE_SCRATCH ((u16 *)0x02020000)





/**
 * @brief 按子命令控制场景效果及相关场景动作
 *
 * @note 操作码 4D；包括背景、调色板、混合和奖励存档路径；部分路径多次更新并保持 PC，不能视为单一开关。
 * @address 0x0804FB24
 */
u16 ScriptCmd_ControlSceneEffect(const u8 **pc)
{
    const u8 *command = *pc;
    u16 value;

    if (command[2] != 0)
    {
        switch (command[1])
        {
        case 0:
            switch (command[2])
            {
            default:
                gSceneEffects.backgroundMask = 7;
                break;
            case 1:
                gSceneEffects.backgroundMask = 1;
                break;
            case 2:
                gSceneEffects.backgroundMask = 3;
                break;
            }
            gSceneEffects.flags |= 3;
            break;
        case 1:
            gSceneEffects.flags |= 4;
            break;
        case 2:
            if (gSceneId == 99)
                (*(vu16 *)REG_DISPCNT) |= 0x400;
            else
                (*(vu16 *)REG_DISPCNT) |= 0x800;
            break;
        case 3:
            gBackgroundMotionState = 1;
            break;
        case 4:
            switch (command[2])
            {
            case 1:
            {
                gSceneEffects.frame++;
                value = gSceneEffects.frame >> 3;
                gBlendCoefficients = (value << 9) + 15 - value;
                if (value > 14)
                    gBlendCoefficients = 0x1F00;
                else
                    return SCRIPT_RESULT_YIELD;
                break;
            }
            case 2:
                (*(vu16 *)REG_DISPCNT) &= ~0x200;
                gBlendControl = 0xC10;
                gBlendCoefficients = 0xF0F;
                DmaCopy(3, D_0808B1B4, PALETTE_RAM[4], sizeof(D_0808B1B4), 16);
                break;
            case 3:
            {
                u16 *colors = SCRIPT_EFFECT_PALETTE_SCRATCH;
                for (value = 0; value < 96; value++)
                    *colors++ = 0;
                DmaCopy(3, SCRIPT_EFFECT_PALETTE_SCRATCH, PALETTE_RAM[1],
                          (u32)(3 * sizeof(PALETTE_RAM[0])), 16);
                break;
            }
            case 4:
            {
                gSceneEffects.frame++;
                value = gSceneEffects.frame >> 2;
                if (gSceneEffects.phase == 0)
                {
                    gBlendCoefficients = (value & 15) | 0x1F00;
                    if (value == 15)
                        gSceneEffects.phase++;
                    return SCRIPT_RESULT_YIELD;
                }
                gSceneBackgroundEffectFrame = 0;
                break;
            }
            case 5:
                gSceneEffects.pulseFrame++;
                if (gSceneEffects.pulseFrame > 62)
                    gBlendCoefficients = 0;
                else
                {
                    if (gSceneEffects.pulseFrame > 31)
                    {
                        gBlendCoefficients = 63 - gSceneEffects.pulseFrame;
                        return SCRIPT_RESULT_YIELD;
                    }
                    else
                    {
                        gBlendCoefficients = gSceneEffects.pulseFrame;
                        return SCRIPT_RESULT_YIELD;
                    }
                }
                break;
            }
            break;
        case 5:
            if (Save_UpdateIo(1) != 0)
                return SCRIPT_RESULT_YIELD;
            break;
        case 7:
            if (gSceneEffects.frame <= 9)
            {
                DmaCopy(3, gScenePalettes[6] + gSceneEffects.frame * 16 + 1,
                          &PALETTE_RAM[gSceneEffects.frame][1], (u32)(15 * sizeof(u16)), 16);
            }
            else
            {
                DmaCopy(3, gSceneOverlayPalette1, PALETTE_RAM[10],
                          sizeof(gSceneOverlayPalette1), 16);
            }
            gSceneEffects.frame++;
            if (gSceneEffects.frame <= 10)
                return SCRIPT_RESULT_YIELD;
            break;
        case 200:
            Scene_LoadStaticBackground(command[2] + 129);
            break;
        case 201:
            Scene_LoadScriptBackground(command[2]);
            break;
        case 202:
            gSceneEffects.blendFrame++;
            if (command[2] == 1)
            {
                if (gSceneEffects.blendFrame > 14)
                {
                    gBlendCoefficients = 0xF1F;
                    gSceneEffects.blendFrame = 0;
                }
                else
                {
                    gBlendCoefficients = (gSceneEffects.blendFrame << 8) + 31;
                    gBlendControl = 0x1E41;
                    return SCRIPT_RESULT_YIELD;
                }
            }
            else
            {
                if (gSceneEffects.blendFrame > 14)
                {
                    gBlendCoefficients = 31;
                    gBlendControl = 0x1E01;
                    gSceneEffects.blendFrame = 0;
                }
                else
                {
                    gBlendCoefficients = ((15 - gSceneEffects.blendFrame) << 8) + 31;
                    return SCRIPT_RESULT_YIELD;
                }
            }
            break;
        case 77:
            Party_RestoreCharacterHpMp(command[2]);
            break;
        }
    }
    else
    {
        switch (command[1])
        {
        case 0:
            gSceneEffects.flags &= ~3;
            break;
        case 1:
            gSceneEffects.flags &= ~4;
            (*(vu16 *)REG_DISPCNT) &= ~0x200;
            break;
        case 2:
            if (gSceneId == 99)
                (*(vu16 *)REG_DISPCNT) &= ~0x400;
            else
                (*(vu16 *)REG_DISPCNT) &= ~0x800;
            break;
        case 3:
            gBackgroundMotionState = 0;
            break;
        case 4:
        case 7:
            gSceneEffects.frame = 0;
            gSceneEffects.phase = 0;
            break;
        case 5:
            gMenuExtraFlags |= 1;
            Save_RequestBonusWrite();
            break;
        case 6:
            if (gKeysPressed & 1)
            {
                Script_Stop(SCRIPT_STOP_RESET);
                System_RequestTitle();
            }
            /* This original path leaves the return register unspecified. */
            return;
        case 8:
            switch (gSceneEffects.paletteSequencePhase)
            {
            case 0:
                Scene_DrawEffectTileBlock(0);
                Palette_LoadAnimationGroupAt(3, 23);
                Palette_ResumeAnimation(3);
                gSceneEffects.paletteSequencePhase++;
                return SCRIPT_RESULT_YIELD;
            case 1:
                if (Palette_GetAnimationFlags(3) != 0)
                    return SCRIPT_RESULT_YIELD;
                gSceneEffects.paletteSequencePhase++;
                return SCRIPT_RESULT_YIELD;
            case 2:
                Scene_DrawEffectTileBlock(1);
                Palette_LoadAnimationGroupAt(3, 24);
                Palette_ResumeAnimation(3);
                gSceneEffects.paletteSequencePhase++;
                return SCRIPT_RESULT_YIELD;
            case 3:
            {
                u8 flags = Palette_GetAnimationFlags(3);

                if (flags != 0)
                    return SCRIPT_RESULT_YIELD;
                gSceneEffects.paletteSequencePhase = flags;
                break;
            }
            }
            break;
        case 77:
            Party_RestoreCharacterHpMp(command[2]);
            break;
        case 100:
            Sound_ResumeBgm();
            break;
        case 200:
            (*(vu16 *)REG_DISPCNT) |= 0x100;
            break;
        case 201:
            Scene_LoadScriptBackground(0);
            break;
        case 202:
            gSceneEffects.blendFrame = 0;
            break;
        }
    }
    *pc += 3;
    return SCRIPT_RESULT_CONTINUE;
}
#else
INCLUDE_ASM("asm/nonmatchings/script", ScriptCmd_ControlSceneEffect);
#endif

/**
 * @brief 采样按键并派发当前脚本命令
 *
 * @note 仅在运行且无待加载 bank 时执行；只有命令结果的低半字等于 1 才继续本轮派发。
 * @address 0x08050014
 */
void Script_Update(void)
{
    u16 keys;
    u16 result;

    if (!(gScriptFlags & SCRIPT_FLAG_RUNNING))
        return;
    if (gScriptFlags & SCRIPT_FLAG_BANK_LOAD_PENDING)
        return;

    keys = ~(*(vu16 *)REG_KEYINPUT);
    gScriptKeysPressed = keys & ~gScriptKeysHeld;
    gScriptKeysHeld = keys;
    Battle_UpdateInputRepeat(gScriptKeysHeld, gScriptUnusedInputScratch);

    do
    {
        result = gScriptCommandTable[*gScriptPc](&gScriptPc);
    } while (result == SCRIPT_RESULT_CONTINUE);
}

/**
 * @brief 上传脚本文本和字形，并推进分块脚本解压
 *
 * @note 字形与解压服务不要求 RUNNING；异步解压结束时重选待执行入口，保留 DMA 控制读回。
 * @address 0x0805008C
 */
void Script_UpdateVBlank(void)
{
    u16 offset;
    u16 *entryOffsets;

    if (gScriptFlags & SCRIPT_FLAG_RUNNING)
    {
        if (!(gScriptFlags & SCRIPT_FLAG_BANK_LOAD_PENDING))
        {
            if (*gScriptPc == SCRIPT_OP_SHOW_DIALOGUE || *gScriptPc == SCRIPT_OP_DRAW_TEXT)
            {
                offset = gScriptFlags & SCRIPT_FLAG_TEXT_LAYER_PERSISTENT;
                if (offset != 0)
                    goto persistent_upload;

                (*(vu16 *)REG_BG0HOFS) = offset;
                (*(vu16 *)REG_BG0VOFS) = offset;
                DmaCopy(3, gScriptTextTilemap, BG_SCREEN_ADDR(31), sizeof(gScriptTextTilemap), 16);
            }

            if (gScriptFlags & SCRIPT_FLAG_TEXT_LAYER_PERSISTENT)
            {
persistent_upload:
                (*(vu16 *)REG_BG0HOFS) = 0;
                (*(vu16 *)REG_BG0VOFS) = 0;
                DmaCopy(3, gScriptTextTilemap, BG_SCREEN_ADDR(31), sizeof(gScriptTextTilemap), 16);
            }
        }
    }

    if (gScriptFlags & SCRIPT_FLAG_GLYPH_UPLOAD_PENDING)
    {
        if (Script_UploadGlyphCache() < 0)
            gScriptFlags &= ~SCRIPT_FLAG_GLYPH_UPLOAD_PENDING;
    }

    if (gScriptFlags & SCRIPT_FLAG_DEFAULT_GLYPHS_PENDING)
    {
        Script_RestoreDefaultGlyphs(0);
        gScriptFlags &= ~SCRIPT_FLAG_DEFAULT_GLYPHS_PENDING;
    }

    if (gScriptFlags & SCRIPT_FLAG_BANK_LOAD_PENDING)
    {
        if (LzStream_DecodeChunk() == 0)
        {
            if (gScriptFlags & SCRIPT_FLAG_ENTRY_RESELECT_PENDING)
            {
                entryOffsets = SCRIPT_ENTRY_OFFSETS;
                gScriptPc = entryOffsets[gScriptPendingEntryId] + (const u8 *)SCRIPT_BYTECODE;
                gScriptFlags &= ~SCRIPT_FLAG_ENTRY_RESELECT_PENDING;
            }
            gScriptFlags &= ~SCRIPT_FLAG_BANK_LOAD_PENDING;
        }
    }
}

/**
 * @brief 扫描字节编码文本并将扩展字形重排到缓存
 *
 * @note 支持 8/16 位扫描步长及追加模式；缓存连续存放每字形的上下 tile，并记录 count - 1。
 * @address 0x080501B8
 */
void Script_CacheGlyphsFromBytes(const u8 *text, u16 scanFlags, u8 bitsPerUnit)
{
    u8 active = 1;
    u8 limit = 0;
    u8 scanned = 0;
    u16 position;
    u16 i;
    u16 row;
    u16 column;
    u16 glyph;
    u8 *tiles;

    if (!((s16)scanFlags & SCRIPT_GLYPH_SCAN_CONTROL_MASK))
        limit = scanFlags;

    position = 0;
    if (!(scanFlags & SCRIPT_GLYPH_SCAN_KEEP_CACHE))
        gScriptCachedGlyphCount = 0;

    while (1)
    {
        switch (text[position + 1] << 8)
        {
        case 0x0100:
        case 0x0200:
        case 0x0300:
        case 0x0400:
        case 0x0500:
        case 0x0600:
        case 0x0700:
        case 0x0900:
        case 0x0A00:
        case 0x0B00:
        case 0x0C00:
        case 0x0D00:
        case 0x0E00:
            break;
        case SCRIPT_TEXT_END:
            active = 0;
            break;
        case SCRIPT_TEXT_PAGE_BREAK:
            if (scanFlags == (text[position + 1] << 8))
                active = 0;
            break;
        default:
            if ((text[position + 1] & 0x10) || (scanFlags & SCRIPT_GLYPH_SCAN_INCLUDE_UNMARKED))
            {
                glyph = ((text[position + 1] << 8) & ~SCRIPT_TEXT_EXTENDED_GLYPH_FLAG) | text[position];
                if (glyph > 0xDF)
                {
                    for (i = 0; i < gScriptCachedGlyphCount; i++)
                    {
                        if (glyph == gScriptCachedGlyphIds[i])
                            break;
                    }
                    if (i >= gScriptCachedGlyphCount)
                    {
                        gScriptCachedGlyphIds[gScriptCachedGlyphCount] = glyph;
                        gScriptCachedGlyphCount++;
                    }
                }
            }
            break;
        }

        if (bitsPerUnit == 8)
            position++;
        else if (bitsPerUnit == 16)
            position += 2;

        if (limit != 0)
        {
            scanned++;
            if (scanned >= limit)
                break;
        }
        if (!active)
            break;
    }

    tiles = (u8 *)gScriptGlyphCacheTiles;
    position = 0;
    while (position < gScriptCachedGlyphCount)
    {
        column = (gScriptCachedGlyphIds[position] - SCRIPT_EXTENDED_GLYPH_BASE) % 32;
        row = (gScriptCachedGlyphIds[position] - SCRIPT_EXTENDED_GLYPH_BASE) / 32;
        DmaCopy(3, (const u8 *)gScriptFontResources.extendedRows + (row * 64 + column) * 32,
                           tiles, (u32)(SCRIPT_GLYPH_TILE_BYTES), 16);
        DmaWait(3);
        tiles += SCRIPT_GLYPH_TILE_BYTES;
        DmaCopy(3, (const u8 *)gScriptFontResources.extendedRows + ((row * 2 + 1) * 32 + column) * 32,
                           tiles, (u32)(SCRIPT_GLYPH_TILE_BYTES), 16);
        DmaWait(3);
        tiles += SCRIPT_GLYPH_TILE_BYTES;
        position++;
    }
    gScriptLastCachedGlyphIndex = gScriptCachedGlyphCount - 1;
}

/**
 * @brief 扫描半字字形序列并准备扩展字形缓存
 *
 * @note 追加模式保留旧编号；按原高字节 switch 和扫描后容量检查执行，不更新最后字形下标。
 * @address 0x08050434
 */
void Script_CacheGlyphsFromWords(const u16 *text, u16 scanFlags)
{
    u8 active = 1;
    u8 limit = 0;
    u8 scanned = 0;
    u16 position;
    u16 i;
    u16 row;
    u16 column;
    u16 glyph;
    u8 *tiles;

    if (!((s16)scanFlags & SCRIPT_GLYPH_SCAN_CONTROL_MASK))
        limit = (scanFlags & (SCRIPT_GLYPH_SCAN_KEEP_CACHE | SCRIPT_GLYPH_SCAN_INCLUDE_UNMARKED)) ? (u8)scanFlags : 0;

    position = 0;
    if (!(scanFlags & SCRIPT_GLYPH_SCAN_KEEP_CACHE))
        gScriptCachedGlyphCount = 0;

    while (active)
    {
        switch (text[position] & 0xFF00)
        {
        /* Original cases retained with the high-byte switch mask. */
        case 0x06E0:
        case 0x06E1:
        case 0x06E3:
        case 0x06E4:
        case 0x06E5:
        case 0x06E6:
            break;
        case SCRIPT_TEXT_END:
            active = 0;
            break;
        case SCRIPT_TEXT_PAGE_BREAK:
            if (scanFlags == (text[position] & 0xFF00))
                active = 0;
            break;
        default:
            glyph = text[position];
            if (glyph > 0xDF)
            {
                for (i = 0; i < gScriptCachedGlyphCount; i++)
                {
                    if (glyph == gScriptCachedGlyphIds[i])
                        break;
                }
                if (i >= gScriptCachedGlyphCount)
                {
                    gScriptCachedGlyphIds[gScriptCachedGlyphCount] = glyph;
                    gScriptCachedGlyphCount++;
                }
            }
            break;
        }

        position++;

        if (limit != 0)
        {
            scanned++;
            if (scanned >= limit)
                break;
        }
        if (gScriptCachedGlyphCount > SCRIPT_GLYPH_CACHE_COUNT - 1)
            break;
    }

    tiles = (u8 *)gScriptGlyphCacheTiles;
    position = 0;
    while (position < gScriptCachedGlyphCount)
    {
        column = (gScriptCachedGlyphIds[position] - SCRIPT_EXTENDED_GLYPH_BASE) % 32;
        row = (gScriptCachedGlyphIds[position] - SCRIPT_EXTENDED_GLYPH_BASE) / 32;
        DmaCopy(3, (const u8 *)gScriptFontResources.extendedRows + (row * 64 + column) * 32,
                           tiles, (u32)(SCRIPT_GLYPH_TILE_BYTES), 16);
        DmaWait(3);
        tiles += SCRIPT_GLYPH_TILE_BYTES;
        DmaCopy(3, (const u8 *)gScriptFontResources.extendedRows + ((row * 2 + 1) * 32 + column) * 32,
                           tiles, (u32)(SCRIPT_GLYPH_TILE_BYTES), 16);
        DmaWait(3);
        tiles += SCRIPT_GLYPH_TILE_BYTES;
        position++;
    }
}

/**
 * @brief 在文本图块表写入名字的一个字形
 *
 * @note 字形小于 0xF00 时以调色板 11 绘制并推进名字下标，返回 0；否则返回 1。
 *       基础字形直接定位，扩展字形查缓存；未命中时沿用缓存数量对应的图块下标。
 * @address 0x0805063C
 */
u8 Script_DrawNameCharacter(u16 *tilemap, u8 nameId, u8 x, u8 y)
{
    u16 *tiles = tilemap + (y * SCRIPT_TILEMAP_WIDTH + x);
    u8 ended = 0;
    u16 index;
    u8 palette = 11;

    if (gScriptNames[nameId].glyphs[gScriptNameGlyphIndex] < 0xF00)
    {
        if (gScriptNames[nameId].glyphs[gScriptNameGlyphIndex] < SCRIPT_EXTENDED_GLYPH_BASE)
        {
            tiles[0] = (palette << 12) + (gScriptNames[nameId].glyphs[gScriptNameGlyphIndex] & 0xFF) * 2;
            tiles[SCRIPT_TILEMAP_WIDTH] = (palette << 12)
                + ((gScriptNames[nameId].glyphs[gScriptNameGlyphIndex] & 0xFF) * 2 + 1);
        }
        else
        {
            for (index = 0; index < gScriptCachedGlyphCount; index++)
            {
                if (gScriptNames[nameId].glyphs[gScriptNameGlyphIndex] == gScriptCachedGlyphIds[index])
                    break;
            }
            index += SCRIPT_EXTENDED_GLYPH_BASE;
            tiles[0] = (palette << 12) + index * 2;
            tiles[SCRIPT_TILEMAP_WIDTH] = (palette << 12) + (index * 2 + 1);
        }
        gScriptNameGlyphIndex++;
    }
    else
        ended = 1;
    return ended;
}

/**
 * @brief 控制对话窗口、逐字绘制、翻页、头像和关闭流程
 *
 * @note 操作码 00；共用文本阶段与字节游标；完成时按头部字数推进 PC，当前轮返回 0。
 * @address 0x08050720
 */
u16 ScriptCmd_ShowDialogue(const u8 **pc)
{
    u16 headerBytes[2];
    u16 fill;
    u16 glyph;
    u16 index;
    u8 cacheIndex;
    u16 speedKey;
    s32 keepDrawing = 1;
    s32 fastText;
    u32 windowStyle;
    u16 *tilemap = gScriptTextTilemap;
    const u8 *text;
    BgCnt bgControl;

    headerBytes[0] = (*pc)[2];
    headerBytes[1] = (*pc)[3];
    gScriptDialogueWordCount = (headerBytes[1] << 8) | headerBytes[0];
    headerBytes[0] = (*pc)[4];
    headerBytes[1] = (*pc)[5];
    windowStyle = Script_ReadHeaderPartsInline(headerBytes);
    text = *pc + sizeof(struct ScriptDialogueHeader);
    Battle_UpdateWindows();
    speedKey = Battle_GetInput() & 0x20;
    fastText = speedKey != 0;

    switch (gScriptTextPhase)
    {
    case SCRIPT_TEXT_PHASE_INIT:
        gScriptTextPosition.byteOffset = 0;
        for (cacheIndex = 0; cacheIndex < SCRIPT_GLYPH_CACHE_COUNT; cacheIndex++)
            gScriptCachedGlyphIds[cacheIndex] = 0;
        gScriptCachedGlyphCount = 0;
        switch (windowStyle)
        {
        case 0:
            gScriptWindowTileX = 8;
            gScriptWindowTileY = 12;
            gScriptPortraitSlot = 3;
            break;
        case 1:
            gScriptWindowTileX = 8;
            gScriptWindowTileY = 1;
            gScriptPortraitSlot = 1;
            break;
        case 2:
            gScriptWindowTileX = 0;
            gScriptWindowTileY = 1;
            gScriptPortraitSlot = 0;
            break;
        case 3:
            gScriptWindowTileX = 0;
            gScriptWindowTileY = 12;
            gScriptPortraitSlot = 2;
            break;
        case 4:
            gScriptWindowTileX = 4;
            gScriptWindowTileY = 6;
            gScriptPortraitSlot = 0;
            break;
        }
        gScriptTextTileX = 0;
        gScriptTextTileY = 0;
        {
            u16 *source = &fill;
            *source = 0;
            DmaSet(3, source, tilemap,
                (u32)(DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS)
                    | (sizeof(gScriptTextTilemap) / 2));
        }
        DmaWait(3);
        gScriptTextPhase = SCRIPT_TEXT_PHASE_CACHE_PAGE;
        break;

    case SCRIPT_TEXT_PHASE_CACHE_PAGE:
        Script_CacheGlyphsFromBytes(text, SCRIPT_TEXT_PAGE_BREAK, 16);
        gScriptFlags |= SCRIPT_FLAG_GLYPH_UPLOAD_PENDING;
        gScriptTextPhase = SCRIPT_TEXT_PHASE_ENABLE_LAYER;
        break;

    case SCRIPT_TEXT_PHASE_ENABLE_LAYER:
        tilemap = BG_SCREEN_ADDR(31);
        {
            u16 *source = &fill;
            *source = 0;
            DmaSet(3, source, tilemap,
                (u32)(DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS)
                    | (sizeof(gScriptTextTilemap) / 2));
        }
        DmaWait(3);
        (*(vu16 *)REG_DISPCNT) |= 0x100;
        bgControl.Priority = 0;
        bgControl.CharBasep = 2;
        bgControl.Dummy_5_4 = 0;
        bgControl.Mosaic = 0;
        bgControl.ColorMode = 0;
        bgControl.ScBasep = 31;
        bgControl.Loop = 0;
        bgControl.Size = 0;
        (*(vu16 *)REG_BG0CNT) = *(u16 *)&bgControl;
        gScriptTextPhase = SCRIPT_TEXT_PHASE_OPEN_WINDOW;
        break;

    case SCRIPT_TEXT_PHASE_OPEN_WINDOW:
        Battle_ResetWindows();
        Battle_OpenWindow(0, gScriptTextTilemap, 11, 5, 2,
                     gScriptWindowTileX, gScriptWindowTileY, 22, 8);
        if (gScriptFlags & SCRIPT_FLAG_PORTRAIT_VISIBLE)
            gScriptFlags &= ~SCRIPT_FLAG_PORTRAIT_VISIBLE;
        gScriptTextPhase = SCRIPT_TEXT_PHASE_WAIT_WINDOW;
        break;

    case SCRIPT_TEXT_PHASE_WAIT_WINDOW:
        if ((u8)Battle_GetWindowState(0) == 4)
            gScriptTextPhase = SCRIPT_TEXT_PHASE_DRAW;
        break;

    case SCRIPT_TEXT_PHASE_DRAW:
        if (gScriptFlags & SCRIPT_FLAG_GLYPH_UPLOAD_PENDING)
            break;
        while (keepDrawing)
        {
            glyph = text[gScriptTextPosition.byteOffset + 1] << 8;
            switch (glyph)
            {
            case 0x0700:
                keepDrawing = 0;
                gScriptTextPhase = SCRIPT_TEXT_PHASE_WAIT_INPUT;
                break;
            case 0x0900:
                gScriptTextTileX = 0;
                gScriptTextTileY += 2;
                break;
            case SCRIPT_TEXT_END:
                keepDrawing = 0;
                gScriptTextPhase = SCRIPT_TEXT_PHASE_CLOSE_PORTRAIT;
                break;
            case SCRIPT_TEXT_PAGE_BREAK:
                gScriptTextTileX = 0;
                gScriptTextTileY = 0;
                Battle_DrawWindowBorder(gScriptTextTilemap, 11,
                             gScriptWindowTileX, gScriptWindowTileY, 22, 8);
                Script_CacheGlyphsFromBytes(text + (gScriptTextPosition.byteOffset + 2),
                                           SCRIPT_TEXT_PAGE_BREAK, 16);
                gScriptFlags |= SCRIPT_FLAG_GLYPH_UPLOAD_PENDING;
                break;
            case 0x0A00:
                gScriptWaitCounter = 0;
                gScriptTextCounter = text[gScriptTextPosition.byteOffset];
                gScriptTextPhase = SCRIPT_TEXT_PHASE_WAIT_FRAMES;
                break;
            case 0x0D00:
                if (windowStyle == 0 || windowStyle == 3)
                {
                    if (gScriptWindowTileX < 8)
                    {
                        Battle_ShiftTilemapRect(gScriptTextTilemap, 1, 8, 0,
                                     gScriptWindowTileX, gScriptWindowTileY, 22, 8);
                        gScriptWindowTileX = 7;
                        Battle_SetWindowRect(0, 7, gScriptWindowTileY, 22, 8);
                    }
                    gScriptPortraitSlot = 2;
                    Scene_QueueOverlayImage(text[gScriptTextPosition.byteOffset], 2);
                }
                else
                {
                    if (gScriptWindowTileX < 8)
                    {
                        Battle_ShiftTilemapRect(gScriptTextTilemap, 1, 8, 0,
                                     gScriptWindowTileX, gScriptWindowTileY, 22, 8);
                        gScriptWindowTileX = 7;
                        Battle_SetWindowRect(0, 7, gScriptWindowTileY, 22, 8);
                    }
                    gScriptPortraitSlot = 0;
                    Scene_QueueOverlayImage(text[gScriptTextPosition.byteOffset], 0);
                }
                if (text[gScriptTextPosition.byteOffset] != 0)
                    gScriptFlags |= SCRIPT_FLAG_PORTRAIT_VISIBLE;
                else
                    gScriptFlags &= ~SCRIPT_FLAG_PORTRAIT_VISIBLE;
                keepDrawing = 0;
                break;
            case 0x0E00:
                if (windowStyle == 0 || windowStyle == 3)
                {
                    if (gScriptWindowTileX > 7)
                    {
                        Battle_ShiftTilemapRect(gScriptTextTilemap, 1, -8, 0,
                                     gScriptWindowTileX, gScriptWindowTileY, 22, 8);
                        gScriptWindowTileX = 0;
                        Battle_SetWindowRect(0, 0, gScriptWindowTileY, 22, 8);
                    }
                    gScriptPortraitSlot = 3;
                    Scene_QueueOverlayImage(text[gScriptTextPosition.byteOffset], 3);
                }
                else
                {
                    if (gScriptWindowTileX > 7)
                    {
                        Battle_ShiftTilemapRect(gScriptTextTilemap, 1, -8, 0,
                                     gScriptWindowTileX, gScriptWindowTileY, 22, 8);
                        gScriptWindowTileX = 0;
                        Battle_SetWindowRect(0, 0, gScriptWindowTileY, 22, 8);
                    }
                    gScriptPortraitSlot = 1;
                    Scene_QueueOverlayImage(text[gScriptTextPosition.byteOffset], 1);
                }
                keepDrawing = 0;
                if (text[gScriptTextPosition.byteOffset] != 0)
                    gScriptFlags |= SCRIPT_FLAG_PORTRAIT_VISIBLE;
                else
                    gScriptFlags &= ~SCRIPT_FLAG_PORTRAIT_VISIBLE;
                /* Keep this join until the final jump pass so the flag-write
                 * tail is shared with PAGE_BREAK in the original direction. */
                do {} while (0);
                break;
            case 0x0200:
                gScriptNameGlyphIndex = 0;
                gScriptNameId = text[gScriptTextPosition.byteOffset];
                if (Script_CacheChoiceNameInline(gScriptNameId) == 0)
                {
                    Script_DrawNameCharacter(gScriptTextTilemap, gScriptNameId,
                        gScriptTextTileX + gScriptWindowTileX + 2,
                        gScriptTextTileY + gScriptWindowTileY + 1);
                    gScriptTextTileX++;
                }
                gScriptTextPhase = SCRIPT_TEXT_PHASE_DRAW_NAME;
                break;
            case 0x0300:
            case 0x0400:
            case 0x0500:
            case 0x0600:
            case 0x0B00:
            case 0x0C00:
            default:
                if (text[gScriptTextPosition.byteOffset + 1] & 0x10)
                {
                    glyph = (text[gScriptTextPosition.byteOffset + 1] << 8)
                        & ~SCRIPT_TEXT_EXTENDED_GLYPH_FLAG;
                    glyph |= text[gScriptTextPosition.byteOffset];
                    if (glyph > 0xDF)
                    {
                        for (index = 0; index < gScriptCachedGlyphCount; index++)
                        {
                            if (gScriptCachedGlyphIds[index] == glyph)
                                break;
                        }
                        glyph = (index + SCRIPT_EXTENDED_GLYPH_BASE) * 2;
                        tilemap[(gScriptTextTileY + gScriptWindowTileY + 1) * SCRIPT_TILEMAP_WIDTH
                                + (gScriptTextTileX + gScriptWindowTileX + 2)] = glyph + 0xB000;
                        tilemap[(gScriptTextTileY + gScriptWindowTileY + 2) * SCRIPT_TILEMAP_WIDTH
                                + (gScriptTextTileX + gScriptWindowTileX + 2)] = glyph + 0xB001;
                    }
                    else
                    {
                        glyph = text[gScriptTextPosition.byteOffset] * 2;
                        tilemap[(gScriptTextTileY + gScriptWindowTileY + 1) * SCRIPT_TILEMAP_WIDTH
                                + (gScriptTextTileX + gScriptWindowTileX + 2)] = glyph + 0xB000;
                        tilemap[(gScriptTextTileY + gScriptWindowTileY + 2) * SCRIPT_TILEMAP_WIDTH
                                + (gScriptTextTileX + gScriptWindowTileX + 2)] = glyph + 0xB001;
                    }
                }
                else if (text[gScriptTextPosition.byteOffset] != 0)
                {
                    glyph = text[gScriptTextPosition.byteOffset] * 2;
                    tilemap[(gScriptTextTileY + gScriptWindowTileY + 1) * SCRIPT_TILEMAP_WIDTH
                            + (gScriptTextTileX + gScriptWindowTileX + 2)] = glyph + 0xB000;
                    tilemap[(gScriptTextTileY + gScriptWindowTileY + 2) * SCRIPT_TILEMAP_WIDTH
                            + (gScriptTextTileX + gScriptWindowTileX + 2)] = glyph + 0xB001;
                }
                else
                {
                    tilemap[(gScriptTextTileY + gScriptWindowTileY + 1) * SCRIPT_TILEMAP_WIDTH
                            + (gScriptTextTileX + gScriptWindowTileX + 2)]
                        = text[gScriptTextPosition.byteOffset] + 0xB001;
                    tilemap[(gScriptTextTileY + gScriptWindowTileY + 2) * SCRIPT_TILEMAP_WIDTH
                            + (gScriptTextTileX + gScriptWindowTileX + 2)]
                        = text[gScriptTextPosition.byteOffset] + 0xB001;
                }
                gScriptTextTileX++;
                break;
            }
            gScriptTextPosition.byteOffset += 2;
            if (!fastText)
                break;
        }
        break;

    case SCRIPT_TEXT_PHASE_WAIT_INPUT:
        gScriptTextCounter = (gScriptTextCounter + 1) % 16;
        tilemap[gScriptWindowTileY * SCRIPT_TILEMAP_WIDTH + gScriptWindowTileX + 0xD4]
            = 0xB266 + (gScriptTextCounter / 8);
        if ((Battle_GetInput() & 0xF000) == 0x1000 || (Battle_GetInput() & 0x20) != 0)
        {
            gScriptTextPhase = SCRIPT_TEXT_PHASE_DRAW;
            tilemap[gScriptWindowTileY * SCRIPT_TILEMAP_WIDTH + gScriptWindowTileX + 0xD4] = 0xC001;
        }
        break;

    case SCRIPT_TEXT_PHASE_CLOSE_PORTRAIT:
        if (gScriptFlags & SCRIPT_FLAG_PORTRAIT_VISIBLE)
            Scene_QueueOverlayImage(0, gScriptPortraitSlot);
        gScriptTextPhase = SCRIPT_TEXT_PHASE_CLOSE_WINDOW;
        break;

    case SCRIPT_TEXT_PHASE_CLOSE_WINDOW:
        Battle_CloseWindow(0, 5, 2);
        gScriptTextPhase = SCRIPT_TEXT_PHASE_FINISH;
        break;

    case SCRIPT_TEXT_PHASE_FINISH:
        if ((u8)Battle_GetWindowState(0) == 0 && Sound_IsSfxPlaying(1) == 0)
        {
            gScriptTextPhase = SCRIPT_TEXT_PHASE_INIT;
            gScriptFlags |= SCRIPT_FLAG_DEFAULT_GLYPHS_PENDING;
            (*(vu16 *)REG_DISPCNT) &= ~0x100;
            if (gScriptFlags & SCRIPT_FLAG_PORTRAIT_VISIBLE)
                gScriptFlags &= ~SCRIPT_FLAG_PORTRAIT_VISIBLE;
            for (cacheIndex = 0; cacheIndex < SCRIPT_GLYPH_CACHE_COUNT; cacheIndex++)
                gScriptCachedGlyphIds[cacheIndex] = 0;
            gScriptCachedGlyphCount = 0;
            *pc += sizeof(struct ScriptDialogueHeader) + gScriptDialogueWordCount * 2;
        }
        break;

    case SCRIPT_TEXT_PHASE_WAIT_FRAMES:
        if (gScriptWaitCounter < gScriptTextCounter)
            gScriptWaitCounter++;
        else
            gScriptTextPhase = SCRIPT_TEXT_PHASE_DRAW;
        break;

    case SCRIPT_TEXT_PHASE_DRAW_NAME:
        if (Script_DrawNameCharacter(gScriptTextTilemap, gScriptNameId,
                gScriptTextTileX + gScriptWindowTileX + 2,
                gScriptTextTileY + gScriptWindowTileY + 1) == 0)
            gScriptTextTileX++;
        else
            gScriptTextPhase = SCRIPT_TEXT_PHASE_DRAW;
        break;
    }
    return SCRIPT_RESULT_YIELD;
}
/**
 * @brief 弹出普通脚本调用栈，空栈时结束脚本
 *
 * @note 操作码 03；有返回地址时恢复 PC 并返回 1；空栈路径停止解释器，返回 0。
 * @address 0x080511A0
 */
u16 ScriptCmd_Return(const u8 **pc)
{
    u16 result = SCRIPT_RESULT_CONTINUE;
    u8 i;

    if (gScriptCallStackDepth != 0)
    {
        gScriptCallStackDepth--;
        *pc = gScriptReturnPcs[gScriptCallStackDepth];
    }
    else
    {
        (*pc)++;
        Scene_SetSavedScriptBank(gScriptBankId);
        gScriptFlags &= ~SCRIPT_FLAG_RUNNING;
        for (i = 0; i < gScriptBankStackDepth; i++)
        {
            gScriptBankReturnPcs[i] = NULL;
            gScriptReturnBankIds[i] = 0;
        }
        gScriptBankStackDepth = 0;
        result = SCRIPT_RESULT_YIELD;
    }
    return result;
}

/**
 * @brief 停止脚本并清理跨 bank 返回栈
 *
 * @note 操作码 06；参数 0 保留当前 PC，非零复位到字节码头；加载未完成时不执行清理，返回 0。
 * @address 0x08051230
 */
u16 ScriptCmd_End(const u8 **pc)
{
    const u8 *command = *pc;
    u8 i;

    if (!(gScriptFlags & SCRIPT_FLAG_BANK_LOAD_PENDING))
    {
        command++;
        Scene_SetSavedScriptBank(gScriptBankId);
        Script_StopInline(*command != 0 ? SCRIPT_STOP_RESET : SCRIPT_STOP_KEEP_CURRENT);
        for (i = 0; i < gScriptBankStackDepth; i++)
        {
            gScriptBankReturnPcs[i] = NULL;
            gScriptReturnBankIds[i] = 0;
        }
        gScriptBankStackDepth = 0;
    }
    return SCRIPT_RESULT_YIELD;
}

/* CallBank keeps the bank value word-sized across this inline boundary. */
static inline void Script_LoadBankInline(u32 bankId, u8 entryId, u8 mode)
{
    const struct LzStreamHeader *bank;
    u32 decompressedSize;
    u16 *entryOffsets;

    gScriptBankId = bankId;
    bank = gScriptBankTable[bankId];
    decompressedSize = bank->decompressedSize;

    /* DISPCNT forced-blank allows the whole bank to be decompressed at once. */
    if ((*(vu16 *)REG_DISPCNT) & DISP_LCDC_OFF)
    {
        LzStream_Init(SCRIPT_ENTRY_OFFSETS, bank, decompressedSize);
        LzStream_DecodeChunk();
    }
    else
    {
        LzStream_Init(SCRIPT_ENTRY_OFFSETS, bank, 0x400);
        gScriptFlags |= SCRIPT_FLAG_BANK_LOAD_PENDING;
    }

    entryOffsets = SCRIPT_ENTRY_OFFSETS;

    switch (mode)
    {
    case SCRIPT_START_HEAD:
    default:
        gScriptPc = (const u8 *)SCRIPT_BYTECODE;
        break;
    case SCRIPT_START_ENTRY:
        gScriptPendingEntryId = entryId;
        gScriptFlags |= SCRIPT_FLAG_ENTRY_RESELECT_PENDING;
        gScriptPc = entryOffsets[entryId] + (const u8 *)SCRIPT_BYTECODE;
        break;
    }
}

/**
 * @brief 保存当前 bank 与返回 PC，再载入另一脚本 bank
 *
 * @note 操作码 15；跨 bank 栈保存 PC + 3；另保留传入 r6/r5 的低字节，其用途未知；返回 0。
 * @address 0x080512C4
 */
u16 ScriptCmd_CallBank(const u8 **pc)
{
    const u8 *command = *pc;
    u32 bankId;
    u8 entryId;

    gScriptReturnBankIds[gScriptBankStackDepth] = gScriptBankId;
    /* Preserve the original stores before these locals are assigned. */
    gScriptUnknownEC8 = entryId;
    gScriptUnknownEC9 = bankId;
    command++;
    bankId = *command++;
    entryId = *command++;
    gScriptBankReturnPcs[gScriptBankStackDepth] = command;
    gScriptBankStackDepth++;

    Script_LoadBankInline(bankId, entryId, SCRIPT_START_ENTRY);
    gScriptFlags |= SCRIPT_FLAG_BANK_CALL;
    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 重新载入上层脚本 bank 并恢复保存的 PC
 *
 * @note 操作码 16；跨 bank 深度先减一，原实现不保护空栈；写回全局 PC，返回 0。
 * @address 0x080513A0
 */
u16 ScriptCmd_ReturnBank(const u8 **pc)
{
    gScriptBankStackDepth--;
    gScriptLastReturnedBankId = gScriptReturnBankIds[gScriptBankStackDepth];
    Script_LoadBankInline(gScriptReturnBankIds[gScriptBankStackDepth], 0, SCRIPT_START_HEAD);
    gScriptPc = gScriptBankReturnPcs[gScriptBankStackDepth];
    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 在给定图块坐标逐字绘制脚本文本
 *
 * @note 操作码 17；头部 X/Y 是绝对 tile 坐标；处理文字控制码，完成时按字数跳过载荷并返回 0。
 * @address 0x0805144C
 */
u16 ScriptCmd_DrawText(const u8 **pc)
{
    u16 *tilemap = (u16 *)SCRIPT_TEXT_TILEMAP_ADDR;
    const u8 *command = *pc;
    u8 x = command[2];
    u8 y = command[3];
    u32 wordCount = command[4];
    u16 high = command[5];
    const u8 *text = command + sizeof(struct ScriptTextHeader);
    u16 index;
    u16 glyph;
    /* Preserve the original single-iteration text loop. */
    s32 keepDrawing = 1;
    BgCnt bgControl;

    wordCount |= high << 8;

    switch (gScriptTextPhase)
    {
    case SCRIPT_TEXT_PHASE_INIT:
        gScriptTextPosition.byteOffset = 0;
        gScriptTextTileX = x;
        gScriptTextTileY = y;
        if (!(gScriptFlags & SCRIPT_FLAG_TEXT_LAYER_PERSISTENT))
        {
            for (index = 0; index < SCRIPT_TILEMAP_WIDTH * SCRIPT_TILEMAP_HEIGHT; index++)
                tilemap[index] = 0;
            tilemap = (u16 *)BG_SCREEN_ADDR(31);
            for (index = 0; index < SCRIPT_TILEMAP_WIDTH * SCRIPT_TILEMAP_HEIGHT; index++)
                tilemap[index] = 0;
            (*(vu16 *)REG_DISPCNT) |= 0x100;
            bgControl.Priority = 0;
            bgControl.CharBasep = 2;
            bgControl.Dummy_5_4 = 0;
            bgControl.Mosaic = 0;
            bgControl.ColorMode = 0;
            bgControl.ScBasep = 31;
            bgControl.Loop = 0;
            bgControl.Size = 0;
            (*(vu16 *)REG_BG0CNT) = *(u16 *)&bgControl;
        }
        Script_CacheGlyphsFromBytes(text, SCRIPT_TEXT_PAGE_BREAK, 16);
        gScriptFlags |= SCRIPT_FLAG_GLYPH_UPLOAD_PENDING;
        gScriptTextPhase = SCRIPT_TEXT_PHASE_DRAW;
        break;

    case SCRIPT_TEXT_PHASE_DRAW:
        if (gScriptFlags & SCRIPT_FLAG_GLYPH_UPLOAD_PENDING)
            break;
        while (keepDrawing)
        {
            switch (text[gScriptTextPosition.byteOffset + 1] << 8)
            {
            case 0x0700:
                keepDrawing = 0;
                gScriptTextPhase = SCRIPT_TEXT_PHASE_WAIT_INPUT;
                break;
            case 0x0900:
                gScriptTextTileX = x;
                gScriptTextTileY += 2;
                break;
            case SCRIPT_TEXT_END:
                keepDrawing = 0;
                gScriptTextPhase = SCRIPT_TEXT_PHASE_FINISH;
                break;
            case SCRIPT_TEXT_PAGE_BREAK:
                gScriptTextTileX = x;
                gScriptTextTileY = y;
                for (index = 0; index < SCRIPT_TILEMAP_WIDTH * SCRIPT_TILEMAP_HEIGHT; index++)
                    tilemap[index] = 0;
                Script_CacheGlyphsFromBytes(text + (gScriptTextPosition.byteOffset + 2),
                                           SCRIPT_TEXT_PAGE_BREAK, 16);
                gScriptFlags |= SCRIPT_FLAG_GLYPH_UPLOAD_PENDING;
                break;
            case 0x0A00:
                gScriptWaitCounter = 0;
                gScriptTextCounter = text[gScriptTextPosition.byteOffset];
                gScriptTextPhase = SCRIPT_TEXT_PHASE_WAIT_FRAMES;
                break;
            case 0x0200:
                gScriptNameGlyphIndex = 0;
                gScriptNameId = text[gScriptTextPosition.byteOffset];
                if (Script_CacheChoiceNameInline(gScriptNameId) == 0)
                {
                    Script_DrawNameCharacter(gScriptTextTilemap, gScriptNameId,
                                             gScriptTextTileX, gScriptTextTileY);
                    gScriptTextTileX++;
                }
                gScriptTextPhase = SCRIPT_TEXT_PHASE_DRAW_NAME;
                break;
            case 0x0100:
            case 0x0300:
            case 0x0400:
            case 0x0500:
            case 0x0600:
            case 0x0B00:
            case 0x0C00:
            case 0x0D00:
            case 0x0E00:
                break;
            default:
                if (text[gScriptTextPosition.byteOffset + 1] & 0x10)
                {
                    glyph = (text[gScriptTextPosition.byteOffset + 1] << 8)
                        & ~SCRIPT_TEXT_EXTENDED_GLYPH_FLAG;
                    glyph |= text[gScriptTextPosition.byteOffset];
                    if (glyph > 0xDF)
                    {
                        for (index = 0; index < gScriptCachedGlyphCount; index++)
                        {
                            if (gScriptCachedGlyphIds[index] == glyph)
                                break;
                        }
                        glyph = (index + SCRIPT_EXTENDED_GLYPH_BASE) * 2;
                        tilemap[gScriptTextTileY * SCRIPT_TILEMAP_WIDTH + gScriptTextTileX]
                            = glyph + 0xB000;
                        tilemap[gScriptTextTileY * SCRIPT_TILEMAP_WIDTH + gScriptTextTileX
                                + SCRIPT_TILEMAP_WIDTH] = glyph + 0xB001;
                    }
                    else
                    {
                        glyph = text[gScriptTextPosition.byteOffset] * 2;
                        tilemap[gScriptTextTileY * SCRIPT_TILEMAP_WIDTH + gScriptTextTileX]
                            = glyph + 0xB000;
                        tilemap[gScriptTextTileY * SCRIPT_TILEMAP_WIDTH + gScriptTextTileX
                                + SCRIPT_TILEMAP_WIDTH] = glyph + 0xB001;
                    }
                }
                else if (text[gScriptTextPosition.byteOffset] != 0)
                {
                    tilemap[gScriptTextTileY * SCRIPT_TILEMAP_WIDTH + gScriptTextTileX]
                        = text[gScriptTextPosition.byteOffset] * 2 + (s16)0xB000;
                    tilemap[gScriptTextTileY * SCRIPT_TILEMAP_WIDTH + gScriptTextTileX
                            + SCRIPT_TILEMAP_WIDTH]
                        = text[gScriptTextPosition.byteOffset] * 2 + (s16)0xB001;
                }
                else
                {
                    tilemap[gScriptTextTileY * SCRIPT_TILEMAP_WIDTH + gScriptTextTileX]
                        = text[gScriptTextPosition.byteOffset] + 0xB001;
                    tilemap[gScriptTextTileY * SCRIPT_TILEMAP_WIDTH + gScriptTextTileX
                            + SCRIPT_TILEMAP_WIDTH]
                        = text[gScriptTextPosition.byteOffset] + 0xB001;
                }
                gScriptTextTileX++;
                break;
            }
            gScriptTextPosition.byteOffset += 2;
            break;
        }
        break;

    case SCRIPT_TEXT_PHASE_WAIT_INPUT:
        if ((Battle_GetInput() & 0xF000) == 0x1000)
        {
            gScriptTextPhase = SCRIPT_TEXT_PHASE_DRAW;
            tilemap[gScriptWindowTileY * SCRIPT_TILEMAP_WIDTH + gScriptWindowTileX + 0xD4]
                = 0xC001;
        }
        gScriptTextCounter = (gScriptTextCounter + 1) % 16;
        tilemap[gScriptWindowTileY * SCRIPT_TILEMAP_WIDTH + gScriptWindowTileX + 0xD4]
            = 0xB266 + (gScriptTextCounter / 8);
        break;

    case SCRIPT_TEXT_PHASE_FINISH:
        gScriptTextPhase = SCRIPT_TEXT_PHASE_INIT;
        *pc += sizeof(struct ScriptTextHeader) + wordCount * 2;
        if (!(gScriptFlags & SCRIPT_FLAG_TEXT_LAYER_PERSISTENT))
            (*(vu16 *)REG_DISPCNT) &= ~0x100;
        break;

    case SCRIPT_TEXT_PHASE_WAIT_FRAMES:
        if (gScriptWaitCounter < gScriptTextCounter)
            gScriptWaitCounter++;
        else
            gScriptTextPhase = SCRIPT_TEXT_PHASE_DRAW;
        break;

    case SCRIPT_TEXT_PHASE_DRAW_NAME:
        if (Script_DrawNameCharacter(gScriptTextTilemap, gScriptNameId,
                                    gScriptTextTileX, gScriptTextTileY) == 0)
            gScriptTextTileX++;
        else
            gScriptTextPhase = SCRIPT_TEXT_PHASE_DRAW;
        break;
    }
    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 清空文本图块表并启用持久 BG0 文本层
 *
 * @note 操作码 24；初始化 BG0 显示寄存器，置持久层位；PC += 1，返回 1。
 * @address 0x08051A1C
 */
u16 ScriptCmd_EnableTextLayer(const u8 **pc)
{
    /* Reuse the source halfword after each DMA transfer has completed. */
    u16 fill;
    BgCnt bg0cnt;

    {
        u16 *source = &fill;
        *source = 0xB000;
        DmaSet(3, source, gScriptTextTilemap,
            (u32)(DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS) | (sizeof(gScriptTextTilemap) / 2));
    }
    DmaWait(3);

    {
        u16 *source = &fill;
        *source = 0;
        DmaSet(3, source, BG_SCREEN_ADDR(31),
            (u32)(DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS) | (sizeof(gScriptTextTilemap) / 2));
    }
    DmaWait(3);

    (*(vu16 *)REG_DISPCNT) |= 0x100;

    bg0cnt.Priority = 0;
    bg0cnt.CharBasep = 2;
    bg0cnt.Dummy_5_4 = 0;
    bg0cnt.Mosaic = 0;
    bg0cnt.ColorMode = 0;
    bg0cnt.ScBasep = 31;
    bg0cnt.Loop = 0;
    bg0cnt.Size = 0;

    (*(vu16 *)REG_BG0CNT) = *(u16 *)&bg0cnt;

    gScriptFlags |= SCRIPT_FLAG_TEXT_LAYER_PERSISTENT;

    *pc += 1;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 按线性、缓入或缓出模式计算有符号半字插值
 *
 * @note 模式 1/2 先以单精度修改 delta 再做整数插值；输入取低 16 位，duration 必须非零。
 * @address 0x08051AEC
 */
s16 Script_Interpolate(s16 start, s16 delta, s16 duration, s16 frame, s8 mode)
{
    u16 adjustedDelta = delta;
    u16 durationValue = duration;
    u16 frameValue = frame;

    switch (mode)
    {
    case 0:
    default:
        break;
    case 1:
    {
        float deltaValue;

        deltaValue = (s16)adjustedDelta;
        deltaValue *= (s16)frameValue * 10.0f / (s16)durationValue / 10.0f;
        adjustedDelta = (s16)deltaValue;
        break;
    }
    case 2:
    {
        float deltaValue;

        deltaValue = (s16)adjustedDelta;
        deltaValue *= ((s16)frameValue * -10.0f / (s16)durationValue + 20.0f) / 10.0f;
        adjustedDelta = (s16)deltaValue;
        break;
    }
    }
    return start + (s16)adjustedDelta * (s16)frameValue / (s16)durationValue;
}

/**
 * @brief 绘制选项窗口，处理光标输入并跳到所选入口
 *
 * @note 操作码 33；文本阶段复用为选择流程；+6 的未知参数使用 LDRH 复制，光标下标共用文本位置半字；返回 0。
 * @address 0x08051BE4
 */
u16 ScriptCmd_ShowChoices(const u8 **pc)
{
    const u8 *command = *pc;
    const u8 *text;
    u16 index;
    u16 glyph;
    s32 keepDrawing = 1;
    /* Original control 0x0E00 reads an uninitialized word at sp + 0x18. */
    s32 overlayMode;
    u16 *tilemap = gScriptTextTilemap;
    BgCnt bgControl;

    Battle_UpdateWindows();
    text = *pc + sizeof(struct ScriptChoiceHeader);

    switch (gScriptTextPhase)
    {
    case SCRIPT_TEXT_PHASE_INIT:
        gScriptChoiceUnknown06 = *(const u16 *)(command + offsetof(struct ScriptChoiceHeader, unknown06LE));
        gScriptWindowTileX = 4;
        gScriptWindowTileY = 6;
        gScriptTextTileX = 0;
        gScriptTextTileY = 0;
        gScriptTextPosition.byteOffset = 0;
        for (index = 0; index < SCRIPT_TILEMAP_WIDTH * SCRIPT_TILEMAP_HEIGHT; index++)
            tilemap[index] = 0;
        tilemap = BG_SCREEN_ADDR(31);
        for (index = 0; index < SCRIPT_TILEMAP_WIDTH * SCRIPT_TILEMAP_HEIGHT; index++)
            tilemap[index] = 0;
        (*(vu16 *)REG_DISPCNT) |= 0x100;
        /* Write each BG0CNT field in the original order; the final value is 0x1F08. */
        bgControl.Priority = 0;
        bgControl.CharBasep = 2;
        bgControl.Dummy_5_4 = 0;
        bgControl.Mosaic = 0;
        bgControl.ColorMode = 0;
        bgControl.ScBasep = 31;
        bgControl.Loop = 0;
        bgControl.Size = 0;
        (*(vu16 *)REG_BG0CNT) = *(u16 *)&bgControl;
        Battle_ResetWindows();
        Battle_OpenWindow(0, gScriptTextTilemap, 11, 5, 2,
                      gScriptWindowTileX, gScriptWindowTileY, 22, 8);
        gScriptTextPhase = SCRIPT_TEXT_PHASE_WAIT_WINDOW;
        gScriptFlags |= SCRIPT_FLAG_TEXT_LAYER_PERSISTENT;
        Script_CacheGlyphsFromBytes(text, SCRIPT_TEXT_PAGE_BREAK, 16);
        gScriptFlags |= SCRIPT_FLAG_GLYPH_UPLOAD_PENDING;
        break;

    case SCRIPT_TEXT_PHASE_WAIT_WINDOW:
        if ((u8)Battle_GetWindowState(0) == 4)
            gScriptTextPhase = SCRIPT_TEXT_PHASE_DRAW;
        break;

    case SCRIPT_TEXT_PHASE_DRAW:
        if (gScriptFlags & SCRIPT_FLAG_GLYPH_UPLOAD_PENDING)
            break;
        while (keepDrawing)
        {
            glyph = text[gScriptTextPosition.byteOffset + 1] << 8;
            switch (glyph)
            {
            case 0x0700:
                keepDrawing = 0;
                gScriptTextCounter = (gScriptTextCounter + 1) % 16;
                tilemap[gScriptWindowTileY * SCRIPT_TILEMAP_WIDTH
                        + gScriptWindowTileX + 0xD4] = 0xB266 + (gScriptTextCounter / 8);
                gScriptTextPhase = SCRIPT_TEXT_PHASE_WAIT_INPUT;
                break;
            case 0x0900:
                gScriptTextTileX = 0;
                gScriptTextTileY += 2;
                break;
            case SCRIPT_TEXT_END:
                keepDrawing = 0;
                gScriptTextPhase = SCRIPT_TEXT_PHASE_WAIT_INPUT;
                break;
            case SCRIPT_TEXT_PAGE_BREAK:
                gScriptTextTileX = 0;
                gScriptTextTileY = 0;
                Battle_DrawWindowBorder(gScriptTextTilemap, 11,
                              gScriptWindowTileX, gScriptWindowTileY, 22, 8);
                Script_CacheGlyphsFromBytes(text + (gScriptTextPosition.byteOffset + 2),
                                           SCRIPT_TEXT_PAGE_BREAK, 16);
                gScriptFlags |= SCRIPT_FLAG_GLYPH_UPLOAD_PENDING;
                break;
            case 0x0A00:
                gScriptWaitCounter = 0;
                gScriptTextCounter = text[gScriptTextPosition.byteOffset];
                gScriptTextPhase = SCRIPT_TEXT_PHASE_WAIT_FRAMES;
                break;
            case 0x0D00:
                if (gScriptWindowTileX < 8)
                {
                    Battle_ShiftTilemapRect(gScriptTextTilemap, 1, 8, 0,
                                  gScriptWindowTileX, gScriptWindowTileY, 22, 8);
                    gScriptWindowTileX = 7;
                    Battle_SetWindowRect(0, 7, gScriptWindowTileY, 22, 8);
                }
                Scene_QueueOverlayImage(text[gScriptTextPosition.byteOffset], 2);
                keepDrawing = 0;
                break;
            case 0x0E00:
                if (overlayMode == 0 || overlayMode == 3)
                {
                    if (gScriptWindowTileX > 7)
                    {
                        Battle_ShiftTilemapRect(gScriptTextTilemap, 1, -8, 0,
                                      gScriptWindowTileX, gScriptWindowTileY, 22, 8);
                        gScriptWindowTileX = 0;
                        Battle_SetWindowRect(0, 0, gScriptWindowTileY, 22, 8);
                    }
                    Scene_QueueOverlayImage(text[gScriptTextPosition.byteOffset], 3);
                }
                else
                {
                    if (gScriptWindowTileX > 7)
                    {
                        Battle_ShiftTilemapRect(gScriptTextTilemap, 1, -8, 0,
                                      gScriptWindowTileX, gScriptWindowTileY, 22, 8);
                        gScriptWindowTileX = 0;
                        Battle_SetWindowRect(0, 0, gScriptWindowTileY, 22, 8);
                    }
                    Scene_QueueOverlayImage(text[gScriptTextPosition.byteOffset], 1);
                }
                keepDrawing = 0;
                break;
            case 0x0200:
                gScriptNameGlyphIndex = 0;
                gScriptNameId = text[gScriptTextPosition.byteOffset];
                if (Script_CacheChoiceNameInline(gScriptNameId) == 0)
                {
                    Script_DrawNameCharacter(gScriptTextTilemap, gScriptNameId,
                        gScriptTextTileX + gScriptWindowTileX + 2,
                        gScriptTextTileY + gScriptWindowTileY + 1);
                    gScriptTextTileX++;
                }
                gScriptTextPhase = SCRIPT_TEXT_PHASE_DRAW_NAME;
                break;
            /* Keep the reserved cases: they affect the original decision tree. */
            case 0x0300:
            case 0x0400:
            case 0x0500:
            case 0x0600:
            case 0x0B00:
            case 0x0C00:
            default:
                if (text[gScriptTextPosition.byteOffset + 1] & 0x10)
                {
                    glyph = (text[gScriptTextPosition.byteOffset + 1] << 8)
                        & ~SCRIPT_TEXT_EXTENDED_GLYPH_FLAG;
                    glyph |= text[gScriptTextPosition.byteOffset];
                    if (glyph > 0xDF)
                    {
                        for (index = 0; index < gScriptCachedGlyphCount; index++)
                        {
                            if (gScriptCachedGlyphIds[index] == glyph)
                                break;
                        }
                        glyph = (index + SCRIPT_EXTENDED_GLYPH_BASE) * 2;
                        tilemap[(gScriptTextTileY + gScriptWindowTileY + 1) * SCRIPT_TILEMAP_WIDTH
                                + (gScriptTextTileX + gScriptWindowTileX + 2)] = glyph + 0xB000;
                        tilemap[(gScriptTextTileY + gScriptWindowTileY + 2) * SCRIPT_TILEMAP_WIDTH
                                + (gScriptTextTileX + gScriptWindowTileX + 2)] = glyph + 0xB001;
                    }
                    else
                    {
                        glyph = text[gScriptTextPosition.byteOffset] * 2;
                        tilemap[(gScriptTextTileY + gScriptWindowTileY + 1) * SCRIPT_TILEMAP_WIDTH
                                + (gScriptTextTileX + gScriptWindowTileX + 2)] = glyph + 0xB000;
                        tilemap[(gScriptTextTileY + gScriptWindowTileY + 2) * SCRIPT_TILEMAP_WIDTH
                                + (gScriptTextTileX + gScriptWindowTileX + 2)] = glyph + 0xB001;
                    }
                }
                else if (text[gScriptTextPosition.byteOffset] != 0)
                {
                    glyph = text[gScriptTextPosition.byteOffset] * 2;
                    tilemap[(gScriptTextTileY + gScriptWindowTileY + 1) * SCRIPT_TILEMAP_WIDTH
                            + (gScriptTextTileX + gScriptWindowTileX + 2)] = glyph + 0xB000;
                    tilemap[(gScriptTextTileY + gScriptWindowTileY + 2) * SCRIPT_TILEMAP_WIDTH
                            + (gScriptTextTileX + gScriptWindowTileX + 2)] = glyph + 0xB001;
                }
                else
                {
                    tilemap[(gScriptTextTileY + gScriptWindowTileY + 1) * SCRIPT_TILEMAP_WIDTH
                            + (gScriptTextTileX + gScriptWindowTileX + 2)]
                        = text[gScriptTextPosition.byteOffset] + 0xB001;
                    tilemap[(gScriptTextTileY + gScriptWindowTileY + 2) * SCRIPT_TILEMAP_WIDTH
                            + (gScriptTextTileX + gScriptWindowTileX + 2)]
                        = text[gScriptTextPosition.byteOffset] + 0xB001;
                }
                gScriptTextTileX++;
                break;
            }
            gScriptTextPosition.byteOffset += 2;
        }
        /* The original always enters input here, even after an inline control. */
        gScriptTextCounter = 0;
        gScriptTextPosition.choiceIndex = 0;
        gScriptTextPhase = SCRIPT_TEXT_PHASE_WAIT_INPUT;
        Battle_SetTilemapPalette(gScriptTextTilemap, 13, (u8)(gScriptWindowTileX + 2),
                      (u8)(gScriptWindowTileY + 1), 19, 2);
        break;

    case SCRIPT_TEXT_PHASE_WAIT_INPUT:
        if (Battle_GetInput() & 0x100)
        {
            Battle_SetTilemapPalette(gScriptTextTilemap, 11, (u8)(gScriptWindowTileX + 2),
                          (u8)(gScriptWindowTileY + 1), 19, 6);
            tilemap[(gScriptTextPosition.choiceIndex * 2 + gScriptWindowTileY) * SCRIPT_TILEMAP_WIDTH
                    + gScriptWindowTileX + 0x21] = 0xB001;
            gScriptTextPosition.choiceIndex = (gScriptTextPosition.choiceIndex == 0
                ? command[1] : gScriptTextPosition.choiceIndex) - 1;
            Sound_PlaySfx(0, 0, 0);
            Battle_SetTilemapPalette(gScriptTextTilemap, 13, (u8)(gScriptWindowTileX + 2),
                          (u8)(gScriptWindowTileY + (gScriptTextPosition.choiceIndex * 2 + 1)), 19, 2);
        }
        else if (Battle_GetInput() & 0x200)
        {
            Battle_SetTilemapPalette(gScriptTextTilemap, 11, (u8)(gScriptWindowTileX + 2),
                          (u8)(gScriptWindowTileY + 1), 19, 6);
            tilemap[(gScriptTextPosition.choiceIndex * 2 + gScriptWindowTileY) * SCRIPT_TILEMAP_WIDTH
                    + gScriptWindowTileX + 0x21] = 0xB001;
            gScriptTextPosition.choiceIndex = (gScriptTextPosition.choiceIndex + 1) % command[1];
            Sound_PlaySfx(0, 0, 0);
            Battle_SetTilemapPalette(gScriptTextTilemap, 13, (u8)(gScriptWindowTileX + 2),
                          (u8)(gScriptWindowTileY + (gScriptTextPosition.choiceIndex * 2 + 1)), 19, 2);
        }
        else if (Battle_GetInput() & 0x1000)
        {
            gScriptTextPhase = SCRIPT_TEXT_PHASE_CLOSE_PORTRAIT;
            Sound_PlaySfx(1, 0, 0);
        }
        gScriptTextCounter = (gScriptTextCounter + 1) % 16;
        tilemap[(gScriptWindowTileY + gScriptTextPosition.choiceIndex * 2 + 1) * SCRIPT_TILEMAP_WIDTH
                + (gScriptWindowTileX + 1)] = 0xB268 + (gScriptTextCounter / 8);
        break;

    case SCRIPT_TEXT_PHASE_CLOSE_PORTRAIT:
        Battle_CloseWindow(0, 5, 2);
        gScriptTextPhase = SCRIPT_TEXT_PHASE_FINISH;
        break;

    case SCRIPT_TEXT_PHASE_DRAW_NAME:
        if (Script_DrawNameCharacter(gScriptTextTilemap, gScriptNameId,
                gScriptTextTileX + gScriptWindowTileX + 2,
                gScriptTextTileY + gScriptWindowTileY + 1) == 0)
            gScriptTextTileX++;
        else
            gScriptTextPhase = SCRIPT_TEXT_PHASE_DRAW;
        break;

    case SCRIPT_TEXT_PHASE_FINISH:
        if ((u8)Battle_GetWindowState(0) == 0 && Sound_IsSfxPlaying(1) == 0)
        {
            gScriptTextPhase = SCRIPT_TEXT_PHASE_INIT;
            *pc = Script_ResolveEntryInline(command[gScriptTextPosition.choiceIndex + 3]);
            Scene_QueueOverlayImage(0, 0);
            Scene_QueueOverlayImage(0, 1);
            Scene_QueueOverlayImage(0, 2);
            Scene_QueueOverlayImage(0, 3);
            (*(vu16 *)REG_DISPCNT) &= ~0x100;
            gScriptFlags |= SCRIPT_FLAG_DEFAULT_GLYPHS_PENDING;
        }
        break;
    }

    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 读取解释器的运行和文本服务标志
 *
 * @note 直接读取私有标志半字，不修改状态；供场景切换判断解释器是否仍在运行。
 * @address 0x08052574
 */
u16 Script_GetFlags(void)
{
    return gScriptFlags;
}

/**
 * @brief 初始化脚本 PC、窗口位置及两类调用栈深度
 *
 * @note 清八个普通返回指针，窗口为 (1,12)；不清整个私有 RAM 区或跨 bank 指针数组。
 * @address 0x08052580
 */
void Script_Init(void)
{
    u8 i;

    gScriptPc = (const u8 *)SCRIPT_BYTECODE;
    gScriptFlags = 0;
    gScriptTextPhase = SCRIPT_TEXT_PHASE_INIT;
    gScriptWindowTileX = 1;
    gScriptWindowTileY = 12;
    gScriptCallStackDepth = 0;

    for (i = 0; i < SCRIPT_CALL_STACK_DEPTH; i++)
        gScriptReturnPcs[i] = NULL;

    gScriptBankStackDepth = 0;
}

/**
 * @brief 载入脚本 bank，并按模式选择起始 PC
 *
 * @note 强制空白时一次解压，显示期间每次预算 0x400 字节；模式 2 延后重选入口，不自行置 RUNNING。
 * @address 0x080525E8
 */
void Script_LoadBank(u8 bankId, u8 entryId, u8 mode)
{
    const struct LzStreamHeader *bank;
    u32 decompressedSize;
    u16 *entryOffsets;

    gScriptBankId = bankId;
    bank = gScriptBankTable[bankId];
    decompressedSize = bank->decompressedSize;

    /* DISPCNT forced-blank allows the whole bank to be decompressed at once. */
    if ((*(vu16 *)REG_DISPCNT) & DISP_LCDC_OFF)
    {
        LzStream_Init(SCRIPT_ENTRY_OFFSETS, bank, decompressedSize);
        LzStream_DecodeChunk();
    }
    else
    {
        LzStream_Init(SCRIPT_ENTRY_OFFSETS, bank, 0x400);
        gScriptFlags |= SCRIPT_FLAG_BANK_LOAD_PENDING;
    }

    entryOffsets = SCRIPT_ENTRY_OFFSETS;

    switch (mode)
    {
    case SCRIPT_START_HEAD:
    default:
        gScriptPc = (const u8 *)SCRIPT_BYTECODE;
        break;
    case SCRIPT_START_ENTRY:
        gScriptPendingEntryId = entryId;
        gScriptFlags |= SCRIPT_FLAG_ENTRY_RESELECT_PENDING;
        gScriptPc = entryOffsets[entryId] + (const u8 *)SCRIPT_BYTECODE;
        break;
    }
}

/**
 * @brief 选择脚本入口并启动解释器
 *
 * @note 模式 1 为 bank 头，2 为指定入口，3 保留 PC；清文本阶段并将八个未消费输入槽设为 FFFF。
 * @address 0x080526A0
 */
void Script_Start(u8 entryId, u8 mode)
{
    u8 i;
    u16 *entryOffsets = SCRIPT_ENTRY_OFFSETS;

    switch (mode)
    {
    case SCRIPT_START_HEAD:
    default:
        gScriptPc = (const u8 *)SCRIPT_BYTECODE;
        break;
    case SCRIPT_START_ENTRY:
        gScriptPc = entryOffsets[entryId] + (const u8 *)SCRIPT_BYTECODE;
        break;
    case SCRIPT_START_CURRENT:
        break;
    }

    for (i = 0; i < 8; i++)
        gScriptUnusedInputScratch[i] |= 0xFFFF;

    gScriptFlags |= SCRIPT_FLAG_RUNNING;
    gScriptTextPhase = SCRIPT_TEXT_PHASE_INIT;
}

/**
 * @brief 停止解释器，并按模式保留或复位 PC
 *
 * @note 只有模式 3 保留 PC；仅清 RUNNING，不清其他请求位或调用栈。
 * @address 0x08052728
 */
void Script_Stop(u8 mode)
{
    switch (mode)
    {
    case SCRIPT_STOP_RESET:
    default:
        gScriptPc = (const u8 *)SCRIPT_BYTECODE;
        break;
    case SCRIPT_STOP_KEEP_CURRENT:
        break;
    }

    gScriptFlags &= ~SCRIPT_FLAG_RUNNING;
}

/**
 * @brief 将默认字形 LZ77 数据解压到文本缓存 VRAM
 *
 * @note fontId 先取低 16 位，非零也强制改为 0；目标固定为 0600B800。
 * @address 0x08052758
 */
void Script_RestoreDefaultGlyphs(u16 fontId)
{
    const u8 *const *sources;

    if (fontId != 0)
        fontId = 0;

    sources = &gScriptFontResources.defaultGlyphsLz;
    LZ77UnCompVram((void *)(sources[fontId]), SCRIPT_GLYPH_VRAM);
}

/**
 * @brief 清空三十个扩展字形编号和缓存计数
 *
 * @note 不擦除 EWRAM tile 数据，也不更新最后字形下标。
 * @address 0x08052780
 */
void Script_ClearGlyphCache(void)
{
    u8 i;

    for (i = 0; i < SCRIPT_GLYPH_CACHE_COUNT; i++)
        gScriptCachedGlyphIds[i] = 0;

    gScriptCachedGlyphCount = 0;
}

/**
 * @brief 用 DMA3 把已缓存字形上传到文本 VRAM
 *
 * @note 每个字形复制 0x40 字节；空缓存不发起 DMA，所有路径返回 -1。
 * @address 0x080527AC
 */
s16 Script_UploadGlyphCache(void)
{
    if (gScriptCachedGlyphCount != 0)
    {
        DmaCopy(3, gScriptGlyphCacheTiles, SCRIPT_GLYPH_VRAM,
                  (u32)(gScriptCachedGlyphCount * sizeof(gScriptGlyphCacheTiles[0])), 32);
        DmaWait(3);
    }
    return -1;
}

/**
 * @brief 返回缓存数量并写出字形编号数组指针
 *
 * @note 输出指针指向编号表，不是 tile 缓冲；其存储仍归本模块私有。
 * @address 0x080527F4
 */
u16 Script_GetGlyphCache(u16 **glyphIds)
{
    *glyphIds = gScriptCachedGlyphIds;
    return gScriptCachedGlyphCount;
}

/**
 * @brief 从当前名字字形下标准备名字所需的扩展字形
 *
 * @note 使用扫描参数 6F1E；缓存非空时请求 VBlank 上传并返回 1，否则返回 0。
 * @address 0x08052808
 */
u8 Script_CacheNameGlyphs(u8 nameId)
{
    u8 result;

    Script_CacheGlyphsFromWords(gScriptNames[nameId].glyphs + gScriptNameGlyphIndex,
                              SCRIPT_GLYPH_SCAN_NAME);
    if (gScriptCachedGlyphCount == 0)
        result = 0;
    else
    {
        gScriptFlags |= SCRIPT_FLAG_GLYPH_UPLOAD_PENDING;
        result = 1;
    }
    return result;
}

/**
 * @brief 跳到当前脚本 bank 的指定入口
 *
 * @note 操作码 01；入口表保存相对字节码头的字节偏移；设置 PC 后返回 1。
 * @address 0x08052858
 */
u16 ScriptCmd_Jump(const u8 **pc)
{
    const u8 *command = *pc;
    u16 *entryOffsets = SCRIPT_ENTRY_OFFSETS;

    *pc = (const u8 *)SCRIPT_BYTECODE + entryOffsets[command[1]];
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 保存返回地址并调用当前 bank 的另一入口
 *
 * @note 操作码 02；八层普通栈保存 PC + 2；栈满时直接跳过此命令，返回 1。
 * @address 0x08052878
 */
u16 ScriptCmd_Call(const u8 **pc)
{
    const u8 *command = *pc;
    u16 offset;

    if (gScriptCallStackDepth < SCRIPT_CALL_STACK_DEPTH)
    {
        gScriptReturnPcs[gScriptCallStackDepth] = command + 2;
        offset = SCRIPT_ENTRY_OFFSETS[command[1]];
        gScriptCallStackDepth++;
        *pc = (const u8 *)SCRIPT_BYTECODE + offset;
    }
    else
        *pc = command + 2;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 保留原 ROM 的空命令入口
 *
 * @note 操作码 05；只有 bx lr，不推进 PC，也不设置返回寄存器；正常派发时保留 PC 指针的低半字。
 * @address 0x080528C4
 */
u16 ScriptCmd_Reserved(const u8 **pc)
{
    (void)pc;
}

/**
 * @brief 修改文本矩形内的图块编号并保留调色板和翻转位
 *
 * @note 操作码 23；参数为图块编号、X、Y、宽、高；保留各格高六位；PC += 7，末参数为 1 时返回 0，否则返回 1。
 * @address 0x080528C8
 */
u16 ScriptCmd_SetTextRectTiles(const u8 **pc)
{
    const u8 *p = *pc;
    u8 tile = *++p;
    u8 x = *++p;
    u8 y = *++p;
    u8 width = *++p;
    u8 height = *++p;
    u8 flag = *++p;

    Battle_SetTilemapTiles(gScriptTextTilemap, tile, x, y, width, height);
    *pc += 7;

    if (flag == 1)
        return SCRIPT_RESULT_YIELD;

    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 关闭 BG0 文本层并请求恢复默认字形
 *
 * @note 操作码 25；保留持久层标志，不把它一起清零；PC += 1，返回 0。
 * @address 0x0805291C
 */
u16 ScriptCmd_DisableTextLayer(const u8 **pc)
{
    u16 fill;

    {
        u16 *source = &fill;
        *source = 0xB000;
        DmaSet(3, source, gScriptTextTilemap,
            (u32)(DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS) | (sizeof(gScriptTextTilemap) / 2));
    }
    DmaWait(3);

    {
        u16 *source = &fill;
        *source = 0;
        DmaSet(3, source, BG_SCREEN_ADDR(31),
            (u32)(DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS) | (sizeof(gScriptTextTilemap) / 2));
    }
    DmaWait(3);

    (*(vu16 *)REG_DISPCNT) &= 0xFEFF;
    gScriptFlags |= SCRIPT_FLAG_DEFAULT_GLYPHS_PENDING;

    *pc += 1;
    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 用共享字节计数器等待指定更新次数
 *
 * @note 操作码 26；首次初始化，随后计数，最后完成；时长 N 共需 N + 2 次调用，零时长也需两次。
 * @address 0x080529B8
 */
u16 ScriptCmd_WaitFrames(const u8 **pc)
{
    const u8 *command = *pc;
    u16 result = SCRIPT_RESULT_YIELD;
    u8 duration;

    command++;
    duration = *command;

    if (!(gScriptFlags & SCRIPT_FLAG_WAITING_FRAMES))
    {
        gScriptWaitCounter = 0;
        gScriptFlags |= SCRIPT_FLAG_WAITING_FRAMES;
    }
    else if (gScriptWaitCounter < duration)
        gScriptWaitCounter++;
    else
    {
        gScriptWaitCounter = 0;
        gScriptFlags &= ~SCRIPT_FLAG_WAITING_FRAMES;
        *pc += 2;
        result = SCRIPT_RESULT_CONTINUE;
    }
    return result;
}

/**
 * @brief 播放指定 BGM 并设置音量
 *
 * @note 操作码 34；参数为曲目字节和音量半字；特殊曲目映射及音量下限交由声音服务，PC += 4，返回 0。
 * @address 0x08052A14
 */
u16 ScriptCmd_PlayBgm(const u8 **pc)
{
    const u8 *command = *pc;
    u16 high = command[3];
    u16 volume = command[2];

    volume |= high << 8;

    Sound_PlayBgm(command[1], volume);
    *pc += 4;
    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 停止当前 BGM
 *
 * @note 操作码 35；PC += 1，返回 0，不在此处改变脚本运行标志。
 * @address 0x08052A38
 */
u16 ScriptCmd_StopBgm(const u8 **pc)
{
    Sound_StopBgm();
    *pc += 1;
    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 修改 BGM 音量
 *
 * @note 操作码 36；只读取 +2/+3 的音量半字，+1 未使用；PC += 4，返回 0。
 * @address 0x08052A50
 */
u16 ScriptCmd_SetBgmVolume(const u8 **pc)
{
    const u8 *command = *pc;
    u16 high = command[3];
    u16 volume = command[2];

    volume |= high << 8;

    Sound_SetBgmVolume(volume);
    *pc += 4;
    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 请求 BGM 按指定时长淡入
 *
 * @note 操作码 37；时长参数为字节，淡入由声音更新器推进；PC += 2，返回 0。
 * @address 0x08052A70
 */
u16 ScriptCmd_FadeInBgm(const u8 **pc)
{
    Sound_FadeInBgm((*pc)[1]);
    *pc += 2;
    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 请求 BGM 按指定时长淡出
 *
 * @note 操作码 38；时长参数为字节，淡出由声音更新器推进；PC += 2，返回 0。
 * @address 0x08052A8C
 */
u16 ScriptCmd_FadeOutBgm(const u8 **pc)
{
    Sound_FadeOutBgm((*pc)[1]);
    *pc += 2;
    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 在指定音效槽播放音效并设置循环状态
 *
 * @note 操作码 39；参数为音效编号、槽号、循环字节；PC += 4，返回 0。
 * @address 0x08052AA8
 */
u16 ScriptCmd_PlaySfx(const u8 **pc)
{
    const u8 *command = *pc;

    Sound_PlaySfx(command[1], command[2], command[3] != 0);
    *pc += 4;
    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 停止指定槽位的音效
 *
 * @note 操作码 3A；槽号为一个字节；PC += 2，返回 0。
 * @address 0x08052ACC
 */
u16 ScriptCmd_StopSfx(const u8 **pc)
{
    Sound_StopSfx((*pc)[1]);
    *pc += 2;
    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 在给定入口编号闭区间内随机选择跳转目标
 *
 * @note 操作码 4E；结束编号不大于起始编号时固定用起始入口；返回 1。
 * @address 0x08052AE8
 */
u16 ScriptCmd_JumpRandom(const u8 **pc)
{
    const u8 *command = *pc;
    u16 *entryOffsets = SCRIPT_ENTRY_OFFSETS;
    u8 firstEntry = command[1];
    u16 offset = entryOffsets[firstEntry];
    u8 lastEntry = command[2];

    if (firstEntry < lastEntry)
    {
        u8 range = lastEntry - firstEntry;
        u32 random = Battle_NextRandom();
        u32 choice = random % (range + 1);
        u8 entry = command[1] + choice;

        offset = entryOffsets[entry];
    }

    *pc = offset + (const u8 *)SCRIPT_BYTECODE;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 通过备用操作码执行普通脚本调用
 *
 * @note 操作码 4F；与操作码 02 共用八层返回栈及满栈跳过规则，保留独立 ROM 入口，返回 1。
 * @address 0x08052B34
 */
u16 ScriptCmd_CallAlternate(const u8 **pc)
{
    const u8 *command = *pc;
    u16 offset;

    if (gScriptCallStackDepth < SCRIPT_CALL_STACK_DEPTH)
    {
        gScriptReturnPcs[gScriptCallStackDepth] = command + 2;
        offset = SCRIPT_ENTRY_OFFSETS[command[1]];
        gScriptCallStackDepth++;
        *pc = (const u8 *)SCRIPT_BYTECODE + offset;
    }
    else
        *pc = command + 2;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 等待持有精灵的角色全部停止执行命令
 *
 * @note 操作码 07；查询角色命令活动位；活动时保持 PC 并返回 0，全部结束后前进一字节并返回 1。
 * @address 0x08052B80
 */
u16 ScriptCmd_WaitActorCommands(const u8 **pc)
{
    if (Actor_HasActiveCommands() == 0)
    {
        *pc += 1;
        return SCRIPT_RESULT_CONTINUE;
    }

    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 请求角色图形槽加载，或设置相机区域边界
 *
 * @note 操作码 08；slot 为 FF 时把资源半字解释为相机区域编号；其他值交给角色加载器，PC += 4，返回 0。
 * @address 0x08052BA0
 */
u16 ScriptCmd_RequestActorGraphics(const u8 **pc)
{
    const u8 *command = *pc;

    if (command[1] == 0xFF)
    {
        gSceneCameraRegionId = command[2] + (command[3] << 8);
        Camera_SetBounds(gSceneCameraRegionId);
    }
    else
    {
        Actor_RequestGraphicsLoad(command[1], command[2] | (command[3] << 8));
    }

    *pc += 4;
    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 请求加载角色调色板槽
 *
 * @note 操作码 09；参数为槽号和逐字节解码的资源编号；设置待加载状态后 PC += 4，返回 0。
 * @address 0x08052BE0
 */
u16 ScriptCmd_RequestActorPalette(const u8 **pc)
{
    const u8 *command = *pc;

    Actor_RequestPaletteLoad(command[1], command[2] | (command[3] << 8));
    *pc += 4;
    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 等待角色图形和调色板请求全部处理完毕
 *
 * @note 操作码 0A；待加载位非零时返回 0；全部清零后 PC += 1，返回 1。
 * @address 0x08052C04
 */
u16 ScriptCmd_WaitActorResources(const u8 **pc)
{
    if (Actor_GetResourceLoadFlags() == 0)
    {
        *pc += 1;
        return SCRIPT_RESULT_CONTINUE;
    }

    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 按参数模式启动画面淡出
 *
 * @note 操作码 0B；模式 0、1、2 选择不同过渡，模式 2 还请求 BGM 淡出；PC += 2，返回 1。
 * @address 0x08052C24
 */
u16 ScriptCmd_FadeScreenOut(const u8 **pc)
{
    const u8 *command = *pc;

    Palette_SaveTransitionSource();

    switch (command[1])
    {
    case 0:
        gSceneBgmControl = 0xFF;
        Transition_Start(4);
        break;

    case 1:
        gSceneBgmControl = 0xFF;
        Transition_Start(7);
        break;

    case 2:
        gSceneBgmControl = 0xFF;
        Sound_FadeOutBgm(0x2E);
        Transition_Start(4);
        break;
    }

    *pc += 2;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 按参数模式启动画面淡入
 *
 * @note 操作码 0C；保留模式 0、1、2 的原过渡选择；PC += 2，返回 1。
 * @address 0x08052C90
 */
u16 ScriptCmd_FadeScreenIn(const u8 **pc)
{
    const u8 *command = *pc;

    switch (command[1])
    {
    case 0:
        Palette_SaveTransitionSource();
        /* fallthrough */

    case 1:
        Transition_Start(3);
        break;

    case 2:
        Palette_SaveTransitionSource();
        Transition_Start(7);
        break;
    }

    *pc += 2;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 等待画面过渡状态归零
 *
 * @note 操作码 0D；过渡中保留 PC 并返回 0；结束后前进一字节并返回 1。
 * @address 0x08052CD0
 */
u16 ScriptCmd_WaitScreenTransition(const u8 **pc)
{
    if (gScreenTransitionType == 0)
    {
        *pc += 1;
        return SCRIPT_RESULT_CONTINUE;
    }

    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 填写地图入口参数并请求场景切换
 *
 * @note 操作码 0E；依次写场景、相机区域、八像素单位的 X/Y 和朝向，选择场景状态 2；PC += 7，返回 0。
 * @address 0x08052CF0
 */
u16 ScriptCmd_ChangeMap(const u8 **pc)
{
    const u8 *command = *pc;

    gPendingSceneId = command[1];
    gSceneCameraRegionId = command[2] + (command[3] << 8);
    gSceneEntryTileX = command[4];
    gSceneEntryTileY = command[5];
    gSceneEntryDirection = command[6];
    gSceneState = 2;
    gVBlankUpdateMode = 1;

    *pc += 7;
    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 事件标志置位时跳到指定脚本入口
 *
 * @note 操作码 0F；用原位掩码判断非零；未置位时 PC += 4，两个分支均返回 1。
 * @address 0x08052D4C
 */
u16 ScriptCmd_JumpIfEventFlagSet(const u8 **pc)
{
    const u8 *command = *pc;

    if (EventFlags_GetMask(command[1] | (command[2] << 8)) != 0)
        *pc = SCRIPT_ENTRY_OFFSETS[command[3]] + (const u8 *)SCRIPT_BYTECODE;
    else
        *pc += 4;

    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 设置一个剧情事件标志
 *
 * @note 操作码 10；从 +1/+2 解码事件编号，保留同字节的其他位；PC += 3，返回 1。
 * @address 0x08052D8C
 */
u16 ScriptCmd_SetEventFlag(const u8 **pc)
{
    const u8 *command = *pc;

    EventFlags_Set(command[1] | (command[2] << 8));
    *pc += 3;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 清除一个剧情事件标志
 *
 * @note 操作码 11；从 +1/+2 解码事件编号，保留同字节的其他位；PC += 3，返回 1。
 * @address 0x08052DAC
 */
u16 ScriptCmd_ClearEventFlag(const u8 **pc)
{
    const u8 *command = *pc;

    EventFlags_Clear(command[1] | (command[2] << 8));
    *pc += 3;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 系统标志置位时跳到指定脚本入口
 *
 * @note 操作码 12；系统表包含持久前缀，不能统称临时标志；未置位时 PC += 4，返回 1。
 * @address 0x08052DCC
 */
u16 ScriptCmd_JumpIfSystemFlagSet(const u8 **pc)
{
    const u8 *command = *pc;

    if (SystemFlags_GetMask(command[1] | (command[2] << 8)) != 0)
        *pc = SCRIPT_ENTRY_OFFSETS[command[3]] + (const u8 *)SCRIPT_BYTECODE;
    else
        *pc += 4;

    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 设置一个系统标志
 *
 * @note 操作码 13；编号直接属于系统位图，此命令不减去 512；PC += 3，返回 1。
 * @address 0x08052E0C
 */
u16 ScriptCmd_SetSystemFlag(const u8 **pc)
{
    const u8 *command = *pc;

    SystemFlags_Set(command[1] | (command[2] << 8));
    *pc += 3;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 清除一个系统标志
 *
 * @note 操作码 14；编号直接属于系统位图，此命令不减去 512；PC += 3，返回 1。
 * @address 0x08052E2C
 */
u16 ScriptCmd_ClearSystemFlag(const u8 **pc)
{
    const u8 *command = *pc;

    SystemFlags_Clear(command[1] | (command[2] << 8));
    *pc += 3;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 暂停相机对玩家的自动跟随
 *
 * @note 操作码 18；设置跟随禁用字节，同时清背景移动时长；PC += 1，返回 1。
 * @address 0x08052E4C
 */
u16 ScriptCmd_DisableCameraFollow(const u8 **pc)
{
    gCameraFollowDisabled = 1;
    *(u8 *)&gBackgroundMoveDuration = 0;
    *pc += 1;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 恢复相机对玩家的自动跟随
 *
 * @note 操作码 19；只清跟随禁用字节，不重置其他移动参数；PC += 1，返回 1。
 * @address 0x08052E6C
 */
u16 ScriptCmd_EnableCameraFollow(const u8 **pc)
{
    gCameraFollowDisabled = 0;
    *pc += 1;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 等待镜头或背景移动时长的低字节归零
 *
 * @note 操作码 1B；不查询累计帧数或高字节；等待时返回 0，结束后 PC += 1 并返回 1。
 * @address 0x08052E80
 */
u16 ScriptCmd_WaitCameraPan(const u8 **pc)
{
    if (*(u8 *)&gBackgroundMoveDuration != 0)
        return SCRIPT_RESULT_YIELD;

    *pc += 1;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 将自定义动画资源载入指定动画槽和绘制槽
 *
 * @note 操作码 1E；参数顺序是资源半字、动画槽、绘制槽；调用 Actor_LoadAnimation，PC += 5，返回 0。
 * @address 0x08052E9C
 */
u16 ScriptCmd_LoadActorAnimation(const u8 **pc)
{
    const u8 *command = *pc;

    Actor_LoadAnimation(command[1] | (command[2] << 8), command[3], command[4]);
    *pc += 5;
    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 重建角色精灵链并启动指定自定义动画槽
 *
 * @note 操作码 1F；先释放旧链，再尝试分配节点；有可用节点才启动动画，PC += 3，返回 0。
 * @address 0x08052EC0
 */
u16 ScriptCmd_StartActorAnimation(const u8 **pc)
{
    const u8 *command = *pc;
    u8 nodeId;

    if (gActorStates[command[1]].spriteSlot != 0)
        Sprite_FreeChain(&gSpriteNodes[gActorStates[command[1]].spriteSlot]);

    nodeId = Sprite_FindFreeNode();
    if (nodeId < 0x70)
    {
        gActorStates[command[1]].spriteSlot = nodeId;
        Actor_StartCustomAnimation(command[1], command[2]);
    }

    *pc += 3;
    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 等待角色自定义动画完成，或按编码强制结束
 *
 * @note 操作码 20；角色编号加 100 表示强制结束；普通查询也会清动画槽 bit7；完成后 PC += 2，返回 1。
 * @address 0x08052F20
 */
u16 ScriptCmd_WaitActorAnimation(const u8 **pc)
{
    if (Actor_FinishCustomAnimation((*pc)[1]) != 0)
    {
        *pc += 2;
        return SCRIPT_RESULT_CONTINUE;
    }

    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 队伍成员的存在状态符合参数时跳转
 *
 * @note 操作码 21；首槽命中记 1，否则累加后四槽匹配数并与 +2 比较；正常无重复队伍可用 0/1 测试缺席或存在。
 * @address 0x08052F44
 */
u16 ScriptCmd_JumpIfPartyMembershipMatches(const u8 **pc)
{
    const u8 *command = *pc;
    u8 count = 0;
    u16 i = 0;

    if (gPartyMembers[0] == command[1])
        count = 1;
    else
        for (; i <= 4; i++)
            if (gPartyMembers[i] == command[1])
            {
                count++;
                break;
            }

    if (count == command[2])
        *pc = SCRIPT_ENTRY_OFFSETS[command[3]] + (const u8 *)SCRIPT_BYTECODE;
    else
        *pc += 4;

    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 从指定起始槽加载一组地图动画
 *
 * @note 操作码 27；参数顺序是组编号、起始槽，与调色板组命令相反；PC += 3，返回 1。
 * @address 0x08052FAC
 */
u16 ScriptCmd_LoadMapAnimationGroup(const u8 **pc)
{
    MapAnimation_LoadGroup((*pc)[1], (*pc)[2]);
    *pc += 3;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 恢复地图动画槽的计时更新
 *
 * @note 操作码 28；清暂停位 02，保留其余标志和当前进度；PC += 2，返回 1。
 * @address 0x08052FC8
 */
u16 ScriptCmd_ResumeMapAnimation(const u8 **pc)
{
    MapAnimation_Resume((*pc)[1]);
    *pc += 2;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 暂停地图动画槽的计时更新
 *
 * @note 操作码 29；置暂停位 02，不卸载动画、不清所属地图层；PC += 2，返回 1。
 * @address 0x08052FE4
 */
u16 ScriptCmd_PauseMapAnimation(const u8 **pc)
{
    MapAnimation_Pause((*pc)[1]);
    *pc += 2;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 等待地图动画槽的 layer 字段变为零
 *
 * @note 操作码 2A；暂停并不满足此条件；layer 非零时保留 PC，归零后 PC += 2，返回 1。
 * @address 0x08053000
 */
u16 ScriptCmd_WaitMapAnimation(const u8 **pc)
{
    if (MapAnimation_GetLayer((*pc)[1]) == 0)
    {
        *pc += 2;
        return SCRIPT_RESULT_CONTINUE;
    }

    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 从指定起始槽加载一组调色板动画
 *
 * @note 操作码 2B；参数顺序是起始槽、组编号；PC += 3，返回 1。
 * @address 0x08053024
 */
u16 ScriptCmd_LoadPaletteAnimationGroup(const u8 **pc)
{
    Palette_LoadAnimationGroupAt((*pc)[1], (*pc)[2]);
    *pc += 3;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 恢复调色板动画槽的计时更新
 *
 * @note 操作码 2C；清暂停位 04，保留其他标志和当前进度；PC += 2，返回 1。
 * @address 0x08053040
 */
u16 ScriptCmd_ResumePaletteAnimation(const u8 **pc)
{
    Palette_ResumeAnimation((*pc)[1]);
    *pc += 2;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 暂停调色板动画槽的计时更新
 *
 * @note 操作码 2D；置暂停位 04，不清整个状态字节；PC += 2，返回 1。
 * @address 0x0805305C
 */
u16 ScriptCmd_PausePaletteAnimation(const u8 **pc)
{
    Palette_PauseAnimation((*pc)[1]);
    *pc += 2;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 等待调色板动画槽的全部标志归零
 *
 * @note 操作码 2E；仅暂停不会结束等待；归零后 PC += 2，返回 1。
 * @address 0x08053078
 */
u16 ScriptCmd_WaitPaletteAnimation(const u8 **pc)
{
    if (Palette_GetAnimationFlags((*pc)[1]) == 0)
    {
        *pc += 2;
        return SCRIPT_RESULT_CONTINUE;
    }

    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 恢复队伍成员的 HP 和 MP
 *
 * @note 操作码 2F；由 Party_RestoreHpMp 使用角色最大值；PC += 1，返回 1。
 * @address 0x0805309C
 */
u16 ScriptCmd_RestorePartyHpMp(const u8 **pc)
{
    Party_RestoreHpMp();
    *pc += 1;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 为指定角色设置一件装备
 *
 * @note 操作码 30；参数依次为角色编号、物品编号、装备槽；继承 Party_SetEquipment 的库存副作用，PC += 4。
 * @address 0x080530B4
 */
u16 ScriptCmd_SetEquipment(const u8 **pc)
{
    const u8 *command = *pc;

    Party_SetEquipment(command[1], command[2], command[3]);
    *pc += 4;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 按命令编码增加或减少物品数量
 *
 * @note 操作码 31；数量编码不大于 100 时增加，超过 100 时减去编码减 100；数量上下限由队伍服务处理。
 * @address 0x080530D4
 */
u16 ScriptCmd_ChangeItemCount(const u8 **pc)
{
    const u8 *command = *pc;

    if (command[2] > 100)
        Party_RemoveItem(command[1], command[2] - 100);
    else
        Party_AddItem(command[1], command[2]);

    *pc += 3;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 增加或扣除指定数量的金钱
 *
 * @note 操作码 32；方向字节非零为增加、零为扣除；金额为小端半字，PC += 4，返回 1。
 * @address 0x08053104
 */
u16 ScriptCmd_ChangeGold(const u8 **pc)
{
    const u8 *command = *pc;

    if (command[1] != 0)
        Party_AddGold(command[2] + (command[3] << 8));
    else
        Party_RemoveGold(command[2] + (command[3] << 8));

    *pc += 4;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 物品数量达到 99 时跳到指定入口
 *
 * @note 操作码 3B；未达到上限则 PC += 3；两个分支均返回 1。
 * @address 0x08053138
 */
u16 ScriptCmd_JumpIfItemCountFull(const u8 **pc)
{
    const u8 *command = *pc;
    s32 offset = command[1];

    if (*(const u8 *)(offset + (u32)gPartyItemCounts) <= PARTY_MAX_ITEM_COUNT - 1)
        *pc += 3;
    else
        *pc = SCRIPT_ENTRY_OFFSETS[command[2]] + (const u8 *)SCRIPT_BYTECODE;

    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 再次切换最近操作的场景宝箱
 *
 * @note 操作码 3C；保存值是场景实例槽；Chest_Toggle 再从实例读取持久编号并同步开闭位与精灵，返回 0。
 * @address 0x0805316C
 */
u16 ScriptCmd_ToggleLastChest(const u8 **pc)
{
    Chest_Toggle(gLastToggledChestId);
    *pc += 1;
    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 按商店编号打开买卖菜单
 *
 * @note 操作码 3D；调用 Shop_Open；此处不是收藏菜单入口，PC += 2，返回 0。
 * @address 0x0805318C
 */
u16 ScriptCmd_OpenShop(const u8 **pc)
{
    Shop_Open((*pc)[1]);
    *pc += 2;
    return SCRIPT_RESULT_YIELD;
}

/* Value parameters reproduce the reference argument and register ordering. */
static inline u16 Script_WaitShopResultInline(const u8 **pc, const u8 *command)
{
    if (gShopActive == 0)
    {
        if (gShopTransactionOccurred == 0)
            *pc += 2;
        else
            *pc = SCRIPT_ENTRY_OFFSETS[command[1]] + (const u8 *)SCRIPT_BYTECODE;
    }

    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 等待商店关闭并按是否发生交易选择脚本入口
 *
 * @note 操作码 3E；营业中保持 PC；关闭后有交易则跳转，无交易则 PC += 2；所有路径返回 0。
 * @address 0x080531A8
 */
u16 ScriptCmd_WaitShopResult(const u8 **pc)
{
    return Script_WaitShopResultInline(pc, *pc);
}

/**
 * @brief 增加一张收藏卡片
 *
 * @note 操作码 3F；卡片数量按半字节存放，服务将数量封顶为 5 并保留相邻卡片；PC += 2，返回 1。
 * @address 0x080531E4
 */
u16 ScriptCmd_AddCard(const u8 **pc)
{
    Collection_AddCard((*pc)[1]);
    *pc += 2;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 减少一张收藏卡片
 *
 * @note 操作码 40；卡片数量最低为零，保留同字节内另一张卡的数量；PC += 2，返回 1。
 * @address 0x08053200
 */
u16 ScriptCmd_RemoveCard(const u8 **pc)
{
    Collection_RemoveCard((*pc)[1]);
    *pc += 2;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 没有指定收藏卡片时跳到另一脚本入口
 *
 * @note 操作码 41；检查卡片数量是否为零；已有卡片则 PC += 3，返回 1。
 * @address 0x0805321C
 */
u16 ScriptCmd_JumpIfCardMissing(const u8 **pc)
{
    const u8 *command = *pc;

    if (Collection_GetCardCount(command[1]) != 0)
        *pc += 3;
    else
        *pc = SCRIPT_ENTRY_OFFSETS[command[2]] + (const u8 *)SCRIPT_BYTECODE;

    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 设置收藏系统的一个事件标志
 *
 * @note 操作码 42；参数属于独立的 64 位收藏事件表；PC += 2，返回 1。
 * @address 0x08053254
 */
u16 ScriptCmd_SetCollectionEventFlag(const u8 **pc)
{
    Collection_SetEventFlag((*pc)[1]);
    *pc += 2;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 按编号列表设置剧情或系统标志
 *
 * @note 操作码 43；读取载荷字节数除以 2 个编号；小于 512 访问剧情表，否则减 512 访问系统表；返回 1。
 * @address 0x08053270
 */
u16 ScriptCmd_SetFlagList(const u8 **pc)
{
    const u8 *command = *pc;
    u8 payloadBytes = command[1];
    u16 index;
    u16 flagId;

    for (index = 0; payloadBytes / 2 > index; index++)
    {
        flagId = command[index * 2 + 2] | (command[index * 2 + 3] << 8);
        if (flagId >= 512)
            SystemFlags_Set(flagId - 512);
        else
            EventFlags_Set(flagId);
    }

    *pc += payloadBytes + 2;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 按编号列表清除剧情或系统标志
 *
 * @note 操作码 44；编号命名空间与 SetFlagList 相同；PC 跳过整个声明载荷，返回 1。
 * @address 0x080532DC
 */
u16 ScriptCmd_ClearFlagList(const u8 **pc)
{
    const u8 *command = *pc;
    u8 payloadBytes = command[1];
    u16 index;
    u16 flagId;

    for (index = 0; payloadBytes / 2 > index; index++)
    {
        flagId = command[index * 2 + 2] | (command[index * 2 + 3] << 8);
        if (flagId >= 512)
            SystemFlags_Clear(flagId - 512);
        else
            EventFlags_Clear(flagId);
    }

    *pc += payloadBytes + 2;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 清除系统标志位图的局部后缀
 *
 * @note 操作码 48；只清 byte 61..79，即 bits 488..639，保留此前的持久前缀；PC += 1，返回 1。
 * @address 0x08053348
 */
u16 ScriptCmd_ClearLocalSystemFlags(const u8 **pc)
{
    SystemFlags_ClearLocal();
    *pc += 1;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 金钱严格大于指定阈值时跳转
 *
 * @note 操作码 49；以无符号 u32 金钱比较小端 u16 阈值；相等时不跳转，返回 1。
 * @address 0x08053360
 */
u16 ScriptCmd_JumpIfGoldGreater(const u8 **pc)
{
    const u8 *command = *pc;
    const u8 *target;

    if (gPartyGold > command[2] + (command[3] << 8))
        target = SCRIPT_ENTRY_OFFSETS[command[1]] + (const u8 *)SCRIPT_BYTECODE;
    else
        target = command + 4;

    *pc = target;
    return SCRIPT_RESULT_CONTINUE;
}

/**
 * @brief 启动标题徽标展示状态机
 *
 * @note 操作码 4A；把标题徽标阶段设为 1；PC += 1，返回 0。
 * @address 0x080533A0
 */
u16 ScriptCmd_StartTitleLogo(const u8 **pc)
{
    gTitleLogoStage = 1;
    *pc += 1;
    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 等待标题徽标展示状态机归零
 *
 * @note 操作码 4B；展示期间保持 PC 并返回 0；结束后 PC += 1，返回 1。
 * @address 0x080533B4
 */
u16 ScriptCmd_WaitTitleLogo(const u8 **pc)
{
    if (gTitleLogoStage == 0)
    {
        *pc += 1;
        return SCRIPT_RESULT_CONTINUE;
    }

    return SCRIPT_RESULT_YIELD;
}

/**
 * @brief 把指定角色提升到目标等级
 *
 * @note 操作码 4C；参数顺序是目标等级、角色编号；目标为显示等级，0 沿用首角色内部等级，并非升级次数。
 * @address 0x080533D4
 */
u16 ScriptCmd_RaiseCharacterLevel(const u8 **pc)
{
    Party_RaiseCharacterLevel((*pc)[1], (*pc)[2]);
    *pc += 3;
    return SCRIPT_RESULT_CONTINUE;
}

/* Split read-only data, in ROM address order. */

/* ROM 0x0808B1B4..0x0808B1D4; .rodata. */
const u16 D_0808B1B4[16] __attribute__((aligned(1))) =
{
    0x7FFF, 0x3964, 0x3205, 0x0EA1, 0x12C2, 0x1AE5, 0x5E6B, 0x2328, 0x2B69, 0x6AEF, 0x2F8C, 0x37AE, 0x3FB1,
    0x7B72, 0x7FF8, 0x2CE2,
};

/* ROM 0x0862D434..0x0862D856; .rodata.0062D434. */
ScriptCommandHandler const gScriptCommandTable[SCRIPT_COMMAND_COUNT] __attribute__((section(".rodata.0062D434"), aligned(1))) =
{
    (ScriptCommandHandler)ScriptCmd_ShowDialogue, (ScriptCommandHandler)ScriptCmd_Jump,
    (ScriptCommandHandler)ScriptCmd_Call, (ScriptCommandHandler)ScriptCmd_Return,
    (ScriptCommandHandler)ScriptCmd_RunActorCommands, (ScriptCommandHandler)ScriptCmd_Reserved,
    (ScriptCommandHandler)ScriptCmd_End, (ScriptCommandHandler)ScriptCmd_WaitActorCommands,
    (ScriptCommandHandler)ScriptCmd_RequestActorGraphics, (ScriptCommandHandler)ScriptCmd_RequestActorPalette,
    (ScriptCommandHandler)ScriptCmd_WaitActorResources, (ScriptCommandHandler)ScriptCmd_FadeScreenOut,
    (ScriptCommandHandler)ScriptCmd_FadeScreenIn, (ScriptCommandHandler)ScriptCmd_WaitScreenTransition,
    (ScriptCommandHandler)ScriptCmd_ChangeMap, (ScriptCommandHandler)ScriptCmd_JumpIfEventFlagSet,
    (ScriptCommandHandler)ScriptCmd_SetEventFlag, (ScriptCommandHandler)ScriptCmd_ClearEventFlag,
    (ScriptCommandHandler)ScriptCmd_JumpIfSystemFlagSet, (ScriptCommandHandler)ScriptCmd_SetSystemFlag,
    (ScriptCommandHandler)ScriptCmd_ClearSystemFlag, (ScriptCommandHandler)ScriptCmd_CallBank,
    (ScriptCommandHandler)ScriptCmd_ReturnBank, (ScriptCommandHandler)ScriptCmd_DrawText,
    (ScriptCommandHandler)ScriptCmd_DisableCameraFollow, (ScriptCommandHandler)ScriptCmd_EnableCameraFollow,
    (ScriptCommandHandler)ScriptCmd_PanCamera, (ScriptCommandHandler)ScriptCmd_WaitCameraPan,
    (ScriptCommandHandler)ScriptCmd_RemovePartyMember, (ScriptCommandHandler)ScriptCmd_AddPartyMember,
    (ScriptCommandHandler)ScriptCmd_LoadActorAnimation, (ScriptCommandHandler)ScriptCmd_StartActorAnimation,
    (ScriptCommandHandler)ScriptCmd_WaitActorAnimation,
    (ScriptCommandHandler)ScriptCmd_JumpIfPartyMembershipMatches, (ScriptCommandHandler)ScriptCmd_StartBattle,
    (ScriptCommandHandler)ScriptCmd_SetTextRectTiles, (ScriptCommandHandler)ScriptCmd_EnableTextLayer,
    (ScriptCommandHandler)ScriptCmd_DisableTextLayer, (ScriptCommandHandler)ScriptCmd_WaitFrames,
    (ScriptCommandHandler)ScriptCmd_LoadMapAnimationGroup, (ScriptCommandHandler)ScriptCmd_ResumeMapAnimation,
    (ScriptCommandHandler)ScriptCmd_PauseMapAnimation, (ScriptCommandHandler)ScriptCmd_WaitMapAnimation,
    (ScriptCommandHandler)ScriptCmd_LoadPaletteAnimationGroup,
    (ScriptCommandHandler)ScriptCmd_ResumePaletteAnimation,
    (ScriptCommandHandler)ScriptCmd_PausePaletteAnimation,
    (ScriptCommandHandler)ScriptCmd_WaitPaletteAnimation, (ScriptCommandHandler)ScriptCmd_RestorePartyHpMp,
    (ScriptCommandHandler)ScriptCmd_SetEquipment, (ScriptCommandHandler)ScriptCmd_ChangeItemCount,
    (ScriptCommandHandler)ScriptCmd_ChangeGold, (ScriptCommandHandler)ScriptCmd_ShowChoices,
    (ScriptCommandHandler)ScriptCmd_PlayBgm, (ScriptCommandHandler)ScriptCmd_StopBgm,
    (ScriptCommandHandler)ScriptCmd_SetBgmVolume, (ScriptCommandHandler)ScriptCmd_FadeInBgm,
    (ScriptCommandHandler)ScriptCmd_FadeOutBgm, (ScriptCommandHandler)ScriptCmd_PlaySfx,
    (ScriptCommandHandler)ScriptCmd_StopSfx, (ScriptCommandHandler)ScriptCmd_JumpIfItemCountFull,
    (ScriptCommandHandler)ScriptCmd_ToggleLastChest, (ScriptCommandHandler)ScriptCmd_OpenShop,
    (ScriptCommandHandler)ScriptCmd_WaitShopResult, (ScriptCommandHandler)ScriptCmd_AddCard,
    (ScriptCommandHandler)ScriptCmd_RemoveCard, (ScriptCommandHandler)ScriptCmd_JumpIfCardMissing,
    (ScriptCommandHandler)ScriptCmd_SetCollectionEventFlag, (ScriptCommandHandler)ScriptCmd_SetFlagList,
    (ScriptCommandHandler)ScriptCmd_ClearFlagList, (ScriptCommandHandler)ScriptCmd_JumpIfAllFlagsSet,
    (ScriptCommandHandler)ScriptCmd_JumpIfAllFlagsClear, (ScriptCommandHandler)ScriptCmd_JumpIfAnyFlagSet,
    (ScriptCommandHandler)ScriptCmd_ClearLocalSystemFlags, (ScriptCommandHandler)ScriptCmd_JumpIfGoldGreater,
    (ScriptCommandHandler)ScriptCmd_StartTitleLogo, (ScriptCommandHandler)ScriptCmd_WaitTitleLogo,
    (ScriptCommandHandler)ScriptCmd_RaiseCharacterLevel, (ScriptCommandHandler)ScriptCmd_ControlSceneEffect,
    (ScriptCommandHandler)ScriptCmd_JumpRandom, (ScriptCommandHandler)ScriptCmd_CallAlternate,
};

const struct ScriptName gScriptNames[SCRIPT_NAME_COUNT] __attribute__((section(".rodata.0062D434"), aligned(1))) =
{
#include "lang/jp/script_names.h"
};

/* ROM 0x087ED6D4..0x087ED910; .rodata.007ED6D4. */
const struct LzStreamHeader *const gScriptBankTable[SCRIPT_BANK_COUNT] __attribute__((section(".rodata.007ED6D4"), aligned(1))) =
{
    (const struct LzStreamHeader *)0x0862D8A4, (const struct LzStreamHeader *)0x0862E2A0,
    (const struct LzStreamHeader *)0x08631CE8, (const struct LzStreamHeader *)0x08632B38,
    (const struct LzStreamHeader *)0x08634F20, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x08638C6C, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0863D240, (const struct LzStreamHeader *)0x0863DAAC,
    (const struct LzStreamHeader *)0x0863E5A4, (const struct LzStreamHeader *)0x086413B0,
    (const struct LzStreamHeader *)0x08642A58, (const struct LzStreamHeader *)0x08644D6C,
    (const struct LzStreamHeader *)0x08648030, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x08649F44,
    (const struct LzStreamHeader *)0x0864C404, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x08650810, (const struct LzStreamHeader *)0x08650E60,
    (const struct LzStreamHeader *)0x086514F4, (const struct LzStreamHeader *)0x08653D1C,
    (const struct LzStreamHeader *)0x086542BC, (const struct LzStreamHeader *)0x08654B08,
    (const struct LzStreamHeader *)0x086578DC, (const struct LzStreamHeader *)0x08658D7C,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0865C2C0,
    (const struct LzStreamHeader *)0x0865CBE4, (const struct LzStreamHeader *)0x0865E964,
    (const struct LzStreamHeader *)0x08660004, (const struct LzStreamHeader *)0x08660084,
    (const struct LzStreamHeader *)0x086601A0, (const struct LzStreamHeader *)0x08662A78,
    (const struct LzStreamHeader *)0x08662E10, (const struct LzStreamHeader *)0x08663E6C,
    (const struct LzStreamHeader *)0x08664EF8, (const struct LzStreamHeader *)0x086666EC,
    (const struct LzStreamHeader *)0x08666BC4, (const struct LzStreamHeader *)0x08667B8C,
    (const struct LzStreamHeader *)0x0866822C, (const struct LzStreamHeader *)0x0866A5FC,
    (const struct LzStreamHeader *)0x0866CCA4, (const struct LzStreamHeader *)0x0866D73C,
    (const struct LzStreamHeader *)0x0866EC00, (const struct LzStreamHeader *)0x0866FC40,
    (const struct LzStreamHeader *)0x0866FCA8, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x08671280, (const struct LzStreamHeader *)0x08672BAC,
    (const struct LzStreamHeader *)0x086755A0, (const struct LzStreamHeader *)0x08675F24,
    (const struct LzStreamHeader *)0x086768F4, (const struct LzStreamHeader *)0x08677078,
    (const struct LzStreamHeader *)0x08677338, (const struct LzStreamHeader *)0x08677FEC,
    (const struct LzStreamHeader *)0x086782E8, (const struct LzStreamHeader *)0x0867A390,
    (const struct LzStreamHeader *)0x0867B1B0, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0867BE94, (const struct LzStreamHeader *)0x0867C3F0,
    (const struct LzStreamHeader *)0x0867D4D8, (const struct LzStreamHeader *)0x0867FE24,
    (const struct LzStreamHeader *)0x086839DC, (const struct LzStreamHeader *)0x08687A54,
    (const struct LzStreamHeader *)0x0868A36C, (const struct LzStreamHeader *)0x0868EF0C,
    (const struct LzStreamHeader *)0x086910D8, (const struct LzStreamHeader *)0x08695B34,
    (const struct LzStreamHeader *)0x08695B80, (const struct LzStreamHeader *)0x08699208,
    (const struct LzStreamHeader *)0x0869BD34, (const struct LzStreamHeader *)0x0869E39C,
    (const struct LzStreamHeader *)0x0869FE24, (const struct LzStreamHeader *)0x086A1CD8,
    (const struct LzStreamHeader *)0x086A3908, (const struct LzStreamHeader *)0x086A68D8,
    (const struct LzStreamHeader *)0x086A7230, (const struct LzStreamHeader *)0x086A7F30,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x086AA9C0,
    (const struct LzStreamHeader *)0x086AB488, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x086AC614,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
    (const struct LzStreamHeader *)0x0862D858, (const struct LzStreamHeader *)0x0862D858,
};

const struct ScriptFontResources gScriptFontResources __attribute__((section(".rodata.007ED6D4"), aligned(1))) =
{ (const u8 *)0x0861C784, (const struct ScriptGlyphAtlasRow *)0x0861CC34, (const u8 *)0x08624C34 };

/* End of split read-only data. */
