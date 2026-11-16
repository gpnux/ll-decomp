#ifndef CORE_H
#define CORE_H

#include "common.h"
#include "battle_types.h"

#define CORE_ACTOR_COUNT 24
#define CORE_SPRITE_NODE_COUNT 128
#define CORE_SPRITE_OAM_COUNT 128
#define CORE_SPRITE_SEARCH_FIRST 2
#define CORE_SPRITE_SEARCH_LAST 111
#define CORE_SPRITE_CHAIN_COUNT_MASK 0x7F
#define CORE_SPRITE_PALETTE_BANK_SHIFT 12
#define CORE_SPRITE_TILE_INDEX_MASK 0x3FF
#define CORE_ACTOR_DIRECTION_MASK 7
#define CORE_ACTOR_CMD_MOVE 0
#define CORE_ACTOR_CMD_WAIT 1
#define CORE_ACTOR_CMD_MOVE_KEEP_FACING 2
#define CORE_ACTOR_CMD_SET_DIRECTION 3
#define CORE_ACTOR_CMD_STOP 0xFD
#define CORE_ACTOR_CMD_REPEAT 0xFE
#define CORE_ACTOR_CMD_END 0xFF
#define CORE_ACTOR_MOVE_COMMAND_BUFFER_SIZE 9
#define CORE_ACTOR_MOVE_TARGET_PLAYER 0xFF
#define CORE_ACTOR_DIRECTION_UP 0
#define CORE_ACTOR_DIRECTION_RIGHT 2
#define CORE_ACTOR_DIRECTION_DOWN 4
#define CORE_ACTOR_DIRECTION_LEFT 6
#define CORE_ACTOR_DIRECTION_OPPOSITE_BIT 4
#define CORE_ACTOR_FLAG_CUSTOM_ANIMATION 0x01
#define CORE_ACTOR_FLAG_NO_SHADOW 0x02
#define CORE_ACTOR_STATE_NO_VISIBLE_SPRITES 0x08
#define CORE_ACTOR_STATE_STOP_MOVEMENT 0x20
#define CORE_ACTOR_STATE_COMMANDS_ACTIVE 0x80
#define CORE_ACTOR_STATE_SCENE_RELATIVE 0x01
#define CORE_ACTOR_STATE_ADVANCE_HISTORY 0x40
#define CORE_PLAYER_TRANSITION_PAUSE_FIRST 6
#define CORE_PLAYER_TRANSITION_PAUSE_LAST 7
#define CORE_PLAYER_TRANSITION_WAIT_FIRST 9
#define CORE_PLAYER_TRANSITION_WAIT_LAST 10
#define CORE_ACTOR_ANIMATION_SLOT_COUNT 32
#define CORE_ACTOR_ANIMATION_WORKSPACE_TILE_COUNT 128
#define CORE_ACTOR_ANIMATION_LOOP 0x80
#define CORE_ACTOR_ANIMATION_FLIP_X 0x40
#define CORE_ACTOR_ANIMATION_RENDER_FLIP_OFFSET 100
#define CORE_ACTOR_PREVIEW_EXTENDED_FIRST 128
#define CORE_SPRITE_SHAPE_COUNT 4
#define CORE_SPRITE_SIZE_COUNT 4
#define CORE_ACTOR_FORCED_COMPLETION_FIRST 100
#define CORE_ACTOR_ANIMATION_COMPLETE 0xFF
#define CORE_ACTOR_ANIMATION_DISABLED 0xFF
/* Encoded formats plus an Actor-path bit; special formats use the full byte. */
#define CORE_SCENE_TYPE_FLAG_CUSTOM_ACTORS 0x80
#define CORE_SCENE_TYPE_SCRIPT_BACKGROUND 0xFD
#define CORE_SCENE_TYPE_LZ77_TILEMAP 0xFE
#define CORE_SCENE_TYPE_RAW_TILEMAP 0xFF
#define CORE_SCENE_X_ORIGIN 256
#define CORE_SPRITE_AFFINE_COUNT 32
#define CORE_ACTOR_GRAPHICS_TILE_COUNT 72
#define CORE_SPRITE_TILE_ROW_COUNT 8
#define CORE_OBJ_PALETTE_COLOR_COUNT 16
#define CORE_SHADOW_SMALL_TILE_COUNT 2
#define CORE_SHADOW_LARGE_TILE_COUNT 4
#define CORE_SPRITE_TRANSFER_COUNT 32
#define CORE_SPRITE_TILE_TRANSFER_NONE 0xFFFF
#define CORE_SPRITE_PALETTE_TRANSFER_NONE 0xFF
#define CORE_ACTOR_RESOURCE_SLOT_COUNT 12
#define CORE_ACTOR_RESOURCE_LOAD_GRAPHICS 0x01
#define CORE_ACTOR_RESOURCE_LOAD_PALETTE 0x02
#define CORE_ACTOR_HISTORY_FROZEN 0x01
#define CORE_SCENE_LOCATION_COUNT 22
#define CORE_ACTOR_GRID_PIXELS 8
#define CORE_PLAYER_HISTORY_COUNT 8
#define CORE_PLAYER_INTERACTION_DIRECTION_COUNT 8
#define CORE_PLAYER_SPRITE_SLOT 1
#define CORE_PLAYER_RESOURCE_SLOT 0
#define CORE_FOLLOWER_RESOURCE_SLOT 1
#define CORE_PLAYER_MOVE_SPEED 2
#define CORE_PLAYER_MOVEMENT_MAX 128
#define CORE_PLAYER_PRIMARY_ATTR1 0x4000
#define CORE_PLAYER_SECONDARY_ATTR0 0x4000
#define CORE_PLAYER_PRIMARY_ATTR2 0x8A0
#define CORE_FOLLOWER_PRIMARY_TILE_BASE 0x8E8
#define CORE_PLAYER_PRIMARY_Y_OFFSET 0xE8
#define CORE_PLAYER_SECONDARY_Y_OFFSET 0xF8
#define CORE_PLAYER_SPRITE_PART_COUNT 2
#define CORE_PLAYER_SPRITE_TAIL_COUNT 1
#define CORE_PLAYER_ATTR2_PRIORITY 0x800
#define CORE_PLAYER_SECONDARY_TILE_OFFSET 0xA4
#define CORE_CHEST_COUNT 16
#define CORE_COLLISION_ACTOR_FIRST 2
#define CORE_COLLISION_ACTOR_COUNT 16
#define CORE_AUXILIARY_ACTOR_ID 18
#define CORE_MENU_ACTOR_FIRST 19
#define CORE_MENU_ACTOR_COUNT (CORE_ACTOR_COUNT - CORE_MENU_ACTOR_FIRST)
#define CORE_MENU_ACTOR_COLUMN_TILES 5
#define CORE_CHEST_CONFIG_COUNT 256
#define CORE_CHEST_FLAG_OPEN 0x01
#define CORE_CHEST_FLAG_ALT_PALETTE 0x80
#define CORE_CHEST_GRAPHICS_TILE_COUNT 10
#define CORE_CHEST_PALETTE_BANK_COUNT 2
#define CORE_CAMERA_REGION_PIXELS 64
#define CORE_SCENE_ACTOR_RESOURCE_COUNT 8
#define CORE_MAP_EXPANDED_WIDTH 128
#define CORE_MAP_COARSE_WIDTH 32
#define CORE_MAP_BLOCK_SIDE 2
#define CORE_MAP_ANIMATION_COUNT 16
#define CORE_SCENE_PALETTE_COLOR_COUNT 160
#define CORE_SCRIPT_BACKGROUND_PALETTE_COLOR_COUNT 32
#define CORE_PALETTE_ANIMATION_COUNT 4
#define CORE_PALETTE_ANIMATION_PAUSED 0x04
#define CORE_FIELD_ANIMATION_COUNT 3
#define CORE_FIELD_ANIMATION_RESOURCE_COUNT 2
#define CORE_TRANSITION_WINDOW_ROW_COUNT 82

/* A generated movement segment; the byte stream ends with CORE_ACTOR_CMD_END. */
struct ActorMoveCommand
{
    u8 command;
    u8 direction;
    u8 moveSpeed;
    u8 moveUpdates;
};

/* IWRAM actor record, 0x28 bytes. x/y are world pixel coordinates. */
struct ActorState
{
    u8 spriteSlot;             /* 0x00; sprite-chain head index, 0 = inactive */
    u8 flags;                  /* 0x01; render/type flags, bit 0: custom animation */
    u8 graphicsSlot;           /* 0x02; standard OBJ tile block, 72 tiles per slot */
    u8 paletteSlot;            /* 0x03; standard OBJ palette bank uses low four bits */
    u8 direction;              /* 0x04; low three bits select direction */
    u8 animationCounter;       /* 0x05; standard animation phase, modulo 32 */
    s16 x;                     /* 0x06 */
    s16 y;                     /* 0x08 */
    u8 minTileX;               /* 0x0A; bounds compare runtime x/y >> 3 */
    u8 minTileY;               /* 0x0B; runtime Y includes the actor foot row */
    u8 maxTileX;               /* 0x0C */
    u8 maxTileY;               /* 0x0D */
    u8 movementDirection;      /* 0x0E */
    u8 moveSpeed;              /* 0x0F; pixel increment on each moving axis */
    u8 moveTimer;              /* 0x10; movement update countdown */
    u8 waitTimer;              /* 0x11; waiting update countdown */
    u8 stateFlags;             /* 0x12; mixed movement/display state, not only animation */
    u8 scriptEntryOrMenuState; /* 0x13; NPC entry id; menu actors use 00/80 state. */
    u16 customAnimationTimer;  /* 0x14; elapsed animation updates, 0xFF = completed */
    u8 customAnimationSlot;    /* 0x16; resource slot, 0xFF = disabled */
    u8 commandOffset;          /* 0x17; byte offset into commandData */
    u8 shadowSpriteSlot;       /* 0x18 */
    u8 verticalTimer;          /* 0x19; vertical-offset interpolation countdown */
    s16 verticalOffset;        /* 0x1A; pixels subtracted from rendered Y */
    s16 verticalFixed;         /* 0x1C; interpolation accumulator, four fractional bits */
    s16 verticalStep;          /* 0x1E; accumulator increment per update */
    s16 verticalTarget;        /* 0x20; final pixel offset */
    u8 unknown22[2];           /* 0x22; no direct use confirmed, possibly alignment */
    const u8 *commandData;     /* 0x24; actor command stream */
};

/* Resource/script actor configuration, 0x10 bytes.
 * Spawn coordinates use eight-pixel tiles; runtime Y is (tileY + 1) * 8.
 * Movement bounds compare the runtime coordinates directly after shifting. */
struct ActorSpawnConfig
{
    u8 flags;
    u8 graphicsSlot;
    u8 paletteSlot;
    u8 direction;
    u8 tileX;
    u8 tileY;
    u8 minTileX;
    u8 minTileY;
    u8 maxTileX;
    u8 maxTileY;
    u8 moveSpeed;
    u8 scriptEntryId;
    const u8 *commandData;
};

/* Software sprite node, 0x14 bytes; separate from the actor record. */
struct SpriteNode
{
    u8 chainInfo;              /* 0x00; low seven bits: chain count, bit 7: hidden */
    u8 flags;                  /* 0x01 */
    u16 attr0;                 /* 0x02 */
    u16 attr1;                 /* 0x04 */
    u16 attr2;                 /* 0x06 */
    u16 submittedX;            /* 0x08; low halfword of the submitted drawing baseline */
    u16 submittedY;            /* 0x0A; cached draw-order key, read as unsigned */
    struct SpriteNode *next;   /* 0x0C */
    u16 xOffset;               /* 0x10; values >256 subtract 511 before signed use */
    u16 yOffset;               /* 0x12; values >128 subtract 255 before signed use */
};

/** @brief Eight-byte GBA OBJ record with named attribute fields.
 *  @note For affine objects, affineIndexLow and the two flip bits form the
 *  five-bit matrix index. The last halfword belongs to the interleaved matrix. */
struct SpriteOamAttributes
{
    u16 y : 8;
    u16 affineMode : 2;
    u16 objMode : 2;
    u16 mosaic : 1;
    u16 bpp : 1;
    u16 shape : 2;
    u16 x : 9;
    u16 affineIndexLow : 3;
    u16 hFlip : 1;
    u16 vFlip : 1;
    u16 size : 2;
    u16 tile : 10;
    u16 priority : 2;
    u16 palette : 4;
    u16 affineParameter;
};

/* Software OAM has bitfield, halfword and two-word transfer views. */
union SpriteOamEntry
{
    struct
    {
        u16 attr0;
        u16 attr1;
        u16 attr2;
        u16 unknown06;
    } attributes;
    struct SpriteOamAttributes fields;
    u32 words[2];
};

/* Four signed fixed-point coefficients, 0x08 bytes per software matrix. */
struct SpriteAffineMatrix
{
    s16 pa;
    s16 pb;
    s16 pc;
    s16 pd;
};

/* One or two contiguous GBA palette banks queued for the frame copy, 0x08 bytes. */
struct SpritePaletteTransferRequest
{
    u8 paletteSlot;            /* PALRAM bank: 0..15 BG, 16..31 OBJ. */
    u8 bankCount;
    u8 unknown02[2];
    const u16 *source;
};

/* World-pixel offsets and dimensions for each cached player direction. */
struct PlayerInteractionBounds
{
    s16 xOffset;
    s16 yOffset;
    u16 width;
    u16 height;
};

/* Interaction location coordinates use eight-pixel tiles. */
struct SceneLocationRecord
{
    u8 sceneId;
    u8 scriptEntryId;
    u8 tileX;
    u8 tileY;
};

/* Collision view: 0x08 bytes, spriteSlot at +2 and world pixels at +4/+6. */
struct ChestState
{
    u8 state;
    u8 persistentId;
    u8 spriteSlot;
    u8 scriptEntryId;
    s16 x;
    s16 y;
};

/* Scene configuration: halfword tile coordinates become runtime pixels. */
struct ChestSpawnConfig
{
    u8 sceneId;
    u8 scriptEntryId;
    u8 paletteFlag;
    u8 unknown03;
    u16 tileX;
    u16 tileY;
};

/* Shared 0x20-byte field effect state, also accessed by script commands. */
struct SceneEffectState
{
    u16 flags;                /* 0x00 */
    u16 scrollXStep;          /* 0x02 */
    u16 scrollYStep;          /* 0x04 */
    u16 unknown06;            /* 0x06 */
    u16 backgroundMask;       /* 0x08 */
    u16 unknown0A[5];         /* 0x0A */
    u16 blendFrame;           /* 0x14; effect 202 */
    u16 pulseFrame;           /* 0x16; effect 4, value 5 */
    u16 paletteSequencePhase; /* 0x18; effect 8 */
    u16 tilemapUploadPending; /* 0x1A; checked by system VBlank */
    u16 phase;                /* 0x1C; shared by several effects */
    u16 frame;                /* 0x1E */
};

/* Three scene decoration animations; each record is 0x14 bytes. */
struct FieldAnimationState
{
    u8 enabled;
    u8 spriteSlot;
    u8 baseTileIndex;
    u8 paletteBank;
    u16 animationTimer;
    s16 x;
    s16 y;
    s16 verticalOffset;
    u16 attr2Bits;
    u8 stateFlags;             /* Bit 3: the entire chain was clipped. */
    u8 unknown0F;
    const u8 *animationData;
};

struct FieldAnimationResource
{
    u8 graphicsId;
    u8 paletteId;
    u8 baseTileIndex;
    u8 animationId;
};

struct FieldAnimationSpawnConfig
{
    u8 resourceSlot;
    u8 attr2High;
    s16 x;
    s16 y;
    s16 verticalOffset;
};

/* Six-byte parts shared by actor and scene decoration animation frames. */
struct SpriteFramePart
{
    u16 attr0;                 /* Low byte is the relative Y coordinate. */
    u16 attr1;                 /* Low nine bits are the relative X coordinate. */
    u8 tileIndex;
    u8 attr2High;
} __attribute__((packed));

struct MapEventTrigger
{
    u8 tileX;
    u8 tileY;
    u8 type;
    u8 index;
};

struct MapEventConfig
{
    const u8 *triggers;        /* Count byte followed by four-byte triggers. */
    const struct MapExitRecord *exits;
    const struct MapRepositionRecord *repositions;
    const struct WorldMapEntryRecord *worldMapEntries;
    const struct MapScriptEventRecord *scriptEvents;
    const struct MapDirectionalScriptEventRecord *directionalScriptEvents;
};

struct MapExitRecord
{
    u8 sceneId;
    u8 tileX;
    u8 tileY;
    u8 direction;
    u16 cameraRegionId;
    u8 unknown06[2];
};

/* Type 1 uses eight-byte records, with the region stored at odd byte offset 3. */
struct MapRepositionRecord
{
    u8 tileX;
    u8 tileY;
    u8 direction;
    u16 cameraRegionId;
    u8 unknown05[3];
} __attribute__((packed));

struct WorldMapEntryRecord
{
    u8 regionId;
    u8 locationId;
    u8 unknown02[2];
};

struct MapScriptEventRecord
{
    u8 scriptEntryId;
    u8 blockingSystemFlagId;   /* A set bit in the system bitmap suppresses the script. */
} __attribute__((packed));

struct MapDirectionalScriptEventRecord
{
    u8 scriptEntryId;
    u8 blockingSystemFlagId;
    u8 direction;              /* 0..7 requires that facing; larger values accept any. */
    u8 unknown03;
};

/* Count-prefixed regions contain pixel coordinates used without scaling. */
struct WorldMapLocationRecord
{
    u8 x;
    u8 y;
} __attribute__((packed));

/* The halfword frame data starts immediately after this eight-byte header.
 * Coordinates and dimensions describe tiles in an expanded map rectangle. */
struct MapAnimationResourceHeader
{
    u8 layerBit;               /* Low bit selects expanded map A/B. */
    u8 flags;
    u8 frameCount;
    u8 frameShift;
    u8 tileX;
    u8 tileY;
    u8 tileWidth;
    u8 tileHeight;
};

/* A variable-length frame-id stream follows this four-byte resource header. */
struct PaletteAnimationHeader
{
    u8 flags;
    u8 frameShift;
    u8 paletteBank;
    u8 frameCount;
};


/* Resource and runtime layouts shared by core interfaces. */
struct ActorAnimationResource
{
    u16 animationId;
    u16 graphicsId;
    u8 paletteId;
    u8 flags;
    u8 unknown06[2];
};

enum BattleAnimationTargetMode
{
    BATTLE_ANIMATION_TARGET_RANDOM = 0,
    BATTLE_ANIMATION_TARGET_SLOT_ZERO = 1,
    BATTLE_ANIMATION_TARGET_RANDOM_ALTERNATE = 2,
    BATTLE_ANIMATION_TARGET_FORMATION = 3,
};

/** @brief Shared 0x14-byte battle animation resource descriptor.
 *  @note The reaction trigger and hit-effect selector are copied to combatant
 *  +B4/+B6; the second is not a duration. Tables stay in ROM resources.
 *  @address 0x08393B28
 */
struct BattleAnimationResourceConfig
{
    const u8 *animationData;            /* 0x00; passed as a u16 definition to battle setup */
    const u16 *palettes;                /* 0x04 */
    u16 graphicsId;                     /* 0x08 */
    u16 graphicsBlockCount;             /* 0x0A */
    u16 hitReactionStartFrame;          /* 0x0C; combatant +B4 */
    u16 hitEffectId;                    /* 0x0E; combatant +B6 */
    u16 targetMode;                     /* 0x10; enum BattleAnimationTargetMode */
    u8 unknown12[2];
};

typedef char BattleAnimationBlockCountCheck[(offsetof(struct BattleAnimationResourceConfig, graphicsBlockCount) == 0xA) ? 1 : -1];
typedef char BattleAnimationReactionStartCheck[(offsetof(struct BattleAnimationResourceConfig, hitReactionStartFrame) == 0xC) ? 1 : -1];
typedef char BattleAnimationHitEffectCheck[(offsetof(struct BattleAnimationResourceConfig, hitEffectId) == 0xE) ? 1 : -1];
typedef char BattleAnimationTargetModeCheck[(offsetof(struct BattleAnimationResourceConfig, targetMode) == 0x10) ? 1 : -1];

struct SpriteDimensions
{
    u8 width;
    u8 height;
} __attribute__((packed));

/* Region coordinates and dimensions use 64-pixel units. */
struct CameraRegionConfig
{
    u8 x;
    u8 y;
    u8 width;
    u8 height;
};

struct SceneResourceConfig
{
    u8 backgroundEffect;
    u8 sceneType;              /* Encoded format byte, cached as u16 gSceneType. */
    u8 scrollMode;
    u8 bg1ScrollMode;
    u8 bgmId;
    u8 unknown05;
    u8 paletteAnimationGroupId;
    u8 mapAnimationGroupId;
    u8 fieldAnimationGroupId;
    u8 actorGroupId;
    u8 encounterConfigId;
    u8 scriptBankId;
    u16 blockingTileThreshold;
    u16 layoutId;
    u16 graphicsId;
    u16 paletteId;
};

struct SceneActorResourceConfig
{
    u8 actorCount;
    u8 graphicsIds[CORE_SCENE_ACTOR_RESOURCE_COUNT];
    u8 paletteIds[CORE_SCENE_ACTOR_RESOURCE_COUNT];
    u8 unknown11;
} __attribute__((packed));

struct MapAnimationState
{
    u8 layer;                 /* 0 = disabled, 1/2 = expanded map layer. */
    u8 frameCount;
    u8 frameShift;
    u8 flags;
    u8 unknown04[2];
    u8 tileX;
    u8 tileY;
    u8 tileWidth;
    u8 tileHeight;
    u16 timer;
    const u16 *frames;
};

struct ActorAnimationDefinition
{
    u16 unknown00;
    u16 timelineOffset;
    u16 frameCount;
    u16 paletteBankCount;
};

struct ActorAnimationTimeline
{
    u16 unknown00;
    u16 duration;
    u16 unknown04;
    u16 recordCount;
};

struct ActorAnimationKeyframe
{
    u16 frameId;
    u16 endTick;
};

struct ActorAnimationFrameHeader
{
    u8 prefixPartCount;
    u8 unknown01;
    u8 partCount;
    u8 unknown03;
};

struct ActorAnimationFramePart
{
    u8 attr0Lo;
    u8 attr0Hi;
    u8 attr1Lo;
    u8 attr1Hi;
    u8 sourceTile;
    u8 paletteOffsetBits;
} __attribute__((packed));

/* Compile-time layout checks for the target ABI. */
typedef char ActorAnimationFrameHeaderSizeCheck[(sizeof(struct ActorAnimationFrameHeader) == 4) ? 1 : -1];
typedef char ActorAnimationFramePartSizeCheck[(sizeof(struct ActorAnimationFramePart) == 6) ? 1 : -1];
typedef char ActorAnimationFramePartSourceTileCheck[(offsetof(struct ActorAnimationFramePart, sourceTile) == 4) ? 1 : -1];
typedef char ActorAnimationFramePartPaletteCheck[(offsetof(struct ActorAnimationFramePart, paletteOffsetBits) == 5) ? 1 : -1];

typedef char CameraRegionConfigSizeCheck[(sizeof(struct CameraRegionConfig) == 4) ? 1 : -1];
typedef char SceneResourceConfigSizeCheck[(sizeof(struct SceneResourceConfig) == 20) ? 1 : -1];
typedef char SceneResourceConfigActorGroupCheck[(offsetof(struct SceneResourceConfig, actorGroupId) == 9) ? 1 : -1];
typedef char SceneActorResourceConfigSizeCheck[(sizeof(struct SceneActorResourceConfig) == 18) ? 1 : -1];
typedef char MapAnimationStateSizeCheck[(sizeof(struct MapAnimationState) == 16) ? 1 : -1];
typedef char MapAnimationStateFramesCheck[(offsetof(struct MapAnimationState, frames) == 12) ? 1 : -1];
typedef char ActorAnimationDefinitionSizeCheck[(sizeof(struct ActorAnimationDefinition) == 0x08) ? 1 : -1];
typedef char ActorAnimationTimelineSizeCheck[(sizeof(struct ActorAnimationTimeline) == 0x08) ? 1 : -1];
typedef char ActorAnimationKeyframeSizeCheck[(sizeof(struct ActorAnimationKeyframe) == 0x04) ? 1 : -1];
typedef char ActorStateSizeCheck[(sizeof(struct ActorState) == 0x28) ? 1 : -1];
typedef char ActorMoveCommandSizeCheck[(sizeof(struct ActorMoveCommand) == 4) ? 1 : -1];
typedef char ActorSpawnConfigSizeCheck[(sizeof(struct ActorSpawnConfig) == 0x10) ? 1 : -1];
typedef char ActorSpawnConfigCommandsCheck[(offsetof(struct ActorSpawnConfig, commandData) == 0x0C) ? 1 : -1];
typedef char ActorStateGraphicsSlotCheck[(offsetof(struct ActorState, graphicsSlot) == 0x02) ? 1 : -1];
typedef char ActorStatePaletteSlotCheck[(offsetof(struct ActorState, paletteSlot) == 0x03) ? 1 : -1];
typedef char ActorStateDirectionCheck[(offsetof(struct ActorState, direction) == 0x04) ? 1 : -1];
typedef char ActorStateXCheck[(offsetof(struct ActorState, x) == 0x06) ? 1 : -1];
typedef char ActorStateYCheck[(offsetof(struct ActorState, y) == 0x08) ? 1 : -1];
typedef char ActorStateMovementDirectionCheck[(offsetof(struct ActorState, movementDirection) == 0x0E) ? 1 : -1];
typedef char ActorStateWaitCheck[(offsetof(struct ActorState, waitTimer) == 0x11) ? 1 : -1];
typedef char ActorStateAnimationTimeCheck[(offsetof(struct ActorState, customAnimationTimer) == 0x14) ? 1 : -1];
typedef char ActorStateAnimationSlotCheck[(offsetof(struct ActorState, customAnimationSlot) == 0x16) ? 1 : -1];
typedef char ActorStateCommandsCheck[(offsetof(struct ActorState, commandData) == 0x24) ? 1 : -1];
typedef char SpriteOamAttributesSizeCheck[(sizeof(struct SpriteOamAttributes) == 8) ? 1 : -1];
typedef char SpriteOamAffineParameterCheck[(offsetof(struct SpriteOamAttributes, affineParameter) == 6) ? 1 : -1];
typedef char SpriteOamEntrySizeCheck[(sizeof(union SpriteOamEntry) == 8) ? 1 : -1];
typedef char SpriteOamAttr2Check[(offsetof(union SpriteOamEntry, attributes.attr2) == 4) ? 1 : -1];
typedef char SpriteOamUnknown06Check[(offsetof(union SpriteOamEntry, attributes.unknown06) == 6) ? 1 : -1];
typedef char SpriteNodeSizeCheck[(sizeof(struct SpriteNode) == 0x14) ? 1 : -1];
typedef char SpriteSubmittedXCheck[(offsetof(struct SpriteNode, submittedX) == 0x08) ? 1 : -1];
typedef char SpriteSubmittedYCheck[(offsetof(struct SpriteNode, submittedY) == 0x0A) ? 1 : -1];
typedef char SpriteNextCheck[(offsetof(struct SpriteNode, next) == 0x0C) ? 1 : -1];
typedef char SpriteAffineSizeCheck[(sizeof(struct SpriteAffineMatrix) == 0x08) ? 1 : -1];
typedef char SpritePaletteTransferRequestSizeCheck[(sizeof(struct SpritePaletteTransferRequest) == 0x08) ? 1 : -1];
typedef char SpritePaletteTransferRequestSourceCheck[(offsetof(struct SpritePaletteTransferRequest, source) == 0x04) ? 1 : -1];
typedef char PlayerInteractionBoundsSizeCheck[(sizeof(struct PlayerInteractionBounds) == 8) ? 1 : -1];
typedef char PlayerInteractionBoundsYCheck[(offsetof(struct PlayerInteractionBounds, yOffset) == 2) ? 1 : -1];
typedef char PlayerInteractionBoundsWidthCheck[(offsetof(struct PlayerInteractionBounds, width) == 4) ? 1 : -1];
typedef char PlayerInteractionBoundsHeightCheck[(offsetof(struct PlayerInteractionBounds, height) == 6) ? 1 : -1];
typedef char ChestStateScriptCheck[(offsetof(struct ChestState, scriptEntryId) == 3) ? 1 : -1];
typedef char ChestSpawnConfigScriptCheck[(offsetof(struct ChestSpawnConfig, scriptEntryId) == 1) ? 1 : -1];
typedef char SceneLocationSizeCheck[(sizeof(struct SceneLocationRecord) == 0x04) ? 1 : -1];
typedef char SceneLocationScriptCheck[(offsetof(struct SceneLocationRecord, scriptEntryId) == 0x01) ? 1 : -1];
typedef char ChestStateSizeCheck[(sizeof(struct ChestState) == 0x08) ? 1 : -1];
typedef char ChestStateSpriteCheck[(offsetof(struct ChestState, spriteSlot) == 0x02) ? 1 : -1];
typedef char ChestStateXCheck[(offsetof(struct ChestState, x) == 0x04) ? 1 : -1];
typedef char ChestStateYCheck[(offsetof(struct ChestState, y) == 0x06) ? 1 : -1];
typedef char ChestSpawnConfigSizeCheck[(sizeof(struct ChestSpawnConfig) == 0x08) ? 1 : -1];
typedef char ChestSpawnConfigXCheck[(offsetof(struct ChestSpawnConfig, tileX) == 0x04) ? 1 : -1];
typedef char ChestSpawnConfigYCheck[(offsetof(struct ChestSpawnConfig, tileY) == 0x06) ? 1 : -1];
typedef char SceneEffectStateSizeCheck[(sizeof(struct SceneEffectState) == 0x20) ? 1 : -1];
typedef char ActorAnimationResourceSizeCheck[(sizeof(struct ActorAnimationResource) == 0x08) ? 1 : -1];
typedef char ActorPreviewRecordSizeCheck[(sizeof(struct BattleEnemyInfo) == 0x2C) ? 1 : -1];
typedef char ActorPreviewAnimationIdCheck[(offsetof(struct BattleEnemyInfo, animationBaseId) == 0x1A) ? 1 : -1];
typedef char ActorExtendedPreviewRecordSizeCheck[(sizeof(struct BattleBossInfo) == 0x40) ? 1 : -1];
typedef char BattleAnimationResourceConfigSizeCheck[(sizeof(struct BattleAnimationResourceConfig) == 0x14) ? 1 : -1];
typedef char BattleAnimationPalettesCheck[(offsetof(struct BattleAnimationResourceConfig, palettes) == 0x04) ? 1 : -1];
typedef char SpriteDimensionsSizeCheck[(sizeof(struct SpriteDimensions) == 0x02) ? 1 : -1];
typedef char FieldAnimationStateSizeCheck[(sizeof(struct FieldAnimationState) == 0x14) ? 1 : -1];
typedef char FieldAnimationStateDataCheck[(offsetof(struct FieldAnimationState, animationData) == 0x10) ? 1 : -1];
typedef char FieldAnimationStateFlagsCheck[(offsetof(struct FieldAnimationState, stateFlags) == 0x0E) ? 1 : -1];
typedef char FieldAnimationResourceSizeCheck[(sizeof(struct FieldAnimationResource) == 4) ? 1 : -1];
typedef char FieldAnimationSpawnConfigSizeCheck[(sizeof(struct FieldAnimationSpawnConfig) == 8) ? 1 : -1];
typedef char SpriteFramePartSizeCheck[(sizeof(struct SpriteFramePart) == 6) ? 1 : -1];
typedef char SpriteFramePartTileCheck[(offsetof(struct SpriteFramePart, tileIndex) == 4) ? 1 : -1];
typedef char MapEventTriggerSizeCheck[(sizeof(struct MapEventTrigger) == 4) ? 1 : -1];
typedef char MapEventConfigSizeCheck[(sizeof(struct MapEventConfig) == 0x18) ? 1 : -1];
typedef char MapExitRecordSizeCheck[(sizeof(struct MapExitRecord) == 8) ? 1 : -1];
typedef char MapExitRecordRegionCheck[(offsetof(struct MapExitRecord, cameraRegionId) == 4) ? 1 : -1];
typedef char PaletteAnimationHeaderSizeCheck[(sizeof(struct PaletteAnimationHeader) == 4) ? 1 : -1];
typedef char MapEventConfigExitsCheck[(offsetof(struct MapEventConfig, exits) == 4) ? 1 : -1];
typedef char MapEventConfigDirectionalScriptsCheck[(offsetof(struct MapEventConfig, directionalScriptEvents) == 20) ? 1 : -1];
typedef char MapRepositionRecordSizeCheck[(sizeof(struct MapRepositionRecord) == 8) ? 1 : -1];
typedef char MapRepositionRecordRegionCheck[(offsetof(struct MapRepositionRecord, cameraRegionId) == 3) ? 1 : -1];
typedef char WorldMapEntryRecordSizeCheck[(sizeof(struct WorldMapEntryRecord) == 4) ? 1 : -1];
typedef char MapScriptEventRecordSizeCheck[(sizeof(struct MapScriptEventRecord) == 2) ? 1 : -1];
typedef char MapDirectionalScriptEventRecordSizeCheck[(sizeof(struct MapDirectionalScriptEventRecord) == 4) ? 1 : -1];
typedef char WorldMapLocationRecordSizeCheck[(sizeof(struct WorldMapLocationRecord) == 2) ? 1 : -1];
typedef char MapAnimationResourceHeaderSizeCheck[(sizeof(struct MapAnimationResourceHeader) == 8) ? 1 : -1];
typedef char MapAnimationResourceHeaderXCheck[(offsetof(struct MapAnimationResourceHeader, tileX) == 4) ? 1 : -1];

/* Parallel palette tracks and decoration records in existing RAM. */
extern struct FieldAnimationState gFieldAnimationStates[3];
extern u8 gFieldAnimationBaseTiles[2];
extern u8 gFieldAnimationIds[2];
extern u8 gFieldAnimationGroupId;
extern const struct FieldAnimationResource gFieldAnimationResourceGroups[][2];
extern const u16 *const gFieldAnimationInstanceGroups[];
extern const u8 *const gFieldAnimationData[];
extern const void *const gFieldAnimationGraphics[];
extern const u16 gFieldAnimationPalettes[][CORE_OBJ_PALETTE_COLOR_COUNT];
extern const struct MapEventConfig *const gMapEventConfigs[];
extern const struct MapEventConfig *gMapEventConfig;
extern u8 gMapEventType;
extern u8 gMapEventIndex;
extern u8 gMapEventSampleXs[4];
extern u8 gMapEventSampleYs[4];
extern u8 gScreenTransitionType;
extern u8 gPaletteTransitionType;
extern u8 gPaletteTransitionUploadPending;
extern u8 gPaletteTransitionFrame;
extern u16 gTransitionWindowRadius;
extern u16 gTransitionWindowRows[CORE_TRANSITION_WINDOW_ROW_COUNT];
extern u16 gScanlineBlendFlags;
extern s16 gScanlineBlendPosition;
extern s16 gScanlineBlendStep;
extern u16 gScanlineBlendUnknownParameter;
extern u8 gSceneEncounterConfigId;
extern u16 gWalkingEncounterCountdown;
extern u8 gWorldMapRegionId;
extern u8 gWorldMapLocationId;
extern const u8 *gWorldMapConnectionIds;
extern u8 gWorldMapConnectionCount;
extern u8 gWorldMapConnectionIndex;
extern u8 gWorldMapLocationNameGroupId;

/* Shared runtime state is defined in core.c at the original RAM addresses. */
extern struct ActorState gActorStates[CORE_ACTOR_COUNT];                  /* 0x03002E80 */
extern struct SpriteNode gSpriteNodes[CORE_SPRITE_NODE_COUNT];  /* 0x03003AC0 */
extern union SpriteOamEntry gSpriteOamBuffer[CORE_SPRITE_OAM_COUNT];
extern struct SceneEffectState gSceneEffects;             /* 0x03004800 */
extern u8 gTitleLogoStage;                                 /* 0x03002604 */
extern struct SpriteAffineMatrix gSpriteAffineMatrices[CORE_SPRITE_AFFINE_COUNT];
extern struct SpriteNode *gSpriteDrawOrder[CORE_SPRITE_NODE_COUNT];
extern struct ChestState gChestStates[CORE_CHEST_COUNT];
extern u8 gChestOpenBits[CORE_CHEST_CONFIG_COUNT / 8];
extern u8 gLastToggledChestId;
extern u16 gSceneId;                  /* Loaded resource/background scene. */
extern u16 gSceneType;                /* Halfword cache of a ROM format byte; bit7 plus FD/FE/FF codes. */
extern u8 gBackgroundScrollMode;      /* Software background strategy, 0..8. */
extern u16 gBackgroundXState;         /* Pixels in modes 2/5; step index in mode 4. */
extern u16 gCameraX;                  /* World-pixel viewport origin. */
extern u16 gCameraY;
extern u16 gBg3ScrollX;             /* BG3 HOFS shadow; ordinary IWRAM, not MMIO. */
extern u16 gBg3ScrollY;             /* BG3 VOFS shadow. */
extern u16 gCameraRegionLeft;          /* 0x03004650; pixel boundaries. */
extern u16 gCameraRegionTop;           /* 0x0300464C */
extern u16 gCameraRegionRight;         /* 0x030047C4 */
extern u16 gCameraRegionBottom;        /* 0x030047EC */
extern s16 gPlayerX;                  /* Regular scene: equals Actor 0 x. */
extern s16 gPlayerY;                  /* Regular scene: equals Actor 0 y - 8. */
extern const struct PlayerInteractionBounds gPlayerInteractionBounds[CORE_PLAYER_INTERACTION_DIRECTION_COUNT];
/* Interleaved signed X/Y steps for direction 0..8; direction 0 is stationary. */
extern const s16 gPlayerCollisionDirectionMasks[];
extern u8 gPlayerDirection;           /* Complete cached direction byte. */
extern u8 gPlayerTransitionStage;     /* Enter/return animation phases, 0..10. */
extern s16 gPlayerPositionHistoryX[CORE_PLAYER_HISTORY_COUNT];
extern s16 gPlayerPositionHistoryY[CORE_PLAYER_HISTORY_COUNT];
extern u8 gPlayerPositionHistoryDirection[CORE_PLAYER_HISTORY_COUNT];
extern u16 gBackgroundYState;         /* Pixels or a mode-specific phase. */
extern u8 gSceneScriptRunningSnapshot;     /* Sampled state; Script_GetFlags is authoritative. */
extern u8 gPlayerFieldFlags;          /* History freeze and menu eligibility. */
extern u8 gPendingSceneScriptEntryId; /* 0xFF means no pending location entry. */
extern u8 gSceneLocationIndex;        /* One-based; zero means no matching record. */

/* Clear chainInfo and next in all 128 nodes. */
void Sprite_ClearNodes(void);
void Sprite_ClearAffineMatrices(void);
void Sprite_ClearDrawOrder(void);
/* Build up to 128 software OAM entries and move the remaining slots offscreen. */
void Sprite_BuildOamBuffer(void);
/* Search slots 2..111; return 0 on failure without claiming the node. */
u8 Sprite_FindFreeNode(void);
/* Initialize OAM data; a returned next slot is not claimed. NULL retains next. */
struct SpriteNode *Sprite_InitNode(struct SpriteNode *node, u8 chainInfo,
    u16 attr0, u16 attr1, u16 attr2);
/* Stop at an inactive node; its existing next pointer is retained. */
void Sprite_FreeChain(struct SpriteNode *node);

void Actor_SetGraphicsSlots(u8 actorId, u8 graphicsSlot, u8 paletteSlot);
void Scene_Update(void);
/* Advance and draw scene Actors/chests, then build software OAM. */
void Scene_UpdateFrame(void);
void Camera_SetBounds(u16 regionId);
void Actor_InitSceneResources(u8 sceneId);
void Actor_LoadSceneResources(u8 sceneId);
void Scene_LoadStaticBackground(u8 sceneId);
void Palette_LoadDefaultBackdropColor(void);
void Actor_ReloadResources(void);
void Map_ExpandLayer(void);
void Map_ExpandScene(void);
/* Build the two 32x22 tilemap regions sampled from the expanded map layers. */
void Map_UpdateTilemaps(u16 cameraX, u16 cameraY);
const u8 *MapAnimation_Init(u16 slot, const u8 *data);
const u8 *MapAnimation_InitCompleted(u16 slot, const u8 *data);
void MapAnimation_DisableAll(void);
void MapAnimation_Update(void);
/* groupIndex is zero-based; the scene configuration uses one-based IDs. */
void MapAnimation_LoadGroup(u8 groupIndex, u8 firstSlot);
void MapAnimation_Pause(u8 slot);
void MapAnimation_Resume(u8 slot);
/* Return 0 for disabled, or expanded map layer 1/2. */
u8 MapAnimation_GetLayer(u8 slot);
/* Apply every record through one scratch slot, then disable that slot. */
void MapAnimation_ApplyCompletedChanges(u16 slot, const u8 *data);
void Actor_Init(u8 actorId, const struct ActorSpawnConfig *config);
void Actor_InitMenu(u8 menuSlot);
/* Initialize a blank sprite head; retain position and custom-animation fields. */
void Actor_InitSprite(u8 actorId);
/* Initialize Actor 18 at the cached field position with a hidden blank head. */
void Actor_InitAuxiliary(void);
/* Parse immediate commands until movement/wait starts or the stream ends. */
void Actor_UpdateCommands(u16 actorId);
s32 Actor_UpdateMovement(u16 actorId);
/* Handle nearby locations, Actors and chests; return scriptEntryId + 1, or zero. */
u8 Scene_Interact(void);
/* Rebuild the player/follower sprite chains and all eight position-history entries. */
void Player_InitActors(void);
/* Synchronize Actor 0 and its follower, maintaining history and standard drawing. */
void Player_UpdateActors(void);
/* Submit shadows on matching frame/Actor parity; a skipped shadow is retained. */
void Actor_UpdateShadow(u8 actorId);
/* Fill eight history entries and align Actor 1 unless history is frozen. */
void Player_ResetPositionHistory(void);
/* Rebuild history and Actor 1 unconditionally, retaining only player flag bit 7. */
void Player_ResetFollowing(void);
/* Background-mode contribution to Actor render height, returned as a halfword. */
u16 Camera_GetBackgroundHeightOffset(void);
void Actor_LoadGraphics(u8 graphicsSlot, u16 graphicsId);
void Actor_LoadPalette(u8 paletteSlot, u16 paletteId);
/* Consume both pending resource kinds, then clear the load flags. */
void Actor_LoadPendingResources(void);
/* Load a resource into an animation slot; renderSlot + 100 requests X flip. */
void Actor_LoadAnimation(u16 resourceId, u8 animationSlot, u8 renderSlot);
/* Load slot 0/render slot 5; the reserved animation-slot argument is ignored. */
void Actor_LoadPreviewAnimation(u16 resourceId, u8 unusedAnimationSlot);
/* Bit 7 selects the larger shadow tile set. */
void Actor_LoadShadowGraphics(u8 flags);
/* Release the main chain and clear spriteSlot; the shadow slot is independent. */
void Actor_FreeSprites(u8 actorId);
/* Release active Actors' counted chains and shadows, then clear all head slots. */
void Actor_FreeAllSprites(void);
/* Initialize the scene's configured Actors starting at slot 2. */
void Actor_InitScene(u8 sceneId);
/* Assign only the stream pointer; starting commands is a separate operation. */
void Actor_SetCommandData(u8 actorId, const u8 *commandData);
void Actor_StartCommands(u8 actorId);
u8 Actor_HasActiveCommands(void);
void Player_FreezePositionHistory(void);
void Actor_RequestGraphicsLoad(u8 graphicsSlot, u16 graphicsId);
void Actor_RequestPaletteLoad(u8 paletteSlot, u16 paletteId);
u8 Actor_GetResourceLoadFlags(void);
/* Inputs use eight-pixel units; clear waiting and set both directions. */
void Actor_SetPosition(u8 actorId, s16 x, s16 y, u8 direction);
/* Pixel displacement: yAxis selects X/Y; positive selects subtract/add. */
void Actor_OffsetPosition(u8 actorId, u8 yAxis, u8 positive, u8 pixels);
/* X = grid*8 + offset; Y = (grid+1)*8 + offset. */
void Actor_SetAxisPosition(u8 actorId, u8 yAxis, u8 gridPosition, u8 pixelOffset);
/* The caller passes sceneId; this helper reads the cached gSceneId instead. */
void Scene_SelectLocationRecord(u16 sceneId);
/* Drawing callers interpret this returned halfword as signed. */
u16 Actor_GetRenderHeight(struct ActorState *actor);
s16 Actor_GetRenderX(struct ActorState *actor);
/* Advance the custom-animation timeline, keeping the last frame on completion. */
void Actor_UpdateCustomAnimation(u8 actorId);
void Actor_StepCustomAnimation(u8 actorId);
/* The ROM entry receives the raw index and consumes its low byte. */
void Actor_StartCustomAnimation(u32 actorId, u8 animationSlot);
/* Clear looping and poll completion; IDs offset by 100 force completion. */
u32 Actor_FinishCustomAnimation(u8 actorId);

u16 Sprite_FindFreeTileTransfer(void);
u8 Sprite_FindFreePaletteTransfer(void);
void Sprite_QueuePaletteTransfer(u8 index, const u16 *source, u8 paletteSlot, u8 twoBanks);
void Sprite_ClearTileTransfers(void);
void Sprite_CopyTileTransfers(void);
void Sprite_CopyPaletteTransfers(void);
void Sprite_QueueTileTransfer(u16 index, const void *source, void *destination, u8 tileCount);
void Sprite_ClearPaletteTransfers(void);

void Chest_InitScene(u8 sceneId);
void Chest_BuildSprites(u8 chestId);
void Chest_Toggle(u8 chestId);
void Chest_LoadGraphics(void);
void Chest_ClearOpenBits(void);
void Chest_ToggleOpenBit(u8 persistentId);
u8 Chest_IsOpen(u8 persistentId);

/* Scene, drawing, effect and world-map entry contracts, in ROM order. */
void Scene_EnterMap(void);
void Scene_StartNewGame(void);
void Scene_RepositionPlayer(void);
void Scene_EnterBattle(void);
void Scene_ReturnFromBattle(void);
void Scene_EnterWorldMap(void);
void Scene_RestoreFieldSnapshot(void);
void Scene_UpdateField(void);
/* verticalOffset displaces drawing in pixels; submitted Y remains the order key. */
/* Coordinates/offset use their low 16 bits; slot/flags use their low 8 bits. */
s32 Sprite_QueueChain(s32 x, s32 y, s32 spriteSlot, s32 verticalOffset, s32 flags);
/* The ROM entry receives the raw index and consumes its low byte. */
void Actor_UpdateAnimation(u32 actorId);
void Actor_BuildAnimationFrame(u32 actorId, const u8 *frame);
u8 Encounter_CheckWalking(void);
void TitleLogo_Update(void);
void TitleLogo_UpdateVBlank(void);
void Scene_UpdateMapTransition(void);
void Scene_UpdateMenu(void);
void Scene_UpdateWorldMap(void);
void Scene_UpdateWorldMapExit(void);
void Scene_UpdateTitleMenu(void);
void Scene_UpdateShop(void);
void Scene_ClearForBackground(void);
void Actor_BuildMoveCommands(u8 actorId, u8 tileX, u8 tileY);
struct SpriteNode *Sprite_AppendOamNode(u16 *entryCount, struct SpriteNode *node);
void Transition_UpdateVBlank(void);
void Blend_UpdateHBlank(void);
void Blend_UpdateVBlank(void);
/* Return 1 on movement or the scene-25 equipment script; the latter retains x/y. */
u8 Player_TryMove(s16 *x, s16 *y, u8 direction, u8 speed);
u16 *Map_GetCollisionTileAddress(s16 x, s16 y);
u16 Map_GetCollisionMask(const u16 *tiles, u16 x, u16 y);
void Camera_UpdateScroll(void);
void Scene_FillEffectTilemap(u16 pattern);
void Scene_LoadResources(u16 sceneId);
void MapAnimation_LoadSceneGroup(u8 groupId);
void MapAnimation_UpdateSlot(s16 slot);
/* Event status uses the full return word; coordinates are signed halfwords. */
s32 Map_FindPlayerEvent(s16 x, s16 y);
/* Type 3 returns 1 when started; type 4 returns 0 even after starting a script. */
s32 Map_RunPlayerEvent(void);
void WorldMap_LoadResources(u8 regionId);
void WorldMap_CreateMarkerSprites(u8 regionId);
u32 WorldMap_LoadConnections(void);
void WorldMap_DrawMarkers(void);
void WorldMap_UpdateInput(u16 keys);
void Scene_QueueOverlayImage(u8 imageId, u8 slot);
void Scene_UpdateScreenEffects(void);
void Scene_LoadScriptBackground(u8 backgroundId);
/* The third argument is stored; its further meaning remains unknown. */
void Blend_StartScanlineEffect(u16 flags, s16 step, u16 unknownParameter);
void Transition_UpdateHBlank(void);
void Transition_Start(u16 effect);
void Scene_LoadSharedTiles(u8 skipUiTiles);
void WorldMap_LoadPlayerResources(void);
void Actor_ReloadResourceSlot(u8 slot);
void WorldMap_SelectLocation(u8 locationId);
void Scene_UploadOverlayImage(void);
void Scene_SetSavedScriptBank(u8 bankId);
void Scene_DrawEffectTileBlock(u8 frame);
void Scene_UploadScriptBackground(void);
void Palette_Update(void);
void Palette_UpdateVBlank(void);
void Palette_StartTransition(u8 transitionId);
void Palette_UploadTransition(void);
void Palette_StepTransition(void);
void Palette_DisableAnimations(void);
/* groupId is one-based; zero leaves the tracks unchanged. */
void Palette_LoadAnimationGroup(u8 groupId);
/* firstSlot precedes the zero-based groupIndex in the original ABI. */
void Palette_LoadAnimationGroupAt(u8 firstSlot, u8 groupIndex);
void Palette_ResumeAnimation(u8 slot);
void Palette_PauseAnimation(u8 slot);
u8 Palette_GetAnimationFlags(u8 slot);
void Palette_SaveTransitionSource(void);
void Palette_FillWhite(void);
const u8 *Palette_InitAnimation(u8 slot, const u8 *data);
void FieldAnim_LoadResources(u8 groupId);
void FieldAnim_InitInstances(u8 groupId);
void FieldAnim_Update(void);
void FieldAnim_BuildSpriteChain(u8 instanceId, const u8 *frame);
void FieldAnim_DisableAll(void);

/* Shared core state: ordinary global definitions live in core.c. */
extern u8 gScriptedEncounterId; /* 0x030025B8 */
extern u8 gSpriteScratchWidth; /* 0x030025BC */
extern u8 gTitlePresentationStage;                              /* 0x030025F0; 0 game, 1 MEDIA RINGS, 2 GAME ARTS, 3 title and title menus. */
extern u8 gSceneBgmControl; /* 0x030025F4 */
extern u16 gScenePhase;                                         /* 0x03002600; Shared phase of the routine selected by gSceneState. */
extern u16 gCoreUnknown2608; /* 0x03002608 */
extern u8 gSpriteScratchHeight; /* 0x03002C30 */
extern u8 gBattleFieldReturnMode; /* 0x03002C34 */
extern u32 gPartyGold; /* 0x03002C38 */
extern u8 gScriptedBattleState; /* 0x03002C48 */
extern u16 gSceneTransitionCounter;                             /* 0x03002C4C; Shared logo timer, title blend/blink counter and field-transition alternation. */
extern u8 gFieldInputDelay; /* 0x03002C50 */
extern u8 gActorAnimationSlotFlags[CORE_ACTOR_ANIMATION_SLOT_COUNT]; /* 0x03002C60 */
extern u16 gPendingActorPaletteId; /* 0x03003240 */
extern const u8 *gActorAnimationTileSources[CORE_ACTOR_ANIMATION_SLOT_COUNT]; /* 0x03003250 */
extern u8 gActorResourceLoadFlags; /* 0x030032D0 */
extern u16 gPendingActorGraphicsId; /* 0x030032D4 */
extern const u16 *gActorAnimationPalettes[CORE_ACTOR_ANIMATION_SLOT_COUNT]; /* 0x030032E0 */
extern u8 gSpriteTileTransferCounts[CORE_SPRITE_TRANSFER_COUNT]; /* 0x03003360 */
extern struct SpritePaletteTransferRequest gSpritePaletteTransfers[CORE_SPRITE_TRANSFER_COUNT]; /* 0x03003380 */
extern u8 gActorAnimationRenderSlots[CORE_ACTOR_ANIMATION_SLOT_COUNT]; /* 0x03003490 */
extern u8 gPendingActorPaletteSlot; /* 0x030034B0 */
extern const void *gSpriteTileTransferAddresses[CORE_SPRITE_TRANSFER_COUNT][2]; /* 0x030039C0 */
extern const u8 *gActorAnimationDefinitions[CORE_ACTOR_ANIMATION_SLOT_COUNT]; /* 0x030044C0 */
extern u8 gPendingActorGraphicsSlot; /* 0x03004540 */
extern u16 gBlendCoefficients;                                  /* 0x03004550; Cached BLDALPHA pair or BLDY coefficient, selected by BLDCNT bit 7. */
extern u8 gSceneBgmId;                                          /* 0x03004608; Scene-config BGM id; menus may play a different song temporarily. */
extern u16 gBg1RasterMode; /* 0x03004610 */
extern u16 gSceneCameraRegionId;                                /* 0x03004614; Current/requested index into the four-byte camera-region records. */
extern s16 gBackgroundMoveStartY; /* 0x0300461C */
extern u16 *gMapExpansionOutput; /* 0x03004620 */
extern u8 gVisitedDestination[2]; /* 0x03004624 */
extern u8 gSceneBackgroundEffect; /* 0x03004628 */
extern s16 gBackgroundMoveTargetX; /* 0x03004630 */
extern u8 gSceneScriptBankId; /* 0x03004634 */
extern u8 gSceneEntryTileY;                                     /* 0x03004638; Destination Y in eight-pixel units, converted to player pixels on entry. */
extern u16 gBlendControl;                                       /* 0x03004658; Active BLDCNT cache consumed by the display uploaders. */
extern u16 gScriptBackgroundPart; /* 0x03004660 */
extern s16 gBackgroundCameraOffset; /* 0x03004664 */
extern u16 gTransitionWindowScanlineRadius; /* 0x03004668 */
extern u8 gActorGraphicsIds[CORE_ACTOR_RESOURCE_SLOT_COUNT]; /* 0x03004670 */
extern u8 gBackgroundMotionState; /* 0x03004680 */
extern u16 gMapCollisionLayer; /* 0x03004688 */
extern u8 gSceneEntryTileX;                                     /* 0x0300468C; Destination X in eight-pixel units, converted to player pixels on entry. */
extern const u8 *gMapChunkDefinitions; /* 0x03004690 */
extern const u16 *gPendingOverlayPalette; /* 0x03004694 */
extern u16 gBg1ScrollMode; /* 0x03004698; system reads the full halfword. */
extern struct MapAnimationState gMapAnimationStates[CORE_MAP_ANIMATION_COUNT]; /* 0x030046A0 */
extern u8 gSceneEntryDirection;                                 /* 0x030047A4; Destination facing byte copied to gPlayerDirection. */
extern u16 gBlockingTileThreshold; /* 0x030047AC */
extern u16 gBackgroundMoveDuration; /* 0x030047B4 */
extern u8 gWorldMapTargetY; /* 0x030047B8; world-map pixel coordinate. */
extern u16 gScriptBackgroundUploadKind; /* 0x030047C8 */
extern const void *gPendingOverlayGraphics; /* 0x030047CC */
extern u8 gActorPaletteIds[CORE_ACTOR_RESOURCE_SLOT_COUNT]; /* 0x030047D0 */
extern s16 gBackgroundMoveStartX; /* 0x030047DC */
extern u8 gPendingSceneId;                                      /* 0x030047E4; Destination resource scene; gSceneId holds the loaded scene. */
extern const u8 *gMapChunkGrid; /* 0x030047E8 */
extern u16 gSceneBackgroundEffectFrame; /* 0x030047F4 */
extern u8 gWorldMapTargetX; /* 0x03004824; world-map pixel coordinate. */
extern u16 gBg2ScrollY; /* 0x03004828 */
extern const u16 *gMapBlockDefinitions; /* 0x0300482C */
extern s16 gBackgroundMoveTargetY; /* 0x03004830 */
extern u8 gPendingOverlaySlot; /* 0x0300483C */
extern u16 gBackgroundMoveFrame; /* 0x03004844 */
extern u16 gBg2ScrollX; /* 0x03004848 */
extern u8 gSavedSceneScriptBankId; /* 0x03004850 */
extern u8 gCameraFollowDisabled; /* 0x03004854 */

/* The same table as MAP_METATILES_A, declared without an inferred capacity.
 * A 2x2 metatile has four halfwords: top left/right, then bottom left/right.
 * Entry views start at +2/+4/+6 bytes; each block advances by four halfwords.
 * This extern declaration does not allocate a second table. */
extern u16 gMapMetatilesA[];

extern const void *const gActorGraphicsTable[];

/* LUNAR title logo resources: 0x20 bytes of palette followed by an LZ77
 * tilemap, then three LZ77 graphics parts. */
extern const u8 gTitleLogoPalette[];
extern const u8 gTitleLogoGraphicsPart0[];
extern const u8 gTitleLogoGraphicsPart1[];
extern const u8 gTitleLogoGraphicsPart2[];

extern const u16 gActorPaletteTable[103][CORE_OBJ_PALETTE_COLOR_COUNT];

extern const u32 gActorSmallShadowTiles[CORE_SHADOW_SMALL_TILE_COUNT][CORE_SPRITE_TILE_ROW_COUNT];

extern const u32 gActorLargeShadowTiles[CORE_SHADOW_LARGE_TILE_COUNT][CORE_SPRITE_TILE_ROW_COUNT];

extern void (*const gSceneStateHandlers[])(void);

extern const struct SceneLocationRecord gSceneLocationRecords[CORE_SCENE_LOCATION_COUNT];

extern const struct ChestSpawnConfig gChestConfigs[CORE_CHEST_CONFIG_COUNT];

extern const u32 gChestTiles[CORE_CHEST_GRAPHICS_TILE_COUNT][CORE_SPRITE_TILE_ROW_COUNT];

extern const u16 gChestPalettes[CORE_CHEST_PALETTE_BANK_COUNT][CORE_OBJ_PALETTE_COLOR_COUNT];

extern const struct ActorAnimationResource gActorAnimationResources[];

extern const u8 *const gActorAnimationDefinitionTable[];

extern const void *const gActorAnimationGraphicsTable[];

extern const u16 gActorAnimationPaletteTable[63][CORE_OBJ_PALETTE_COLOR_COUNT];

extern const struct BattleEnemyInfo gActorPreviewConfigs[101];

extern const struct BattleBossInfo gExtendedActorPreviewConfigs[17];

extern const struct BattleAnimationResourceConfig gBattleAnimationConfigs[];

extern const void *const gBattleAnimationGraphicsTable[];

/* Standard phases 0,1,2,1 followed by the square-layout phases 0,1,0,1. */
extern const u8 gActorStandardAnimationTileOffsets[8];
extern const u8 gPlayerDirectionByKeyMask[];
/* Direction rows and square-layout flip bits; the full table extent is unknown. */
extern const u8 gActorDirectionFlipFlags[];

extern const u8 gSpriteDimensions[CORE_SPRITE_SHAPE_COUNT * CORE_SPRITE_SIZE_COUNT * sizeof(struct SpriteDimensions)];

extern const u8 gSpriteTileCounts[CORE_SPRITE_SHAPE_COUNT * CORE_SPRITE_SIZE_COUNT];

extern const struct CameraRegionConfig gCameraRegionConfigs[];

extern const struct SceneResourceConfig gSceneResourceConfigs[];
extern const u8 gTravelDestinationSceneIds[];                   /* 0x080870DC */
extern const u8 gTransitionWindowCurve[];                      /* 0x080870EC */
extern const s16 gBackgroundMotionSteps[];                     /* 0x080871EA */
extern const void *const gSceneCompressedGraphics[];
extern const void *const gSceneCompressedLayouts[];

extern const struct SceneActorResourceConfig gSceneActorResourceConfigs[];

extern const void *const gSceneStaticBackgroundGraphics[];

extern const void *const gSceneBackgroundTilemaps[];

extern const u16 gScenePalettes[162][CORE_SCENE_PALETTE_COLOR_COUNT];

extern const void *const gScriptBackgroundGraphics[];

extern const void *const gScriptBackgroundTilemaps[];

extern const u16 gScriptBackgroundPalettes[][CORE_SCRIPT_BACKGROUND_PALETTE_COLOR_COUNT];

extern const u16 gDefaultBackdropColor;

extern const u8 *const gMapAnimationGroups[];

extern const struct ActorSpawnConfig *const gSceneActorConfigs[];

extern const u8 *const gPaletteAnimationGroups[];
/* Count-prefixed palette-animation streams, padded to a halfword boundary. */
extern const u8 gPaletteAnimationGroup00[];
extern const u8 gPaletteAnimationGroup01[];
extern const u8 gPaletteAnimationGroup02[];
extern const u8 gPaletteAnimationGroup03[];
extern const u8 gPaletteAnimationGroup04[];
extern const u8 gPaletteAnimationGroup05[];
extern const u8 gPaletteAnimationGroup06[];
extern const u8 gPaletteAnimationGroup07[];
extern const u8 gPaletteAnimationGroup08[];
extern const u8 gPaletteAnimationGroup09[];
extern const u8 gPaletteAnimationGroup10[];
extern const u8 gPaletteAnimationGroup11[];
extern const u8 gPaletteAnimationGroup12[];
extern const u8 gPaletteAnimationGroup13[];
extern const u8 gPaletteAnimationGroup14[];
extern const u8 gPaletteAnimationGroup15[];
extern const u8 gPaletteAnimationGroup16[];
extern const u8 gPaletteAnimationGroup17[];
extern const u8 gPaletteAnimationGroup18[];
extern const u8 gPaletteAnimationGroup19[];
extern const u8 gPaletteAnimationGroup20[];
extern const u8 gPaletteAnimationGroup21[];
extern const u8 gPaletteAnimationGroup22[];
extern const u8 gPaletteAnimationGroup23[];
extern const u8 gPaletteAnimationGroup24[];
extern const u8 gPaletteAnimationGroup25[];
extern const u8 gPaletteAnimationColors[];
extern const void *const gOverlayImageGraphics[];
extern const u8 gOverlayImagePaletteIds[];
extern const u16 gOverlayImagePalettes[29][CORE_OBJ_PALETTE_COLOR_COUNT];
extern u16 *const gOverlayImageTilemapDestinations[];
/* A count byte precedes each region's WorldMapLocationRecord array. */
extern const void *const gWorldMapCompressedGraphics[];
extern const void *const gWorldMapCompressedLayouts[];
extern const u16 *const gWorldMapPalettes[];
extern const u8 gSceneSharedTilePage0[];
extern const u8 gSceneSharedTilePage1[];
extern const u8 gSceneSharedTilePage2[];
extern const u8 gSceneSharedTilePage3[];
extern const u16 gSceneSharedPalettes[48];
extern const u8 gWorldMapUiTilemap0[];                          /* 0x08095A1C */
extern const u8 gWorldMapUiTilemap1[];                          /* 0x08095C94 */
extern const u8 gWorldMapUiTilemap2[];                          /* 0x08095F14 */
extern const u8 gWorldMapUiTilemap3[];                          /* 0x0809619C */
extern const u16 gSceneOverlayTiles0[256];
extern const u16 gSceneOverlayTiles1[256];
extern const u16 gSceneOverlayPalette0[16];
extern const u16 gSceneOverlayPalette1[16];
extern const u16 gSceneOverlayPalette2[16];
extern const u16 gSceneOverlayPalette3[16];
extern const u16 gSceneOverlayPalette4[16];
extern const u8 gSceneOverlayTilemap0[];
extern const u8 gSceneOverlayTilemap1[];
extern const u8 gSceneOverlayTilemap2[];
extern const u8 gSceneOverlayTilemap3[];
extern const u8 gSceneOverlayTilemap4[];
extern const u8 gSceneOverlayTilemap5[];
extern const u8 gSceneOverlayTilemap6[];
extern const u8 gSceneOverlayTilemap7[];
extern const u8 gSceneOverlayTilemap8[];
extern const u8 gSceneOverlayTilemap9[];
extern const u8 gSceneOverlayTilemap10[];
extern const u8 gSceneOverlayTilemap11[];
extern const u8 gSceneOverlayTilemap12[];
extern const u8 gSceneOverlayTilemap13[];
extern const u8 gSceneOverlayTilemap14[];
extern const u8 gSceneOverlayTilemap15[];
extern const u8 gSceneEffect18Tiles[];
extern const u8 *const gWorldMapRegionNameGroups[];
/* Five region headings, each parsed by Menu_DrawLayout. */
extern const u8 gWorldMapRegion0NameLayout[];
extern const u8 gWorldMapRegion1NameLayout[];
extern const u8 gWorldMapRegion2NameLayout[];
extern const u8 gWorldMapRegion3NameLayout[];
extern const u8 gWorldMapRegion4NameLayout[];

extern const u8 gWorldMapConnectionLists[];
extern const u8 gWorldMapLocationRecords[];
extern const u8 gWorldMapPairedMarkerTiles[];
extern const u8 gWorldMapSingleMarkerTiles[];
/* Connection low nibbles 9..15 select these u16 event-flag ids. */
extern const u16 gWorldMapConnectionUnlockFlags[7];
/* Region/packed endpoint pairs index the Actor command-stream pointer table. */
extern const u8 gWorldMapDestinationRecords[];                 /* 0x0808823A */
/* Eight-byte location/scene records; each region ends with a single 0xFF. */
extern const u8 gWorldMapRouteCommands[];                      /* 0x080882E2 */
extern const u8 *const gWorldMapRouteCommandStreams[];          /* 0x087E96C8 */
/* Actor command streams named by region and source/destination location ids. */
extern const u8 gWorldMapRegion0Route0To1Commands[];
extern const u8 gWorldMapRegion0Route1To0Commands[];
extern const u8 gWorldMapRegion0Route2To3Commands[];
extern const u8 gWorldMapRegion0Route2To4Commands[];
extern const u8 gWorldMapRegion0Route3To2Commands[];
extern const u8 gWorldMapRegion0Route3To4Commands[];
extern const u8 gWorldMapRegion0Route4To2Commands[];
extern const u8 gWorldMapRegion0Route4To3Commands[];
extern const u8 gWorldMapRegion1Route0To1Commands[];
extern const u8 gWorldMapRegion1Route1To0Commands[];
extern const u8 gWorldMapRegion1Route1To2Commands[];
extern const u8 gWorldMapRegion1Route2To1Commands[];
extern const u8 gWorldMapRegion1Route2To3Commands[];
extern const u8 gWorldMapRegion1Route3To2Commands[];
extern const u8 gWorldMapRegion1Route4To5Commands[];
extern const u8 gWorldMapRegion1Route5To4Commands[];
extern const u8 gWorldMapRegion1Route0To2Commands[];
extern const u8 gWorldMapRegion1Route2To0Commands[];
extern const u8 gWorldMapRegion1Route0To3Commands[];
extern const u8 gWorldMapRegion1Route3To0Commands[];
extern const u8 gWorldMapRegion1Route1To3Commands[];
extern const u8 gWorldMapRegion1Route3To1Commands[];
extern const u8 gWorldMapRegion1Route0To6Commands[];
extern const u8 gWorldMapRegion1Route1To6Commands[];
extern const u8 gWorldMapRegion1Route2To6Commands[];
extern const u8 gWorldMapRegion1Route3To6Commands[];
extern const u8 gWorldMapRegion1Route6To0Commands[];
extern const u8 gWorldMapRegion1Route6To1Commands[];
extern const u8 gWorldMapRegion1Route6To2Commands[];
extern const u8 gWorldMapRegion1Route6To3Commands[];
extern const u8 gWorldMapRegion2Route0To1Commands[];
extern const u8 gWorldMapRegion2Route1To0Commands[];
extern const u8 gWorldMapRegion2Route1To2Commands[];
extern const u8 gWorldMapRegion2Route1To3Commands[];
extern const u8 gWorldMapRegion2Route1To4Commands[];
extern const u8 gWorldMapRegion2Route1To5Commands[];
extern const u8 gWorldMapRegion2Route1To6Commands[];
extern const u8 gWorldMapRegion2Route2To1Commands[];
extern const u8 gWorldMapRegion2Route2To4Commands[];
extern const u8 gWorldMapRegion2Route2To5Commands[];
extern const u8 gWorldMapRegion2Route2To6Commands[];
extern const u8 gWorldMapRegion2Route3To1Commands[];
extern const u8 gWorldMapRegion2Route4To1Commands[];
extern const u8 gWorldMapRegion2Route4To2Commands[];
extern const u8 gWorldMapRegion2Route4To5Commands[];
extern const u8 gWorldMapRegion2Route4To6Commands[];
extern const u8 gWorldMapRegion2Route5To1Commands[];
extern const u8 gWorldMapRegion2Route5To2Commands[];
extern const u8 gWorldMapRegion2Route5To4Commands[];
extern const u8 gWorldMapRegion2Route5To6Commands[];
extern const u8 gWorldMapRegion2Route6To1Commands[];
extern const u8 gWorldMapRegion2Route6To2Commands[];
extern const u8 gWorldMapRegion2Route6To4Commands[];
extern const u8 gWorldMapRegion2Route6To5Commands[];
extern const u8 gWorldMapRegion2Route7To8Commands[];
extern const u8 gWorldMapRegion2Route8To7Commands[];
extern const u8 gWorldMapRegion3Route0To1Commands[];
extern const u8 gWorldMapRegion3Route0To2Commands[];
extern const u8 gWorldMapRegion3Route0To3Commands[];
extern const u8 gWorldMapRegion3Route1To0Commands[];
extern const u8 gWorldMapRegion3Route1To2Commands[];
extern const u8 gWorldMapRegion3Route1To3Commands[];
extern const u8 gWorldMapRegion3Route2To0Commands[];
extern const u8 gWorldMapRegion3Route2To1Commands[];
extern const u8 gWorldMapRegion3Route2To3Commands[];
extern const u8 gWorldMapRegion3Route3To0Commands[];
extern const u8 gWorldMapRegion3Route3To1Commands[];
extern const u8 gWorldMapRegion3Route3To2Commands[];
extern const u8 gWorldMapRegion3Route4To5Commands[];
extern const u8 gWorldMapRegion3Route5To4Commands[];
extern const u8 gWorldMapRegion3Route6To7Commands[];
extern const u8 gWorldMapRegion3Route6To8Commands[];
extern const u8 gWorldMapRegion3Route7To6Commands[];
extern const u8 gWorldMapRegion3Route7To8Commands[];
extern const u8 gWorldMapRegion3Route8To6Commands[];
extern const u8 gWorldMapRegion3Route8To7Commands[];
extern const u8 gWorldMapRegion4Route0To1Commands[];
extern const u8 gWorldMapRegion4Route1To0Commands[];
extern const u8 gWorldMapRegion4Route2To3Commands[];
extern const u8 gWorldMapRegion4Route2To4Commands[];
extern const u8 gWorldMapRegion4Route3To2Commands[];
extern const u8 gWorldMapRegion4Route3To4Commands[];
extern const u8 gWorldMapRegion4Route4To2Commands[];
extern const u8 gWorldMapRegion4Route4To3Commands[];

#endif
