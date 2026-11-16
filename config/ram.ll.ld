/* Recovered C allocations only; other RAM objects retain fallback addresses. */
SECTIONS
{
  .bss.core 0x03000000 (NOLOAD) :
  {
    "build/core.o"(.bss .bss.*)
  }

  .bss.core.scene 0x030025B0 (NOLOAD) : ALIGN(4)
  {
    . = 0x0; gPlayerDirection = .;
    . = 0x4; gCameraX = .;
    . = 0x8; gScriptedEncounterId = .;
    . = 0xC; gSpriteScratchWidth = .;
    . = 0x10; gPlayerPositionHistoryX = .;
    . = 0x24; gWalkingEncounterCountdown = .;
    . = 0x28; gSceneScriptRunningSnapshot = .;
    . = 0x30; gPlayerPositionHistoryY = .;
    . = 0x40; gTitlePresentationStage = .;
    . = 0x44; gSceneBgmControl = .;
    . = 0x48; gPlayerX = .;
    . = 0x4C; gCameraY = .;
    . = 0x50; gScenePhase = .;
    . = 0x54; gTitleLogoStage = .;
    . = 0x58; gCoreUnknown2608 = .;
    . = 0x5C; gPlayerTransitionStage = .;
    . = 0x60;
  }

  .bss.core.resource_scratch 0x03002C30 (NOLOAD) : ALIGN(4)
  {
    . = 0x0; gSpriteScratchHeight = .;
    . = 0x4; gBattleFieldReturnMode = .;
    . = 0x8;
  }

  .bss.core.gold 0x03002C38 (NOLOAD) : ALIGN(4)
  {
    gPartyGold = .;
    . += 0x4;
  }

  .bss.core.common 0x03002C3C (NOLOAD) : ALIGN(4)
  {
    . = 0x0; gPlayerY = .;
    /* system-owned auto-scroll counter occupies this existing four-byte gap. */
    . = 0x4; gBg1AutoScrollPhase = .;
    . = 0x8; gPlayerFieldFlags = .;
    . = 0xC; gScriptedBattleState = .;
    . = 0x10; gSceneTransitionCounter = .;
    . = 0x14; gFieldInputDelay = .;
    . = 0x1C; gPlayerPositionHistoryDirection = .;
    . = 0x24; gActorAnimationSlotFlags = .;
    . = 0x44; gSpriteDrawOrder = .;
    . = 0x244; gActorStates = .;
    . = 0x604; gPendingActorPaletteId = .;
    . = 0x614; gActorAnimationTileSources = .;
    . = 0x694; gActorResourceLoadFlags = .;
    . = 0x698; gPendingActorGraphicsId = .;
    . = 0x6A4; gActorAnimationPalettes = .;
    . = 0x724; gSpriteTileTransferCounts = .;
    . = 0x744; gSpritePaletteTransfers = .;
    . = 0x844; gPendingSceneScriptEntryId = .;
    . = 0x854; gActorAnimationRenderSlots = .;
    . = 0x874; gPendingActorPaletteSlot = .;
    . = 0x884; gSpriteAffineMatrices = .;
    . = 0x984; gSpriteOamBuffer = .;
    . = 0xD84; gSpriteTileTransferAddresses = .;
    . = 0xE84; gSpriteNodes = .;
    . = 0x1884; gActorAnimationDefinitions = .;
    . = 0x1904; gPendingActorGraphicsSlot = .;
    . = 0x1914; gBlendCoefficients = .;
    . = 0x1924; gTransitionWindowRows = .;
    . = 0x19C8; gTransitionWindowRadius = .;
    . = 0x19CC; gSceneBgmId = .;
    . = 0x19D0; gBackgroundScrollMode = .;
    . = 0x19D4; gBg1RasterMode = .;
    . = 0x19D8; gSceneCameraRegionId = .;
    . = 0x19DC; gSceneLocationIndex = .;
    . = 0x19E0; gBackgroundMoveStartY = .;
    . = 0x19E4; gMapExpansionOutput = .;
    . = 0x19E8; gVisitedDestination = .;
    . = 0x19EC; gSceneBackgroundEffect = .;
    . = 0x19F0; gWorldMapConnectionIds = .;
    . = 0x19F4; gBackgroundMoveTargetX = .;
    . = 0x19F8; gSceneScriptBankId = .;
    . = 0x19FC; gSceneEntryTileY = .;
    . = 0x1A00; gMapEventType = .;
    . = 0x1A04; gWorldMapConnectionCount = .;
    . = 0x1A08; gMapEventSampleYs = .;
    . = 0x1A0C; gBg3ScrollY = .;
    . = 0x1A10; gCameraRegionTop = .;
    . = 0x1A14; gCameraRegionLeft = .;
    . = 0x1A18; gMapEventIndex = .;
    . = 0x1A1C; gBlendControl = .;
    . = 0x1A20; gScanlineBlendFlags = .;
    . = 0x1A24; gScriptBackgroundPart = .;
    . = 0x1A28; gBackgroundCameraOffset = .;
    . = 0x1A2C; gTransitionWindowScanlineRadius = .;
    . = 0x1A30; gWorldMapConnectionIndex = .;
    . = 0x1A34; gActorGraphicsIds = .;
    . = 0x1A40; gSceneType = .;
    . = 0x1A44; gBackgroundMotionState = .;
    . = 0x1A48; gBackgroundYState = .;
    . = 0x1A4C; gMapCollisionLayer = .;
    . = 0x1A50; gSceneEntryTileX = .;
    . = 0x1A54; gMapChunkDefinitions = .;
    . = 0x1A58; gPendingOverlayPalette = .;
    . = 0x1A5C; gBg1ScrollMode = .;
    . = 0x1A60; gWorldMapLocationNameGroupId = .;
    . = 0x1A64; gMapAnimationStates = .;
    . = 0x1B64; gMapEventConfig = .;
    . = 0x1B68; gSceneEntryDirection = .;
    . = 0x1B6C; gScanlineBlendPosition = .;
    . = 0x1B70; gBlockingTileThreshold = .;
    . = 0x1B74; gSceneId = .;
    . = 0x1B78; gBackgroundMoveDuration = .;
    . = 0x1B7C; gWorldMapTargetY = .;
    . = 0x1B80; gWorldMapRegionId = .;
    . = 0x1B84; gBackgroundXState = .;
    . = 0x1B88; gCameraRegionRight = .;
    . = 0x1B8C; gScriptBackgroundUploadKind = .;
    . = 0x1B90; gPendingOverlayGraphics = .;
    . = 0x1B94; gActorPaletteIds = .;
    . = 0x1BA0; gBackgroundMoveStartX = .;
    . = 0x1BA4; gWorldMapLocationId = .;
    . = 0x1BA8; gPendingSceneId = .;
    . = 0x1BAC; gMapChunkGrid = .;
    . = 0x1BB0; gCameraRegionBottom = .;
    . = 0x1BB4; gScanlineBlendStep = .;
    . = 0x1BB8; gSceneBackgroundEffectFrame = .;
    . = 0x1BC4; gSceneEffects = .;
    . = 0x1BE4; gSceneEncounterConfigId = .;
    . = 0x1BE8; gWorldMapTargetX = .;
    . = 0x1BEC; gBg2ScrollY = .;
    . = 0x1BF0; gMapBlockDefinitions = .;
    . = 0x1BF4; gBackgroundMoveTargetY = .;
    . = 0x1BF8; gScanlineBlendUnknownParameter = .;
    . = 0x1BFC; gMapEventSampleXs = .;
    . = 0x1C00; gPendingOverlaySlot = .;
    . = 0x1C04; gScreenTransitionType = .;
    . = 0x1C08; gBackgroundMoveFrame = .;
    . = 0x1C0C; gBg2ScrollX = .;
    . = 0x1C10; gBg3ScrollX = .;
    . = 0x1C14; gSavedSceneScriptBankId = .;
    . = 0x1C18; gCameraFollowDisabled = .;
    . = 0x1C24; gLastToggledChestId = .;
    . = 0x1C34; gChestOpenBits = .;
    . = 0x1C54; gChestStates = .;
    . = 0x1CD4; gPaletteTransitionType = .;
    . = 0x1CD8; gPaletteTransitionUploadPending = .;
    . = 0x1CDC; gPaletteTransitionFrame = .;
    . = 0x1CE4; gFieldAnimationGroupId = .;
    . = 0x1CF4; gFieldAnimationStates = .;
    . = 0x1D30; gFieldAnimationBaseTiles = .;
    . = 0x1D34; gFieldAnimationIds = .;
    . = 0x1D38;
    "build/core.o"(COMMON)
  }

  .bss.script 0x03000E68 (NOLOAD) :
  {
    "build/script.o"(.bss .bss.*)
    . = ALIGN(8);
  }

  /* Script-owned COMMON storage at the original shared EWRAM addresses. */
  .bss.script.text_tilemap 0x02005800 (NOLOAD) : ALIGN(16)
  {
    gScriptTextTilemap = .;
    . += 0x800;
  }

  .bss.script.glyph_tiles 0x0203DE00 (NOLOAD) : ALIGN(16)
  {
    gScriptGlyphCacheTiles = .;
    . += 0x780;
  }

  .bss.script.common 0x03004F9C (NOLOAD) :
  {
    "build/script.o"(COMMON)
  }

  .bss.sound 0x03000F38 (NOLOAD) :
  {
    "build/sound.o"(.bss .bss.*)
  }

  /*
   * GNU ld's COMMON order depends on symbol names, not C declaration order.
   * Resolve the six tentative definitions within this NOBITS section and
   * reserve their known sizes; their input OBJECT types/sizes are retained.
   * Selecting COMMON below also catches unexpected additional allocations.
   */
  .bss.sound.common 0x03005AB0 (NOLOAD) : ALIGN(16)
  {
    gMPlayInfoBgm = .;
    . += 0x40;
    gMPlayInfoSfx0 = .;
    . += 0x40;
    gMPlayInfoSfx1 = .;
    . += 0x40;
    gMPlayInfoSfx3 = .;
    . += 0x40;
    gMPlayMemAccArea = .;
    . += 0x10;
    gMPlayInfoSfx2 = .;
    . += 0x40;
    "build/sound.o"(COMMON)
  }

  /* Shared party COMMON definitions retain their original RAM range. */
  .bss.party.common 0x03004980 (NOLOAD) : ALIGN(16)
  {
    gPartyItemCounts = .;
    . += 0x100;
    gPartyEquipmentSetDefBonus = .;
    . += 0x4;
    . = ALIGN(8);
    gPartyCharacterSlots = .;
    . += 0x8;
    gPartyEquipmentAtcBonus = .;
    . += 0x4;
    gPartyEquipmentResBonus = .;
    . += 0x4;
    gPartyEquipmentMenBonus = .;
    . += 0x4;
    . = ALIGN(8);
    gPartyMembers = .;
    . += 0x8;
    gPartyEquipmentDefBonus = .;
    . += 0x4;
    gPartyEquipmentSetAtcBonus = .;
    . += 0x4;
    gPartyEquipmentAglBonus = .;
    . += 0x4;
    gPartyEquipmentNoaBonus = .;
    . += 0x4;
    gPartyEquipmentLucBonus = .;
    . += 0x4;
    . = ALIGN(16);
    gPartyCharacters = .;
    . += 0x280;
    /* Already positioned symbols are not allocated again; catch any new COMMON. */
    "build/party.o"(COMMON)
  }
  /* Ordinary private definitions naturally reproduce the original menu RAM. */
  .bss.menu 0x03000048 (NOLOAD) :
  {
    "build/menu.o"(.bss .bss.*)
  }

  .bss.menu.common 0x03004D40 (NOLOAD) : ALIGN(4)
  {
    . = 0x0; gMenuContext = .;
    . = 0x4; gSaveIoState = .;
    . = 0x8; gMenuExtraFlags = .;
    . = 0xC; gMenuState = .;
    . = 0x10; gSaveLastUsedSlot = .;
    . = 0x20; gCollectionPackedCardCounts = .;
    . = 0x7C; gCollectionRatingAnimationCounter = .;
    . = 0x80; gSceneEffectTilemapUploadPending = .;
    . = 0x84; gShopActive = .;
    . = 0x88; gCollectionEventFlags = .;
    . = 0x90; gSaveIoBlockIndex = .;
    . = 0x94; gShopId = .;
    . = 0x98; gShopTransactionOccurred = .;
    . = 0x9C; gMenuSavedDisplayControl = .;
    . = 0xA0; gMenuSavedBlendControl = .;
    . = 0xA4; gCollectionOwnedCardTypeCount = .;
    . = 0xA8; gSaveBonusSlotValid = .;
    . = 0xAC;
    "build/menu.o"(COMMON)
  }

  .bss.serial_link.common 0x03004DF0 (NOLOAD) : ALIGN(16)
  {
    . = 0x0; gMultiSioArea = .;
    . = 0x130; gSerialLinkSession = .;
    . = 0x190; gSerialLinkTransfer = .;
    . = 0x1A0;
    "build/serial_link.o"(COMMON)
  }

  .bss.system.common 0x030018F0 (NOLOAD) : ALIGN(16)
  {
    . = 0x0; gSystemFlags = .;
    . = 0x50; gLinkPollResult = .;
    . = 0x54; gSceneState = .;
    . = 0x58; gPlayTimeFrames = .;
    . = 0x5C;
  }

  /* Three plain static definitions; the six-byte internal gap is natural alignment. */
  .bss.system 0x03001950 (NOLOAD) :
  {
    "build/system.o"(.bss .bss.*)
  }

  .bss.system.input 0x030019B0 (NOLOAD) : ALIGN(16)
  {
    . = 0x0; gKeysHeld = .;
    . = 0x10; gBg1RasterYOffsets = .;
    . = 0x110; gGameLoopMode = .;
    . = 0x120; gCardLinkReceiveBuffer = .;
    /* Only the first 16 bytes of the receive range are given a C type. */
    . = 0x1A0; gLinkServiceState = .;
    . = 0x1A4; gBg1RasterLineOffset = .;
    . = 0x1B0; gBg1RasterXOffsets = .;
    . = 0x2B0;
  }

  .bss.system.event_flags 0x03001C60 (NOLOAD) : ALIGN(16)
  {
    gEventFlags = .;
    . += 0x40;
  }

  .bss.system.link 0x03001CA0 (NOLOAD) : ALIGN(16)
  {
    . = 0x0; gKeysPressed = .;
    . = 0x10; gCardLinkSendBuffer = .;
    /* The gap after the 16-byte payload remains without a C object. */
    . = 0x90; gCardLinkTimeoutFrames = .;
    . = 0x94;
  }

  .bss.system.irq_code 0x03001D80 (NOLOAD) : ALIGN(16)
  {
    gIrqCodeBuffer = .;
    . += 0x800;
  }

  .bss.system.counters 0x0300259C (NOLOAD) : ALIGN(4)
  {
    . = 0x0; gVBlankUpdateMode = .;
    . = 0x4; gVBlankFrameCounter = .;
    . = 0x8; gRandomTableIndex = .;
    . = 0xC; gLinkTransferStatus = .;
    . = 0x10;
    /* All 21 COMMON symbols were positioned above; catch unplaced additions. */
    "build/system.o"(COMMON)
  }

  /* Battle private state uses ordinary .lcomm definitions in its owner TU. */
  .bss.battle 0x03000240 (NOLOAD) :
  {
    "build/battle.o"(.bss .bss.*)
  }
  .bss.battle_command_menu 0x03000768 (NOLOAD) :
  {
    "build/battle_command_menu.o"(.bss .bss.*)
  }

  .bss.battle_action_effects 0x03000820 (NOLOAD) :
  {
    "build/battle_action_effects.o"(.bss .bss.*)
  }

  .bss.battle_rules 0x030008A8 (NOLOAD) :
  {
    "build/battle_rules.o"(.bss)
  }
  .bss.battle_rules.result 0x03000910 (NOLOAD) :
  {
    "build/battle_rules.o"(.bss.battle_rules.result)
  }

  .bss.battle_sprite_wipe 0x030009D0 (NOLOAD) :
  {
    "build/battle_sprite_wipe.o"(.bss .bss.*)
  }

  .bss.battle_palette 0x03000AE0 (NOLOAD) :
  {
    "build/battle_palette.o"(.bss .bss.*)
  }

  .bss.battle_ai 0x03000D38 (NOLOAD) :
  {
    "build/battle_ai.o"(.bss .bss.*)
  }
  .bss.battle_items 0x03000D48 (NOLOAD) :
  {
    "build/battle_items.o"(.bss .bss.*)
  }

  /* EWRAM buffers are real COMMON objects; aliases do not allocate storage. */
  .bss.battle.status_tilemap 0x020352C0 (NOLOAD) : ALIGN(16)
  {
    gBattleStatusTilemap = .; . += 0x800;
  }
  .bss.battle.window_tilemap 0x02035AC0 (NOLOAD) : ALIGN(16)
  {
    gBattleWindowTilemap = .; . += 0x800;
  }
  .bss.battle.animation_tilemap 0x020362C0 (NOLOAD) : ALIGN(16)
  {
    gBattleAnimationTilemap = .; . += 0x800;
  }
  .bss.battle_palette.obj_backup 0x02036AC0 (NOLOAD) : ALIGN(16)
  {
    gBattleObjPaletteBackup = .; . += 0x200;
  }
  .bss.battle_palette.bg_backup 0x02036CC0 (NOLOAD) : ALIGN(16)
  {
    gBattleBgPaletteBackup = .; . += 0x200;
    "build/battle_palette.o"(COMMON)
  }
  .bss.battle.wave_samples 0x02036EC0 (NOLOAD) : ALIGN(16)
  {
    gBattleWaveSamples = .; . += 0x168;
  }
  .bss.battle.records 0x02037028 (NOLOAD) : ALIGN(8)
  {
    gBattleCombatants = .; . += 0x960;
    "build/battle.o"(COMMON)
  }
  .bss.battle_action_effects.pending_statuses 0x03004F90 (NOLOAD) : ALIGN(16)
  {
    gBattleActionPendingStatuses = .; . += 0xC;
    "build/battle_action_effects.o"(COMMON)
  }
}

gMenuWidgetEntries = ADDR(.bss.menu) + 0x74;
gBattleEnemyCombatants = gBattleCombatants + 5 * 0xC8;

ASSERT(SIZEOF(.bss.battle) == 0x526, "battle private RAM must end at 0x03000766")
ASSERT(ADDR(.bss.battle_command_menu) == 0x03000768, "battle command menu private state start")
ASSERT(SIZEOF(.bss.battle_command_menu) == 0xB3, "battle command menu private state must end at 0x0300081B")
ASSERT(ADDR(.bss.battle_action_effects) == 0x03000820, "battle action effects private state start")
ASSERT(SIZEOF(.bss.battle_action_effects) == 0x86, "battle action effects private state must end at 0x030008A6")
ASSERT(SIZEOF(.bss.battle_rules) == 0x63, "battle rules private state must end at 0x0300090B")
ASSERT(SIZEOF(.bss.battle_rules.result) == 0xBE, "battle result private state must end at 0x030009CE")
ASSERT(SIZEOF(.bss.battle_sprite_wipe) == 0x110, "battle sprite wipe private state must end at 0x03000AE0")
ASSERT(SIZEOF(.bss.battle_palette) == 0x20A, "battle palette private state must end at 0x03000CEA")
ASSERT(SIZEOF(.bss.battle_palette.obj_backup) == 0x200, "battle OBJ palette backup must end at 0x02036CC0")
ASSERT(SIZEOF(.bss.battle_palette.bg_backup) == 0x200, "battle BG palette backup must end at 0x02036EC0")
ASSERT(SIZEOF(.bss.battle_action_effects.pending_statuses) == 0xC, "battle pending statuses must end at 0x03004F9C")
ASSERT(SIZEOF(.bss.battle_ai) == 0xB, "battle AI target memory must end at 0x03000D43")
ASSERT(SIZEOF(.bss.battle_items) == 0x119, "battle item private state must end at 0x03000E61")
ASSERT(SIZEOF(.bss.battle.status_tilemap) == 0x800, "battle status tilemap size")
ASSERT(SIZEOF(.bss.battle.window_tilemap) == 0x800, "battle window tilemap size")
ASSERT(SIZEOF(.bss.battle.animation_tilemap) == 0x800, "battle animation tilemap size")
ASSERT(SIZEOF(.bss.battle.wave_samples) == 0x168, "battle wave samples must end at 0x02037028")
ASSERT(SIZEOF(.bss.battle.records) == 0x960, "battle combatants must end at 0x02037988")
ASSERT(gBattleCombatants == 0x02037028, "battle combatant array address")
ASSERT(gBattleEnemyCombatants == 0x02037410, "battle enemy view must alias slot 5")

ASSERT(SIZEOF(.bss.script) == 0xD0, "script .bss must end at sound RAM 0x03000F38")
ASSERT(SIZEOF(.bss.script.text_tilemap) == 0x800, "script tilemap must end at map RAM 0x02006000")
ASSERT(SIZEOF(.bss.script.glyph_tiles) == 0x780, "script glyph cache must end at 0x0203E580")
ASSERT(gScriptTextTilemap == 0x02005800, "script text tilemap address")
ASSERT(gScriptGlyphCacheTiles == 0x0203DE00, "script glyph cache address")
ASSERT(SIZEOF(.bss.core) == 0x48, "core .bss must end at menu RAM 0x03000048")
ASSERT(ADDR(.bss.core) == 0x03000000, "core private RAM start")
ASSERT(SIZEOF(.bss.script.common) == 0x4, "script COMMON must end at m4a RAM 0x03004FA0")
ASSERT(SIZEOF(.bss.sound) == 0x610, "sound .bss must end at dp-bit RAM 0x03001548")
ASSERT(SIZEOF(.bss.sound.common) == 0x150, "sound COMMON must end at 0x03005C00")
ASSERT(SIZEOF(.bss.core.gold) == 0x4, "core gold must end at 0x03002C3C")
ASSERT(gPartyGold == 0x03002C38, "core gold address")
ASSERT(SIZEOF(.bss.party.common) == 0x3C0, "party COMMON must end at menu RAM 0x03004D40")
ASSERT(SIZEOF(.bss.menu) == 0x1F2, "menu .bss must end at 0x0300023A")
ASSERT(gMenuWidgetEntries == 0x030000BC, "menu widget alias must address entries[5]")
ASSERT(SIZEOF(.bss.menu.common) == 0xAC, "menu COMMON must end at 0x03004DEC")
ASSERT(SIZEOF(.bss.serial_link.common) == 0x1A0, "serial link COMMON must end at 0x03004F90")
ASSERT(gMultiSioArea == 0x03004DF0, "serial link transport address")
ASSERT(gSerialLinkSession == 0x03004F20, "serial link session address")
ASSERT(gSerialLinkTransfer == 0x03004F80, "serial link transfer parameters address")
ASSERT(SIZEOF(.bss.system.event_flags) == 0x40, "event flags must end at input 0x03001CA0")
ASSERT(SIZEOF(.bss.system.common) == 0x5C, "system COMMON prefix must end at 0x0300194C")
ASSERT(SIZEOF(.bss.system) == 0x60, "system private RAM must end at keys held 0x030019B0")
ASSERT(SIZEOF(.bss.system.input) == 0x2B0, "system input/raster range must end at event flags")
ASSERT(SIZEOF(.bss.system.link) == 0x94, "system link state must end at 0x03001D34")
ASSERT(SIZEOF(.bss.system.irq_code) == 0x800, "IRQ copy must occupy exactly 0x800 bytes")
ASSERT(SIZEOF(.bss.system.counters) == 0x10, "system frame counters must end at 0x030025AC")
ASSERT(gBg1AutoScrollPhase == 0x03002C40, "system BG1 auto-scroll counter address")
ASSERT(gMenuExtraFlags == 0x3004D48, "gMenuExtraFlags RAM address")

ASSERT(SIZEOF(.bss.core.scene) == 0x60, "core scene COMMON range")
ASSERT(SIZEOF(.bss.core.resource_scratch) == 0x8, "core scratch must end at gold 0x03002C38")
ASSERT(SIZEOF(.bss.core.common) == 0x1D38, "core COMMON must end at 0x03004974")
