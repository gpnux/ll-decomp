#ifndef CORE_INL_H
#define CORE_INL_H

#include "core.h"

/* Private actor, player and sprite helpers used by core.c. */
#define CORE_ACTOR_ANIMATION_FRAME_OFFSET_BASE 4
/* Per-actor EWRAM command streams; this view does not allocate RAM. */
#define ACTOR_MOVE_COMMANDS ((u8 (*)[CORE_ACTOR_MOVE_COMMAND_BUFFER_SIZE])0x0203EE00)

static inline void Actor_AdvanceSceneMovementInline(u16 actorId, struct ActorState *actor)
{
    u16 result = Actor_UpdateMovement(actorId);

    if (result == 1 && actor->commandData != NULL)
    {
        actor->moveTimer++;
        result = 2;
    }
    if (result <= 1)
        Actor_UpdateCommands(actorId);
}

static inline void Actor_ClearShadowInline(struct ActorState *actor)
{
    if (actor->shadowSpriteSlot != 0)
    {
        gSpriteNodes[actor->shadowSpriteSlot].chainInfo = 0;
        gSpriteNodes[actor->shadowSpriteSlot].next = NULL;
        actor->shadowSpriteSlot = 0;
    }
}

static inline u16 Actor_GetAnimationFrameTableOffsetInline(const u8 *frameIdBytes)
{
    return (u16)((frameIdBytes[0] + (frameIdBytes[1] << 8)) * sizeof(u16));
}

static inline const u8 *Actor_ResolveAnimationFrameInline(const u8 *data, u32 offsetsAddress)
{
    u32 frameAddress = (u32)data
        + *(const u8 *)(offsetsAddress + sizeof(struct ActorAnimationDefinition));

    offsetsAddress = *(const u8 *)(offsetsAddress + sizeof(struct ActorAnimationDefinition) + 1);
    frameAddress += offsetsAddress << 8;
    return (const u8 *)(frameAddress + CORE_ACTOR_ANIMATION_FRAME_OFFSET_BASE);
}

static inline const u16 *Sprite_GetPaletteTransferSourceInline(const u8 *sources, u16 index)
{
    return *(const u16 *const *)(sources + index * sizeof(struct SpritePaletteTransferRequest));
}

static inline u16 Actor_NormalizeDirectionInline(u32 direction)
{
    return direction & CORE_ACTOR_DIRECTION_MASK;
}

static inline void Actor_UpdateVerticalOffsetInline(struct ActorState *actor)
{
    if (actor->verticalTimer != 0)
    {
        actor->verticalTimer--;
        if (actor->verticalTimer == 0)
            actor->verticalOffset = actor->verticalTarget;
        else
        {
            actor->verticalFixed += actor->verticalStep;
            actor->verticalOffset = actor->verticalFixed / 16;
        }
    }
}

static inline u32 Player_FindFreeSpriteSlotInline(void)
{
    u16 slot = CORE_SPRITE_SEARCH_FIRST;
    u32 foundSlot;
    for (; slot <= CORE_SPRITE_SEARCH_LAST; slot++)
    {
        if (gSpriteNodes[slot].chainInfo == 0)
        {
            foundSlot = slot;
            goto found;
        }
    }
    foundSlot = 0;
found:
    return foundSlot;
}

static inline struct SpriteNode *Player_InitSpriteNodeInline(struct SpriteNode *node,
    u8 count, u16 attr0, u16 attr1, u16 attr2)
{
    u32 slot;
    node->chainInfo = count;
    node->attr0 = attr0;
    node->attr1 = attr1;
    node->attr2 = attr2;
    node->flags = 0;
    if ((count & CORE_SPRITE_CHAIN_COUNT_MASK) == CORE_PLAYER_SPRITE_TAIL_COUNT)
        return NULL;
    slot = Player_FindFreeSpriteSlotInline();
    if (slot == 0)
        return NULL;
    node->next = &gSpriteNodes[slot];
    return node->next;
}

static inline void Player_ShiftPositionHistoryInline(struct ActorState *actor)
{
    u16 index = CORE_PLAYER_HISTORY_COUNT - 1;
    do
    {
        gPlayerPositionHistoryX[index] = gPlayerPositionHistoryX[index - 1];
        gPlayerPositionHistoryY[index] = gPlayerPositionHistoryY[index - 1];
        gPlayerPositionHistoryDirection[index] = gPlayerPositionHistoryDirection[index - 1];
        index--;
    } while (index != 0);
    gPlayerPositionHistoryX[0] = actor->x;
    gPlayerPositionHistoryY[0] = actor->y;
    gPlayerPositionHistoryDirection[0] = actor->direction & CORE_ACTOR_DIRECTION_MASK;
}

static inline u8 Sprite_FindFreeNodeInline(void)
{
    u16 index;

    for (index = CORE_SPRITE_SEARCH_FIRST; index <= CORE_SPRITE_SEARCH_LAST; index++)
    {
        if (gSpriteNodes[index].chainInfo == 0)
            return index;
    }

    return 0;
}

static inline void Sprite_FreeChainInline(struct SpriteNode *node)
{
    struct SpriteNode *next;

    if (node->chainInfo == 0)
        return;

    node->chainInfo = 0;
    next = node->next;
    while (next != NULL)
    {
        node->next = NULL;
        node = next;
        if (node->chainInfo == 0)
            return;

        node->chainInfo = 0;
        next = node->next;
    }
}

static inline void Actor_AppendMoveCommandInline(u8 **cursor, s32 delta,
    s16 magnitude, u8 negativeDirection)
{
    u32 count = delta;
    s32 signedDelta = (s16)delta;

    if (signedDelta != 0)
    {
        u8 *data = *cursor;

        *data++ = CORE_ACTOR_CMD_MOVE;
        if (signedDelta < 0)
        {
            *data++ = negativeDirection;
            count = (u16)magnitude;
        }
        else
        {
            *data++ = negativeDirection ^ CORE_ACTOR_DIRECTION_OPPOSITE_BIT;
        }
        *data++ = 1;
        *data = count;
        *cursor += sizeof(struct ActorMoveCommand);
    }
}

#endif
