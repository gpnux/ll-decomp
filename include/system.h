#ifndef SYSTEM_H
#define SYSTEM_H

#include "common.h"

#define SYSTEM_EVENT_FLAG_BYTES 64
#define SYSTEM_FLAG_BYTES 80
#define SYSTEM_LOCAL_FLAG_FIRST_BYTE 61
#define SYSTEM_RASTER_TABLE_SIZE 256
#define SYSTEM_CARD_LINK_PAYLOAD_BYTES 16
#define SYSTEM_IRQ_HANDLER_COUNT 13
#define SYSTEM_IRQ_TABLE_COPY_BYTES 0x38
#define SYSTEM_IRQ_CODE_BYTES 0x800
#define SYSTEM_GAME_LOOP_COUNT 2

enum GameLoopMode
{
    GAME_LOOP_SCENE = 0,
    GAME_LOOP_BATTLE = 1,
};

enum VBlankUpdateMode
{
    VBLANK_UPDATE_SOUND_ONLY = 0,
    VBLANK_UPDATE_SCENE = 1,
    VBLANK_UPDATE_BATTLE = 2,
    VBLANK_UPDATE_TEXT = 3,
    VBLANK_UPDATE_TITLE = 4,
    VBLANK_UPDATE_CARD_LINK = 5,
    VBLANK_UPDATE_SCRIPT_BACKGROUND = 6,
};

/* Custom LZ format: two aligned words, token bytes, then LSB-first flag bits. */
struct LzStreamHeader
{
    u32 decompressedSize; /* 0x00; script banks include their 0x200-byte entry table. */
    u32 tokenDataSize;    /* 0x04; byte distance from the token stream to its flags. */
};

typedef char LzStreamHeaderSizeCheck[(sizeof(struct LzStreamHeader) == 8) ? 1 : -1];
typedef char LzStreamHeaderTokensCheck[(offsetof(struct LzStreamHeader, tokenDataSize) == 4) ? 1 : -1];

/* Shared state; all definitions are in system.c and retain their RAM addresses. */
extern u8 gSystemFlags[SYSTEM_FLAG_BYTES]; /* 030018F0; bytes 0..60 survive normal load. */
extern u8 gSceneState;                    /* 03001944; scene handler index. */
extern u32 gPlayTimeFrames;               /* 03001948; saved, resettable, saturated frame time. */
extern u16 gKeysHeld;                     /* 030019B0; full inverse of KEYINPUT. */
extern u8 gGameLoopMode;                  /* 03001AC0; enum GameLoopMode. */
/* Only the 16-byte payload is established; the following gaps remain untyped. */
extern u8 gCardLinkReceiveBuffer[SYSTEM_CARD_LINK_PAYLOAD_BYTES]; /* 03001AD0 */
extern u32 gLinkServiceState;             /* 03001B50; card service states 0..9. */
extern u8 gEventFlags[SYSTEM_EVENT_FLAG_BYTES]; /* 03001C60; 512 saved story bits. */
extern u16 gKeysPressed;                  /* 03001CA0; held & ~previousHeld. */
extern u8 gCardLinkSendBuffer[SYSTEM_CARD_LINK_PAYLOAD_BYTES]; /* 03001CB0 */
extern u32 gCardLinkTimeoutFrames;        /* 03001D30; includes original zero underflow. */
extern u8 gVBlankUpdateMode;              /* 0300259C; enum VBlankUpdateMode. */
extern u32 gVBlankFrameCounter;           /* 030025A0; independent wrapping frame count. */
extern u8 gRandomTableIndex;              /* 030025A4; advanced by VBlank and Random_NextByte. */
extern u32 gLinkTransferStatus;           /* 030025A8; UI states include 0,1,2,3,5,FF. */

/* ROM tables. The 14th word copied at boot is outside the 13-entry IRQ table. */
extern void (*const gSystemIrqHandlers[SYSTEM_IRQ_HANDLER_COUNT])(void);
extern void (*const gGameLoopHandlers[SYSTEM_GAME_LOOP_COUNT])(void);
extern const u8 gBg1RasterWave[128];
extern const u8 gRandomTable[256];

void Display_UpdateSceneVBlank(void);
void Display_UpdateTextVBlank(void);
void Display_UpdateBg1RasterLine(u16 line);
void System_VBlank(void);
void System_SuspendAudioAndBlankDisplay(void);
s32 Link_UpdateCardExchange(void);
/* Callers supply zero in r0; the reset body does not consume this argument. */
void System_ClearGameState(u32 unused);
void Display_BuildBg1RasterOffsets(u16 mode);
u32 LzStream_DecodeChunk(void);
void System_SetCardLinkIrqMode(u8 enabled);
void System_ResumeAudioAndDisplay(void);
void System_RequestTitle(void);
void func_08000F90(void); /* Empty resource-loading hook; purpose unresolved. */
void System_WaitVBlankAndUpdateSound(void);
void System_WaitVisibleFrameAndUpdateSound(void);
void LzStream_Init(void *destination, const void *source, u32 budget);
u8 Random_NextByte(void);
void EventFlags_ClearAll(void);
u8 EventFlags_GetMask(u16 flagId);
void EventFlags_Set(u16 flagId);
void EventFlags_Clear(u16 flagId);
void SystemFlags_ClearAll(void);
u8 SystemFlags_GetMask(u16 flagId);
void SystemFlags_Set(u16 flagId);
void SystemFlags_Clear(u16 flagId);
void SystemFlags_ClearLocal(void);
void System_Init(void);
void Input_PollKeysCopy(void);
void Input_PollKeys(void);
void System_EmptyIrqHandler(void);
void System_HBlank(void);
void System_EmptyVCountHandler(void);
void func_08001288(void); /* Empty entry; no established production role. */
void System_RunMainLoop(void);
void Display_SubmitLinkFrame(void);

#endif
