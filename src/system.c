#include "battle.h"
#include "battle_command_menu.h"
#include "serial_link.h"
#include "system.h"
#include "core.h"
#include "gba.h"
#include "sound.h"
#include "script.h"
#include "menu.h"
#include "startup.h"

#define MAP_TILEMAP_A ((u16 *)0x02004000)
#define SCENE_EFFECT_TILEMAP ((u16 *)0x02005000)

/* ROM 0x0805769C..0x08057850; linked separately from this module's .text. */
void (*const gSystemIrqHandlers[SYSTEM_IRQ_HANDLER_COUNT])(void) =
{
    MultiSioIntr,
    System_VBlank,
    System_HBlank,
    System_EmptyVCountHandler,
    System_EmptyIrqHandler,
    System_EmptyIrqHandler,
    System_EmptyIrqHandler,
    System_EmptyIrqHandler,
    System_EmptyIrqHandler,
    System_EmptyIrqHandler,
    System_EmptyIrqHandler,
    System_EmptyIrqHandler,
    System_EmptyIrqHandler,
};

/* The boot copy also reads the first four bytes of this adjacent table. */
const u8 gBg1RasterWave[128] =
{
    0x00, 0x04, 0x09, 0x0E, 0x13, 0x18, 0x1D, 0x21, 0x26, 0x2A, 0x2F, 0x33, 0x37, 0x3B, 0x3F, 0x43,
    0x46, 0x4A, 0x4D, 0x50, 0x53, 0x55, 0x58, 0x5A, 0x5C, 0x5E, 0x5F, 0x61, 0x62, 0x62, 0x63, 0x63,
    0x64, 0x63, 0x63, 0x62, 0x62, 0x61, 0x5F, 0x5E, 0x5C, 0x5A, 0x58, 0x55, 0x53, 0x50, 0x4D, 0x4A,
    0x46, 0x43, 0x3F, 0x3B, 0x37, 0x33, 0x2F, 0x2A, 0x26, 0x21, 0x1D, 0x18, 0x13, 0x0E, 0x09, 0x04,
    0x00, 0xFC, 0xF7, 0xF2, 0xED, 0xE8, 0xE3, 0xDF, 0xDA, 0xD6, 0xD1, 0xCD, 0xC9, 0xC5, 0xC1, 0xBD,
    0xBA, 0xB6, 0xB3, 0xB0, 0xAD, 0xAB, 0xA8, 0xA6, 0xA4, 0xA2, 0xA1, 0x9F, 0x9E, 0x9E, 0x9D, 0x9D,
    0x9C, 0x9D, 0x9D, 0x9E, 0x9E, 0x9F, 0xA1, 0xA2, 0xA4, 0xA6, 0xA8, 0xAB, 0xAD, 0xB0, 0xB3, 0xB6,
    0xBA, 0xBD, 0xC1, 0xC5, 0xC9, 0xCD, 0xD1, 0xD6, 0xDA, 0xDF, 0xE3, 0xE8, 0xED, 0xF2, 0xF7, 0xFC,
};

const u8 gRandomTable[256] =
{
    0xE8, 0x86, 0xE1, 0x87, 0xDF, 0x8F, 0xED, 0x82, 0x23, 0x45, 0x05, 0x49, 0x1A, 0x71, 0x0A, 0x8B,
    0x24, 0x40, 0x3D, 0x4F, 0x29, 0x36, 0x32, 0x4E, 0xFF, 0x85, 0x9F, 0x92, 0x98, 0x30, 0xF1, 0x35,
    0x2A, 0xFE, 0x79, 0xFA, 0x80, 0x89, 0x06, 0x9D, 0x43, 0x74, 0x3F, 0x8A, 0x48, 0xF5, 0x5E, 0xE9,
    0xFC, 0x13, 0x77, 0x03, 0x76, 0x61, 0x16, 0x75, 0x27, 0x3C, 0xD6, 0x4A, 0xC1, 0x02, 0xBE, 0x0C,
    0x0E, 0x4C, 0xCC, 0x34, 0xC5, 0x25, 0x00, 0x38, 0xEE, 0xA2, 0xC6, 0x14, 0xE0, 0x58, 0xEF, 0x5D,
    0x1E, 0x9E, 0xEC, 0x8C, 0xEA, 0x97, 0x1F, 0x83, 0x31, 0x91, 0x8D, 0xA4, 0x88, 0x2F, 0x5A, 0xA9,
    0x9B, 0x53, 0x21, 0x56, 0x4D, 0x78, 0x9A, 0x6F, 0x73, 0x68, 0xFD, 0x52, 0xF9, 0x6B, 0x8E, 0x5F,
    0x37, 0x41, 0x09, 0x5B, 0x08, 0x44, 0x3A, 0x5C, 0x39, 0x67, 0x42, 0x6A, 0x4B, 0x3B, 0x33, 0x1D,
    0x22, 0xAD, 0xAA, 0x94, 0x84, 0x60, 0x20, 0x65, 0x55, 0x11, 0x95, 0xA6, 0x90, 0xA0, 0x62, 0x81,
    0x59, 0x1B, 0x57, 0x2D, 0x50, 0x2E, 0x6D, 0x19, 0x99, 0x7B, 0x96, 0x66, 0xAB, 0x04, 0x93, 0xAF,
    0x9C, 0x7C, 0xAE, 0x7F, 0x28, 0x54, 0xA8, 0x6E, 0x01, 0x7D, 0x72, 0x64, 0x7E, 0x69, 0x1C, 0x63,
    0xF3, 0xD1, 0xC9, 0xD8, 0xC4, 0x12, 0xF7, 0x17, 0xA7, 0xFB, 0xDC, 0x15, 0xD7, 0xAC, 0xA1, 0x2B,
    0xB3, 0xDB, 0xB1, 0xC7, 0xC2, 0xCF, 0xB2, 0xD0, 0xE6, 0x26, 0xF6, 0xBA, 0x0F, 0xC0, 0xC3, 0xD3,
    0x0D, 0xD5, 0xD2, 0xDA, 0xB7, 0xBC, 0xF4, 0xA3, 0x07, 0xBD, 0xE3, 0xCA, 0xDE, 0xD4, 0x2C, 0xCB,
    0x6C, 0xF8, 0x10, 0x0B, 0x3E, 0xB4, 0x7A, 0xB5, 0x70, 0xE7, 0xA5, 0xD9, 0xB8, 0xE4, 0x51, 0xE5,
    0xE2, 0xB9, 0x47, 0xBF, 0x46, 0xCE, 0xDD, 0xBB, 0xC8, 0x18, 0xB6, 0xEB, 0xB0, 0xF2, 0xCD, 0xF0,
};

/* One active decoder, shared by initialization and chunk advancement. */
struct LzStreamState
{
    u8 *destination;          /* 0x00; next output byte, advanced after each write. */
    const u8 *tokenData;     /* 0x04; literals or two-byte backward-copy tokens. */
    const u8 *flagBits;      /* 0x08; one LSB-first flag per token. */
    u32 decompressedSize;   /* 0x0C; total size copied from the resource header. */
    u32 tokenIndex;         /* 0x10; chooses flag byte and bit. */
    u32 chunkBudget;        /* 0x14; clamped to remaining before each chunk. */
    u32 producedThisChunk;  /* 0x18; reset by DecodeChunk, not by Init. */
    u32 remaining;          /* 0x1C; terminal return may leave this nonzero. */
};

typedef char LzStreamStateSizeCheck[(sizeof(struct LzStreamState) == 0x20) ? 1 : -1];
typedef char LzStreamDestinationCheck[(offsetof(struct LzStreamState, destination) == 0x00) ? 1 : -1];
typedef char LzStreamTokensCheck[(offsetof(struct LzStreamState, tokenData) == 0x04) ? 1 : -1];
typedef char LzStreamFlagsCheck[(offsetof(struct LzStreamState, flagBits) == 0x08) ? 1 : -1];
typedef char LzStreamSizeCheck[(offsetof(struct LzStreamState, decompressedSize) == 0x0C) ? 1 : -1];
typedef char LzStreamIndexCheck[(offsetof(struct LzStreamState, tokenIndex) == 0x10) ? 1 : -1];
typedef char LzStreamBudgetCheck[(offsetof(struct LzStreamState, chunkBudget) == 0x14) ? 1 : -1];
typedef char LzStreamProducedCheck[(offsetof(struct LzStreamState, producedThisChunk) == 0x18) ? 1 : -1];
typedef char LzStreamRemainingCheck[(offsetof(struct LzStreamState, remaining) == 0x1C) ? 1 : -1];

/* BSS: natural .lcomm alignment gives offsets 0, 0x38 and 0x40, in address order. */
static u8 sIrqTableCopy[SYSTEM_IRQ_TABLE_COPY_BYTES];           /* 0x03001950; raw copy, including the next ROM word. */
static u16 sBg1RasterPhase;                                     /* 0x03001988; VBlank raster phase. */
static struct LzStreamState sLzStream;                          /* 0x03001990; private decoder state. */

/* COMMON: tentative definitions, in address order.
 * Single-module objects in this interleaved region retain external linkage;
 * only state with independent consumers is exposed through system.h. */
u8 gSystemFlags[SYSTEM_FLAG_BYTES];                             /* 0x030018F0 */
s32 gLinkPollResult;                                            /* 0x03001940 */
u8 gSceneState;                                                 /* 0x03001944 */
u32 gPlayTimeFrames;                                            /* 0x03001948 */
u16 gKeysHeld;                                                  /* 0x030019B0 */
u8 gBg1RasterYOffsets[SYSTEM_RASTER_TABLE_SIZE];                /* 0x030019C0; Builder leaves index 255 untouched. */
u8 gGameLoopMode;                                               /* 0x03001AC0 */
u8 gCardLinkReceiveBuffer[SYSTEM_CARD_LINK_PAYLOAD_BYTES];      /* 0x03001AD0 */
u32 gLinkServiceState;                                          /* 0x03001B50 */
u8 gBg1RasterLineOffset;                                        /* 0x03001B54 */
u8 gBg1RasterXOffsets[SYSTEM_RASTER_TABLE_SIZE];                /* 0x03001B60; Read index wraps at 256. */
u8 gEventFlags[SYSTEM_EVENT_FLAG_BYTES];                        /* 0x03001C60 */
u16 gKeysPressed;                                               /* 0x03001CA0 */
u8 gCardLinkSendBuffer[SYSTEM_CARD_LINK_PAYLOAD_BYTES];         /* 0x03001CB0 */
u32 gCardLinkTimeoutFrames;                                     /* 0x03001D30 */
u8 gIrqCodeBuffer[SYSTEM_IRQ_CODE_BYTES];                       /* 0x03001D80; DMA destination; executed as ARM through the BIOS vector. */
u8 gVBlankUpdateMode;                                           /* 0x0300259C */
u32 gVBlankFrameCounter;                                        /* 0x030025A0 */
u8 gRandomTableIndex;                                           /* 0x030025A4 */
u32 gLinkTransferStatus;                                        /* 0x030025A8 */
u16 gBg1AutoScrollPhase;                                        /* 0x03002C40; BG1 scroll modes 2/3 only. */

/**
 * @brief 提交场景精灵、背景滚动、文字和过渡资源
 *
 * @note 按 BG1 滚动模式写寄存器；光栅相位按模式以 1024 或 2048 环绕。
 * @address 0x080002A0
 */
void Display_UpdateSceneVBlank(void)
{
    Sprite_CopyTileTransfers();
    switch (gBg1ScrollMode)
    {
    case 1:
        (*(vu16 *)REG_BG1HOFS) = 0;
        (*(vu16 *)REG_BG1VOFS) = 0;
        break;
    case 2:
        gBg1AutoScrollPhase++;
        (*(vu16 *)REG_BG1HOFS) = ((s16)gBg1AutoScrollPhase >> 1) & 0xFF;
        (*(vu16 *)REG_BG1VOFS) = 0;
        break;
    case 3:
        gBg1AutoScrollPhase++;
        (*(vu16 *)REG_BG1HOFS) = ((s16)gBg1AutoScrollPhase >> 2) & 0xFF;
        (*(vu16 *)REG_BG1VOFS) = 16;
        break;
    case 4:
        (*(vu16 *)REG_BG1HOFS) = 64;
        (*(vu16 *)REG_BG1VOFS) = 0;
        break;
    case 5:
        (*(vu16 *)REG_BG1HOFS) = *(u8 *)&gCameraX >> 3;
        (*(vu16 *)REG_BG1VOFS) = 0;
        break;
    case 6:
        (*(vu16 *)REG_BG1HOFS) = 0;
        (*(vu16 *)REG_BG1VOFS) = 30;
        break;
    default:
        (*(vu16 *)REG_BG1HOFS) = gCameraX & 0x1F;
        (*(vu16 *)REG_BG1VOFS) = gCameraY & 0x1F;
        break;
    }
    (*(vu16 *)REG_BG2HOFS) = gBg2ScrollX;
    (*(vu16 *)REG_BG2VOFS) = gBg2ScrollY;
    (*(vu16 *)REG_BG3HOFS) = gBg3ScrollX;
    (*(vu16 *)REG_BG3VOFS) = gBg3ScrollY;
    DmaCopy(3, gSpriteOamBuffer, ((void *)OAM), sizeof(gSpriteOamBuffer), 16);
    TitleLogo_UpdateVBlank();
    Scene_UploadOverlayImage();
    if (gTitleLogoStage == 0)
        Script_UpdateVBlank();
    Menu_UpdateVBlank();
    if (gSceneEffects.tilemapUploadPending)
    {
        DmaCopy(3, SCENE_EFFECT_TILEMAP + 0x1C0,
            (u8 *)BG_SCREEN_ADDR(30) + 0x380, 0x100, 16);
        gSceneEffects.tilemapUploadPending = 0;
    }
    (*(vu16 *)REG_BLDCNT) = gBlendControl;
    if (gBlendControl & 0x80)
        (*(vu16 *)REG_BLDY) = gBlendCoefficients;
    else
        (*(vu16 *)REG_BLDALPHA) = gBlendCoefficients;
    Transition_UpdateVBlank();
    Actor_LoadPendingResources();
    switch (gBg1RasterMode)
    {
    case 0:
        break;
    case 1:
    case 3:
        sBg1RasterPhase = (sBg1RasterPhase - 1) & 0x3FF;
        gBg1RasterLineOffset = (sBg1RasterPhase >> 2) + *(u8 *)&gCameraY;
        break;
    case 2:
    case 4:
        sBg1RasterPhase = (sBg1RasterPhase - 1) & 0x7FF;
        gBg1RasterLineOffset = (sBg1RasterPhase >> 3) + *(u8 *)&gCameraY;
        break;
    }
}

/**
 * @brief 清背景滚动并提交 OAM 和完整文字图块映射
 *
 * @note 更新混色和过渡；光栅相位按 1024 环绕，与场景提交路径的模式选择不同。
 * @address 0x080004F8
 */
void Display_UpdateTextVBlank(void)
{
    (*(vu16 *)REG_BG0HOFS) = 0;
    (*(vu16 *)REG_BG0VOFS) = 0;
    (*(vu16 *)REG_BG1HOFS) = 0;
    (*(vu16 *)REG_BG1VOFS) = 0;
    (*(vu16 *)REG_BG2HOFS) = 0;
    (*(vu16 *)REG_BG2VOFS) = 0;
    (*(vu16 *)REG_BG3HOFS) = 0;
    (*(vu16 *)REG_BG3VOFS) = 0;
    DmaCopy(3, gSpriteOamBuffer, ((void *)OAM), sizeof(gSpriteOamBuffer), 16);
    (*(vu16 *)REG_BLDCNT) = gBlendControl;
    (*(vu16 *)REG_BLDALPHA) = gBlendCoefficients;
    DmaCopy(3, gScriptTextTilemap, BG_SCREEN_ADDR(31), sizeof(gScriptTextTilemap), 16);
    Transition_UpdateVBlank();
    sBg1RasterPhase = (sBg1RasterPhase - 1) & 0x3FF;
    gBg1RasterLineOffset = (sBg1RasterPhase >> 2) + *(u8 *)&gCameraY;
}

/**
 * @brief 按扫描行索引提交 BG1 水平和垂直光栅偏移
 *
 * @note 模式 1/2 叠加相机低五位，模式 3/4 使用表值；其他模式不写滚动寄存器。
 * @address 0x080005A8
 */
void Display_UpdateBg1RasterLine(u16 line)
{
    switch (gBg1RasterMode)
    {
    case 1:
    case 2:
        (*(vu16 *)REG_BG1HOFS) = gBg1RasterXOffsets[(gBg1RasterLineOffset + line) & 0xFF]
            + (gCameraX & 0x1F);
        (*(vu16 *)REG_BG1VOFS) = gBg1RasterYOffsets[(gBg1RasterLineOffset + line) & 0xFF]
            + (gCameraY & 0x1F);
        break;
    case 3:
        (*(vu16 *)REG_BG1HOFS) = gBg1RasterXOffsets[(gBg1RasterLineOffset + line) & 0xFF];
        (*(vu16 *)REG_BG1VOFS) = gBg1RasterYOffsets[(gBg1RasterLineOffset + line) & 0xFF];
        break;
    case 4:
        (*(vu16 *)REG_BG1HOFS) = gBg1RasterXOffsets[(gBg1RasterLineOffset + line) & 0xFF];
        (*(vu16 *)REG_BG1VOFS) = gBg1RasterYOffsets[(gBg1RasterLineOffset + line) & 0xFF];
        break;
    }
}

/* Inline the card-link display path within the VBlank dispatcher. */
static inline void Display_SubmitLinkFrameInline(void)
{
    SerialLink_ServiceVBlank();
    m4aSoundVSync();
    Sprite_CopyTileTransfers();
    Sprite_CopyPaletteTransfers();
    CpuFastSet(gSpriteOamBuffer, ((void *)OAM), sizeof(gSpriteOamBuffer) / 4);
    (*(vu16 *)REG_BG1HOFS) = 0;
    (*(vu16 *)REG_BG1VOFS) = 0;
    (*(vu16 *)REG_BG2HOFS) = 0;
    (*(vu16 *)REG_BG2VOFS) = 0;
    (*(vu16 *)REG_BG3HOFS) = 0;
    (*(vu16 *)REG_BG3VOFS) = 0;
    (*(vu16 *)REG_BLDCNT) = gBlendControl;
    (*(vu16 *)REG_BLDALPHA) = gBlendCoefficients;
    CpuFastSet(gScriptTextTilemap, BG_SCREEN_ADDR(31), sizeof(gScriptTextTilemap) / 4);
    if (gSceneEffectTilemapUploadPending)
    {
        CpuFastSet(SCENE_EFFECT_TILEMAP, BG_SCREEN_ADDR(30), BG_SCREEN_BLOCK_SIZE / 4);
        gSceneEffectTilemapUploadPending = 0;
    }
}

/**
 * @brief 派发 VBlank 显示服务并推进时间和随机表索引
 *
 * @note 游戏时间饱和到 215996400，独立帧计数自然回绕；末尾设置 BIOS 中断检查位。
 * @address 0x0800065C
 */
void System_VBlank(void)
{
    switch (gVBlankUpdateMode)
    {
    case VBLANK_UPDATE_SCENE:
        m4aSoundVSync();
        Display_UpdateSceneVBlank();
        break;
    case VBLANK_UPDATE_BATTLE:
        m4aSoundVSync();
        Battle_UpdateVBlank();
        break;
    case VBLANK_UPDATE_TEXT:
        m4aSoundVSync();
        Display_UpdateTextVBlank();
        break;
    case VBLANK_UPDATE_TITLE:
        m4aSoundVSync();
        Sprite_CopyTileTransfers();
        (*(vu16 *)REG_BG1HOFS) = 0;
        (*(vu16 *)REG_BG1VOFS) = 0;
        (*(vu16 *)REG_BG2HOFS) = 4;
        (*(vu16 *)REG_BG2VOFS) = 0;
        (*(vu16 *)REG_BG3HOFS) = 4;
        (*(vu16 *)REG_BG3VOFS) = 0;
        (*(vu16 *)REG_BLDCNT) = gBlendControl;
        (*(vu16 *)REG_BLDALPHA) = gBlendCoefficients;
        DmaCopy(3, gSpriteOamBuffer, ((void *)OAM), sizeof(gSpriteOamBuffer), 16);
        DmaCopy(3, gScriptTextTilemap, BG_SCREEN_ADDR(31), sizeof(gScriptTextTilemap), 16);
        if (gSceneEffectTilemapUploadPending)
        {
            DmaCopy(3, SCENE_EFFECT_TILEMAP, BG_SCREEN_ADDR(30), (u32)(BG_SCREEN_BLOCK_SIZE), 16);
            gSceneEffectTilemapUploadPending = 0;
        }
        Transition_UpdateVBlank();
        break;
    case VBLANK_UPDATE_CARD_LINK:
        Display_SubmitLinkFrameInline();
        break;
    case VBLANK_UPDATE_SCRIPT_BACKGROUND:
        m4aSoundVSync();
        (*(vu16 *)REG_BG1HOFS) = 0;
        (*(vu16 *)REG_BG1VOFS) = 0;
        (*(vu16 *)REG_BG2HOFS) = 0;
        (*(vu16 *)REG_BG2VOFS) = 0;
        (*(vu16 *)REG_BG3HOFS) = 0;
        (*(vu16 *)REG_BG3VOFS) = 0;
        Sprite_CopyTileTransfers();
        DmaCopy(3, gSpriteOamBuffer, ((void *)OAM), sizeof(gSpriteOamBuffer), 16);
        Scene_UploadScriptBackground();
        Script_UpdateVBlank();
        (*(vu16 *)REG_BLDCNT) = gBlendControl;
        if (gBlendControl & 0x80)
            (*(vu16 *)REG_BLDY) = gBlendCoefficients;
        else
            (*(vu16 *)REG_BLDALPHA) = gBlendCoefficients;
        Transition_UpdateVBlank();
        Actor_LoadPendingResources();
        break;
    default:
        m4aSoundVSync();
        break;
    }
    gRandomTableIndex++;
    if (gPlayTimeFrames <= 0x0CDFD7F0 - 2)
        gPlayTimeFrames++;
    else
        gPlayTimeFrames = 0x0CDFD7F0;
    gVBlankFrameCounter++;
    (*(vu16 *)REG_IME) = 0;
    BIOS_IRQ_CHECK |= V_BLANK_INTR_FLAG;
    (*(vu16 *)REG_IME) = 1;
}

/**
 * @brief 停止音效和 BGM，暂停声音同步并强制白屏
 *
 * @note 按原帧等待顺序更新音频；请求音量 0 经声音层钳制为 2。
 * @address 0x080008CC
 */
void System_SuspendAudioAndBlankDisplay(void)
{
    Sound_StopSfx(0);
    Sound_StopSfx(1);
    Sound_StopSfx(2);
    Sound_StopSfx(3);
    Sound_SetBgmVolume(0);
    VBlankIntrWait();
    while ((*(vu16 *)REG_STAT) & STAT_V_BLANK)
        ;
    Sound_Update();
    Sound_StopBgm();
    VBlankIntrWait();
    while ((*(vu16 *)REG_STAT) & STAT_V_BLANK)
        ;
    Sound_Update();
    Sound_DisableVSync();
    VBlankIntrWait();
    while ((*(vu16 *)REG_STAT) & STAT_V_BLANK)
        ;
    Sound_Update();
    VBlankIntrWait();
    (*(vu16 *)REG_DISPCNT) |= DISP_LCDC_OFF;
}

/**
 * @brief 推进卡片交换服务状态并处理超时和传输结果
 *
 * @note 双向有效载荷均为 16 字节；返回连接签名查询结果，当前菜单调用者忽略它。
 * @address 0x0800096C
 */
s32 Link_UpdateCardExchange(void)
{
    switch (gLinkServiceState)
    {
    case 0:
        (*(vu16 *)REG_RCNT) = 0xC000;
        VBlankIntrWait();
        SerialLink_ConfigureTransfer(gCardLinkReceiveBuffer, gCardLinkSendBuffer,
            sizeof(gCardLinkReceiveBuffer), 0);
        SerialLink_RequestBlockTransferInline();
        gLinkServiceState++;
        /* Fall through to the first protocol poll. */
    case 1:
        gLinkPollResult = SerialLink_PollBlockTransfer();
        if (gLinkPollResult == -1)
        {
            gLinkTransferStatus = 5;
            gLinkServiceState = 6;
        }
        else if (SerialLink_HasPeerSignature() == 0 && gLinkPollResult <= 0xFF)
        {
            if (gCardLinkTimeoutFrames == 0)
            {
                SerialLink_Close();
                gLinkServiceState = 4;
                gLinkTransferStatus = 2;
            }
            gCardLinkTimeoutFrames--;
        }
        else
        {
            switch (gLinkPollResult)
            {
            case 0:
            case 1:
                if (gCardLinkTimeoutFrames == 0)
                {
                    SerialLink_Close();
                    gLinkServiceState = 4;
                    gLinkTransferStatus = 2;
                }
                else
                    gCardLinkTimeoutFrames--;
                break;
            case 2:
                if (SerialLink_HasPeerSignature() == 0)
                {
                    SerialLink_Close();
                    gLinkServiceState = 5;
                    gLinkTransferStatus = 3;
                }
                else if (gLinkTransferStatus == 0)
                {
                    SerialLink_RequestBlockTransferInline();
                    gLinkTransferStatus = 1;
                }
                break;
            case 0x101:
            case 0x102:
            case 0x103:
            case 0x104:
                gLinkTransferStatus = 3;
                gLinkServiceState = 5;
                break;
            }
        }
        break;
    case 2:
    case 3:
    case 4:
        SerialLink_Close();
        gLinkServiceState = 9;
        break;
    case 5:
        SerialLink_Close();
        gLinkServiceState = 9;
        break;
    case 6:
        gLinkServiceState = 0;
        break;
    case 7:
        SerialLink_Close();
        break;
    case 8:
        if (gLinkTransferStatus == 0)
        {
            SerialLink_Close();
            gLinkServiceState = 0;
        }
        break;
    case 9:
        SerialLink_Close();
        gLinkServiceState = 8;
        break;
    }
    return SerialLink_HasPeerSignature();
}

/**
 * @brief 清显示缓冲、运行状态、剧情位图和系统位图
 *
 * @note 启动和新游戏调用；重置游戏时间及指定子系统，不清整个 IWRAM。
 * @address 0x08000B58
 */
void System_ClearGameState(u32 unused)
{
    /* Reuse the same two ordinary staging values for all four DMA fills. */
    u16 fill16;
    u32 fill32;
    u16 index;


    {
        u16 *source = &fill16;
        *source = 0;
        DmaSet(3, &fill16, ((void *)VRAM),
            (u32)(DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS) | (VRAM_SIZE / 2));
    }

    {
        fill32 = 0;
        DmaSet(3, &fill32, ((void *)OAM),
            (u32)(DMA_ENABLE | DMA_SRC_FIX | DMA_32BIT_BUS) | (OAM_SIZE / 4));
    }

    {
        u16 *source = &fill16;
        *source = 0;
        DmaSet(3, &fill16, PALETTE_RAM,
            (u32)(DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS) | (PLTT_SIZE / 2));
    }

    {
        u16 *source = &fill16;
        *source = 0;
        DmaSet(3, &fill16, MAP_TILEMAP_A,
            (u32)(DMA_ENABLE | DMA_SRC_FIX | DMA_16BIT_BUS) | (0x2000 / 2));
    }
    Sprite_ClearNodes();
    Sprite_ClearAffineMatrices();
    gPlayTimeFrames = 0;
    gActorResourceLoadFlags = 0;
    gTitleLogoStage = 0;
    gPaletteTransitionType = 0;
    gScreenTransitionType = 0;
    gScanlineBlendFlags = 0;
    gSceneBgmControl = 0xFF;
    for (index = 0; index < sizeof(gSceneEffects) / sizeof(u16); index++)
        ((u16 *)&gSceneEffects)[index] = 0;
    for (index = 0; index < CORE_SPRITE_OAM_COUNT; index++)
    {
        gSpriteOamBuffer[index].words[0] = 0;
        gSpriteOamBuffer[index].words[1] = 0;
    }
    Sprite_ClearTileTransfers();
    Sprite_ClearPaletteTransfers();
    Palette_DisableAnimations();
    SystemFlags_ClearAll();
    EventFlags_ClearAll();
    Chest_ClearOpenBits();
    gVBlankUpdateMode = VBLANK_UPDATE_SOUND_ONLY;
    gMenuState = 0;
    gMenuContext = 0;
    gCameraFollowDisabled = 0;
    gSceneScriptRunningSnapshot = 0;
    gPendingOverlaySlot = 0;
    gScriptedBattleState = 0;
}

/**
 * @brief 按四组步频和除数生成 BG1 光栅偏移表
 *
 * @note 以无符号字节读取 ROM 波形，只生成索引 0..254；第 255 项保留原值。
 * @address 0x08000C98
 */
void Display_BuildBg1RasterOffsets(u16 mode)
{
    s8 xDivisor;
    s8 xStep;
    s8 yDivisor;
    s8 yStep;
    u16 mask = 127;
    u16 index;

    switch (mode)
    {
    case 1:
        xDivisor = 12;
        xStep = 32;
        yDivisor = 12;
        yStep = 16;
        break;
    case 2:
        xDivisor = 16;
        xStep = 8;
        yDivisor = 4;
        yStep = 16;
        mask = 63;
        break;
    case 3:
        xDivisor = 16;
        xStep = 32;
        yDivisor = 32;
        yStep = 4;
        break;
    case 4:
        xDivisor = 4;
        xStep = 1;
        yDivisor = 16;
        yStep = 1;
        mask = 63;
        break;
    default:
        return;
    }
    for (index = 0; index < SYSTEM_RASTER_TABLE_SIZE - 1; index++)
    {
        gBg1RasterXOffsets[index] = gBg1RasterWave[(index * xStep) & mask] / xDivisor;
        gBg1RasterYOffsets[index] = gBg1RasterWave[(index * yStep) & mask] / yDivisor;
    }
}

/**
 * @brief 推进独立标志流驱动的分块 LZ 解压
 *
 * @note 完整回引可超出预算；以返回 0 判完成，终块不一定清除 remaining 字段。
 * @address 0x08000D5C
 */
u32 LzStream_DecodeChunk(void)
{
    if (sLzStream.chunkBudget > sLzStream.remaining)
        sLzStream.chunkBudget = sLzStream.remaining;
    sLzStream.producedThisChunk = 0;
    while (sLzStream.chunkBudget > sLzStream.producedThisChunk)
    {
        if ((sLzStream.flagBits[sLzStream.tokenIndex >> 3] >> (sLzStream.tokenIndex & 7)) & 1)
        {
            u16 token = sLzStream.tokenData[0] + (sLzStream.tokenData[1] << 8);
            u32 distance;
            s32 length;
            s32 index;

            sLzStream.tokenData += 2;
            distance = (token & 0xFFF) + 1;
            length = (token >> 12) + 3;
            for (index = 0; index < length; index++)
            {
                *sLzStream.destination = *(sLzStream.destination - distance);
                sLzStream.destination++;
                sLzStream.producedThisChunk++;
            }
        }
        else
        {
            *sLzStream.destination++ = *sLzStream.tokenData++;
            sLzStream.producedThisChunk++;
        }
        sLzStream.tokenIndex++;
    }
    {
        u32 remaining;

        if (sLzStream.remaining <= sLzStream.chunkBudget)
            remaining = 0;
        else
        {
            sLzStream.remaining -= sLzStream.producedThisChunk;
            remaining = sLzStream.remaining;
        }
        return remaining;
    }
}

/**
 * @brief 为标题卡片交换切换 IRQ 代码副本及显示服务
 *
 * @note 0 恢复普通 IRQ 和 VBlank 模式 4；非 0 安装嵌套 IRQ、选择模式 5 并禁用 HBlank。
 * @address 0x08000E1C
 */
void System_SetCardLinkIrqMode(u8 enabled)
{
    (*(vu16 *)REG_IME) = 0;
    if (enabled == 0)
    {
        gVBlankUpdateMode = VBLANK_UPDATE_TITLE;
        DmaCopy(3, func_080000FC, gIrqCodeBuffer, sizeof(gIrqCodeBuffer), 16);
        (*(vu16 *)REG_IE) = CASSETTE_INTR_FLAG | H_BLANK_INTR_FLAG | V_BLANK_INTR_FLAG;
        (*(vu16 *)REG_STAT) |= STAT_H_BLANK_IF_ENABLE;
    }
    else
    {
        gVBlankUpdateMode = VBLANK_UPDATE_CARD_LINK;
        DmaCopy(3, func_08000170, gIrqCodeBuffer, sizeof(gIrqCodeBuffer), 16);
        (*(vu16 *)REG_IE) &= ~H_BLANK_INTR_FLAG;
        (*(vu16 *)REG_STAT) &= ~STAT_H_BLANK_IF_ENABLE;
    }
    BIOS_IRQ_HANDLER = (u32)gIrqCodeBuffer;
    (*(vu16 *)REG_IME) = 1;
}

/**
 * @brief 解除强制白屏并恢复声音同步和更新
 *
 * @note 先等待 VCOUNT 低八位不大于 200，再按原帧顺序恢复声音；音量请求经驱动钳制。
 * @address 0x08000ED8
 */
void System_ResumeAudioAndDisplay(void)
{
    while (((*(vu16 *)REG_VCOUNT) & 0xFF) > 200)
        ;
    (*(vu16 *)REG_DISPCNT) &= ~DISP_LCDC_OFF;
    VBlankIntrWait();
    while ((*(vu16 *)REG_STAT) & STAT_V_BLANK)
        ;
    Sound_EnableVSync();
    Sound_SetBgmVolume(0);
    VBlankIntrWait();
    while ((*(vu16 *)REG_STAT) & STAT_V_BLANK)
        ;
    Sound_Update();
}

/**
 * @brief 请求标题场景并停止当前游戏音频
 *
 * @note 设置场景状态 11、阶段 0 和 VBlank 模式 0；主循环模式由调用者管理。
 * @address 0x08000F54
 */
void System_RequestTitle(void)
{
    gVBlankUpdateMode = VBLANK_UPDATE_SOUND_ONLY;
    gSceneState = 11;
    gScenePhase = 0;
    Sound_StopBgm();
    VBlankIntrWait();
    Sound_Update();
    Sound_DisableVSync();
    VBlankIntrWait();
    Sound_Update();
}

/**
 * @brief 保留资源加载路径调用的空入口
 *
 * @note 原指令只有返回；用途未确定，保留地址名，不能解释为等待或缓存操作。
 * @address 0x08000F90
 */
void func_08000F90(void)
{
}

/**
 * @brief 等待 VBlank 中断后更新游戏音频
 *
 * @note 返回时不额外等待可见扫描期。
 * @address 0x08000F94
 */
void System_WaitVBlankAndUpdateSound(void)
{
    VBlankIntrWait();
    Sound_Update();
}

/**
 * @brief 等待 VBlank 后进入可见扫描期再更新音频
 *
 * @note 轮询 DISPSTAT 的 VBlank 位清零，保留硬件读取次序。
 * @address 0x08000FA4
 */
void System_WaitVisibleFrameAndUpdateSound(void)
{
    VBlankIntrWait();
    while ((*(vu16 *)REG_STAT) & STAT_V_BLANK)
        ;
    Sound_Update();
}

/**
 * @brief 为带独立标志流的压缩资源初始化解压状态
 *
 * @note 头部依次为解压长度和 token 字节数；不改 producedThisChunk，下一次解压才清它。
 * @address 0x08000FD0
 */
void LzStream_Init(void *destination, const void *source, u32 budget)
{
    struct LzStreamState *stream = &sLzStream;
    const struct LzStreamHeader *header = source;
    u32 size = header->decompressedSize;

    stream->decompressedSize = size;
    stream->remaining = size;
    stream->destination = destination;
    stream->tokenData = (const u8 *)(header + 1);
    stream->flagBits = stream->tokenData + header->tokenDataSize;
    stream->tokenIndex = 0;
    stream->chunkBudget = budget;
}

/**
 * @brief 读取随机置换表并推进八位索引
 *
 * @note 返回旧索引处的字节；VBlank 也推进同一索引，战斗模块直接读取它。
 * @address 0x08000FF8
 */
u8 Random_NextByte(void)
{
    const u8 *table = gRandomTable;

    return table[gRandomTableIndex++];
}

/**
 * @brief 清除全部 512 个剧情标志
 *
 * @note 逐字节清空 64 字节位图。
 * @address 0x08001014
 */
void EventFlags_ClearAll(void)
{
    u16 index;

    for (index = 0; index < SYSTEM_EVENT_FLAG_BYTES; index++)
        gEventFlags[index] = 0;
}

/**
 * @brief 查询剧情标志并返回其原位掩码
 *
 * @note 有效编号 0..511；未置位返回 0，置位返回 1 << (flagId & 7)，不规范化为布尔值。
 * @address 0x08001030
 */
u8 EventFlags_GetMask(u16 flagId)
{
    return gEventFlags[flagId >> 3] & (1 << (flagId & 7));
}

/**
 * @brief 置位一个剧情标志
 *
 * @note 保留同字节的其他位；调用者负责保证编号在 0..511。
 * @address 0x08001050
 */
void EventFlags_Set(u16 flagId)
{
    gEventFlags[flagId >> 3] |= 1 << (flagId & 7);
}

/**
 * @brief 清除一个剧情标志
 *
 * @note 保留同字节的其他位；调用者负责保证编号在 0..511。
 * @address 0x08001070
 */
void EventFlags_Clear(u16 flagId)
{
    gEventFlags[flagId >> 3] &= ~(1 << (flagId & 7));
}

/**
 * @brief 清空系统位图的持久前缀和局部后缀
 *
 * @note 新游戏清全部 80 字节；普通局部重置使用 SystemFlags_ClearLocal。
 * @address 0x08001090
 */
void SystemFlags_ClearAll(void)
{
    u16 index;

    for (index = 0; index < SYSTEM_FLAG_BYTES; index++)
        gSystemFlags[index] = 0;
}

/**
 * @brief 查询系统标志并返回其原位掩码
 *
 * @note 有效编号 0..639；整张表含有存档持久前缀，不能统称为临时标志。
 * @address 0x080010AC
 */
u8 SystemFlags_GetMask(u16 flagId)
{
    return gSystemFlags[flagId >> 3] & (1 << (flagId & 7));
}

/**
 * @brief 置位一个系统标志
 *
 * @note 按字节访问并保留其他位；不检查编号边界。
 * @address 0x080010CC
 */
void SystemFlags_Set(u16 flagId)
{
    gSystemFlags[flagId >> 3] |= 1 << (flagId & 7);
}

/**
 * @brief 清除一个系统标志
 *
 * @note 按字节访问并保留其他位；不检查编号边界。
 * @address 0x080010EC
 */
void SystemFlags_Clear(u16 flagId)
{
    gSystemFlags[flagId >> 3] &= ~(1 << (flagId & 7));
}

/**
 * @brief 清除系统位图中会被读档和场景切换复位的后缀
 *
 * @note 仅清 byte 61..79，即 bits 488..639；byte 61..63 虽存入存档，载入后仍被清除。
 * @address 0x0800110C
 */
void SystemFlags_ClearLocal(void)
{
    u16 index;

    for (index = SYSTEM_LOCAL_FLAG_FIRST_BYTE; index < SYSTEM_FLAG_BYTES; index++)
        gSystemFlags[index] = 0;
}

/**
 * @brief 初始化 RAM、IRQ、显示资源和声音服务
 *
 * @note 安装 0x800 字节 IRQ 副本；表副本共 0x38 字节，最后一个 word 属于相邻波形数据。
 * @address 0x08001128
 */
void System_Init(void)
{
    RegisterRamReset(3);
    (*(vu16 *)REG_WAITCNT) = 0x4014;
    gGameLoopMode = GAME_LOOP_SCENE;
    Palette_FillWhite();
    DmaCopy(3, gSystemIrqHandlers, sIrqTableCopy, sizeof(sIrqTableCopy), 32);
    DmaCopy(3, func_080000FC, gIrqCodeBuffer, sizeof(gIrqCodeBuffer), 16);
    BIOS_IRQ_HANDLER = (u32)gIrqCodeBuffer;
    gSceneState = 11;
    gScenePhase = 0;
    (*(vu16 *)REG_IE) = CASSETTE_INTR_FLAG | H_BLANK_INTR_FLAG | V_BLANK_INTR_FLAG;
    (*(vu16 *)REG_STAT) = STAT_V_BLANK_IF_ENABLE | STAT_H_BLANK_IF_ENABLE;
    (*(vu16 *)REG_IME) = 1;
    VBlankIntrWait();
    (*(vu16 *)REG_DISPCNT) = 0x37C0;
    System_ClearGameState(0);
    Sound_Init();
    BattleMenu_ResetHistory();
    gScenePhase = 0;
}

/**
 * @brief 采样按键并计算本次新增按下位的同字节入口
 *
 * @note 与 Input_PollKeys 的原指令完全相同；此独立入口的生产调用和原始用途尚未确定。
 * @address 0x080011F0
 */
void Input_PollKeysCopy(void)
{
    u16 keys = ~(*(vu16 *)REG_KEYINPUT);

    gKeysPressed = keys & ~gKeysHeld;
    gKeysHeld = keys;
}

/**
 * @brief 采样 KEYINPUT 并计算保持和新按下的按键位
 *
 * @note 对完整半字取反，不屏蔽高六位；不生成自动连发，先写 pressed 再写 held。
 * @address 0x0800121C
 */
void Input_PollKeys(void)
{
    u16 keys = ~(*(vu16 *)REG_KEYINPUT);

    gKeysPressed = keys & ~gKeysHeld;
    gKeysHeld = keys;
}

/**
 * @brief 为 IRQ 表中的未处理槽提供空回调
 *
 * @note 原入口直接返回；保留独立地址和函数身份。
 * @address 0x08001248
 */
void System_EmptyIrqHandler(void)
{
}

/**
 * @brief 按显示模式派发 HBlank 扫描行服务
 *
 * @note 模式 1/3 提交 BG1 光栅偏移，模式 2 调用战斗扫描行服务。
 * @address 0x0800124C
 */
void System_HBlank(void)
{
    switch (gVBlankUpdateMode)
    {
    case VBLANK_UPDATE_SCENE:
    case VBLANK_UPDATE_TEXT:
        Display_UpdateBg1RasterLine((*(vu16 *)REG_VCOUNT) & 0xFF);
        break;
    case VBLANK_UPDATE_BATTLE:
        Battle_UpdateHBlank();
        break;
    }
}

/**
 * @brief 为 VCount 中断表槽提供空回调
 *
 * @note 原入口直接返回；本轮只验证定向调用，未逐项触发硬件 IRQ。
 * @address 0x08001284
 */
void System_EmptyVCountHandler(void)
{
}

/**
 * @brief 保留用途未确定的独立空入口
 *
 * @note 原指令只有返回；未确认生产调用者，保留地址名。
 * @address 0x08001288
 */
void func_08001288(void)
{
}

/**
 * @brief 初始化系统并持续派发场景或战斗主循环
 *
 * @note 按八位模式索引两项处理器表，随后等待 VBlank 并更新音频；原代码无越界检查。
 * @address 0x0800128C
 */
void System_RunMainLoop(void)
{
    gGameLoopMode = GAME_LOOP_SCENE;
    System_Init();
    for (;;)
    {
        gGameLoopHandlers[gGameLoopMode]();
        VBlankIntrWait();
        Sound_Update();
    }
}

/**
 * @brief 执行联机服务并提交精灵和文字画面
 *
 * @note 操作与 VBlank 模式 5 相同；独立入口的生产调用尚未确认，名称只描述已验证行为。
 * @address 0x080012B8
 */
void Display_SubmitLinkFrame(void)
{
    SerialLink_ServiceVBlank();
    m4aSoundVSync();
    Sprite_CopyTileTransfers();
    Sprite_CopyPaletteTransfers();
    CpuFastSet(gSpriteOamBuffer, ((void *)OAM), sizeof(gSpriteOamBuffer) / 4);
    (*(vu16 *)REG_BG1HOFS) = 0;
    (*(vu16 *)REG_BG1VOFS) = 0;
    (*(vu16 *)REG_BG2HOFS) = 0;
    (*(vu16 *)REG_BG2VOFS) = 0;
    (*(vu16 *)REG_BG3HOFS) = 0;
    (*(vu16 *)REG_BG3VOFS) = 0;
    (*(vu16 *)REG_BLDCNT) = gBlendControl;
    (*(vu16 *)REG_BLDALPHA) = gBlendCoefficients;
    CpuFastSet(gScriptTextTilemap, BG_SCREEN_ADDR(31), sizeof(gScriptTextTilemap) / 4);
    if (gSceneEffectTilemapUploadPending)
    {
        CpuFastSet(SCENE_EFFECT_TILEMAP, BG_SCREEN_ADDR(30), BG_SCREEN_BLOCK_SIZE / 4);
        gSceneEffectTilemapUploadPending = 0;
    }
}

/* Split read-only data, in ROM address order. */

/* ROM 0x087E83F0..0x087E83F8; .rodata.007E83F0. */
void (*const gGameLoopHandlers[SYSTEM_GAME_LOOP_COUNT])(void) __attribute__((section(".rodata.007E83F0"), aligned(1))) =
{ (void (*)(void))Scene_Update, (void (*)(void))Battle_Update };

/* End of split read-only data. */
