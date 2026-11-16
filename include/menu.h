#ifndef MENU_H
#define MENU_H

#include "common.h"
#include "core.h"
#include "party.h"
#include "script.h"
#include "sound.h"

#define SAVE_SLOT_COUNT 4
#define SAVE_SLOT_SIZE 0x2000
#define SAVE_SLOT_BUFFER ((u8 *)0x02021000)

#define MENU_ENTRY_COUNT 15
#define MENU_WIDGET_COUNT 10
#define MENU_SAVED_SELECTION_COUNT 16
#define MENU_INVENTORY_WINDOW_SIZE 10
#define MENU_EQUIPMENT_WINDOW_SIZE 5
#define MENU_DESTINATION_COUNT 16
#define MENU_HINT_BANK_COUNT 8
#define MENU_MOVE_FRAMES 8

enum MenuPage
{
    MENU_PAGE_PARTY = 0,
    MENU_PAGE_MAIN = 1,
    MENU_PAGE_STATUS = 2,
    MENU_PAGE_ITEMS = 3,
    MENU_PAGE_EQUIPMENT = 4,
    MENU_PAGE_FIELD_SKILLS = 5,
    MENU_PAGE_SAVE = 6,
    MENU_PAGE_FORMATION = 7
};

enum MenuDrawMode
{
    MENU_DRAW_OVERVIEW = 0,
    MENU_DRAW_STATUS = 1,
    MENU_DRAW_ITEMS = 2,
    MENU_DRAW_EQUIPMENT = 3,
    MENU_DRAW_FIELD_SKILLS = 4,
    MENU_DRAW_SAVE = 5,
    MENU_DRAW_FORMATION = 6
};

enum MenuEntryFlags
{
    MENU_ENTRY_BLINK = 0x01,
    MENU_ENTRY_FIXED_SELECTED_FRAME = 0x02,
    MENU_ENTRY_LABEL_PALETTE = 0x04,
    MENU_ENTRY_HIGHLIGHT = 0x08,
    MENU_ENTRY_HIDDEN = 0x40,
    MENU_ENTRY_ACTIVE = 0x80
};

enum MenuExtraFlags
{
    MENU_EXTRA_SOUND_TEST_UNLOCKED = 0x01
};

/* Software cursor in pixels; bit 0 selects the settled cursor tiles. */
struct MenuCursor
{
    u8 flags;                 /* +00; bit 0 is clear while moving */
    u8 blinkCounter;          /* +01; bit 3 alternates the displayed tiles */
    u8 transitionFrames;      /* +02; remaining updates in an eight-frame move */
    u8 reserved03;            /* +03; reset to zero, no other use confirmed */
    u16 x;                    /* +04 */
    u16 y;                    /* +06 */
    u16 targetX;              /* +08 */
    u16 targetY;              /* +0A */
    u16 originX;              /* +0C */
    u16 originY;              /* +0E */
};

struct MenuCursorPosition
{
    u16 x;
    u16 y;
};

typedef char MenuCursorPositionSizeCheck[(sizeof(struct MenuCursorPosition) == 4) ? 1 : -1];

/* Fifteen UI records. Entries 0..4 are party portraits, 5..14 are labels. */
struct MenuEntry
{
    u8 flags;                 /* +00; MenuEntryFlags */
    u8 blinkCounter;          /* +01; bit 3 selects tile +0x10 when blinking */
    u8 transitionFrames;      /* +02 */
    u8 spriteNodeIndex;       /* +03; index in gSpriteNodes */
    u16 x;                    /* +04; software pixel coordinate */
    u16 y;                    /* +06 */
    u16 targetX;              /* +08 */
    u16 targetY;              /* +0A */
    u16 originX;              /* +0C */
    u16 originY;              /* +0E */
    u16 tile;                 /* +10; base OBJ tile number */
    u16 reserved12;           /* +12; no access found in the menu update paths */
};

/* The two opening screens share one packed tiles/palettes/tilemaps resource. */
struct TitlePresentationResources
{
    u8 firstTiles[0x76C] __attribute__((aligned(4)));
    u8 secondTiles[0x794] __attribute__((aligned(4)));
    u8 firstPalette[0x84] __attribute__((aligned(4)));
    u8 secondPalette[0x64] __attribute__((aligned(4)));
    u8 firstTilemap[0x16C] __attribute__((aligned(4)));
    u8 secondTilemap[0x1D0] __attribute__((aligned(4)));
};

typedef char TitlePresentationResourcesSizeCheck[(sizeof(struct TitlePresentationResources) == 0x1324) ? 1 : -1];
typedef char TitlePresentationSecondTilesCheck[(offsetof(struct TitlePresentationResources, secondTiles) == 0x76C) ? 1 : -1];
typedef char TitlePresentationFirstPaletteCheck[(offsetof(struct TitlePresentationResources, firstPalette) == 0xF00) ? 1 : -1];
typedef char TitlePresentationSecondPaletteCheck[(offsetof(struct TitlePresentationResources, secondPalette) == 0xF84) ? 1 : -1];
typedef char TitlePresentationFirstTilemapCheck[(offsetof(struct TitlePresentationResources, firstTilemap) == 0xFE8) ? 1 : -1];
typedef char TitlePresentationSecondTilemapCheck[(offsetof(struct TitlePresentationResources, secondTilemap) == 0x1154) ? 1 : -1];

struct CollectionCardBackgroundResources
{
    u8 tilemap[0x1F0] __attribute__((aligned(4)));
    u16 palettes[3][16];
};

typedef char CollectionCardBackgroundResourcesSizeCheck[(sizeof(struct CollectionCardBackgroundResources) == 0x250) ? 1 : -1];

/* Shared RAM declarations, in address order. */
extern struct MenuEntry gMenuWidgetEntries[MENU_WIDGET_COUNT];  /* 0x030000BC; Alias of gMenuEntries[5], with no separate allocation. */
extern u8 gMenuContext;                                         /* 0x03004D40; chooses menu layout/context */
extern u8 gSaveIoState;                                         /* 0x03004D44; FE is ready, 1 reads, 3 writes */
extern u16 gMenuExtraFlags;                                     /* 0x03004D48; persisted in bonus slot; COMMON occupies 4 bytes */
extern u8 gMenuState;                                           /* 0x03004D4C; Menu dispatch state; zero permits field movement. */
extern u8 gSaveLastUsedSlot;                                    /* 0x03004D50; last normal save/load slot (0..2) */
extern u8 gCollectionPackedCardCounts[90];                      /* 0x03004D60; Even ids use the low nibble, odd ids the high nibble. */
extern u16 gCollectionRatingAnimationCounter;                   /* 0x03004DBC; Draw counter: bits 2..3 animate stars; bits 4..5 cycle the complete rating palette. */
extern u8 gSceneEffectTilemapUploadPending;                     /* 0x03004DC0 */
extern u8 gShopActive;                                          /* 0x03004DC4; Cleared on close; script opcode 3E waits while nonzero. */
extern u8 gCollectionEventFlags[8];                             /* 0x03004DC8; Sixty-four collection event bits, also consumed by the gallery. */
extern u8 gSaveIoBlockIndex;                                    /* 0x03004DD0; 0x800-byte SRAM block cursor; bonus writes start at 12. */
extern u8 gShopId;                                              /* 0x03004DD4; Index of an eight-item shop catalogue. */
extern u8 gShopTransactionOccurred;                             /* 0x03004DD8; Reset on open; successful buy or sale sets 1. */
extern u16 gMenuSavedDisplayControl;                            /* 0x03004DDC; Saved DISPCNT shared by the field menu and shop. */
extern u16 gMenuSavedBlendControl;                              /* 0x03004DE0; Saved BLDCNT shared by the field menu and shop. */
extern u16 gCollectionOwnedCardTypeCount;                       /* 0x03004DE4; Distinct owned ordinary types 0..175; excludes four special cards. */
extern u16 gSaveBonusSlotValid;                                 /* 0x03004DE8; Fourth SRAM slot signature accepted; also selects the default title option. */

/* Read-only ROM data, in address order. */
extern const u8 gGalleryUnlockEventIds[];                       /* 0x08089B90; Forty-nine gallery records mapped to collection event bits. */
extern const u16 gMenuTextPalettes[2][16];                     /* 0x0808B7D4; UI palette banks 14 and 15. */
extern const u8 gShopItemCatalogs[];                            /* 0x08093550; Shop inventory: eight bytes per shop. */
extern const u8 gMenuHintText[];                                /* 0x080936A0; 456 strings */
extern const u8 gMenuItemNames[PARTY_ITEM_COUNT][8];            /* 0x08095028 */
extern const u8 gMenuSkillNames[62][8];                         /* 0x08095828; one-based skill ids */
extern const struct TitlePresentationResources gTitlePresentationResources; /* 0x080963C0 */
extern const struct CollectionCardBackgroundResources gCollectionCardBackgroundResources;         /* 0x08097B60; LZ tilemap and three BG palettes. */
extern const u16 gCollectionEnemyCardFrameTiles[];              /* 0x08097DB0; Enemy/special-card frame template: ten columns by sixteen rows. */
extern const u16 gCollectionDragonCardFrameTiles[];             /* 0x08097EF0; Dragon-card frame template: ten columns by sixteen rows. */
extern const u16 gCollectionCharacterCardFrameTiles[];          /* 0x08098030; Character-card frame template: ten columns by sixteen rows. */
extern const u8 gTitleGameArtsCreditText[];                     /* 0x08098170; Single-row copyright and GAME ARTS credit, terminated by FF. */
extern const u8 gTitleMediaRingsCreditText[];                   /* 0x08098180; Single-row MEDIA RINGS CORPORATION credit, terminated by FF. */
extern const u8 gSaveSlotSignature[12];                         /* 0x08098199 */
extern const u8 gMenuEmptySaveText[10];                         /* 0x080981A5 */
extern const u16 gSaveNormalFieldSizes[];                                  /* 0x080981B0; Normal save field lengths, terminated by zero. */
extern const u16 gSaveBonusFieldSizes[];                                  /* 0x080981E6; Bonus field lengths, terminated by zero. */
extern const u8 gMenuTravelDestinationRecords[];                /* 0x080981EE; Fifteen six-byte records: scene, x, y, direction, region lo/hi. */
extern const u16 gSoundTestSfxIds[];                            /* 0x0809824A; Ninety-five sound-test entries mapped to actual SFX ids. */
extern const u8 gMenuCharacterNameTiles[11][4];                 /* 0x08098308; Four BG tiles per character. */
extern const struct MenuCursorPosition gMenuPartyCursorPositions[];
extern const struct MenuCursorPosition gMenuMainCursorPositions[];
extern const struct MenuCursorPosition gMenuStatusCursorPositions[];
extern const struct MenuCursorPosition gMenuItemCursorPositions[];
extern const struct MenuCursorPosition gMenuEquipmentCursorPositions[];
extern const struct MenuCursorPosition gMenuFieldSkillCursorPositions[];
extern const struct MenuCursorPosition gMenuSaveCursorPositions[];
extern const struct MenuCursorPosition gMenuFormationPageCoordinates[]; /* Formation slots begin at entry 2. */
extern const struct MenuCursorPosition gTitleInactiveCursorPositions[];
extern const struct MenuCursorPosition gTitleMainCursorPositions[];
extern const struct MenuCursorPosition gTitleSaveCursorPositions[];
extern const struct MenuCursorPosition gTitleOptionCursorPositions[];
extern const struct MenuCursorPosition gCollectionCardCursorPositions[];
extern const struct MenuCursorPosition gShopActionCursorPositions[];
extern const struct MenuCursorPosition gShopBuyCursorPositions[];
extern const struct MenuCursorPosition gShopSellCursorPositions[];
extern const u8 gMenuOverviewLayout[];                          /* 0x080984EC */
extern const u8 gMenuExperienceLayout[];                        /* 0x08098510 */
extern const u8 gMenuCharacterStatsLayout[];                    /* 0x08098520 */
extern const u8 gMenuCharacterEquipmentLayout[];                /* 0x08098563 */
extern const u8 gMenuItemHeaderLayout[];                        /* 0x08098588 */
extern const u8 gMenuEquipmentStatsLayout[];                    /* 0x08098599 */
extern const u8 gMenuEquipmentSlotLabels[];                     /* 0x080985DC */
extern const u8 gMenuUnequipLayout[];                           /* 0x08098601 */
extern const u8 gMenuSelectedUnequipLayout[];                   /* 0x08098609 */
extern const u8 gMenuFieldSkillLayout[];                                   /* 0x08098611; Field skill page labels. */
extern const u8 gTitleLunarLogoLayout[];                        /* 0x08098622; Double-row L U N A R logo text. */
extern const u8 gTitleOptionsHeaderLayout[];                  /* 0x08098630; OPTION heading. */
extern const u8 gGalleryHeaderLayout[];                       /* 0x0809863B; LUNAR GALLERY and record number headings. */
extern const u8 gShopBuyTitleLayout[];                          /* 0x08098656; Seven-byte buy-page title at x=6, y=1, palette 13. */
extern const u8 gShopSellTitleLayout[];                         /* 0x0809865D; Seven-byte sell-page title at x=6, y=1, palette 13. */
extern const u8 gShopCannotSellText[];                          /* 0x08098748; Zero-price sale warning, followed by FF. */
/* Independent positioned labels and count-prefixed OBJ layouts. */
extern const u8 gMenuSaveLabel[];
extern const u8 gMenuLoadLabel[];
extern const u8 gMenuReturnToTitleLabel[];
extern const u8 gMenuSaveConfirmYesLabel[];
extern const u8 gMenuSaveConfirmNoLabel[];
extern const u8 gMenuFormationLabel[];
extern const u8 gMenuTacticsLabel[];
extern const u8 gMenuTacticsAttackLabel[];
extern const u8 gMenuTacticsSkillLabel[];
extern const u8 gMenuTacticsDefendLabel[];
extern const u8 gMenuTacticsItemLabel[];
extern const u8 gMenuSelectedTacticsAttackLabel[];
extern const u8 gMenuSelectedTacticsSkillLabel[];
extern const u8 gMenuSelectedTacticsDefendLabel[];
extern const u8 gMenuSelectedTacticsItemLabel[];
extern const u8 gTitleBgmLabel[];
extern const u8 gTitleSfxLabel[];
extern const u8 gTitleCardLabel[];
extern const u8 gTitleGalleryLabel[];
extern const u8 gCollectionCardHeaderLabel[];
extern const u8 gCollectionViewCardLabel[];
extern const u8 gCollectionTradeCardLabel[];
extern const u8 gCollectionCardNumberLabel[];
extern const u8 gCollectionConfirmYesLabel[];
extern const u8 gCollectionConfirmNoLabel[];
extern const u8 gTitleLockedBgmLabel[];
extern const u8 gTitleLockedSfxLabel[];
extern const u8 gShopBuyLabel[];
extern const u8 gShopSellLabel[];
extern const u8 gShopExitLabel[];
extern const u8 gShopPurchaseYesLabel[];
extern const u8 gShopPurchaseNoLabel[];
extern const u8 gShopEquipYesLabel[];
extern const u8 gShopEquipNoLabel[];
extern const u8 gMenuOverviewSpriteLayout[];
extern const u8 gMenuCharacterSpriteLayout[];
extern const u8 gMenuTacticsSpriteLayout[];
extern const u8 gShopHeaderSpriteLayout[];
extern const u16 gMenuPreferredInventoryItems[];                                  /* 0x0809878A; Preferred inventory selection order. */
extern const u8 gMenuPartyActionHintIds[];                                   /* 0x0809879C; Main-menu hint ids for a selected party member. */
extern const u32 gMenuDecimalPowers[8];                         /* 0x080987A4; 1 through 10000000 */
extern const u8 gDungeonEscapeDestinations[];                   /* 0x080987C4; Twenty-one seven-byte records: source, destination, x, y, direction, region lo/hi; zero terminator. */
extern const u8 gTitleLogoSubtitleGlyphs[];                     /* 0x08098858; Five single-row subtitle glyphs, followed by FF. */
extern const u8 gTitlePressStartText[];                         /* 0x0809885E; PRESS START BUTTON single-row text, terminated by FF. */
extern const u8 gTitleNewGameText[];                            /* 0x08098871; NEW GAME single-row text, terminated by FF. */
extern const u8 gTitleLoadGameText[];                           /* 0x0809887A; LOAD GAME single-row text, terminated by FF. */
extern const u8 gTitleOptionText[];                             /* 0x08098884; OPTION single-row text, terminated by FF. */
extern const u8 gCollectionCardDescriptors[180 * 4];            /* 0x0809888B; 180 four-byte descriptors: kind, id low/high, flags. */
extern const u16 gMenuPartyPortraitPalettes[11][16];            /* 0x0809E4E4 */
extern const u16 gMenuSharedSpritePalette[16];                  /* 0x0809E644; Shared cursor OBJ palette. */
/* Portrait indices match gPartyMembers; the last sheet contains shared cursor graphics. */
extern const u8 gMenuCharacter00PortraitTiles[];
extern const u8 gMenuCharacter01PortraitTiles[];
extern const u8 gMenuCharacter02PortraitTiles[];
extern const u8 gMenuCharacter03PortraitTiles[];
extern const u8 gMenuCharacter04PortraitTiles[];
extern const u8 gMenuCharacter05PortraitTiles[];
extern const u8 gMenuCharacter06PortraitTiles[];
extern const u8 gMenuCharacter07PortraitTiles[];
extern const u8 gMenuCharacter08PortraitTiles[];
extern const u8 gMenuCharacter09PortraitTiles[];
extern const u8 gMenuCharacter10PortraitTiles[];
extern const u8 gMenuSharedSpriteTiles[];
extern const u8 gTitleOptionTiles[];                          /* 0x080A0B58; Compressed title option graphics. */
extern const u16 gTitleOptionPalettes[2][16];                 /* 0x080A12D0; OBJ palette banks 10 and 11. */
extern const u8 gMenuSceneNames[];                              /* 0x0830FC04; sequential FF-terminated names */
extern const u8 gTitleMenuTilemap[];                          /* 0x083937E8; Compressed title menu background tilemap. */
extern u8 *const gSaveNormalFieldPointers[];                                  /* 0x087EB180; Normal save field sources and destinations. */
extern u8 *const gSaveBonusFieldPointers[];                                  /* 0x087EB1E8; Bonus field destinations: flags, packed card counts and event bits. */
extern const struct MenuCursorPosition *const gMenuCursorPositionTables[8]; /* Field-menu pages. */
extern const struct MenuCursorPosition *const gTitleMenuCursorPositionTables[6]; /* Title-menu pages. */
extern const struct MenuCursorPosition *const gMenuContextCursorPositionTables[4]; /* Shop/context pages. */
extern const u8 *const gMenuSaveOptionTexts[5];                 /* 0x087EB23C */
extern const u8 *const gMenuFormationOptionTexts[10];           /* 0x087EB250 */
extern const u8 *const gTitleOptionLabelLayouts[];              /* 0x087EB278; Title/card labels; entries 10/11 are the locked BGM/SFX question marks. */
extern const u8 *const gShopActionAndConfirmationTexts[];       /* 0x087EB2A8; Seven shop action/confirmation labels: x, y, glyphs..., FF. */
extern const u8 *const gMenuLabelLayouts[7];                    /* 0x087EB2C4; Four count/x/y/tile layouts, followed by three title option texts. */
extern const void *const gMenuSpriteGraphicsSources[12];        /* 0x087EB2E0 */

#define MENU_TEXT_ADDR(x, y) ((u16 *)0x02005800 + (x) + (y) * 32)

void Menu_ResetNavigation(void);
void Menu_UpdateState(void);
void Menu_UpdateVBlank(void);
void Menu_UpdateFrame(void);
void Menu_UpdateInputAndCursor(void);
void Menu_InitEntries(u8 skipLabels);
void Menu_InitSprites(void);
void Menu_OnCursorMoveFinished(void);
/* FF keeps the page while moving the cursor to its current selection. */
void Menu_TransitionToPage(u8 page);
void Menu_SetPartyEntryState(u8 mode, u8 state, u8 position);
void Menu_SetSelectedPartyEntryState(u8 mode, u8 state);
void Menu_MovePartyEntry(u8 position, u8 mode);
void Menu_CreateLabelEntries(u8 layoutId);
void Menu_UpdateEntrySprites(void);
void Menu_SetOtherEntriesVisible(u8 position, u8 mode);
void Menu_HideLabelEntries(void);
/* 5 means the character was not found in the first five party slots. */
u8 Menu_FindPartySlotForFormationSlot(u8 position);
void Menu_LoadPartyPortrait(u8 partySlot);
void Menu_UploadPartyPortrait(u8 partySlot);
void Menu_LoadSharedSpriteGraphics(void);
void Menu_UploadSharedSpriteGraphics(void);
void Menu_DrawWindowTransition(u16 frame);
void Menu_ClearTextRect(u8 x, u8 y, u8 width, u8 height);
void Menu_DrawScrollArrow(u8 x, u8 y, u8 mode);
u16 *Menu_GetTextAddress(u8 x, u8 y);
void Menu_DrawSingleRowText(u16 *dest, const u8 *text, u8 palette);
void Menu_EraseSingleRowText(u16 *dest, const u8 *text);
void Menu_DrawTextAt(u8 x, u8 y, u8 palette, const u8 *text);
void Menu_RefreshPartyHeaders(void);
void Menu_DrawContents(void);
void Menu_DrawCharacterName(u8 characterId, u8 x, u8 y);
void Menu_DrawCharacterLevel(u8 characterId, u8 x, u8 y);
void Menu_DrawCharacterHp(u8 characterId, u8 x, u8 y);
void Menu_DrawCharacterMp(u8 characterId, u8 x, u8 y);
/* Value and palette inputs are normalized to their low 16 bits. */
void Menu_DrawNumber3(s32 value, u16 *dest, u32 paletteBits);
void Menu_DrawDefaultNumber3(s16 value, u16 *dest);
void Menu_DrawGlyph(u16 *dest, u16 glyph, u8 palette);
void Menu_DrawLayout(const u8 *layout);
void Menu_DrawDecimal(u16 *dest, u32 value, u8 palette);
void Menu_DrawPlayTime(u16 *dest, u32 frames, u8 palette);
u8 Menu_ExtractDecimalDigit(s32 *value, s32 divisor);
u16 *Menu_DrawSceneName(u16 *dest, u16 sceneId, u8 palette);
void Menu_DrawSceneNameWithClosingBracket(u16 *dest, u16 sceneId, u8 palette);
void Menu_InitHintBanks(void);
void Menu_DrawHintText(const u8 *text, u8 palette);
void Menu_ShowHint(u16 hintId);
void Menu_ShowHintWithPalette(u16 hintId, u8 palette);
/* Preserves the original return-register ABI; the result must be ignored. */
s32 Menu_SetTextVisible(const u8 *text, u8 palette, u8 visible);
u8 Menu_GetSelectedPartySlot(void);
void Menu_DrawCharacterPanel(u8 panel);
void Menu_DrawItemName(u8 itemId, u8 x, u8 y, u8 palette);
void Menu_DrawSkillName(u8 skillId, u8 x, u8 y, u8 palette);
void Menu_DrawInventory(void);
void Menu_RefreshInventoryScrollItems(void);
void Menu_SelectFirstInventoryItem(void);
u8 Menu_FindPreviousInventoryItem(void);
u8 Menu_FindInventoryItemAtOffset(u8 offset);
/* These inventory actions return the hint id to display. */
u16 Menu_SelectInventoryItem(u8 itemId);
u16 Menu_UseRecoveryItem(void);
void Menu_DrawDestinations(void);
void Menu_BuildTravelDestinations(void);
u8 Menu_ConfirmTravelDestination(void);
u8 Menu_TryEscapeDungeon(void);
void Menu_BuildEquipmentCandidates(void);
void Menu_RefreshEquipmentScrollItems(void);
s32 Menu_EquipItem(u8 slot, u8 itemId);
s32 Menu_EquipSelectedItem(void);
void Menu_PreviewEquipment(u8 itemId, u8 slot, u8 characterIndex);
void Menu_PreviewSelectedEquipment(void);
u8 Menu_GetAdjustedSkillMpCost(u8 characterId, u8 skillId);
void Menu_DrawFieldSkillList(u8 characterId, u8 skillIndex);
/* 0 unavailable, 1 action completed, 2 recovery skill prepared. */
u8 Menu_PrepareFieldSkill(u8 skillId);
void Menu_UseFieldSkill(u8 mode);
void Menu_SetRecoveryAnimation(u8 position, u8 mode);
void Menu_DrawSaveSlots(void);
void Menu_LoadTacticsForEditing(void);
void Menu_CommitTactics(void);
u8 Menu_CanReserveTacticsItem(u8 characterIndex, u8 itemId);
void Menu_DrawTacticsSkillList(void);
void Menu_DrawTacticsItemList(void);
u8 Menu_GetTacticsSkillAtOffset(u8 offset);
/* One-based white-list selection; 0 means no available item. */
u8 Menu_GetFirstTacticsItemSelection(void);
u8 Menu_FindPreviousTacticsItemIndex(void);
u8 Menu_GetTacticsItemAtIndex(u8 index);
/* 0 no successor, FF incomplete window, otherwise a white-list index. */
u8 Menu_FindNextFullTacticsItemWindow(void);

typedef char MenuCursorSizeCheck[(sizeof(struct MenuCursor) == 0x10) ? 1 : -1];
typedef char MenuCursorPositionCheck[(offsetof(struct MenuCursor, x) == 4) ? 1 : -1];
typedef char MenuCursorOriginCheck[(offsetof(struct MenuCursor, originX) == 0xC) ? 1 : -1];
typedef char MenuEntrySizeCheck[(sizeof(struct MenuEntry) == 0x14) ? 1 : -1];
typedef char MenuEntryNodeCheck[(offsetof(struct MenuEntry, spriteNodeIndex) == 3) ? 1 : -1];
typedef char MenuEntryTileCheck[(offsetof(struct MenuEntry, tile) == 0x10) ? 1 : -1];

/* Shop services. */
void Shop_Open(u8 shopId);
void Shop_UpdateState(void);
void Shop_UpdateFrame(void);
void Shop_InitSprites(void);
void Shop_OnCursorMoveFinished(void);
void Shop_UpdateInputAndCursor(void);
void Shop_SetActionLabelsVisible(u8 visible);
/* 0/1 erase/draw list confirmation; 2/3 erase/draw header confirmation. */
void Shop_SetConfirmationLabels(u8 mode);
void Shop_DrawBuyList(void);
void Shop_DrawSellList(void);
void Shop_UpdateBuyScrollArrows(void);
/* Four-row shop sale list backed by the player's inventory. */
void Shop_UpdateSellScrollArrows(void);

/* Save services. */
void Save_ReadSlotSummary(u8 slot);
void Save_DrawSlotSummary(u8 slot, u8 x, u8 y, u8 palette);

/* A request is advanced by Save_UpdateIo: 1 busy, 0 ready. */
void Save_RequestRead(void);
void Save_RequestWriteAll(void);
void Save_RequestBonusWrite(void);
u8 Save_UpdateIo(u8 suppressSlotDraw);
/* 0 valid, 1 invalid, FF invalid slot. Signature checking omits checksum. */
u8 Save_CheckSlotSignature(u8 slot);
u8 Save_VerifySlotChecksum(u8 slot);
void Save_PackGameSlot(u8 slot);
void Save_LoadGameSlot(u8 slot);
void Save_PackBonusSlot(void);
void Save_UnpackBonusSlot(void);
void Save_LoadBonusData(void);

/* Collection services. */
/* Card ids 0..179 use packed counts; completion counts only ids 0..175. */
/* Returns 0..15 in the full word; menu callers narrow the result to u8. */
u32 Collection_GetCardCount(u8 cardId);
void Collection_AddCard(u8 cardId);
void Collection_RemoveCard(u8 cardId);
void Collection_RefreshOwnedCardCount(void);
void Collection_SetEventFlag(u8 flagId);
u8 Collection_GetEventFlag(u8 flagId);
/* Counts distinct ordinary card types; stars begin at 36, 71, 106, 141, 176. */
void Collection_DrawCompletionRating(u16 ownedTypes);
/* Metadata kinds 0/3 use enemy previews; flag 80 selects the wide frame. */
void Collection_DrawCardPreview(u16 cardId);
void Collection_DrawCardMenu(void);

/* Title menu services. */
void TitleMenu_UpdateState(void);
void TitleMenu_UpdateInputAndCursor(void);
void TitleMenu_DrawMainOptions(void);
void TitleMenu_DrawOptions(void);
void TitleMenu_CreateCursorSprite(void);

#endif
