#ifndef SCRIPT_H
#define SCRIPT_H

#include "common.h"
#include "core.h"
#include "system.h"

#define SCRIPT_COMMAND_COUNT 80
#define SCRIPT_BANK_COUNT 140
#define SCRIPT_ENTRY_COUNT 256
#define SCRIPT_CALL_STACK_DEPTH 8
#define SCRIPT_BANK_STACK_DEPTH 8
#define SCRIPT_GLYPH_CACHE_COUNT 30
#define SCRIPT_NAME_COUNT 41
#define SCRIPT_NAME_GLYPH_COUNT 9
#define SCRIPT_TILEMAP_WIDTH 32
#define SCRIPT_TILEMAP_HEIGHT 32
#define SCRIPT_GLYPH_TILE_BYTES 32
#define SCRIPT_GLYPH_ATLAS_ROW_GLYPHS 32

#define SCRIPT_RESULT_YIELD 0
#define SCRIPT_RESULT_CONTINUE 1

#define SCRIPT_FLAG_RUNNING 0x0001
#define SCRIPT_FLAG_BANK_CALL 0x0002 /* Set by CallBank; no clear/read in this module. */
#define SCRIPT_FLAG_TEXT_LAYER_PERSISTENT 0x0010
#define SCRIPT_FLAG_WAITING_FRAMES 0x0020
#define SCRIPT_FLAG_GLYPH_UPLOAD_PENDING 0x0040
#define SCRIPT_FLAG_PORTRAIT_VISIBLE 0x0080
#define SCRIPT_FLAG_DEFAULT_GLYPHS_PENDING 0x0100
#define SCRIPT_FLAG_BANK_LOAD_PENDING 0x0200
#define SCRIPT_FLAG_ENTRY_RESELECT_PENDING 0x0400

#define SCRIPT_START_HEAD 1
#define SCRIPT_START_ENTRY 2
#define SCRIPT_START_CURRENT 3
#define SCRIPT_STOP_RESET 1
#define SCRIPT_STOP_KEEP_CURRENT 3

#define SCRIPT_TEXT_PAGE_BREAK 0x0800
#define SCRIPT_TEXT_END 0x0F00
#define SCRIPT_EXTENDED_GLYPH_BASE 0x00E0
#define SCRIPT_GLYPH_SCAN_NAME 0x6F1E
#define SCRIPT_GLYPH_SCAN_KEEP_CACHE 0x2000
#define SCRIPT_GLYPH_SCAN_INCLUDE_UNMARKED 0x4000
#define SCRIPT_GLYPH_SCAN_CONTROL_MASK 0x0F00
#define SCRIPT_TEXT_EXTENDED_GLYPH_FLAG 0x1000

enum ScriptOpcode
{
    SCRIPT_OP_SHOW_DIALOGUE = 0x00,
    SCRIPT_OP_JUMP = 0x01,
    SCRIPT_OP_CALL = 0x02,
    SCRIPT_OP_RETURN = 0x03,
    SCRIPT_OP_RUN_ACTOR_COMMANDS = 0x04,
    SCRIPT_OP_RESERVED = 0x05,
    SCRIPT_OP_END = 0x06,
    SCRIPT_OP_WAIT_ACTOR_COMMANDS = 0x07,
    SCRIPT_OP_REQUEST_ACTOR_GRAPHICS = 0x08,
    SCRIPT_OP_REQUEST_ACTOR_PALETTE = 0x09,
    SCRIPT_OP_WAIT_ACTOR_RESOURCES = 0x0A,
    SCRIPT_OP_FADE_SCREEN_OUT = 0x0B,
    SCRIPT_OP_FADE_SCREEN_IN = 0x0C,
    SCRIPT_OP_WAIT_SCREEN_TRANSITION = 0x0D,
    SCRIPT_OP_CHANGE_MAP = 0x0E,
    SCRIPT_OP_JUMP_IF_EVENT_FLAG_SET = 0x0F,
    SCRIPT_OP_SET_EVENT_FLAG = 0x10,
    SCRIPT_OP_CLEAR_EVENT_FLAG = 0x11,
    SCRIPT_OP_JUMP_IF_SYSTEM_FLAG_SET = 0x12,
    SCRIPT_OP_SET_SYSTEM_FLAG = 0x13,
    SCRIPT_OP_CLEAR_SYSTEM_FLAG = 0x14,
    SCRIPT_OP_CALL_BANK = 0x15,
    SCRIPT_OP_RETURN_BANK = 0x16,
    SCRIPT_OP_DRAW_TEXT = 0x17,
    SCRIPT_OP_DISABLE_CAMERA_FOLLOW = 0x18,
    SCRIPT_OP_ENABLE_CAMERA_FOLLOW = 0x19,
    SCRIPT_OP_PAN_CAMERA = 0x1A,
    SCRIPT_OP_WAIT_CAMERA_PAN = 0x1B,
    SCRIPT_OP_REMOVE_PARTY_MEMBER = 0x1C,
    SCRIPT_OP_ADD_PARTY_MEMBER = 0x1D,
    SCRIPT_OP_LOAD_ACTOR_ANIMATION = 0x1E,
    SCRIPT_OP_START_ACTOR_ANIMATION = 0x1F,
    SCRIPT_OP_WAIT_ACTOR_ANIMATION = 0x20,
    SCRIPT_OP_JUMP_IF_PARTY_MEMBERSHIP_MATCHES = 0x21,
    SCRIPT_OP_START_BATTLE = 0x22,
    SCRIPT_OP_SET_TEXT_RECT_TILES = 0x23,
    SCRIPT_OP_ENABLE_TEXT_LAYER = 0x24,
    SCRIPT_OP_DISABLE_TEXT_LAYER = 0x25,
    SCRIPT_OP_WAIT_FRAMES = 0x26,
    SCRIPT_OP_LOAD_MAP_ANIMATION_GROUP = 0x27,
    SCRIPT_OP_RESUME_MAP_ANIMATION = 0x28,
    SCRIPT_OP_PAUSE_MAP_ANIMATION = 0x29,
    SCRIPT_OP_WAIT_MAP_ANIMATION = 0x2A,
    SCRIPT_OP_LOAD_PALETTE_ANIMATION_GROUP = 0x2B,
    SCRIPT_OP_RESUME_PALETTE_ANIMATION = 0x2C,
    SCRIPT_OP_PAUSE_PALETTE_ANIMATION = 0x2D,
    SCRIPT_OP_WAIT_PALETTE_ANIMATION = 0x2E,
    SCRIPT_OP_RESTORE_PARTY_HP_MP = 0x2F,
    SCRIPT_OP_SET_EQUIPMENT = 0x30,
    SCRIPT_OP_CHANGE_ITEM_COUNT = 0x31,
    SCRIPT_OP_CHANGE_GOLD = 0x32,
    SCRIPT_OP_SHOW_CHOICES = 0x33,
    SCRIPT_OP_PLAY_BGM = 0x34,
    SCRIPT_OP_STOP_BGM = 0x35,
    SCRIPT_OP_SET_BGM_VOLUME = 0x36,
    SCRIPT_OP_FADE_IN_BGM = 0x37,
    SCRIPT_OP_FADE_OUT_BGM = 0x38,
    SCRIPT_OP_PLAY_SFX = 0x39,
    SCRIPT_OP_STOP_SFX = 0x3A,
    SCRIPT_OP_JUMP_IF_ITEM_COUNT_FULL = 0x3B,
    SCRIPT_OP_TOGGLE_LAST_CHEST = 0x3C,
    SCRIPT_OP_OPEN_SHOP = 0x3D,
    SCRIPT_OP_WAIT_SHOP_RESULT = 0x3E,
    SCRIPT_OP_ADD_CARD = 0x3F,
    SCRIPT_OP_REMOVE_CARD = 0x40,
    SCRIPT_OP_JUMP_IF_CARD_MISSING = 0x41,
    SCRIPT_OP_SET_COLLECTION_EVENT_FLAG = 0x42,
    SCRIPT_OP_SET_FLAG_LIST = 0x43,
    SCRIPT_OP_CLEAR_FLAG_LIST = 0x44,
    SCRIPT_OP_JUMP_IF_ALL_FLAGS_SET = 0x45,
    SCRIPT_OP_JUMP_IF_ALL_FLAGS_CLEAR = 0x46,
    SCRIPT_OP_JUMP_IF_ANY_FLAG_SET = 0x47,
    SCRIPT_OP_CLEAR_LOCAL_SYSTEM_FLAGS = 0x48,
    SCRIPT_OP_JUMP_IF_GOLD_GREATER = 0x49,
    SCRIPT_OP_START_TITLE_LOGO = 0x4A,
    SCRIPT_OP_WAIT_TITLE_LOGO = 0x4B,
    SCRIPT_OP_RAISE_CHARACTER_LEVEL = 0x4C,
    SCRIPT_OP_CONTROL_SCENE_EFFECT = 0x4D,
    SCRIPT_OP_JUMP_RANDOM = 0x4E,
    SCRIPT_OP_CALL_ALTERNATE = 0x4F,
};

enum ScriptTextPhase
{
    SCRIPT_TEXT_PHASE_INIT = 0,
    SCRIPT_TEXT_PHASE_CACHE_PAGE = 1,
    SCRIPT_TEXT_PHASE_ENABLE_LAYER = 2,
    SCRIPT_TEXT_PHASE_OPEN_WINDOW = 3,
    SCRIPT_TEXT_PHASE_WAIT_WINDOW = 4,
    SCRIPT_TEXT_PHASE_DRAW = 5,
    SCRIPT_TEXT_PHASE_WAIT_INPUT = 6,
    SCRIPT_TEXT_PHASE_CLOSE_PORTRAIT = 7,
    SCRIPT_TEXT_PHASE_FINISH = 8,
    SCRIPT_TEXT_PHASE_CLOSE_WINDOW = 9,
    SCRIPT_TEXT_PHASE_UNUSED = 10,
    SCRIPT_TEXT_PHASE_WAIT_FRAMES = 11,
    SCRIPT_TEXT_PHASE_DRAW_NAME = 12,
};

/* Decompressed bank prefix; bytecode is variable length. */
struct ScriptBank
{
    u16 entryOffsets[SCRIPT_ENTRY_COUNT]; /* 0x000; relative to bytecode */
    u8 bytecode[1];                       /* 0x200; trailing array for agbcc */
} __attribute__((packed, aligned(2)));

/* Halfword-aligned 18-byte ROM records; agbcc otherwise rounds them to 20. */
struct ScriptName
{
    u16 glyphs[SCRIPT_NAME_GLYPH_COUNT]; /* 0x00; padded with SCRIPT_TEXT_END */
} __attribute__((packed, aligned(2)));

/* One 8x16 glyph is two consecutive 4bpp, 8x8 tiles. */
struct ScriptGlyphTiles
{
    u8 top[SCRIPT_GLYPH_TILE_BYTES];    /* 0x00 */
    u8 bottom[SCRIPT_GLYPH_TILE_BYTES]; /* 0x20 */
};

/* ROM stores 32 top tiles, then the same 32 glyphs' bottom tiles. */
struct ScriptGlyphAtlasRow
{
    u8 top[SCRIPT_GLYPH_ATLAS_ROW_GLYPHS][SCRIPT_GLYPH_TILE_BYTES];    /* 0x000 */
    u8 bottom[SCRIPT_GLYPH_ATLAS_ROW_GLYPHS][SCRIPT_GLYPH_TILE_BYTES]; /* 0x400 */
};

struct ScriptFontResources
{
    const u8 *defaultGlyphsLz;                        /* 0x00; BIOS LZ77 stream */
    const struct ScriptGlyphAtlasRow *extendedRows;  /* 0x04; ROM atlas rows */
    const u8 *unknown08;                             /* 0x08; unused by script */
};

/*
 * Serialized headers have byte alignment. These byte arrays describe the ROM
 * encoding, not permission to replace byte reads with aligned halfword reads.
 * Most fields use two LDRB operations. ShowChoices' unknown06 copy is an actual
 * LDRH; its original access behavior must be preserved separately.
 */
struct ScriptEntryCommand
{
    u8 opcode;  /* 0x00; Jump, Call, or CallAlternate */
    u8 entryId; /* 0x01; index into the current bank's entry-offset table */
} __attribute__((packed));

struct ScriptActorResourceCommand
{
    u8 opcode;          /* 0x00; RequestActorGraphics or RequestActorPalette */
    u8 slot;            /* 0x01; graphics command also accepts FF for camera bounds */
    u8 resourceIdLE[2]; /* 0x02; actor resource id, or camera region for graphics/FF */
} __attribute__((packed));

struct ScriptFlagCommand
{
    u8 opcode;      /* 0x00; Set/ClearEventFlag or Set/ClearSystemFlag */
    u8 flagIdLE[2]; /* 0x01; opcode selects the event or system namespace */
} __attribute__((packed));

struct ScriptFlagBranchCommand
{
    u8 opcode;      /* 0x00; JumpIfEventFlagSet or JumpIfSystemFlagSet */
    u8 flagIdLE[2]; /* 0x01 */
    u8 entryId;     /* 0x03; target in the current bank */
} __attribute__((packed));

struct ScriptFlagListHeader
{
    u8 opcode;       /* 0x00; SetFlagList or ClearFlagList */
    u8 payloadBytes; /* 0x01; followed by payloadBytes / 2 little-endian ids */
} __attribute__((packed));

struct ScriptFlagListBranchHeader
{
    u8 opcode;       /* 0x00; JumpIfAllFlagsSet/Clear or JumpIfAnyFlagSet */
    u8 payloadBytes; /* 0x01; flag ids below 512 are event ids; others subtract 512 */
    u8 entryId;      /* 0x02; the flag-id byte pairs follow this header */
} __attribute__((packed));

struct ScriptDialogueHeader
{
    u8 opcode;           /* 0x00; SCRIPT_OP_SHOW_DIALOGUE */
    u8 unknown01;        /* 0x01; not read by the handler */
    u8 wordCountLE[2];   /* 0x02 */
    u8 windowStyleLE[2]; /* 0x04 */
} __attribute__((packed));

struct ScriptTextHeader
{
    u8 opcode;         /* 0x00; SCRIPT_OP_DRAW_TEXT */
    u8 unknown01;      /* 0x01; not read by the handler */
    u8 x;              /* 0x02; tile coordinate */
    u8 y;              /* 0x03; tile coordinate */
    u8 wordCountLE[2]; /* 0x04 */
} __attribute__((packed));

struct ScriptChoiceHeader
{
    u8 opcode;           /* 0x00; SCRIPT_OP_SHOW_CHOICES */
    u8 choiceCount;      /* 0x01 */
    u8 unknown02;        /* 0x02; not read by the handler */
    u8 targetEntries[3]; /* 0x03; entry for each option */
    u8 unknown06LE[2];   /* 0x06; original LDRH copies this to gScriptChoiceUnknown06 */
} __attribute__((packed));

typedef char ScriptBankPrefixCheck[(sizeof(struct ScriptBank) == 0x202
    && offsetof(struct ScriptBank, bytecode) == 0x200) ? 1 : -1];
typedef char ScriptNameStrideCheck[(sizeof(struct ScriptName) == 18
    && __alignof__(struct ScriptName) == 2) ? 1 : -1];
typedef char ScriptGlyphTilesCheck[(sizeof(struct ScriptGlyphTiles) == 0x40
    && offsetof(struct ScriptGlyphTiles, bottom) == 0x20) ? 1 : -1];
typedef char ScriptGlyphAtlasRowCheck[(sizeof(struct ScriptGlyphAtlasRow) == 0x800
    && offsetof(struct ScriptGlyphAtlasRow, bottom) == 0x400) ? 1 : -1];
typedef char ScriptFontResourcesCheck[(sizeof(struct ScriptFontResources) == 12
    && offsetof(struct ScriptFontResources, extendedRows) == 4) ? 1 : -1];
typedef char ScriptEntryCommandCheck[(sizeof(struct ScriptEntryCommand) == 2) ? 1 : -1];
typedef char ScriptActorResourceCommandCheck[(sizeof(struct ScriptActorResourceCommand) == 4
    && offsetof(struct ScriptActorResourceCommand, resourceIdLE) == 2) ? 1 : -1];
typedef char ScriptFlagCommandCheck[(sizeof(struct ScriptFlagCommand) == 3
    && offsetof(struct ScriptFlagCommand, flagIdLE) == 1) ? 1 : -1];
typedef char ScriptFlagBranchCommandCheck[(sizeof(struct ScriptFlagBranchCommand) == 4
    && offsetof(struct ScriptFlagBranchCommand, entryId) == 3) ? 1 : -1];
typedef char ScriptFlagListHeadersCheck[(sizeof(struct ScriptFlagListHeader) == 2
    && sizeof(struct ScriptFlagListBranchHeader) == 3) ? 1 : -1];
typedef char ScriptTextHeadersCheck[(sizeof(struct ScriptDialogueHeader) == 6
    && offsetof(struct ScriptDialogueHeader, wordCountLE) == 2
    && offsetof(struct ScriptDialogueHeader, windowStyleLE) == 4
    && sizeof(struct ScriptTextHeader) == 6
    && offsetof(struct ScriptTextHeader, wordCountLE) == 4) ? 1 : -1];
typedef char ScriptChoiceHeaderCheck[(sizeof(struct ScriptChoiceHeader) == 8
    && offsetof(struct ScriptChoiceHeader, targetEntries) == 3
    && offsetof(struct ScriptChoiceHeader, unknown06LE) == 6) ? 1 : -1];
struct ScriptActorCommandHeader
{
    u8 opcode;      /* 0x00; SCRIPT_OP_RUN_ACTOR_COMMANDS */
    u8 payloadSize; /* 0x01; byte length, not a command count */
} __attribute__((packed));

typedef char ScriptActorCommandHeaderCheck[(sizeof(struct ScriptActorCommandHeader) == 2) ? 1 : -1];

/* Only result 1 causes Script_Update to dispatch another command immediately. */
typedef u16 (*ScriptCommandHandler)(const u8 **pc);

/* Private IWRAM .bss objects (0x03000E68..0x03000F30) live in src/script.c. */
/* Shared EWRAM COMMON definitions live in script.c; the linker fixes their addresses. */
extern u16 gScriptTextTilemap[SCRIPT_TILEMAP_WIDTH * SCRIPT_TILEMAP_HEIGHT]; /* 0x02005800 */
#define SCRIPT_ENTRY_OFFSETS ((u16 *)0x02016000)
#define SCRIPT_BYTECODE ((u16 *)0x02016200) /* Variable-length bank; PC offsets are bytes. */
#define SCRIPT_GLYPH_VRAM ((void *)0x0600B800)
extern struct ScriptGlyphTiles gScriptGlyphCacheTiles[SCRIPT_GLYPH_CACHE_COUNT]; /* 0x0203DE00 */

/* COMMON object: its tentative definition is in src/script.c. */
extern u16 gScriptChoiceUnknown06;         /* 0x03004F9C; meaning not established */

/* ROM objects. Bank table contains aliases to the same compressed bank. */
extern ScriptCommandHandler const gScriptCommandTable[SCRIPT_COMMAND_COUNT]; /* 0x0862D434 */
extern const struct ScriptName gScriptNames[SCRIPT_NAME_COUNT]; /* 0x0862D574 */
extern const struct LzStreamHeader *const gScriptBankTable[SCRIPT_BANK_COUNT]; /* 0x087ED6D4 */
extern const struct ScriptFontResources gScriptFontResources; /* 0x087ED904 */

void Script_Update(void);
void Script_UpdateVBlank(void);
u16 Script_GetFlags(void);
void Script_Init(void);
/* LoadBank selects an entry for mode 2 and the bank head for all other modes. */
void Script_LoadBank(u8 bankId, u8 entryId, u8 mode);
/* Start modes: 1 = bank head, 2 = selected entry, 3 = current pc. */
void Script_Start(u8 entryId, u8 mode);
/* Stop mode 3 preserves pc; all other modes reset it to the bank head. */
void Script_Stop(u8 mode);

/* fontId is narrowed to u16 and always forced to zero. */
void Script_RestoreDefaultGlyphs(u16 fontId);
void Script_ClearGlyphCache(void);
/* scanFlags controls append, termination, and optional length; bitsPerUnit is 8 or 16. */
void Script_CacheGlyphsFromBytes(const u8 *text, u16 scanFlags, u8 bitsPerUnit);
void Script_CacheGlyphsFromWords(const u16 *text, u16 scanFlags);
/* Returns -1 after completing the upload, including an empty cache. */
s16 Script_UploadGlyphCache(void);
u16 Script_GetGlyphCache(u16 **glyphIds);
/* Returns 1 if an extended glyph upload was requested, otherwise 0. */
u8 Script_CacheNameGlyphs(u8 nameId);
/* Draws one glyph and advances the private name-glyph index; 1 means terminator. */
u8 Script_DrawNameCharacter(u16 *tilemap, u8 nameId, u8 x, u8 y);
/* Modes 0/1/2 are linear/ease-in/ease-out. duration must be nonzero. */
s16 Script_Interpolate(s16 start, s16 delta, s16 duration, s16 frame, s8 mode);

/* Opcode entry points; pc points to the current opcode. See docs/SCRIPT.md. */
u16 ScriptCmd_ShowDialogue(const u8 **pc); /* 00 */
u16 ScriptCmd_Jump(const u8 **pc); /* 01 */
u16 ScriptCmd_Call(const u8 **pc); /* 02 */
u16 ScriptCmd_Return(const u8 **pc); /* 03 */
u16 ScriptCmd_RunActorCommands(const u8 **pc); /* 04 */
u16 ScriptCmd_Reserved(const u8 **pc); /* 05 */
u16 ScriptCmd_End(const u8 **pc); /* 06 */
u16 ScriptCmd_WaitActorCommands(const u8 **pc); /* 07 */
u16 ScriptCmd_RequestActorGraphics(const u8 **pc); /* 08 */
u16 ScriptCmd_RequestActorPalette(const u8 **pc); /* 09 */
u16 ScriptCmd_WaitActorResources(const u8 **pc); /* 0A */
u16 ScriptCmd_FadeScreenOut(const u8 **pc); /* 0B */
u16 ScriptCmd_FadeScreenIn(const u8 **pc); /* 0C */
u16 ScriptCmd_WaitScreenTransition(const u8 **pc); /* 0D */
u16 ScriptCmd_ChangeMap(const u8 **pc); /* 0E */
u16 ScriptCmd_JumpIfEventFlagSet(const u8 **pc); /* 0F */
u16 ScriptCmd_SetEventFlag(const u8 **pc); /* 10 */
u16 ScriptCmd_ClearEventFlag(const u8 **pc); /* 11 */
u16 ScriptCmd_JumpIfSystemFlagSet(const u8 **pc); /* 12 */
u16 ScriptCmd_SetSystemFlag(const u8 **pc); /* 13 */
u16 ScriptCmd_ClearSystemFlag(const u8 **pc); /* 14 */
u16 ScriptCmd_CallBank(const u8 **pc); /* 15 */
u16 ScriptCmd_ReturnBank(const u8 **pc); /* 16 */
u16 ScriptCmd_DrawText(const u8 **pc); /* 17 */
u16 ScriptCmd_DisableCameraFollow(const u8 **pc); /* 18 */
u16 ScriptCmd_EnableCameraFollow(const u8 **pc); /* 19 */
u16 ScriptCmd_PanCamera(const u8 **pc); /* 1A */
u16 ScriptCmd_WaitCameraPan(const u8 **pc); /* 1B */
u16 ScriptCmd_RemovePartyMember(const u8 **pc); /* 1C */
u16 ScriptCmd_AddPartyMember(const u8 **pc); /* 1D */
u16 ScriptCmd_LoadActorAnimation(const u8 **pc); /* 1E */
u16 ScriptCmd_StartActorAnimation(const u8 **pc); /* 1F */
u16 ScriptCmd_WaitActorAnimation(const u8 **pc); /* 20 */
u16 ScriptCmd_JumpIfPartyMembershipMatches(const u8 **pc); /* 21 */
u16 ScriptCmd_StartBattle(const u8 **pc); /* 22 */
u16 ScriptCmd_SetTextRectTiles(const u8 **pc); /* 23 */
u16 ScriptCmd_EnableTextLayer(const u8 **pc); /* 24 */
u16 ScriptCmd_DisableTextLayer(const u8 **pc); /* 25 */
u16 ScriptCmd_WaitFrames(const u8 **pc); /* 26 */
u16 ScriptCmd_LoadMapAnimationGroup(const u8 **pc); /* 27 */
u16 ScriptCmd_ResumeMapAnimation(const u8 **pc); /* 28 */
u16 ScriptCmd_PauseMapAnimation(const u8 **pc); /* 29 */
u16 ScriptCmd_WaitMapAnimation(const u8 **pc); /* 2A */
u16 ScriptCmd_LoadPaletteAnimationGroup(const u8 **pc); /* 2B */
u16 ScriptCmd_ResumePaletteAnimation(const u8 **pc); /* 2C */
u16 ScriptCmd_PausePaletteAnimation(const u8 **pc); /* 2D */
u16 ScriptCmd_WaitPaletteAnimation(const u8 **pc); /* 2E */
u16 ScriptCmd_RestorePartyHpMp(const u8 **pc); /* 2F */
u16 ScriptCmd_SetEquipment(const u8 **pc); /* 30 */
u16 ScriptCmd_ChangeItemCount(const u8 **pc); /* 31 */
u16 ScriptCmd_ChangeGold(const u8 **pc); /* 32 */
u16 ScriptCmd_ShowChoices(const u8 **pc); /* 33 */
u16 ScriptCmd_PlayBgm(const u8 **pc); /* 34 */
u16 ScriptCmd_StopBgm(const u8 **pc); /* 35 */
u16 ScriptCmd_SetBgmVolume(const u8 **pc); /* 36 */
u16 ScriptCmd_FadeInBgm(const u8 **pc); /* 37 */
u16 ScriptCmd_FadeOutBgm(const u8 **pc); /* 38 */
u16 ScriptCmd_PlaySfx(const u8 **pc); /* 39 */
u16 ScriptCmd_StopSfx(const u8 **pc); /* 3A */
u16 ScriptCmd_JumpIfItemCountFull(const u8 **pc); /* 3B */
u16 ScriptCmd_ToggleLastChest(const u8 **pc); /* 3C */
u16 ScriptCmd_OpenShop(const u8 **pc); /* 3D */
u16 ScriptCmd_WaitShopResult(const u8 **pc); /* 3E */
u16 ScriptCmd_AddCard(const u8 **pc); /* 3F */
u16 ScriptCmd_RemoveCard(const u8 **pc); /* 40 */
u16 ScriptCmd_JumpIfCardMissing(const u8 **pc); /* 41 */
u16 ScriptCmd_SetCollectionEventFlag(const u8 **pc); /* 42 */
u16 ScriptCmd_SetFlagList(const u8 **pc); /* 43 */
u16 ScriptCmd_ClearFlagList(const u8 **pc); /* 44 */
u16 ScriptCmd_JumpIfAllFlagsSet(const u8 **pc); /* 45 */
u16 ScriptCmd_JumpIfAllFlagsClear(const u8 **pc); /* 46 */
u16 ScriptCmd_JumpIfAnyFlagSet(const u8 **pc); /* 47 */
u16 ScriptCmd_ClearLocalSystemFlags(const u8 **pc); /* 48 */
u16 ScriptCmd_JumpIfGoldGreater(const u8 **pc); /* 49 */
u16 ScriptCmd_StartTitleLogo(const u8 **pc); /* 4A */
u16 ScriptCmd_WaitTitleLogo(const u8 **pc); /* 4B */
u16 ScriptCmd_RaiseCharacterLevel(const u8 **pc); /* 4C */
u16 ScriptCmd_ControlSceneEffect(const u8 **pc); /* 4D */
u16 ScriptCmd_JumpRandom(const u8 **pc); /* 4E */
u16 ScriptCmd_CallAlternate(const u8 **pc); /* 4F */
/* Reserved (05) leaves pc and the return register unchanged. */


/* Read-only palettes used by script commands. */
extern const u16 D_0808B1B4[16];                                /* 0x0808B1B4 */

#endif
