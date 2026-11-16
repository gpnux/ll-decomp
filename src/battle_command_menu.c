#include "battle_command_menu.h"
#include "battle_ai.h"
#include "battle.h"
#include "battle_items.h"
#include "battle_palette.h"
#include "battle_rules.h"
#include "core.h"
#include "gba.h"
#include "script.h"
#include "sound.h"

#define BATTLE_MENU_SCROLL_ARROW_VRAM ((void *)0x0600BBC0)
#define BATTLE_MENU_ITEM_ARROW_TOP_ROW ((u16 *)0x02035CCE)

/* .lcomm: private objects in original RAM order; natural alignment preserves gaps. */
/** @brief Active command-menu descriptor.
 *  @note Signed byte; -1 disables the menu, otherwise enum BattleMenuState.
 *  @address 0x03000768
 */
static s8 sBattleMenuState;

/** @brief Phase shared by command transitions, lists and target selection.
 *  @note Each command path interprets the byte using its own phase enum.
 *  @address 0x03000769
 */
static s8 sBattleMenuPhase;

/** @brief Selected icon within the current command descriptor.
 *  @note The root and per-character command histories restore this signed byte.
 *  @address 0x0300076A
 */
static s8 sBattleMenuCommandIndex;

/** @brief Current member of the filtered manual-input party list.
 *  @note This is an index into the caller's pointer list, not a raw combatant slot.
 *  @address 0x0300076B
 */
static s8 sBattleMenuPartyIndex;

/** @brief Pending command, redraw, target and party-transition flags.
 *  @note Bits 4..7 encode one transition value; other bits are independent flags.
 *  @address 0x0300076C
 */
static u16 sBattleMenuFlags;

/** @brief Timer shared by party movement and list arrows.
 *  @note Movement passes its signed halfword view; arrows advance modulo 16.
 *  @address 0x0300076E
 */
static u16 sBattleMenuAnimationFrame;

/** @brief Number of entries in the current character's skill list.
 *  @note At most eight learned slots and one direct special-skill entry.
 *  @address 0x03000770
 */
static u8 sBattleSkillChoiceCount;

/** @brief Skill selections in display order.
 *  @note Values 0..7 select learned slots; the optional ninth entry is a direct skill id.
 *  @address 0x03000778
 */
static u8 sBattleSkillChoices[BATTLE_MENU_SKILL_CHOICE_COUNT];

/** @brief First visible skill-list entry.
 *  @note Three rows are shown; the stored byte retains the original signed comparisons.
 *  @address 0x03000781
 */
static s8 sBattleSkillScroll;

/** @brief Selected entry within the complete skill list.
 *  @note Saved separately from the scroll position in the character history.
 *  @address 0x03000782
 */
static s8 sBattleSkillIndex;

/** @brief Usability bits for displayed skill choices.
 *  @note One bit per choice controls disabled colors and confirmation.
 *  @address 0x03000784
 */
static u16 sBattleSkillUsableMask;

/** @brief Command and list selections remembered for ten characters.
 *  @note Ten packed five-byte records; history lookup uses character id minus one.
 *  @address 0x03000788
 */
static struct BattleCommandHistory sBattleCommandHistory[BATTLE_COMMAND_HISTORY_COUNT];

/** @brief Last selected root command icon.
 *  @note Root movement saves immediately; character commands save on confirmation.
 *  @address 0x030007BA
 */
static u8 sBattleLastCommand;

/** @brief Combatant slots highlighted for the selected skill.
 *  @note Seven signed entries; -1 marks an unused position.
 *  @address 0x030007BC
 */
static s8 sBattleSkillTargetSlots[BATTLE_ENEMY_COUNT];

/** @brief Items available for the current battle command.
 *  @note The items module fills the caller-owned item-id/count records.
 *  @address 0x030007C8
 */
static struct BattleItemStack sBattleMenuItems[BATTLE_TACTICS_ITEM_COUNT];

/** @brief Number of populated records in the battle item list.
 *  @note Updated by BattleItem_BuildMenuList before opening the item command.
 *  @address 0x03000808
 */
static u8 sBattleMenuItemCount;

/** @brief First visible item-list entry.
 *  @note The item list displays three rows and preserves an unsigned scroll byte.
 *  @address 0x03000809
 */
static u8 sBattleMenuItemScroll;

/** @brief Selected entry within the complete item list.
 *  @note Used for description rendering, history and item reservation.
 *  @address 0x0300080A
 */
static u8 sBattleMenuItemIndex;

/** @brief Party slots whose old defend animations must return to idle.
 *  @note Filled when command input begins, after changing their actions to attack.
 *  @address 0x0300080C
 */
static u8 sBattleCommandResetSlots[BATTLE_PARTY_COUNT];

/** @brief Number of queued defend-animation resets.
 *  @note The reset worker processes only populated entries.
 *  @address 0x03000811
 */
static u8 sBattleCommandResetCount;

/** @brief Current entry in the defend-animation reset queue.
 *  @note Advance after the combatant animation stops loading graphics.
 *  @address 0x03000812
 */
static u8 sBattleCommandResetIndex;

/** @brief Phase of the current defend-animation reset.
 *  @note Enum BattleMenuCommandResetPhase is stored in this single byte.
 *  @address 0x03000813
 */
static u8 sBattleCommandResetPhase;

/** @brief Column remembered when vertical navigation cannot retain it.
 *  @note -1 is unset; other values are the low formation nibble.
 *  @address 0x03000814
 */
static s8 sBattleTargetPreviousColumn;

/** @brief Row remembered when horizontal navigation cannot retain it.
 *  @note Keeps the high formation nibble unshifted and the original negative test.
 *  @address 0x03000815
 */
static s8 sBattleTargetPreviousRowBits;

/** @brief Deferred command-hint request.
 *  @note One enables updates, two skips an update, and zero services window flags.
 *  @address 0x03000816
 */
static u8 sBattleMenuHintRequest;

/** @brief Command-hint window flags.
 *  @note Bit 0x1000 remains active until window zero is closed.
 *  @address 0x03000818
 */
static u16 sBattleMenuHintFlags;

/** @brief Horizontal distance between command icons.
 *  @note The root descriptor uses 32 pixels and child descriptors use 24 pixels.
 *  @address 0x0300081A
 */
static u8 sBattleMenuCommandSpacing;

/** @brief Inline the command-hint request store in menu transitions.
 *  @note The value parameter preserves the dispatcher's original load/store order.
 *  @address 0x0802550C
 */
static inline void BattleMenu_SetHintRequestInline(u8 request)
{
    sBattleMenuHintRequest = request;
}

/** @brief Select an animation frame from the shared menu timer.
 *  @note The value parameter retains the halfword reload; u32 keeps the shift logical.
 *  @address 0x08025682, 0x080256AC
 */
static inline u16 BattleMenu_GetScrollArrowFrameInline(u16 shift)
{
    return (u32)sBattleMenuAnimationFrame >> shift;
}

/*
 * 08021130..080257D8：战斗命令、技能/道具菜单及手动目标导航。
 * 03000768 起是本模块工作区；技能和物品列表由规则/道具函数填入传参缓冲。
 * 自动目标和敌人决策还会调用 battle_ai，二者不是同一个 UI 层。
 * 职责、运行覆盖和合并结论见 docs/BATTLE-MODULES.md（2026-11-11）。
 * 输入调度匹配记录见 docs/BATTLE-COMMAND-INPUT.md。
 */

/**
 * @brief 清空十条命令、技能和物品选择历史，并复位上次命令。
 * @note 由 System_Init 调用；逐字段写零，u8 索引保留截断；普通 C 匹配 0x54 字节（含字面池）。
 * @address 0x08021130
 */
void BattleMenu_ResetHistory(void)
{
    u8 index;

    for (index = 0; index < BATTLE_COMMAND_HISTORY_COUNT; index++)
    {
        sBattleCommandHistory[index].command = 0;
        sBattleCommandHistory[index].skillScroll = 0;
        sBattleCommandHistory[index].skillIndex = 0;
        sBattleCommandHistory[index].itemScroll = 0;
        sBattleCommandHistory[index].itemIndex = 0;
    }

    sBattleLastCommand = 0;
}

/**
 * @brief 按菜单模式恢复角色的命令、技能或物品选择
 * @note 模式 0 恢复上次命令；模式 3 恢复角色命令并按需置刷新位；模式 6/7 恢复列表位置。
 * @note 历史索引按 s8 比较，只检查上界；越界时选择最后一项并滚动到末页。普通 C 匹配 0x130 字节。
 * @address 0x08021184
 */
void BattleMenu_RestoreSelection(s8 mode, const struct BattleCombatant *combatant)
{
    u8 index;

    if (combatant->id != 0)
        index = combatant->id - 1;
    else
        index = combatant->id;

    switch (mode)
    {
    case 0:
        sBattleMenuCommandIndex = sBattleLastCommand;
        break;
    case 3:
        if (sBattleMenuCommandIndex != sBattleCommandHistory[index].command)
            sBattleMenuFlags |= BATTLE_MENU_REFRESH_SELECTION;
        sBattleMenuCommandIndex = sBattleCommandHistory[index].command;
        break;
    case 6:
        if (sBattleCommandHistory[index].skillIndex < sBattleSkillChoiceCount)
        {
            sBattleSkillScroll = sBattleCommandHistory[index].skillScroll;
            sBattleSkillIndex = sBattleCommandHistory[index].skillIndex;
        }
        else
        {
            sBattleSkillScroll = sBattleSkillChoiceCount - 1 > 1
                ? sBattleSkillChoiceCount - 3 : 0;
            sBattleSkillIndex = sBattleSkillChoiceCount - 1;
        }
        break;
    case 7:
        if (sBattleCommandHistory[index].itemIndex < sBattleMenuItemCount)
        {
            sBattleMenuItemScroll = sBattleCommandHistory[index].itemScroll;
            sBattleMenuItemIndex = sBattleCommandHistory[index].itemIndex;
        }
        else
        {
            sBattleMenuItemScroll = sBattleMenuItemCount - 1 > 1
                ? sBattleMenuItemCount - 3 : 0;
            sBattleMenuItemIndex = sBattleMenuItemCount - 1;
        }
        break;
    }
}

/**
 * @brief 移动命令或列表光标，并保存根菜单或角色选择历史。
 * @note 操作 0/1/2 为下一项/上一项/保存；列表滚动或边界拒绝返回 0，边界播放音效 3。
 * @note 比较前设置字节索引指针，共用递增与重绘尾部；保留技能 s8、物品 u8 比较，普通 C 匹配 0x268 字节。
 * @address 0x080212B4
 */
u8 BattleMenu_MoveSelection(s8 state, const union BattleCommandMenu *menu,
    const struct BattleCombatant *combatant, u8 operation)
{
    u8 samePage = 1;
    u8 historyIndex;
    u8 *selection;

    if (combatant->id != 0)
        historyIndex = combatant->id - 1;
    else
        historyIndex = combatant->id;

    switch (state)
    {
    case BATTLE_MENU_ROOT:
        switch (operation)
        {
        case BATTLE_MENU_SELECTION_NEXT:
            sBattleMenuCommandIndex = (sBattleMenuCommandIndex + 1) % menu->fields.count;
            break;
        case BATTLE_MENU_SELECTION_PREVIOUS:
            if (sBattleMenuCommandIndex == 0)
                sBattleMenuCommandIndex = menu->fields.count - 1;
            else
                sBattleMenuCommandIndex--;
            break;
        case BATTLE_MENU_SELECTION_SAVE:
            break;
        }
        sBattleLastCommand = sBattleMenuCommandIndex;
        break;
    case BATTLE_MENU_MANUAL:
        switch (operation)
        {
        case BATTLE_MENU_SELECTION_NEXT:
            sBattleMenuCommandIndex = (sBattleMenuCommandIndex + 1) % menu->fields.count;
            break;
        case BATTLE_MENU_SELECTION_PREVIOUS:
            if (sBattleMenuCommandIndex == 0)
                sBattleMenuCommandIndex = menu->fields.count - 1;
            else
                sBattleMenuCommandIndex--;
            break;
        case BATTLE_MENU_SELECTION_SAVE:
            sBattleCommandHistory[historyIndex].command = sBattleMenuCommandIndex;
            break;
        }
        break;
    case BATTLE_MENU_SKILL:
        switch (operation)
        {
        case BATTLE_MENU_SELECTION_NEXT:
            selection = (u8 *)&sBattleSkillIndex;
            if (sBattleSkillIndex - sBattleSkillScroll <= 1)
            {
                if (sBattleSkillIndex < sBattleSkillChoiceCount - 1)
                    sBattleSkillIndex++;
                else
                    goto rejectMove;
            }
            else if (sBattleSkillIndex < sBattleSkillChoiceCount - 1)
            {
                sBattleSkillScroll++;
advanceIndex:
                (*selection)++;
                goto pageChanged;
            }
            else
                goto rejectMove;
            break;
        case BATTLE_MENU_SELECTION_PREVIOUS:
            if (sBattleSkillIndex - sBattleSkillScroll > 0)
                sBattleSkillIndex--;
            else if (sBattleSkillScroll > 0)
            {
                sBattleSkillScroll--;
                sBattleSkillIndex--;
                goto pageChanged;
            }
            else
                goto rejectMove;
            break;
        case BATTLE_MENU_SELECTION_SAVE:
            sBattleCommandHistory[historyIndex].skillScroll = sBattleSkillScroll;
            sBattleCommandHistory[historyIndex].skillIndex = sBattleSkillIndex;
            break;
        }
        break;
    case BATTLE_MENU_ITEM:
        switch (operation)
        {
        case BATTLE_MENU_SELECTION_NEXT:
            selection = &sBattleMenuItemIndex;
            if (sBattleMenuItemIndex - sBattleMenuItemScroll <= 1)
            {
                if (sBattleMenuItemIndex < sBattleMenuItemCount - 1)
                    sBattleMenuItemIndex++;
                else
                    goto rejectMove;
            }
            else if (sBattleMenuItemIndex < sBattleMenuItemCount - 1)
            {
                sBattleMenuItemScroll++;
                goto advanceIndex;
            }
            else
                goto rejectMove;
            break;
        case BATTLE_MENU_SELECTION_PREVIOUS:
            if (sBattleMenuItemIndex - sBattleMenuItemScroll > 0)
                sBattleMenuItemIndex--;
            else if (sBattleMenuItemScroll > 0)
            {
                sBattleMenuItemScroll--;
                sBattleMenuItemIndex--;
                goto pageChanged;
            }
            else
            {
rejectMove:
                Sound_PlaySfx(3, 0, 0);
pageChanged:
                samePage = 0;
            }
            break;
        case BATTLE_MENU_SELECTION_SAVE:
            sBattleCommandHistory[historyIndex].itemScroll = sBattleMenuItemScroll;
            sBattleCommandHistory[historyIndex].itemIndex = sBattleMenuItemIndex;
            break;
        }
        break;
    }
    return samePage;
}

/** @brief Begin manual input or finish immediately with automatic commands.
 *  @note Character 9 and incapacitated members are excluded from manual eligibility.
 *  @address 0x0802151C
 */
u8 BattleMenu_BeginInput(u8 mode, struct BattleCombatant *cursor)
{
    u8 slots[BATTLE_PARTY_COUNT];
    struct BattleCombatant *combatants = Battle_GetCombatants();
    u8 count = Battle_ListCombatants(combatants, slots, 0, 0x7F);
    u8 blocked = 0;
    u16 index;

    for (index = 0; index < count; index++)
    {
        if (combatants[slots[index]].id != 9
            && (u8)Battle_CheckStatusMask(&combatants[slots[index]], 0x3C) == BATTLE_STATUS_MATCH_ACCEPTED)
            blocked++;
    }
    for (index = 0; index < count; index++)
    {
        if (combatants[slots[index]].id == 9)
        {
            count--;
            break;
        }
    }
    if (blocked < count)
    {
        if (!(Battle_GetControlFlags() & BATTLE_CONTROL_AUTO))
        {
            sBattleMenuState = mode;
            BattleMenu_RestoreSelection(sBattleMenuState, NULL);
            sBattleMenuPartyIndex = 0;
            sBattleMenuFlags = BATTLE_MENU_REFRESH_SELECTION;
            sBattleMenuPhase = 0;
            sBattleMenuAnimationFrame = 0;
            Battle_SetCursorAnimation(cursor, 2);
            Battle_InitWindowTilemap();
            Battle_OpenWindow(0, gBattleWindowTilemap, 11, 2, 2, 0, 0, 30, 5);
            sBattleMenuHintFlags = 0;
            BattleMenu_SetHintRequest(BATTLE_MENU_HINT_ENABLE);
            BattlePalette_LoadObjBanks(gBattleMenuCommandPalettes, 13, 2);
            BattleMenu_ResetDirectionalSelection();
            BattleItem_ResetReservations();
            Battle_SetControlFlags(BATTLE_CONTROL_SHOW_STATUS_ICONS);
            sBattleCommandResetCount = sBattleCommandResetIndex = 0;
            for (index = 0; index < count; index++)
            {
                if (combatants[slots[index]].action == BATTLE_ACTION_DEFEND)
                {
                    sBattleCommandResetSlots[sBattleCommandResetCount] = slots[index];
                    combatants[slots[index]].action = BATTLE_ACTION_ATTACK;
                    sBattleCommandResetCount++;
                }
            }
            return 0;
        }
        else
            BattleAi_BuildPartyCommands();
    }
    else
    {
        BattleAi_BuildConfusedPartyCommands(combatants);
        if (Battle_GetControlFlags() & BATTLE_CONTROL_AUTO)
            BattleAi_BuildPartyCommands();
    }
    return 1;
}

/** @brief Restore former defend commands to idle, one combatant at a time.
 *  @note Advance only after the animation's graphics-loading flag clears.
 *  @address 0x08021700
 */
u8 BattleMenu_UpdateCommandReset(void)
{
    u8 done = 0;

    if (sBattleCommandResetIndex < sBattleCommandResetCount)
    {
        struct BattleCombatant *combatant = Battle_GetCombatants()
            + sBattleCommandResetSlots[sBattleCommandResetIndex];

        switch (sBattleCommandResetPhase)
        {
        case BATTLE_MENU_RESET_START:
            Battle_RestoreIdleAnimation(combatant, combatant->homeX, combatant->homeY,
                combatant->animation.baseTile, combatant->animation.paletteSlot);
            sBattleCommandResetPhase = BATTLE_MENU_RESET_WAIT_ANIMATION;
            break;
        case BATTLE_MENU_RESET_WAIT_ANIMATION:
            if (!(combatant->animation.flags & BATTLE_ANIM_LOADING))
            {
                sBattleCommandResetIndex++;
                sBattleCommandResetPhase = BATTLE_MENU_RESET_START;
            }
            break;
        }
    }
    else
        done = 1;

    return done;
}

/** @brief 处理命令提示请求，按根窗口状态刷新或停用提示。
 *  @note 窗口关闭时清除启用位，命令完成后停止绘制；普通 C 匹配 0x94 字节。
 *  @note 启用与跳过分支分别清零请求并 break，保留原寄存器分配。
 *  @address 0x08021788
 */
void BattleMenu_UpdateCommandHint(u8 option)
{
    u8 *request = &sBattleMenuHintRequest;

    switch (*request)
    {
    case BATTLE_MENU_HINT_UPDATE:
        if (sBattleMenuHintFlags & 0x1000)
        {
            if (Battle_GetWindowState(0) == 0)
                sBattleMenuHintFlags &= ~0x1000;
            else if (Battle_GetWindowState(0) == BATTLE_WINDOW_OPEN
                && !(sBattleMenuFlags & BATTLE_MENU_COMMANDS_READY))
                BattleMenu_DrawCommandHint(gBattleWindowTilemap, 24, 2, option);
        }
        break;
    case BATTLE_MENU_HINT_ENABLE:
        sBattleMenuHintFlags |= 0x1000;
        *request = BATTLE_MENU_HINT_UPDATE;
        break;
    case BATTLE_MENU_HINT_SKIP:
        *request = BATTLE_MENU_HINT_UPDATE;
        break;
    }
}

/** @brief 清空四列提示区并绘制战斗命令标签。
 *  @note 编号 1..9 有效；文本按五字节步长索引，扫描上限 16 字节，0xFF 结束。
 *  @note s16 编号转 u16 比较，清空后初始化文本指针，并保留原 4 字节栈空间；普通 C 匹配 0x110 字节（含字面池）。
 *  @address 0x0802181C
 */
void BattleMenu_DrawCommandHint(u16 *tilemap, u8 x, u8 y, u8 option)
{
    u16 stackPadding[2];
    s16 optionIndex = option - 1;
    u16 *tiles = tilemap + (y * SCRIPT_TILEMAP_WIDTH + 24);
    u8 index = 0;
    u8 length;
    const u8 *text;

    for (; index < 4; index++)
    {
        tiles[index] = 0xB001;
        tiles[index + SCRIPT_TILEMAP_WIDTH] = 0xB001;
    }
    text = gBattleMenuCommandHints[option - 1];
    for (index = 0; index < 16 && text[index] < 0xFF; index++)
        ;
    length = index;
    tiles = tilemap + (y * SCRIPT_TILEMAP_WIDTH + x);
    if ((u16)optionIndex <= 8)
    {
        for (index = 0; index < length; index++)
        {
            if ((u8)(text[index] - 1) <= 0xFD)
            {
                tiles[index] = (s16)0xB000 + text[index] * 2;
                tiles[index + SCRIPT_TILEMAP_WIDTH] = (s16)0xB001 + text[index] * 2;
            }
            else
            {
                tiles[index] = 0xB001;
                tiles[index + SCRIPT_TILEMAP_WIDTH] = 0xB001;
            }
        }
    }
    else
    {
        tiles = tilemap + (y * SCRIPT_TILEMAP_WIDTH + 24);
        for (index = 0; index < 4; index++)
        {
            tiles[index] = 0xB001;
            tiles[index + SCRIPT_TILEMAP_WIDTH] = 0xB001;
        }
    }
}

/**
 * @brief 调度自动、作战预设和手动命令输入
 * @note 根据 03000768 处理命令状态；手动路径继续进入技能、道具及目标选择函数。
 * @address 0x0802192C
 */
u8 BattleMenu_UpdateInput(struct BattleCombatant *combatants, struct BattleCombatant *cursor, u8 *oamCursor)
{
    struct BattleCombatant *party[BATTLE_PARTY_COUNT];
    const union BattleCommandMenu *menu;
    u16 input;
    u16 tile;
    u8 done = 0;
    u8 sprite = *oamCursor;
    u8 count = 0;
    u8 i;
    s32 state;

    /* Keep the clear, eligibility test, pointer store and u8 increment separate. */
    for (i = 0; i < BATTLE_PARTY_COUNT; i++)
    {
        struct BattleCombatant *combatant;
        party[i] = NULL;
        combatant = &combatants[i];
        if ((u8)Battle_CheckStatusMask(combatant, 0x1BC) == BATTLE_STATUS_MATCH_REJECTED
            && combatant->id != 9)
        {
            party[count] = combatant;
            count++;
        }
    }

    switch (sBattleMenuState)
    {
    case 3:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
        if (sBattleMenuFlags & 0x800)
        {
            for (i = 0; i < count; i++)
                Battle_UpdateStatusPaletteOffset(party[i], 0);
        }
        else
        {
            for (i = 0; i < count; i++)
            {
                if (sBattleMenuPartyIndex == i)
                    Battle_UpdateStatusPaletteOffset(party[i], 0);
                else
                    Battle_UpdateStatusPaletteOffset(party[i], 4);
            }
        }
        break;
    default:
        for (i = 0; i < count; i++)
            Battle_UpdateStatusPaletteOffset(party[i], 0);
        break;
    }

    tile = 600;
    input = Battle_GetInput();
    state = sBattleMenuState;
    menu = &gBattleMenuDescriptors[state];
    if ((input & 0xF000) == 0x2000)
    {
        if (state == 0)
            Sound_PlaySfx(2, 0, 0);
        if (menu->fields.parent >= 0 && !(input & 0xF00) && !(sBattleMenuFlags & 0x100))
            goto cancel;
    }
    if (sBattleMenuFlags & 0x200)
    {
    cancel:
        if ((sBattleMenuFlags & 0xF8) == 8)
        {
            if (sBattleMenuPartyIndex > 0)
            {
                sBattleMenuFlags |= 0x10;
                sBattleMenuPartyIndex--;
                party[sBattleMenuPartyIndex]->animation.priority = 0;
                BattleMenu_RestoreSelection(sBattleMenuState, party[sBattleMenuPartyIndex]);
            }
            else
            {
                sBattleMenuFlags |= 0x30;
                sBattleMenuFlags &= ~8;
                BattleMenu_RestoreSelection(sBattleMenuState, party[sBattleMenuPartyIndex]);
            }
            if (party[sBattleMenuPartyIndex]->action == BATTLE_ACTION_DEFEND)
            {
                party[sBattleMenuPartyIndex]->action = BATTLE_ACTION_ATTACK;
                Battle_SetPartyAnimation(party[sBattleMenuPartyIndex], 0,
                    party[sBattleMenuPartyIndex]->animation.baseTile,
                    party[sBattleMenuPartyIndex]->animation.paletteSlot, 0);
            }
            BattleItem_CancelReservation(sBattleMenuPartyIndex);
        }
        if (sBattleMenuState == 5)
            BattleMenu_SetHintRequestInline(1);
        if (sBattleMenuFlags & 0x200)
            sBattleMenuFlags &= 0xFDFF;
        if (sBattleMenuState == 3)
            Sound_PlaySfx(2, 0, 0);
        sBattleMenuFlags |= 1;
    }

    if ((sBattleMenuFlags & 1) && !(sBattleMenuFlags & 0xF8))
    {
        Battle_SetTilemapTiles(gBattleWindowTilemap, 1, 10, 2, 9, 2);
        sBattleMenuPhase = 0;
        sBattleMenuState = menu->fields.parent;
        sBattleMenuFlags &= ~1;
        sBattleMenuFlags |= 2;
        /* A one-case switch preserves the signed reload and shared return path. */
        switch (sBattleMenuState)
        {
        case 3:
            sBattleMenuFlags |= 8;
            Battle_StopCombatantHighlight(&combatants[party[sBattleMenuPartyIndex]->targetSlot]);
            break;
        }
        BattleMenu_ResetDirectionalSelection();
        BattleMenu_RestoreSelection(sBattleMenuState, party[sBattleMenuPartyIndex]);
        if (menu->fields.count > 0)
        {
            BattleMenu_RestoreSelection(sBattleMenuState, party[sBattleMenuPartyIndex]);
            return done;
        }
    }

    switch (sBattleMenuState)
    {
    case 1:
        BattleAi_ClearTargetMemory();
        BattleAi_BuildPartyCommands();
        Battle_SetControlFlags(BATTLE_CONTROL_AUTO);
        BattleMenu_SetHintRequestInline(2);
        sBattleMenuState = 0;
        sBattleMenuFlags |= 4;
        break;
    case 2:
        BattleAi_ClearTargetMemory();
        Battle_RestorePartyTacticsCommands();
        BattleMenu_SetHintRequestInline(2);
        sBattleMenuState = 0;
        sBattleMenuFlags |= 4;
        break;
    case 3:
        BattleMenu_UpdatePartyCommandPhase(party);
        break;
    case 4:
        Battle_SetPartyEscapeCommands();
        Battle_SetControlFlags(0x2000);
        BattleMenu_SetHintRequestInline(2);
        sBattleMenuState = 0;
        sBattleMenuFlags |= 4;
        break;
    case 5:
        if (!(sBattleMenuFlags & 4))
        {
            if (BattleMenu_UpdateAttackTarget(party[sBattleMenuPartyIndex], combatants, cursor, input) == 1)
            {
                if (sBattleMenuPartyIndex < count - 1)
                {
                    sBattleMenuPartyIndex++;
                    sBattleMenuState = menu->fields.parent;
                    BattleMenu_RestoreSelection(sBattleMenuState, party[sBattleMenuPartyIndex]);
                    sBattleMenuPhase = 3;
                    sBattleMenuFlags |= 0xA;
                    BattleMenu_SetHintRequestInline(1);
                }
                else
                    sBattleMenuFlags |= 0x44;
            }
        }
        break;
    case 6:
        if (!(sBattleMenuFlags & 4))
        {
            if (BattleMenu_UpdateSkillSelection(party[sBattleMenuPartyIndex], combatants, cursor, input) == 1)
            {
                if (sBattleMenuPartyIndex < count - 1)
                {
                    sBattleMenuPartyIndex++;
                    sBattleMenuState = menu->fields.parent;
                    BattleMenu_RestoreSelection(sBattleMenuState, party[sBattleMenuPartyIndex]);
                    sBattleMenuPhase = 3;
                    sBattleMenuFlags |= 0x40A;
                    BattleMenu_SetHintRequestInline(1);
                }
                else
                    sBattleMenuFlags |= 0x44;
            }
        }
        break;
    case 7:
        if (!(sBattleMenuFlags & 4))
        {
            if (BattleMenu_UpdateItemSelection(party[sBattleMenuPartyIndex], combatants, cursor, input) == 1)
            {
                if (sBattleMenuPartyIndex < count - 1)
                {
                    sBattleMenuPartyIndex++;
                    sBattleMenuState = menu->fields.parent;
                    BattleMenu_RestoreSelection(sBattleMenuState, party[sBattleMenuPartyIndex]);
                    sBattleMenuPhase = 3;
                    sBattleMenuFlags |= 0x40A;
                    BattleMenu_SetHintRequestInline(1);
                }
                else
                    sBattleMenuFlags |= 0x44;
            }
        }
        break;
    case 8:
        party[sBattleMenuPartyIndex]->action = BATTLE_ACTION_DEFEND;
        Battle_SetPartyAnimation(party[sBattleMenuPartyIndex], 7,
            party[sBattleMenuPartyIndex]->animation.baseTile,
            party[sBattleMenuPartyIndex]->animation.paletteSlot, 0);
        if (sBattleMenuPartyIndex < count - 1)
        {
            sBattleMenuPartyIndex++;
            sBattleMenuState = menu->fields.parent;
            BattleMenu_RestoreSelection(sBattleMenuState, party[sBattleMenuPartyIndex]);
            sBattleMenuPhase = 3;
            sBattleMenuFlags |= 0x40A;
        }
        else
        {
            sBattleMenuFlags |= 0x44;
            sBattleMenuState = 0;
            BattleMenu_SetHintRequestInline(2);
        }
        break;
    case 9:
        party[sBattleMenuPartyIndex]->action = 4;
        if (sBattleMenuPartyIndex < count - 1)
        {
            sBattleMenuPartyIndex++;
            sBattleMenuState = menu->fields.parent;
            BattleMenu_RestoreSelection(sBattleMenuState, party[sBattleMenuPartyIndex]);
            sBattleMenuPhase = 3;
            sBattleMenuFlags |= 0x40A;
        }
        else
        {
            sBattleMenuFlags |= 0x44;
            sBattleMenuState = 0;
            BattleMenu_SetHintRequestInline(2);
        }
        break;
    default:
        if (sBattleMenuPhase == 0)
            sBattleMenuPhase = 30;
        break;
    }

    /* One signed byte plus one aligned word covers all five command options. */
    if (menu->fields.options[0] != -1 || menu->words[1] != -1)
    {
        if (input & 0x400)
        {
            Sound_PlaySfx(0, 0, 0);
            BattleMenu_MoveSelection(sBattleMenuState, menu, party[sBattleMenuPartyIndex], 1);
            sBattleMenuFlags |= 2;
        }
        if (input & 0x800)
        {
            Sound_PlaySfx(0, 0, 0);
            BattleMenu_MoveSelection(sBattleMenuState, menu, party[sBattleMenuPartyIndex], 0);
            sBattleMenuFlags |= 2;
        }
        if (Battle_GetPressedKeys() == 1 && menu->fields.options[sBattleMenuCommandIndex] >= 0
            && !(sBattleMenuFlags & 0xF0) && !(input & 0xC))
        {
            /* The first aligned skill read includes +98, which is masked out;
             * together +98/+9C word reads and +A0 byte cover skills[0..7]. */
            if ((menu->fields.options[sBattleMenuCommandIndex] == 6
                    && party[sBattleMenuPartyIndex]->status != BATTLE_STATUS_SILENCED
                    && ((*(const u32 *)((const u8 *)party[sBattleMenuPartyIndex]
                            + offsetof(struct BattleCombatant, skills) - 1) & 0xFFFFFF00) != 0xFFFFFF00
                        || *(const u32 *)((const u8 *)party[sBattleMenuPartyIndex]
                            + offsetof(struct BattleCombatant, skills) + 3) != 0xFFFFFFFF
                        || party[sBattleMenuPartyIndex]->skills[7] != BATTLE_NO_SKILL))
                || (menu->fields.options[sBattleMenuCommandIndex] == 7
                    && (sBattleMenuItemCount = BattleItem_BuildMenuList(sBattleMenuItems)) != 0)
                || (menu->fields.options[sBattleMenuCommandIndex] == 9 && !(Battle_GetControlFlags() & 0x220))
                || (menu->fields.options[sBattleMenuCommandIndex] == 4 && !(Battle_GetControlFlags() & 0x220))
                || (menu->fields.options[sBattleMenuCommandIndex] != 6 && menu->fields.options[sBattleMenuCommandIndex] != 7
                    && menu->fields.options[sBattleMenuCommandIndex] != 9 && menu->fields.options[sBattleMenuCommandIndex] != 4))
            {
                Sound_PlaySfx(1, 0, 0);
                BattleMenu_MoveSelection(sBattleMenuState, menu, party[sBattleMenuPartyIndex], 2);
                sBattleMenuPhase = 0;
                sBattleMenuFlags = 0;
                sBattleMenuState = menu->fields.options[sBattleMenuCommandIndex];
                menu = &gBattleMenuDescriptors[sBattleMenuState];
                switch (sBattleMenuState)
                {
                case 3:
                    sBattleMenuFlags |= 8;
                    sBattleMenuPartyIndex = 0;
                    sBattleMenuPhase = 3;
                    break;
                case 5:
                case 6:
                case 7:
                    BattleMenu_SetHintRequestInline(2);
                    break;
                }
                BattleMenu_ResetDirectionalSelection();
                BattleMenu_RestoreSelection(sBattleMenuState, party[sBattleMenuPartyIndex]);
                sBattleMenuFlags |= 2;
                if (menu->fields.count > 0)
                {
                    BattleMenu_RestoreSelection(sBattleMenuState, party[sBattleMenuPartyIndex]);
                    return done;
                }
            }
            else
                Sound_PlaySfx(2, 0, 0);
        }
    }

    BattleMenu_UpdateCommandHint(menu->fields.options[sBattleMenuCommandIndex]);
    if (menu->fields.count < 0)
        menu = &gBattleMenuDescriptors[menu->fields.parent];
    if (menu->fields.parent == -1)
        sBattleMenuCommandSpacing = 32;
    else
        sBattleMenuCommandSpacing = 24;
    for (i = 0; i < menu->fields.count; i++)
    {
        tile = BattleMenu_DrawCommandIcon(&gSpriteOamBuffer[sprite], sBattleMenuCommandSpacing * i + menu->fields.x,
            15, tile, 0, 1, i != sBattleMenuCommandIndex && menu->fields.options[i] >= 0);
        sprite--;
    }
    *oamCursor = sprite;
    BattleMenu_UpdatePartyTransition(party, input);
    if (sBattleMenuFlags & 2)
    {
        if (menu->fields.count < 0)
            menu = &gBattleMenuDescriptors[menu->fields.parent];
        else if (sBattleMenuFlags & 0x400)
        {
            menu = &gBattleMenuDescriptors[menu->fields.parent];
            sBattleMenuFlags &= 0xFBFF;
        }
        Battle_MoveCursor(cursor, sBattleMenuCommandSpacing * sBattleMenuCommandIndex + menu->fields.x + 24, 28, 5, 2);
        sBattleMenuFlags &= 0xFFFD;
    }
    if ((sBattleMenuFlags & 0xF4) == 4)
    {
        BattleItem_CommitReservations();
        Battle_ReturnCursorToRest(cursor, 3, 2);
        BattlePalette_ReleaseObjBanks(13, 2);
        sBattleMenuState = -1;
        done = 1;
        for (i = 0; i < count; i++)
            Battle_UpdateStatusPaletteOffset(party[i], 0);
        BattleAi_BuildConfusedPartyCommands(combatants);
        BattleAi_ChooseCharacter9Command();
        Battle_ClearControlFlags(0x100);
        sBattleMenuFlags = 0;
    }
    return done;
}

/** @brief 通过 DMA3 上传当前菜单的命令图标到 OBJ VRAM。
 *  @note 负状态提前返回，负计数改用父菜单；从图块 600 起排列，每个图标消耗一个 OAM 槽。
 *  @note 目标图块号转 u16，保留官方 DmaCopy/DmaWait 及 u8 截断；普通 C 匹配 0xF8 字节（含字面池）。
 *  @address 0x08022458
 */
u8 BattleMenu_UploadCommandIcons(u8 oamCursor)
{
    const union BattleCommandMenu *menu;
    u8 tileOffset;
    u8 index;

    if (sBattleMenuState < 0)
        return oamCursor;

    menu = &gBattleMenuDescriptors[sBattleMenuState];
    if (menu->fields.count < 0)
        menu = &gBattleMenuDescriptors[menu->fields.parent];
    tileOffset = 0;
    for (index = 0; index < menu->fields.count; index++)
    {
        u8 icon = menu->fields.graphics[index];

        DmaCopy(3, D_08619AA4 + gBattleMenuCommandIcons[icon].sourceTile * 32,
            OBJ_GRAPHICS[(u16)(600 + tileOffset)],
            gBattleObjTileCounts[gBattleMenuCommandIcons[icon].shape * 4
                + gBattleMenuCommandIcons[icon].size] * 32, 16);
        DmaWait(3);
        tileOffset += gBattleObjTileCounts[gBattleMenuCommandIcons[icon].shape * 4
            + gBattleMenuCommandIcons[icon].size];
        oamCursor--;
    }
    return oamCursor;
}

/** @brief 收集同一阵形行或列的战斗单位，并写入三个有序槽位。
 *  @note 保留 formation <=3 筛选、<=2 排序的不同阈值和 u8 截断；空槽为 -1。
 *  @note 六个 case 分别写入槽位，保留原寄存器分配和 48 项跳转表；普通 C 匹配 0x1C0 字节。
 *  @address 0x08022550
 */
void BattleMenu_BuildFormationLine(const struct BattleCombatant *combatants,
    u8 formation, u8 side, s8 slots[BATTLE_MENU_FORMATION_LINE_SIZE])
{
    s8 candidates[BATTLE_MENU_FORMATION_LINE_SIZE];
    u8 index;
    u8 count;
    u8 mask;

    for (index = 0; index < BATTLE_MENU_FORMATION_LINE_SIZE; index++)
        slots[index] = -1;
    count = 0;
    mask = 0xF0;
    if (formation <= 3)
        mask = 0x0F;
    switch (side)
    {
    case 0:
        for (index = 0; index < BATTLE_PARTY_COUNT; index++)
        {
            if ((combatants[index].formationBits & mask) == formation
                && combatants[index].id != 0xFF)
            {
                candidates[count] = index;
                count++;
            }
        }
        break;
    case 1:
        for (index = BATTLE_PARTY_COUNT; index < BATTLE_COMBATANT_COUNT; index++)
        {
            if ((combatants[index].formationBits & mask) == formation
                && combatants[index].id != 0xFF)
            {
                candidates[count] = index;
                count++;
            }
        }
        break;
    }
    mask = 0x0F;
    if (formation <= 2)
        mask = 0xF0;
    for (index = 0; index < count; index++)
    {
        switch (combatants[candidates[index]].formationBits & mask)
        {
        case 0x01:
            slots[0] = candidates[index];
            break;
        case 0x10:
            slots[0] = candidates[index];
            break;
        case 0x02:
            slots[1] = candidates[index];
            break;
        case 0x20:
            slots[1] = candidates[index];
            break;
        case 0x03:
            slots[2] = candidates[index];
            break;
        case 0x30:
            slots[2] = candidates[index];
            break;
        }
    }
}

/** @brief Find the next party or enemy target in the requested direction.
 *  @note current must be present in the registered combatant list with status 0..8.
 *  Enemy moves prefer the current perpendicular coordinate, then its remembered
 *  value, then the first candidate. Keep the original wrap rules and u8 indices.
 *  @address 0x08022710
 */
u8 BattleMenu_FindDirectionalTarget(const struct BattleCombatant *current,
    const struct BattleCombatant *combatants, u8 direction)
{
    struct BattleCombatant *all = Battle_GetCombatants();
    /* The reference leaves eight bytes below slots unaccessed. Their original
     * type/use is unknown; ordinary automatic storage preserves that frame. */
    u8 reservedStack[8];
    u8 slots[BATTLE_ENEMY_COUNT];
    u8 count;
    u8 index;
    u8 selected;
    u8 start;
    u8 wrapped;
    u8 formation;

    if (current->id < BATTLE_CHARACTER_ID_COUNT)
    {
        count = Battle_ListCombatants(all, slots, BATTLE_SIDE_PARTY, BATTLE_STATUS_MASK_ALL);
        for (index = 0; index < count; index++)
        {
            if (all[slots[index]].id == current->id)
            {
                selected = index;
                break;
            }
        }
        switch (direction)
        {
        case BATTLE_MENU_UP:
            break;
        case BATTLE_MENU_DOWN:
            break;
        case BATTLE_MENU_LEFT:
            if (selected != 0)
                selected--;
            else
                selected = count - 1;
            break;
        case BATTLE_MENU_RIGHT:
            selected = (selected + 1) % count;
            break;
        }
        selected = slots[selected];
    }
    else
    {
        for (index = BATTLE_PARTY_COUNT; index < BATTLE_COMBATANT_COUNT; index++)
        {
            if (combatants[index].formationBits == current->formationBits)
            {
                selected = index;
                break;
            }
        }
        /* Separate cases retain the original branches and shared exit blocks. */
        switch (direction)
        {
        case BATTLE_MENU_UP:
            count = Battle_ListCombatants(all, slots, BATTLE_SIDE_ENEMY, BATTLE_STATUS_MASK_ALL);
            if (count > 1)
            {
                Battle_SortCombatantSlots(slots, count, BATTLE_SORT_FORMATION_ROW);
                for (index = 0; index < count; index++)
                {
                    if (all[slots[index]].formationBits == current->formationBits)
                    {
                        start = index;
                        break;
                    }
                }
                wrapped = 0;
                formation = current->formationBits & BATTLE_FORMATION_ROW_MASK;
                while (formation == (all[slots[index]].formationBits & BATTLE_FORMATION_ROW_MASK))
                {
                    if (index == 0)
                        index = count - 1;
                    else
                        index--;
                    if (start == index)
                    {
                        if (index == 0)
                            index = count - 1;
                        else
                            index--;
                        wrapped = 1;
                        break;
                    }
                }
                if (!wrapped)
                {
                    count = Battle_ListEnemiesInFormation(&all[slots[index]], slots, BATTLE_FORMATION_ROW_MASK);
                    formation = current->formationBits & BATTLE_FORMATION_COLUMN_MASK;
                    for (index = 0; index < count; index++)
                        if (formation == (all[slots[index]].formationBits & BATTLE_FORMATION_COLUMN_MASK))
                            break;
                    if (index >= count)
                    {
                        if (sBattleTargetPreviousColumn >= 0)
                        {
                            for (index = 0; index < count; index++)
                                if ((all[slots[index]].formationBits & BATTLE_FORMATION_COLUMN_MASK) == sBattleTargetPreviousColumn)
                                    break;
                            if (index >= count)
                                index = 0;
                        }
                        else
                            index = 0;
                    }
                }
                sBattleTargetPreviousColumn = current->formationBits & BATTLE_FORMATION_COLUMN_MASK;
                selected = slots[index];
            }
            else
            {
                sBattleTargetPreviousColumn = current->formationBits & BATTLE_FORMATION_COLUMN_MASK;
                selected = slots[0];
            }
            break;
        case BATTLE_MENU_DOWN:
            count = Battle_ListCombatants(all, slots, BATTLE_SIDE_ENEMY, BATTLE_STATUS_MASK_ALL);
            if (count > 1)
            {
                Battle_SortCombatantSlots(slots, count, BATTLE_SORT_FORMATION_ROW);
                for (index = 0; index < count; index++)
                {
                    if (all[slots[index]].formationBits == current->formationBits)
                    {
                        start = index;
                        break;
                    }
                }
                wrapped = 0;
                formation = current->formationBits & BATTLE_FORMATION_ROW_MASK;
                while (formation == (all[slots[index]].formationBits & BATTLE_FORMATION_ROW_MASK))
                {
                    index = (index + 1) % count;
                    if (start == index)
                    {
                        if (index == 0)
                            index = 1;
                        else
                            index = count - 1;
                        wrapped = 1;
                        break;
                    }
                }
                if (!wrapped)
                {
                    count = Battle_ListEnemiesInFormation(&all[slots[index]], slots, BATTLE_FORMATION_ROW_MASK);
                    formation = current->formationBits & BATTLE_FORMATION_COLUMN_MASK;
                    for (index = 0; index < count; index++)
                        if (formation == (all[slots[index]].formationBits & BATTLE_FORMATION_COLUMN_MASK))
                            break;
                    if (index >= count)
                    {
                        if (sBattleTargetPreviousColumn >= 0)
                        {
                            for (index = 0; index < count; index++)
                                if ((all[slots[index]].formationBits & BATTLE_FORMATION_COLUMN_MASK) == sBattleTargetPreviousColumn)
                                    break;
                            if (index >= count)
                                index = 0;
                        }
                        else
                            index = 0;
                    }
                }
                sBattleTargetPreviousColumn = current->formationBits & BATTLE_FORMATION_COLUMN_MASK;
                selected = slots[index];
            }
            else
            {
                sBattleTargetPreviousColumn = current->formationBits & BATTLE_FORMATION_COLUMN_MASK;
                selected = slots[0];
            }
            break;
        case BATTLE_MENU_LEFT:
            count = Battle_ListCombatants(all, slots, BATTLE_SIDE_ENEMY, BATTLE_STATUS_MASK_ALL);
            if (count > 1)
            {
                Battle_SortCombatantSlots(slots, count, BATTLE_SORT_FORMATION_COLUMN);
                for (index = 0; index < count; index++)
                {
                    if (all[slots[index]].formationBits == current->formationBits)
                    {
                        start = index;
                        break;
                    }
                }
                wrapped = 0;
                formation = current->formationBits & BATTLE_FORMATION_COLUMN_MASK;
                while (formation == (all[slots[index]].formationBits & BATTLE_FORMATION_COLUMN_MASK))
                {
                    if (index == 0)
                        index = count - 1;
                    else
                        index--;
                    if (start == index)
                    {
                        if (index == 0)
                            index = count - 1;
                        else
                            index--;
                        wrapped = 1;
                        break;
                    }
                }
                if (!wrapped)
                {
                    count = Battle_ListEnemiesInFormation(&all[slots[index]], slots, BATTLE_FORMATION_COLUMN_MASK);
                    formation = current->formationBits & BATTLE_FORMATION_ROW_MASK;
                    for (index = 0; index < count; index++)
                        if (formation == (all[slots[index]].formationBits & BATTLE_FORMATION_ROW_MASK))
                            break;
                    if (index >= count)
                    {
                        if (sBattleTargetPreviousRowBits >= 0)
                        {
                            for (index = 0; index < count; index++)
                                if ((all[slots[index]].formationBits & BATTLE_FORMATION_ROW_MASK) == sBattleTargetPreviousRowBits)
                                    break;
                            if (index >= count)
                                index = 0;
                        }
                        else
                            index = 0;
                    }
                }
                sBattleTargetPreviousRowBits = current->formationBits & BATTLE_FORMATION_ROW_MASK;
                selected = slots[index];
            }
            else
            {
                sBattleTargetPreviousRowBits = current->formationBits & BATTLE_FORMATION_ROW_MASK;
                selected = slots[0];
            }
            break;
        case BATTLE_MENU_RIGHT:
            count = Battle_ListCombatants(all, slots, BATTLE_SIDE_ENEMY, BATTLE_STATUS_MASK_ALL);
            if (count > 1)
            {
                Battle_SortCombatantSlots(slots, count, BATTLE_SORT_FORMATION_COLUMN);
                for (index = 0; index < count; index++)
                {
                    if (all[slots[index]].formationBits == current->formationBits)
                    {
                        start = index;
                        break;
                    }
                }
                wrapped = 0;
                formation = current->formationBits & BATTLE_FORMATION_COLUMN_MASK;
                while (formation == (all[slots[index]].formationBits & BATTLE_FORMATION_COLUMN_MASK))
                {
                    index = (index + 1) % count;
                    if (start == index)
                    {
                        if (index == 0)
                            index = 1;
                        else
                            index = count - 1;
                        wrapped = 1;
                        break;
                    }
                }
                if (!wrapped)
                {
                    count = Battle_ListEnemiesInFormation(&all[slots[index]], slots, BATTLE_FORMATION_COLUMN_MASK);
                    formation = current->formationBits & BATTLE_FORMATION_ROW_MASK;
                    for (index = 0; index < count; index++)
                        if (formation == (all[slots[index]].formationBits & BATTLE_FORMATION_ROW_MASK))
                            break;
                    if (index >= count)
                    {
                        if (sBattleTargetPreviousRowBits >= 0)
                        {
                            for (index = 0; index < count; index++)
                                if ((all[slots[index]].formationBits & BATTLE_FORMATION_ROW_MASK) == sBattleTargetPreviousRowBits)
                                    break;
                            if (index >= count)
                                index = 0;
                        }
                        else
                            index = 0;
                    }
                }
                sBattleTargetPreviousRowBits = current->formationBits & BATTLE_FORMATION_ROW_MASK;
                selected = slots[index];
            }
            else
            {
                sBattleTargetPreviousRowBits = current->formationBits & BATTLE_FORMATION_ROW_MASK;
                selected = slots[0];
            }
            break;
        }
    }
    return selected;
}

/**
 * @brief Draw the selected enemy's name in a nine-column, two-row tilemap area.
 * @note Party ids leave the area unchanged; id 11 only clears it. Normal enemy
 * names use bytes, while ids >= 113 use halfword glyphs and the script cache.
 * @address 0x08022F2C
 */
void BattleMenu_DrawTargetName(u16 *tilemap, const struct BattleCombatant *combatant, u8 x, u8 y)
{
    u16 *tiles = tilemap + (y * SCRIPT_TILEMAP_WIDTH + x);
    u16 i;
    u16 nameIndex;
    u16 index;
    u16 glyphCount;
    u16 *glyphIds;
    const u8 *name;
    const u16 *words;

    if (combatant->id > BATTLE_CHARACTER_ID_COUNT - 1)
    {
        /* Keep this initialization inside the enemy path: agbcc then shares
         * the original 0xB000 constant across clearing and both name formats. */
        u8 palette = 11;

        for (i = 0; i < SCRIPT_NAME_GLYPH_COUNT; i++)
        {
            tiles[i] = (palette << 12) + 1;
            tiles[i + SCRIPT_TILEMAP_WIDTH] = (palette << 12) + 1;
        }
        if ((u8)(combatant->id - 12) <= 100)
        {
            nameIndex = combatant->id - 12;
            name = gEnemyNames + nameIndex * SCRIPT_NAME_GLYPH_COUNT;
            for (i = 0; i < SCRIPT_NAME_GLYPH_COUNT; i++)
            {
                if (name[i] < 0xFF)
                {
                    if (name[i] != 0)
                    {
                        tiles[i] = (palette << 12) + name[i] * 2;
                        tiles[i + SCRIPT_TILEMAP_WIDTH] = (palette << 12) + (name[i] * 2 + 1);
                    }
                    else
                    {
                        tiles[i] = (palette << 12) + 1;
                        tiles[i + SCRIPT_TILEMAP_WIDTH] = (palette << 12) + 1;
                    }
                }
                else
                    break;
            }
        }
        else if (combatant->id > 112)
        {
            nameIndex = combatant->id - 113;
            words = D_0839D348 + nameIndex * SCRIPT_NAME_GLYPH_COUNT;
            Script_CacheGlyphsFromWords(words, SCRIPT_GLYPH_SCAN_NAME & ~SCRIPT_GLYPH_SCAN_KEEP_CACHE);
            glyphCount = Script_GetGlyphCache(&glyphIds);
            for (i = 0; i < SCRIPT_NAME_GLYPH_COUNT; i++)
            {
                if (words[i] < SCRIPT_TEXT_END)
                {
                    if (words[i] < SCRIPT_EXTENDED_GLYPH_BASE)
                    {
                        tiles[i] = (palette << 12) + (words[i] & 0xFF) * 2;
                        tiles[i + SCRIPT_TILEMAP_WIDTH] = (palette << 12) + ((words[i] & 0xFF) * 2 + 1);
                    }
                    else
                    {
                        for (index = 0; index < glyphCount; index++)
                        {
                            if (words[i] == glyphIds[index])
                                break;
                        }
                        /* Preserve the original count-based fallback on a miss. */
                        index += SCRIPT_EXTENDED_GLYPH_BASE;
                        tiles[i] = (palette << 12) + index * 2;
                        tiles[i + SCRIPT_TILEMAP_WIDTH] = (palette << 12) + (index * 2 + 1);
                    }
                }
                else
                    break;
            }
            if (glyphCount != 0)
                Battle_SetControlFlags(0x400);
        }
    }
}

/**
 * @brief Update normal-attack target selection, highlighting and cursor motion.
 * @note Entry requires at least one nonempty enemy slot. Cursor movement blocks
 * input; confirmation enters phase 30, and the following call commits the attack.
 * Keep the two confirmation tests separate: held and repeated directions both
 * suppress A, while Up intentionally plays its sound after changing the target.
 * @address 0x080230BC
 */
u8 BattleMenu_UpdateAttackTarget(struct BattleCombatant *combatant, struct BattleCombatant *combatants,
    struct BattleCombatant *cursor, u16 input)
{
    struct BattleCombatant *target;
    u8 done = 0;

    switch (sBattleMenuPhase)
    {
    case BATTLE_MENU_TARGET_INIT:
        combatant->targetSlot = BATTLE_PARTY_COUNT;
        while (combatants[combatant->targetSlot].id == BATTLE_NO_COMBATANT)
        {
            combatant->targetSlot++;
            if (combatant->targetSlot == BATTLE_COMBATANT_COUNT)
                combatant->targetSlot = BATTLE_PARTY_COUNT;
        }
        target = &combatants[combatant->targetSlot];
        Battle_StartCombatantHighlight(target, 16);
        Battle_MoveCursor(cursor,
            target->animation.x + (Battle_GetCombatantDimension(target, BATTLE_COMBATANT_WIDTH) >> 1),
            target->animation.y - (Battle_GetCombatantDimension(target, BATTLE_COMBATANT_HEIGHT) >> 1), 5, 2);
        BattleMenu_DrawTargetName(gBattleWindowTilemap, target, 10, 2);
        sBattleMenuPhase = BATTLE_MENU_TARGET_SELECT;
        sBattleTargetPreviousColumn = -1;
        sBattleTargetPreviousRowBits = -1;
        break;
    case BATTLE_MENU_TARGET_SELECT:
        if ((cursor->displayState & BATTLE_CURSOR_MOTION_MASK) == BATTLE_CURSOR_MOVING)
            break;
        target = &combatants[combatant->targetSlot];
        if (input & BATTLE_INPUT_REPEAT_UP)
        {
            combatant->targetSlot = BattleMenu_FindDirectionalTarget(target, combatants, BATTLE_MENU_UP);
            Sound_PlaySfx(0, 0, 0);
        }
        else if (input & BATTLE_INPUT_REPEAT_DOWN)
        {
            Sound_PlaySfx(0, 0, 0);
            combatant->targetSlot = BattleMenu_FindDirectionalTarget(target, combatants, BATTLE_MENU_DOWN);
        }
        else if (input & BATTLE_INPUT_REPEAT_LEFT)
        {
            Sound_PlaySfx(0, 0, 0);
            combatant->targetSlot = BattleMenu_FindDirectionalTarget(target, combatants, BATTLE_MENU_LEFT);
        }
        else if (input & BATTLE_INPUT_REPEAT_RIGHT)
        {
            Sound_PlaySfx(0, 0, 0);
            combatant->targetSlot = BattleMenu_FindDirectionalTarget(target, combatants, BATTLE_MENU_RIGHT);
        }
        if (input & BATTLE_INPUT_REPEAT_DIRECTIONS)
        {
            Battle_StopCombatantHighlight(target);
            target = &combatants[combatant->targetSlot];
            Battle_StartCombatantHighlight(target, 16);
            Battle_MoveCursor(cursor,
                target->animation.x + (Battle_GetCombatantDimension(target, BATTLE_COMBATANT_WIDTH) >> 1),
                target->animation.y - (Battle_GetCombatantDimension(target, BATTLE_COMBATANT_HEIGHT) >> 1), 5, 2);
            BattleMenu_DrawTargetName(gBattleWindowTilemap, target, 10, 2);
        }
        if ((input & (BATTLE_INPUT_PRESS_A | BATTLE_INPUT_HELD_DIRECTIONS)) == BATTLE_INPUT_PRESS_A
            && (input & BATTLE_INPUT_REPEAT_DIRECTIONS) == 0)
        {
            Sound_PlaySfx(1, 0, 0);
            Battle_StopCombatantHighlight(target);
            sBattleMenuPhase = BATTLE_MENU_TARGET_CONFIRMED;
        }
        break;
    case BATTLE_MENU_TARGET_CONFIRMED:
        combatant->action = BATTLE_ACTION_ATTACK;
        sBattleMenuFlags |= BATTLE_MENU_REFRESH_SELECTION;
        Battle_SetTilemapTiles(gBattleWindowTilemap, 1, 10, 2, 9, 2);
        done = 1;
        break;
    }
    return done;
}

/** @brief Start the character movement associated with entering or leaving a command.
 *  @note The phase byte becomes 30 while transition flags drive the movement.
 *  @address 0x08023320
 */
void BattleMenu_UpdatePartyCommandPhase(struct BattleCombatant **party)
{
    switch (sBattleMenuPhase)
    {
    case 0:
        sBattleMenuPhase = 30;
        break;
    case 1:
        sBattleMenuAnimationFrame = 0;
        if (sBattleMenuPartyIndex == 0)
            sBattleMenuFlags |= BATTLE_MENU_MOVE_TO_ROOT;
        else
        {
            party[sBattleMenuPartyIndex]->animation.priority = 0;
            party[sBattleMenuPartyIndex]->displayState |= 4;
            party[sBattleMenuPartyIndex + 1]->animation.priority = 0;
            sBattleMenuFlags |= BATTLE_MENU_MOVE_TO_PREVIOUS_PARTY;
        }
        sBattleMenuPhase = 30;
        break;
    case 3:
        sBattleMenuAnimationFrame = 0;
        party[sBattleMenuPartyIndex]->animation.priority = 0;
        if (sBattleMenuPartyIndex > 0)
            party[sBattleMenuPartyIndex - 1]->animation.priority = 0;
        party[sBattleMenuPartyIndex]->displayState |= 4;
        sBattleMenuFlags |= BATTLE_MENU_MOVE_TO_NEXT_PARTY;
        sBattleMenuPhase = 30;
        break;
    }
}

/** @brief Move party members between their formation and the command-input position.
 *  @note A skips to frame 15; the root-entry path then increments to 16 as in the ROM.
 *  @address 0x08023414
 */
void BattleMenu_UpdatePartyTransition(struct BattleCombatant **party, u16 input)
{
    struct BattleCombatant *current;
    struct BattleCombatant *previous;

    switch (sBattleMenuFlags & BATTLE_MENU_PARTY_TRANSITION_MASK)
    {
    case BATTLE_MENU_MOVE_TO_PREVIOUS_PARTY:
        current = party[sBattleMenuPartyIndex];
        previous = party[sBattleMenuPartyIndex + 1];
        current->homeX = Battle_InterpolateS16(gBattlePartyHomeX[current->formationSlot],
            35 - gBattlePartyHomeX[current->formationSlot], 15, (s16)sBattleMenuAnimationFrame, 2);
        current->homeY = Battle_InterpolateS16(gBattlePartyHomeY[current->formationSlot],
            35 - gBattlePartyHomeY[current->formationSlot], 15, (s16)sBattleMenuAnimationFrame, 2);
        previous->homeX = Battle_InterpolateS16(35, gBattlePartyHomeX[previous->formationSlot] - 35,
            15, (s16)sBattleMenuAnimationFrame, 2);
        previous->homeY = Battle_InterpolateS16(35, gBattlePartyHomeY[previous->formationSlot] - 35,
            15, (s16)sBattleMenuAnimationFrame, 2);
        if (sBattleMenuAnimationFrame < 15)
        {
            if (input & BATTLE_INPUT_PRESS_A)
                sBattleMenuAnimationFrame = 15;
            else
                sBattleMenuAnimationFrame++;
        }
        else
        {
            previous->animation.priority = 3;
            sBattleMenuAnimationFrame = 0;
            previous->displayState &= ~4;
            sBattleMenuFlags &= ~BATTLE_MENU_PARTY_TRANSITION_MASK;
        }
        break;
    case BATTLE_MENU_MOVE_TO_NEXT_PARTY:
        current = party[sBattleMenuPartyIndex];
        previous = party[sBattleMenuPartyIndex - 1];
        current->homeX = Battle_InterpolateS16(gBattlePartyHomeX[current->formationSlot],
            35 - gBattlePartyHomeX[current->formationSlot], 15, (s16)sBattleMenuAnimationFrame, 2);
        current->homeY = Battle_InterpolateS16(gBattlePartyHomeY[current->formationSlot],
            35 - gBattlePartyHomeY[current->formationSlot], 15, (s16)sBattleMenuAnimationFrame, 2);
        previous->homeX = Battle_InterpolateS16(35, gBattlePartyHomeX[previous->formationSlot] - 35,
            15, (s16)sBattleMenuAnimationFrame, 2);
        previous->homeY = Battle_InterpolateS16(35, gBattlePartyHomeY[previous->formationSlot] - 35,
            15, (s16)sBattleMenuAnimationFrame, 2);
        if (sBattleMenuAnimationFrame < 15)
        {
            if (input & BATTLE_INPUT_PRESS_A)
                sBattleMenuAnimationFrame = 15;
            else
                sBattleMenuAnimationFrame++;
        }
        else
        {
            previous->animation.priority = 3;
            sBattleMenuAnimationFrame = 0;
            previous->displayState &= ~4;
            sBattleMenuFlags &= ~BATTLE_MENU_PARTY_TRANSITION_MASK;
        }
        break;
    case BATTLE_MENU_MOVE_TO_ROOT:
        current = party[0];
        party[0]->homeX = Battle_InterpolateS16(35, gBattlePartyHomeX[current->formationSlot] - 35,
            15, (s16)sBattleMenuAnimationFrame, 2);
        party[0]->homeY = Battle_InterpolateS16(35, gBattlePartyHomeY[party[0]->formationSlot] - 35,
            15, (s16)sBattleMenuAnimationFrame, 2);
        if (sBattleMenuAnimationFrame < 15)
        {
            if (input & BATTLE_INPUT_PRESS_A)
                sBattleMenuAnimationFrame = 15;
            else
                sBattleMenuAnimationFrame++;
        }
        else
        {
            party[0]->animation.priority = 3;
            party[0]->displayState &= ~4;
            sBattleMenuAnimationFrame = 0;
            sBattleMenuFlags &= ~BATTLE_MENU_PARTY_TRANSITION_MASK;
        }
        break;
    case BATTLE_MENU_RETURN_LAST_PARTY:
        current = party[sBattleMenuPartyIndex];
        current->homeX = Battle_InterpolateS16(35, gBattlePartyHomeX[current->formationSlot] - 35,
            15, (s16)sBattleMenuAnimationFrame, 2);
        current->homeY = Battle_InterpolateS16(35, gBattlePartyHomeY[current->formationSlot] - 35,
            15, (s16)sBattleMenuAnimationFrame, 2);
        if (sBattleMenuAnimationFrame < 15)
        {
            if (input & BATTLE_INPUT_PRESS_A)
                sBattleMenuAnimationFrame = 15;
            else
                sBattleMenuAnimationFrame++;
        }
        else
        {
            current->animation.priority = 3;
            current->displayState &= ~4;
            sBattleMenuAnimationFrame = 0;
            sBattleMenuFlags &= ~BATTLE_MENU_PARTY_TRANSITION_MASK;
        }
        break;
    case BATTLE_MENU_MOVE_FROM_ROOT:
        current = party[0];
        party[0]->homeX = Battle_InterpolateS16(gBattlePartyHomeX[current->formationSlot],
            35 - gBattlePartyHomeX[current->formationSlot], 15, (s16)sBattleMenuAnimationFrame, 2);
        party[0]->homeY = Battle_InterpolateS16(gBattlePartyHomeY[party[0]->formationSlot],
            35 - gBattlePartyHomeY[party[0]->formationSlot], 15, (s16)sBattleMenuAnimationFrame, 2);
        if (sBattleMenuAnimationFrame < 15)
        {
            if (input & BATTLE_INPUT_PRESS_A)
                sBattleMenuAnimationFrame = 15;
            sBattleMenuAnimationFrame++;
        }
        else
        {
            sBattleMenuAnimationFrame = 0;
            sBattleMenuFlags &= ~BATTLE_MENU_PARTY_TRANSITION_MASK;
        }
        break;
    }
}

/**
 * @brief Build and update the skill list, descriptions, windows and target selection.
 * @note Choices contain learned-slot indices plus an optional direct special-skill id.
 * Preserve byte indices, signed -1 target entries and the explicit target-mode cases.
 * Phase 14 clears navigation memory even while windows are still closing; phase 30
 * commits the selected skill on a later call. The dispatcher supplies a usable list.
 * @address 0x08023820
 */
u8 BattleMenu_UpdateSkillSelection(struct BattleCombatant *combatant, struct BattleCombatant *combatants,
    struct BattleCombatant *cursor, u16 input)
{
    u8 slots[BATTLE_ENEMY_COUNT];
    struct BattleCombatant *target;
    u8 i;
    u8 direct;
    u8 done = 0;
    u8 scrollFlags = 0;

    switch (sBattleMenuPhase)
    {
    case BATTLE_MENU_SKILL_INIT:
        sBattleMenuFlags = (sBattleMenuFlags & 0xFFFD) | BATTLE_MENU_LOCK_COMMAND_INPUT;
        sBattleSkillUsableMask = 0;
        sBattleSkillScroll = sBattleSkillIndex = sBattleSkillChoiceCount = 0;
        for (i = 0; i < BATTLE_SKILL_COUNT; i++)
        {
            if (combatant->skills[i] != BATTLE_NO_SKILL)
            {
                sBattleSkillChoices[sBattleSkillChoiceCount] = i;
                if (combatant->mp >= Battle_GetSkillSlotMpCost(combatant, i))
                    sBattleSkillUsableMask |= 1 << sBattleSkillChoiceCount;
                sBattleSkillChoiceCount++;
            }
        }
        if ((s8)combatant->source.party.specialSkill >= 0 && combatant->source.party.specialGauge > 31)
        {
            sBattleSkillChoices[sBattleSkillChoiceCount] = combatant->source.party.specialSkill;
            sBattleSkillUsableMask |= 1 << sBattleSkillChoiceCount;
            sBattleSkillChoiceCount++;
        }
        BattleMenu_RestoreSelection(sBattleMenuState, combatant);
        DmaCopy(3, gBattleMenuScrollArrowTiles, BATTLE_MENU_SCROLL_ARROW_VRAM,
            sizeof(gBattleMenuScrollArrowTiles), 16);
        sBattleMenuPhase = BATTLE_MENU_SKILL_OPEN;
        break;
    case BATTLE_MENU_SKILL_OPEN:
        cursor->animation.flags |= 0x20;
        Battle_OpenWindow(1, gBattleWindowTilemap, 11, 2, 2, 7, 7, 17, 8);
        Battle_OpenWindow(2, gBattleWindowTilemap, 11, 2, 2, 0, 16, 30, 4);
        Battle_MoveCursor(cursor, 40, (sBattleSkillIndex - sBattleSkillScroll) * 16 + 80, 5, 2);
        cursor->animation.flags |= 0x20;
        sBattleMenuPhase = BATTLE_MENU_SKILL_WAIT_OPEN;
        BattleMenu_ResetDirectionalSelection();
        break;
    case BATTLE_MENU_SKILL_WAIT_OPEN:
        if (Battle_GetWindowState(1) == BATTLE_WINDOW_OPEN && Battle_GetWindowState(2) == BATTLE_WINDOW_OPEN)
            sBattleMenuPhase = BATTLE_MENU_SKILL_DRAW;
        break;
    case BATTLE_MENU_SKILL_DRAW:
        Battle_SetTilemapTiles(gBattleWindowTilemap, 1, 8, 8, 15, 6);
        for (i = sBattleSkillScroll; i < sBattleSkillScroll + BATTLE_MENU_SKILL_PAGE_ROWS && i < sBattleSkillChoiceCount; i++)
        {
            BattleMenu_DrawSkillName(gBattleWindowTilemap, combatant, i - sBattleSkillScroll, sBattleSkillChoices[i]);
            BattleMenu_DrawSkillMpCost(gBattleWindowTilemap, combatant, i - sBattleSkillScroll, sBattleSkillChoices[i]);
        }
        BattleMenu_DrawSkillDescription(gBattleWindowTilemap, combatant, sBattleSkillChoices[sBattleSkillIndex]);
        sBattleMenuPhase = BATTLE_MENU_SKILL_WAIT_UPLOAD;
        scrollFlags |= BATTLE_MENU_SCROLL_VISIBLE;
        if (sBattleSkillScroll > 0)
            scrollFlags |= BATTLE_MENU_SCROLL_UP;
        if (sBattleSkillScroll + BATTLE_MENU_SKILL_PAGE_ROWS < sBattleSkillChoiceCount)
            scrollFlags |= BATTLE_MENU_SCROLL_DOWN;
        BattleMenu_DrawScrollArrows(gBattleWindowTilemap, scrollFlags);
        Battle_SetControlFlags(0x400);
        BattleMenu_UpdateSkillListPalette(gBattleWindowTilemap);
        break;
    case BATTLE_MENU_SKILL_WAIT_UPLOAD:
        if (!(Battle_GetControlFlags() & 0x400))
            sBattleMenuPhase = BATTLE_MENU_SKILL_SELECT;
        break;
    case BATTLE_MENU_SKILL_SELECT:
        BattleMenu_UpdateSkillListPalette(gBattleWindowTilemap);
        if ((cursor->displayState & BATTLE_CURSOR_MOTION_MASK) != BATTLE_CURSOR_MOVING)
        {
            switch (input & BATTLE_INPUT_REPEAT_DIRECTIONS)
            {
            case BATTLE_INPUT_REPEAT_UP:
                if (BattleMenu_MoveSelection(sBattleMenuState, NULL, combatant, 1))
                {
                    Battle_MoveCursor(cursor, 40, (sBattleSkillIndex - sBattleSkillScroll) * 16 + 80, 5, 2);
                    sBattleMenuPhase = BATTLE_MENU_SKILL_WAIT_CURSOR;
                }
                else
                    sBattleMenuPhase = BATTLE_MENU_SKILL_DRAW;
                Sound_PlaySfx(0, 0, 0);
                BattleMenu_DrawSkillDescription(gBattleWindowTilemap, combatant, sBattleSkillChoices[sBattleSkillIndex]);
                break;
            case BATTLE_INPUT_REPEAT_DOWN:
                if (BattleMenu_MoveSelection(sBattleMenuState, NULL, combatant, 0))
                {
                    Battle_MoveCursor(cursor, 40, (sBattleSkillIndex - sBattleSkillScroll) * 16 + 80, 5, 2);
                    sBattleMenuPhase = BATTLE_MENU_SKILL_WAIT_CURSOR;
                }
                else
                    sBattleMenuPhase = BATTLE_MENU_SKILL_DRAW;
                Sound_PlaySfx(0, 0, 0);
                BattleMenu_DrawSkillDescription(gBattleWindowTilemap, combatant, sBattleSkillChoices[sBattleSkillIndex]);
                break;
            case BATTLE_INPUT_REPEAT_LEFT:
                break;
            case BATTLE_INPUT_REPEAT_RIGHT:
                break;
            default:
                switch (input & 0xF000)
                {
                case BATTLE_INPUT_PRESS_A:
                    if ((sBattleSkillUsableMask >> sBattleSkillIndex) & 1)
                    {
                        sBattleMenuPhase = BATTLE_MENU_SKILL_CLOSE_FOR_TARGET;
                        Sound_PlaySfx(1, 0, 0);
                        Battle_SetSelection(0, 0);
                        Battle_SetSelection(1, 0);
                        Battle_SetSelection(2, 0);
                        Battle_SetSelection(3, 0);
                        Battle_SetSelection(4, 1);
                        Battle_SetSelection(5, 2);
                    }
                    else
                        Sound_PlaySfx(3, 0, 0);
                    break;
                case 0x2000:
                    Sound_PlaySfx(2, 0, 0);
                    sBattleMenuPhase = BATTLE_MENU_SKILL_CANCEL_CLOSE;
                    break;
                }
                break;
            }
        }
        scrollFlags |= BATTLE_MENU_SCROLL_VISIBLE;
        if (sBattleSkillScroll > 0)
            scrollFlags |= BATTLE_MENU_SCROLL_UP;
        if (sBattleSkillScroll + BATTLE_MENU_SKILL_PAGE_ROWS < sBattleSkillChoiceCount)
            scrollFlags |= BATTLE_MENU_SCROLL_DOWN;
        BattleMenu_DrawScrollArrows(gBattleWindowTilemap, scrollFlags);
        break;
    case BATTLE_MENU_SKILL_WAIT_CURSOR:
        if ((cursor->displayState & BATTLE_CURSOR_MOTION_MASK) != BATTLE_CURSOR_MOVING)
            sBattleMenuPhase = BATTLE_MENU_SKILL_SELECT;
        scrollFlags |= BATTLE_MENU_SCROLL_VISIBLE;
        if (sBattleSkillScroll > 0)
            scrollFlags |= BATTLE_MENU_SCROLL_UP;
        if (sBattleSkillScroll + BATTLE_MENU_SKILL_PAGE_ROWS < sBattleSkillChoiceCount)
            scrollFlags |= BATTLE_MENU_SCROLL_DOWN;
        BattleMenu_DrawScrollArrows(gBattleWindowTilemap, scrollFlags);
        break;
    case BATTLE_MENU_SKILL_CLOSE_FOR_TARGET:
        scrollFlags |= BATTLE_MENU_SCROLL_VISIBLE;
        Battle_CloseWindow(1, 5, 2);
        Battle_CloseWindow(2, 5, 2);
        sBattleMenuPhase = BATTLE_MENU_SKILL_WAIT_TARGET;
        sBattleMenuFlags |= BATTLE_MENU_SELECTING_TARGETS;
        BattleMenu_DrawScrollArrows(gBattleWindowTilemap, scrollFlags);
        break;
    case BATTLE_MENU_SKILL_WAIT_TARGET:
        if (Battle_GetWindowState(1) == BATTLE_WINDOW_CLOSED && Battle_GetWindowState(2) == BATTLE_WINDOW_CLOSED)
        {
            direct = 0;
            /* Preserve separate mode cases: each has its original list call. */
            switch (Battle_GetSkillTargetMode(combatant, sBattleSkillChoices[sBattleSkillIndex]))
            {
            case BATTLE_SKILL_TARGET_ALL_ENEMIES:
                Battle_ListCombatants(combatants, slots, BATTLE_SIDE_ENEMY, BATTLE_STATUS_MASK_ALL);
                combatant->targetSlot = slots[0];
                sBattleMenuPhase = BATTLE_MENU_SKILL_SELECT_TARGET;
                break;
            case BATTLE_SKILL_TARGET_ENEMY:
                Battle_ListCombatants(combatants, slots, BATTLE_SIDE_ENEMY, BATTLE_STATUS_MASK_ALL);
                combatant->targetSlot = slots[0];
                sBattleMenuPhase = BATTLE_MENU_SKILL_SELECT_TARGET;
                break;
            case BATTLE_SKILL_TARGET_ENEMY_COLUMN:
                Battle_ListCombatants(combatants, slots, BATTLE_SIDE_ENEMY, BATTLE_STATUS_MASK_ALL);
                combatant->targetSlot = slots[0];
                sBattleMenuPhase = BATTLE_MENU_SKILL_SELECT_TARGET;
                break;
            case BATTLE_SKILL_TARGET_ENEMY_ROW_PAIR:
                Battle_ListCombatants(combatants, slots, BATTLE_SIDE_ENEMY, BATTLE_STATUS_MASK_ALL);
                combatant->targetSlot = slots[0];
                sBattleMenuPhase = BATTLE_MENU_SKILL_SELECT_TARGET;
                break;
            case BATTLE_SKILL_TARGET_PARTY:
                Battle_ListCombatants(combatants, slots, BATTLE_SIDE_PARTY, BATTLE_STATUS_MASK_ALL);
                combatant->targetSlot = slots[0];
                sBattleMenuPhase = BATTLE_MENU_SKILL_SELECT_TARGET;
                break;
            case BATTLE_SKILL_TARGET_ALL_PARTY:
                Battle_ListCombatants(combatants, slots, BATTLE_SIDE_PARTY, BATTLE_STATUS_MASK_ALL);
                combatant->targetSlot = slots[0];
                sBattleMenuPhase = BATTLE_MENU_SKILL_SELECT_TARGET;
                break;
            case BATTLE_SKILL_TARGET_NO_SELECTION:
                Battle_ListCombatants(combatants, slots, BATTLE_SIDE_PARTY, BATTLE_STATUS_MASK_ALL);
                combatant->targetSlot = slots[0];
                direct = 1;
                sBattleMenuPhase = BATTLE_MENU_SKILL_COMMIT;
                break;
            }
            if (!direct)
            {
                Battle_BuildSkillTargetList(combatants, Battle_GetSkillTargetMode(combatant, sBattleSkillChoices[sBattleSkillIndex]),
                    combatant->targetSlot, sBattleSkillTargetSlots);
                target = &combatants[combatant->targetSlot];
                for (i = 0; i < BATTLE_ENEMY_COUNT; i++)
                {
                    if (sBattleSkillTargetSlots[i] >= 0)
                    {
                        Battle_StartCombatantHighlight(&combatants[sBattleSkillTargetSlots[i]], 16);
                        Battle_StartStatusHighlight(&combatants[sBattleSkillTargetSlots[i]], 5, 16);
                    }
                }
                BattlePalette_StartBgPulse(8, 1, 31, 31, 31, 16, -1, 2);
                if (combatant->targetSlot < BATTLE_PARTY_COUNT)
                    Battle_MoveCursor(cursor, combatant->targetSlot * 40 + 24, 144, 5, 2);
                else
                {
                    Battle_MoveCursor(cursor,
                        target->animation.x + (Battle_GetCombatantDimension(target, BATTLE_COMBATANT_WIDTH) >> 1),
                        target->animation.y - (Battle_GetCombatantDimension(target, BATTLE_COMBATANT_HEIGHT) >> 1), 5, 2);
                    cursor->animation.flags &= 0xFFDF;
                }
                BattleMenu_DrawTargetName(gBattleWindowTilemap, target, 10, 2);
            }
        }
        sBattleTargetPreviousColumn = -1;
        sBattleTargetPreviousRowBits = -1;
        break;
    case BATTLE_MENU_SKILL_SELECT_TARGET:
        if ((cursor->displayState & BATTLE_CURSOR_MOTION_MASK) == BATTLE_CURSOR_MOVING)
            break;
        target = &combatants[combatant->targetSlot];
        if (input & BATTLE_INPUT_REPEAT_UP)
        {
            Sound_PlaySfx(0, 0, 0);
            combatant->targetSlot = BattleMenu_FindDirectionalTarget(target, combatants, BATTLE_MENU_UP);
        }
        else if (input & BATTLE_INPUT_REPEAT_DOWN)
        {
            Sound_PlaySfx(0, 0, 0);
            combatant->targetSlot = BattleMenu_FindDirectionalTarget(target, combatants, BATTLE_MENU_DOWN);
        }
        else if (input & BATTLE_INPUT_REPEAT_LEFT)
        {
            Sound_PlaySfx(0, 0, 0);
            combatant->targetSlot = BattleMenu_FindDirectionalTarget(target, combatants, BATTLE_MENU_LEFT);
        }
        else if (input & BATTLE_INPUT_REPEAT_RIGHT)
        {
            Sound_PlaySfx(0, 0, 0);
            combatant->targetSlot = BattleMenu_FindDirectionalTarget(target, combatants, BATTLE_MENU_RIGHT);
        }
        if (input & BATTLE_INPUT_REPEAT_DIRECTIONS)
        {
            for (i = 0; i < BATTLE_ENEMY_COUNT; i++)
            {
                if (sBattleSkillTargetSlots[i] >= 0)
                {
                    Battle_StopCombatantHighlight(&combatants[sBattleSkillTargetSlots[i]]);
                    Battle_StopStatusHighlight(&combatants[sBattleSkillTargetSlots[i]], 5);
                }
            }
            target = &combatants[combatant->targetSlot];
            Battle_BuildSkillTargetList(combatants, Battle_GetSkillTargetMode(combatant, sBattleSkillChoices[sBattleSkillIndex]),
                combatant->targetSlot, sBattleSkillTargetSlots);
            for (i = 0; i < BATTLE_ENEMY_COUNT; i++)
            {
                if (sBattleSkillTargetSlots[i] >= 0)
                {
                    Battle_StartCombatantHighlight(&combatants[sBattleSkillTargetSlots[i]], 16);
                    Battle_StartStatusHighlight(&combatants[sBattleSkillTargetSlots[i]], 5, 16);
                }
            }
            if (combatant->targetSlot < BATTLE_PARTY_COUNT)
                Battle_MoveCursor(cursor, combatant->targetSlot * 40 + 24, 144, 5, 2);
            else
                Battle_MoveCursor(cursor,
                    target->animation.x + (Battle_GetCombatantDimension(target, BATTLE_COMBATANT_WIDTH) >> 1),
                    target->animation.y - (Battle_GetCombatantDimension(target, BATTLE_COMBATANT_HEIGHT) >> 1), 5, 2);
            BattleMenu_DrawTargetName(gBattleWindowTilemap, target, 10, 2);
        }
        if (!(input & (BATTLE_INPUT_REPEAT_DIRECTIONS | BATTLE_INPUT_HELD_DIRECTIONS)))
        {
            switch (input & 0xF000)
            {
            case BATTLE_INPUT_PRESS_A:
                for (i = 0; i < BATTLE_ENEMY_COUNT; i++)
                {
                    if (sBattleSkillTargetSlots[i] >= 0)
                    {
                        Battle_StopCombatantHighlight(&combatants[sBattleSkillTargetSlots[i]]);
                        Battle_StopStatusHighlight(&combatants[sBattleSkillTargetSlots[i]], 5);
                    }
                }
                sBattleMenuFlags &= 0xF7FF;
                Sound_PlaySfx(1, 0, 0);
                Battle_SetTilemapTiles(gBattleWindowTilemap, 1, 10, 2, 9, 2);
                sBattleMenuPhase = BATTLE_MENU_SKILL_COMMIT;
                break;
            case 0x2000:
                for (i = 0; i < BATTLE_ENEMY_COUNT; i++)
                {
                    if (sBattleSkillTargetSlots[i] >= 0)
                    {
                        Battle_StopCombatantHighlight(&combatants[sBattleSkillTargetSlots[i]]);
                        Battle_StopStatusHighlight(&combatants[sBattleSkillTargetSlots[i]], 5);
                    }
                }
                Battle_SetTilemapTiles(gBattleWindowTilemap, 1, 10, 2, 9, 2);
                cursor->animation.flags |= 0x20;
                sBattleMenuFlags &= 0xF7FF;
                Sound_PlaySfx(2, 0, 0);
                sBattleMenuPhase = BATTLE_MENU_SKILL_OPEN;
                break;
            }
        }
        break;
    case BATTLE_MENU_SKILL_CANCEL_CLOSE:
        Battle_CloseWindow(1, 5, 2);
        Battle_CloseWindow(2, 5, 2);
        sBattleMenuPhase = BATTLE_MENU_SKILL_CANCEL_WAIT;
        break;
    case BATTLE_MENU_SKILL_CANCEL_WAIT:
        if (Battle_GetWindowState(1) == BATTLE_WINDOW_CLOSED && Battle_GetWindowState(2) == BATTLE_WINDOW_CLOSED)
            sBattleMenuPhase = BATTLE_MENU_SKILL_CANCEL_FINISH;
        break;
    case BATTLE_MENU_SKILL_CANCEL_FINISH:
        sBattleMenuFlags = (sBattleMenuFlags & 0xFEFF) | (BATTLE_MENU_CANCEL_COMMAND | BATTLE_MENU_REFRESH_SELECTION);
        cursor->animation.flags &= 0xFFDF;
        BattleMenu_SetHintRequestInline(1);
        sBattleMenuAnimationFrame = 0;
        break;
    case BATTLE_MENU_SKILL_COMMIT:
        BattleMenu_MoveSelection(sBattleMenuState, NULL, combatant, 2);
        combatant->skillSelection = sBattleSkillChoices[sBattleSkillIndex];
        combatant->action = BATTLE_ACTION_SKILL;
        cursor->animation.flags &= 0xFFDF;
        sBattleMenuFlags &= 0xFEFF;
        sBattleMenuAnimationFrame = 0;
        done = 1;
        break;
    }
    return done;
}

/** @brief Draw a skill name on one of the three visible list rows.
 *  @note Learned slots resolve through the combatant; direct skill ids stay disabled-colored.
 *  @address 0x080244BC
 */
void BattleMenu_DrawSkillName(u16 *tilemap, const struct BattleCombatant *combatant,
    u8 row, u16 selection)
{
    u16 *tiles = tilemap + (row * 2 * SCRIPT_TILEMAP_WIDTH + 0x108);
    u8 palette = 1;
    u16 index;
    u16 entry;
    u16 glyphCount;
    u16 *glyphIds;
    const u16 *text;

    if (selection < BATTLE_SKILL_COUNT)
    {
        palette &= ~(sBattleSkillUsableMask >> (row + sBattleSkillScroll));
        selection = combatant->skills[selection];
    }
    palette += 11;
    index = 0;
    entry = 0;
    while (entry < selection)
    {
        if (D_0839B462[index] == SCRIPT_TEXT_END)
            entry++;
        index++;
    }
    text = D_0839B462 + index;
    Script_CacheGlyphsFromWords(text, row != 0 ? 0x6F1E : 0x4F00);
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

/**
 * @brief Draw the selected skill's description in the lower battle window.
 * @note Selections 0..7 are learned-skill slots; higher values are direct ids.
 * The hint table and selected string are separate pointers to preserve the
 * original register lifetimes. FF terminates a string; zero draws blank tiles.
 * @address 0x08024618
 */
void BattleMenu_DrawSkillDescription(u16 *tilemap, const struct BattleCombatant *combatant, u8 selection)
{
    u16 *tiles = tilemap + (17 * SCRIPT_TILEMAP_WIDTH + 1);
    const u8 *hints;
    const u8 *text;
    u16 offset;
    u16 entry;

    if (selection < BATTLE_SKILL_COUNT)
        selection = combatant->skills[selection];
    Battle_SetTilemapTiles(tilemap, 1, 1, 17, 28, 2);
    hints = Battle_GetHintText();
    offset = 0;
    entry = 0;
    while (entry < selection)
    {
        if (hints[offset] == 0xFF)
            entry++;
        offset++;
    }
    text = hints + offset;
    for (offset = 0; text[offset] != 0xFF; offset++)
    {
        if (text[offset] != 0)
        {
            tiles[offset] = 0xB000 + text[offset] * 2;
            tiles[offset + SCRIPT_TILEMAP_WIDTH] = 0xB000 + (text[offset] * 2 + 1);
        }
        else
        {
            /* Preserve byte reads on both sides of the first tile write. */
            tiles[offset] = 0xB001 + text[offset];
            tiles[offset + SCRIPT_TILEMAP_WIDTH] = 0xB001 + text[offset];
        }
    }
}

/** @brief Draw the selected learned skill's MP cost beside its list row.
 *  @note Direct ids above 39 are skipped; the original nonzero-hundreds glyph is preserved.
 *  @address 0x080246E8
 */
void BattleMenu_DrawSkillMpCost(u16 *tilemap, const struct BattleCombatant *combatant,
    u8 row, u16 selection)
{
    u16 digits[3];
    u16 *tiles = tilemap + (row * 2 * SCRIPT_TILEMAP_WIDTH + 0x112);
    u8 palette;
    u8 cost;
    u16 index;

    if (selection <= 39)
    {
        s32 usable = sBattleSkillUsableMask >> (row + sBattleSkillScroll);

        palette = 1;
        palette &= ~usable;
        cost = Battle_GetSkillSlotMpCost(combatant, selection);
        digits[0] = (u8)((u32)cost / 100);
        digits[1] = (cost - digits[0] * 100) / 10;
        digits[2] = cost - (digits[0] * 100 + digits[1] * 10);
        for (index = 0; index < 3; index++)
        {
            u16 *hundreds = &digits[0];
            u16 *ones = &digits[2];

            switch (index)
            {
            case 0:
                if (*hundreds != 0)
                    *hundreds = 0xA2;
                break;
            case 1:
                {
                    u16 *tens = &digits[1];

                    if (*tens == 0 && *hundreds == 0)
                        *tens = 0;
                    else
                        digits[index] += 0xA2;
                }
                break;
            case 2:
                *ones += 0xA2;
                break;
            }
        }
        palette += 11;
        for (index = 0; index < 3; index++)
        {
            if (digits[index] != 0)
            {
                tiles[index] = (palette << 12) + digits[index] * 2;
                tiles[index + SCRIPT_TILEMAP_WIDTH] = (palette << 12) + (digits[index] * 2 + 1);
            }
            else
            {
                tiles[index] = (palette << 12) + 1;
                tiles[index + SCRIPT_TILEMAP_WIDTH] = (palette << 12) + 1;
            }
        }
    }
}

/** @brief Reset the six input-selection slots for the active command state.
 *  @note The root, manual and item paths retain distinct switch cases.
 *  @address 0x08024820
 */
void BattleMenu_ResetDirectionalSelection(void)
{
    switch (sBattleMenuState)
    {
    case BATTLE_MENU_ROOT:
    case BATTLE_MENU_AUTO:
    case BATTLE_MENU_TACTICS:
    case BATTLE_MENU_ESCAPE_ALL:
        Battle_SetSelection(0, -1);
        Battle_SetSelection(1, -1);
        Battle_SetSelection(2, 0);
        Battle_SetSelection(3, 0);
        Battle_SetSelection(4, 1);
        Battle_SetSelection(5, 2);
        break;
    case BATTLE_MENU_MANUAL:
        Battle_SetSelection(0, -1);
        Battle_SetSelection(1, -1);
        Battle_SetSelection(2, 0);
        Battle_SetSelection(3, 0);
        Battle_SetSelection(4, 1);
        Battle_SetSelection(5, 2);
        break;
    case BATTLE_MENU_ATTACK:
        Battle_SetSelection(0, 0);
        Battle_SetSelection(1, 0);
        Battle_SetSelection(2, 0);
        Battle_SetSelection(3, 0);
        Battle_SetSelection(4, 1);
        Battle_SetSelection(5, 2);
        break;
    case BATTLE_MENU_SKILL:
        Battle_SetSelection(0, 0);
        Battle_SetSelection(1, 0);
        Battle_SetSelection(2, -1);
        Battle_SetSelection(3, -1);
        Battle_SetSelection(4, 1);
        Battle_SetSelection(5, 2);
        break;
    case BATTLE_MENU_ITEM:
    case BATTLE_MENU_DEFEND:
    case BATTLE_MENU_ESCAPE:
        Battle_SetSelection(0, -1);
        Battle_SetSelection(1, -1);
        Battle_SetSelection(2, 0);
        Battle_SetSelection(3, 0);
        Battle_SetSelection(4, 1);
        Battle_SetSelection(5, 2);
        break;
    }
}

/**
 * @brief 内联更新物品窗口的上下滚动箭头。
 * @note row 是上箭头行基址；固定 EWRAM 行视图保留原地址加载、计时器重读和共享尾部。
 */
static inline void BattleMenu_DrawItemScrollArrowsInline(u16 *row, u8 flags)
{
    if (flags != 0)
    {
        sBattleMenuAnimationFrame = (sBattleMenuAnimationFrame + 1) % 16;
        if (flags & BATTLE_MENU_SCROLL_VISIBLE)
        {
            if (flags & BATTLE_MENU_SCROLL_UP)
                row[15] = BattleMenu_GetScrollArrowFrameInline(3) + 0xB9DE;
            else
                row[15] = 0xB001;
            row += 0xA0;
            if (flags & BATTLE_MENU_SCROLL_DOWN)
                row[15] = BattleMenu_GetScrollArrowFrameInline(3) + 0xB1DE;
            else
                row[15] = 0xB001;
        }
        else
        {
            row[15] = 0xB000;
            row += 0xA0;
            row[15] = 0xB000;
        }
    }
}

/**
 * @brief 更新物品列表、说明窗口和目标选择，并在确认时预留物品。
 * @note 阶段 21 用技能数量判断下箭头；阶段 28 以 __builtin_alloca(0) 复现原版零字节栈缓冲和保存寄存器覆盖。
 * @note 仅阶段 30 完成输入时返回 1；普通 C 匹配原跨度 0xBCC 字节（含字面池）。
 * @address 0x08024940
 */
u8 BattleMenu_UpdateItemSelection(struct BattleCombatant *combatant, struct BattleCombatant *combatants,
    struct BattleCombatant *cursor, u16 input)
{
    struct BattleCombatant *target;
    u8 i;
    u8 scrollFlags = 0;

    switch (sBattleMenuPhase)
    {
    case BATTLE_MENU_ITEM_INIT:
        sBattleMenuFlags = (sBattleMenuFlags & 0xFFFD) | BATTLE_MENU_LOCK_COMMAND_INPUT;
        sBattleMenuItemScroll = sBattleMenuItemIndex = 0;
        cursor->animation.flags |= 0x20;
        BattleMenu_RestoreSelection(sBattleMenuState, combatant);
        Battle_MoveCursor(cursor, 40, (sBattleMenuItemIndex - sBattleMenuItemScroll) * 16 + 80, 5, 2);
        sBattleMenuPhase = BATTLE_MENU_ITEM_OPEN;
        break;
    case BATTLE_MENU_ITEM_OPEN:
        Battle_OpenWindow(1, gBattleWindowTilemap, 11, 2, 2, 7, 7, 17, 8);
        Battle_OpenWindow(2, gBattleWindowTilemap, 11, 2, 2, 0, 16, 30, 4);
        DmaCopy(3, gBattleMenuScrollArrowTiles, BATTLE_MENU_SCROLL_ARROW_VRAM, sizeof(gBattleMenuScrollArrowTiles), 16);
        DmaWait(3);
        sBattleMenuPhase = BATTLE_MENU_ITEM_WAIT_OPEN;
        break;
    case BATTLE_MENU_ITEM_WAIT_OPEN:
        if (Battle_GetWindowState(1) == BATTLE_WINDOW_OPEN && Battle_GetWindowState(2) == BATTLE_WINDOW_OPEN)
        {
            Battle_MoveCursor(cursor, 40, (sBattleMenuItemIndex - sBattleMenuItemScroll) * 16 + 80, 5, 2);
            sBattleMenuPhase = BATTLE_MENU_ITEM_DRAW;
        }
        break;
    case BATTLE_MENU_ITEM_DRAW:
        for (i = 0; i < 3 && sBattleMenuItemScroll + i < sBattleMenuItemCount; i++)
        {
            BattleItem_DrawNameAndCount(gBattleWindowTilemap, 8, 8, &sBattleMenuItems[sBattleMenuItemScroll + i], i, 11, 1);
            BattleItem_DrawDescription(gBattleWindowTilemap, 1, 17, &sBattleMenuItems[sBattleMenuItemIndex], 11);
        }
        sBattleMenuPhase = BATTLE_MENU_ITEM_SELECT;
        scrollFlags |= BATTLE_MENU_SCROLL_VISIBLE;
        if (sBattleMenuItemScroll > 0)
            scrollFlags |= BATTLE_MENU_SCROLL_UP;
        if (sBattleMenuItemScroll + 3 < sBattleSkillChoiceCount)
            scrollFlags |= BATTLE_MENU_SCROLL_DOWN;
        BattleMenu_DrawItemScrollArrowsInline(BATTLE_MENU_ITEM_ARROW_TOP_ROW, scrollFlags);
        break;
    case BATTLE_MENU_ITEM_SELECT:
        if ((cursor->displayState & BATTLE_CURSOR_MOTION_MASK) != BATTLE_CURSOR_MOVING)
        {
            switch (input & BATTLE_INPUT_REPEAT_DIRECTIONS)
            {
            case BATTLE_INPUT_REPEAT_UP:
                if (BattleMenu_MoveSelection(sBattleMenuState, NULL, combatant, 1))
                {
                    Battle_MoveCursor(cursor, 40, (sBattleMenuItemIndex - sBattleMenuItemScroll) * 16 + 80, 5, 2);
                    sBattleMenuPhase = BATTLE_MENU_ITEM_WAIT_CURSOR;
                }
                else
                    sBattleMenuPhase = BATTLE_MENU_ITEM_DRAW;
                Sound_PlaySfx(0, 0, 0);
                BattleItem_DrawDescription(gBattleWindowTilemap, 1, 17, &sBattleMenuItems[sBattleMenuItemIndex], 11);
                break;
            case BATTLE_INPUT_REPEAT_DOWN:
                if (BattleMenu_MoveSelection(sBattleMenuState, NULL, combatant, 0))
                {
                    Battle_MoveCursor(cursor, 40, (sBattleMenuItemIndex - sBattleMenuItemScroll) * 16 + 80, 5, 2);
                    sBattleMenuPhase = BATTLE_MENU_ITEM_WAIT_CURSOR;
                }
                else
                    sBattleMenuPhase = BATTLE_MENU_ITEM_DRAW;
                Sound_PlaySfx(0, 0, 0);
                BattleItem_DrawDescription(gBattleWindowTilemap, 1, 17, &sBattleMenuItems[sBattleMenuItemIndex], 11);
                break;
            case BATTLE_INPUT_REPEAT_LEFT:
                break;
            case BATTLE_INPUT_REPEAT_RIGHT:
                break;
            default:
                switch (input & 0xF000)
                {
                case BATTLE_INPUT_PRESS_A:
                    BattleMenu_MoveSelection(sBattleMenuState, NULL, combatant, 2);
                    sBattleMenuPhase = BATTLE_MENU_ITEM_CLOSE_FOR_TARGET;
                    Battle_SetSelection(0, 0);
                    Battle_SetSelection(1, 0);
                    Battle_SetSelection(2, 0);
                    Battle_SetSelection(3, 0);
                    Battle_SetSelection(4, 1);
                    Battle_SetSelection(5, 2);
                    Sound_PlaySfx(1, 0, 0);
                    break;
                case 0x2000:
                    Sound_PlaySfx(2, 0, 0);
                    sBattleMenuPhase = BATTLE_MENU_ITEM_CANCEL_CLOSE;
                    break;
                }
                break;
            }
        }
        BattleMenu_UpdateItemListPalette(gBattleWindowTilemap);
        scrollFlags |= BATTLE_MENU_SCROLL_VISIBLE;
        if (sBattleMenuItemScroll > 0)
            scrollFlags |= BATTLE_MENU_SCROLL_UP;
        if (sBattleMenuItemScroll + 3 < sBattleMenuItemCount)
            scrollFlags |= BATTLE_MENU_SCROLL_DOWN;
        BattleMenu_DrawItemScrollArrowsInline(BATTLE_MENU_ITEM_ARROW_TOP_ROW, scrollFlags);
        break;
    case BATTLE_MENU_ITEM_WAIT_CURSOR:
        if ((cursor->displayState & BATTLE_CURSOR_MOTION_MASK) != BATTLE_CURSOR_MOVING)
            sBattleMenuPhase = BATTLE_MENU_ITEM_SELECT;
        scrollFlags |= BATTLE_MENU_SCROLL_VISIBLE;
        if (sBattleMenuItemScroll > 0)
            scrollFlags |= BATTLE_MENU_SCROLL_UP;
        if (sBattleMenuItemScroll + 3 < sBattleMenuItemCount)
            scrollFlags |= BATTLE_MENU_SCROLL_DOWN;
        BattleMenu_DrawItemScrollArrowsInline(BATTLE_MENU_ITEM_ARROW_TOP_ROW, scrollFlags);
        break;
    case BATTLE_MENU_ITEM_CANCEL_CLOSE:
        Battle_CloseWindow(1, 5, 2);
        Battle_CloseWindow(2, 5, 2);
        sBattleMenuPhase = BATTLE_MENU_ITEM_CANCEL_WAIT;
        scrollFlags |= BATTLE_MENU_SCROLL_VISIBLE;
        if (sBattleMenuItemScroll > 0)
            scrollFlags |= BATTLE_MENU_SCROLL_UP;
        if (sBattleMenuItemScroll + 3 < sBattleMenuItemCount)
            scrollFlags |= BATTLE_MENU_SCROLL_DOWN;
        BattleMenu_DrawItemScrollArrowsInline(BATTLE_MENU_ITEM_ARROW_TOP_ROW, scrollFlags);
        break;
    case BATTLE_MENU_ITEM_CANCEL_WAIT:
        if (Battle_GetWindowState(1) == BATTLE_WINDOW_CLOSED && Battle_GetWindowState(2) == BATTLE_WINDOW_CLOSED)
            sBattleMenuPhase = BATTLE_MENU_ITEM_CANCEL_FINISH;
        break;
    case BATTLE_MENU_ITEM_CANCEL_FINISH:
        BattleMenu_SetHintRequestInline(1);
        sBattleMenuFlags = (sBattleMenuFlags & 0xFEFF) | (BATTLE_MENU_CANCEL_COMMAND | BATTLE_MENU_REFRESH_SELECTION);
        cursor->animation.flags &= 0xFFDF;
        sBattleMenuAnimationFrame = 0;
        break;
    case BATTLE_MENU_ITEM_CLOSE_FOR_TARGET:
        Battle_CloseWindow(1, 5, 2);
        Battle_CloseWindow(2, 5, 2);
        sBattleMenuPhase = BATTLE_MENU_ITEM_WAIT_TARGET;
        sBattleMenuFlags |= BATTLE_MENU_SELECTING_TARGETS;
        break;
    case BATTLE_MENU_ITEM_WAIT_TARGET:
        if (Battle_GetWindowState(1) == BATTLE_WINDOW_CLOSED && Battle_GetWindowState(2) == BATTLE_WINDOW_CLOSED)
        {
            if (sBattleMenuItems[sBattleMenuItemIndex].itemId >= BATTLE_FIRST_RECOVERY_ITEM)
                Battle_ListCombatants(combatants, __builtin_alloca(0), BATTLE_SIDE_PARTY, BATTLE_STATUS_MASK_ALL);
            else
                Battle_ListCombatants(combatants, __builtin_alloca(0), BATTLE_SIDE_ENEMY, BATTLE_STATUS_MASK_ALL);
            combatant->targetSlot = ((u8 *)__builtin_alloca(0))[0];
            target = &combatants[combatant->targetSlot];
            if (combatant->targetSlot < BATTLE_PARTY_COUNT)
            {
                Battle_MoveCursor(cursor, combatant->targetSlot * 40 + 24, 144, 5, 2);
                Battle_StartCombatantHighlight(&combatants[combatant->targetSlot], 16);
                Battle_UpdateStatusPaletteOffset(&combatants[combatant->targetSlot], 5);
                Battle_StartStatusHighlight(target, 5, 16);
            }
            else
            {
                Battle_StartCombatantHighlight(target, 16);
                Battle_MoveCursor(cursor, target->animation.x + (Battle_GetCombatantDimension(target, BATTLE_COMBATANT_WIDTH) >> 1),
                    target->animation.y - (Battle_GetCombatantDimension(target, BATTLE_COMBATANT_HEIGHT) >> 1), 5, 2);
                cursor->animation.flags &= 0xFFDF;
            }
            BattleMenu_DrawTargetName(gBattleWindowTilemap, target, 10, 2);
            sBattleMenuPhase = BATTLE_MENU_ITEM_SELECT_TARGET;
        }
        BattleMenu_DrawItemScrollArrowsInline(BATTLE_MENU_ITEM_ARROW_TOP_ROW, scrollFlags);
        sBattleTargetPreviousColumn = -1;
        sBattleTargetPreviousRowBits = -1;
        break;
    case BATTLE_MENU_ITEM_SELECT_TARGET:
        target = &combatants[combatant->targetSlot];
        if ((cursor->displayState & BATTLE_CURSOR_MOTION_MASK) != BATTLE_CURSOR_MOVING)
        {
            if (input & BATTLE_INPUT_REPEAT_UP)
            {
                Sound_PlaySfx(0, 0, 0);
                combatant->targetSlot = BattleMenu_FindDirectionalTarget(target, combatants, BATTLE_MENU_UP);
            }
            else if (input & BATTLE_INPUT_REPEAT_DOWN)
            {
                Sound_PlaySfx(0, 0, 0);
                combatant->targetSlot = BattleMenu_FindDirectionalTarget(target, combatants, BATTLE_MENU_DOWN);
            }
            else if (input & BATTLE_INPUT_REPEAT_LEFT)
            {
                Sound_PlaySfx(0, 0, 0);
                combatant->targetSlot = BattleMenu_FindDirectionalTarget(target, combatants, BATTLE_MENU_LEFT);
            }
            else if (input & BATTLE_INPUT_REPEAT_RIGHT)
            {
                Sound_PlaySfx(0, 0, 0);
                combatant->targetSlot = BattleMenu_FindDirectionalTarget(target, combatants, BATTLE_MENU_RIGHT);
            }
            if (input & BATTLE_INPUT_REPEAT_DIRECTIONS)
            {
                Battle_StopCombatantHighlight(target);
                Battle_StopStatusHighlight(target, 5);
                target = &combatants[combatant->targetSlot];
                Battle_StartCombatantHighlight(target, 16);
                Battle_StartStatusHighlight(target, 5, 16);
                if (combatant->targetSlot < BATTLE_PARTY_COUNT)
                    Battle_MoveCursor(cursor, combatant->targetSlot * 40 + 24, 144, 5, 2);
                else
                    Battle_MoveCursor(cursor, target->animation.x + (Battle_GetCombatantDimension(target, BATTLE_COMBATANT_WIDTH) >> 1),
                        target->animation.y - (Battle_GetCombatantDimension(target, BATTLE_COMBATANT_HEIGHT) >> 1), 5, 2);
                BattleMenu_DrawTargetName(gBattleWindowTilemap, target, 10, 2);
            }
            if (!(input & (BATTLE_INPUT_REPEAT_DIRECTIONS | BATTLE_INPUT_HELD_DIRECTIONS)))
            {
                switch (input & 0xF000)
                {
                case BATTLE_INPUT_PRESS_A:
                    Battle_StopCombatantHighlight(target);
                    Battle_StopStatusHighlight(target, 5);
                    BattleItem_Reserve(&sBattleMenuItems[sBattleMenuItemIndex], combatant, sBattleMenuPartyIndex);
                    sBattleMenuPhase = BATTLE_MENU_ITEM_FINISH;
                    Battle_SetTilemapTiles(gBattleWindowTilemap, 1, 10, 2, 9, 2);
                    sBattleMenuFlags &= 0xF7FF;
                    Sound_PlaySfx(1, 0, 0);
                    break;
                case 0x2000:
                    Battle_StopCombatantHighlight(target);
                    Battle_StopStatusHighlight(target, 5);
                    sBattleMenuPhase = BATTLE_MENU_ITEM_OPEN;
                    sBattleMenuFlags &= 0xF7FF;
                    Battle_SetTilemapTiles(gBattleWindowTilemap, 1, 10, 2, 9, 2);
                    cursor->animation.flags |= 0x20;
                    Sound_PlaySfx(2, 0, 0);
                    break;
                }
            }
        }
        Battle_UpdateStatusPaletteOffset(&combatants[combatant->targetSlot], 5);
        break;
    case BATTLE_MENU_ITEM_FINISH:
        cursor->animation.flags &= 0xFFDF;
        sBattleMenuFlags &= 0xFEFF;
        sBattleMenuAnimationFrame = 0;
        return 1;
    }
    return 0;
}

/** @brief Set the deferred command-hint request byte.
 *  @note One enables the hint and two skips the current update; processing clears the request.
 *  @address 0x0802550C
 */
void BattleMenu_SetHintRequest(u8 request)
{
    sBattleMenuHintRequest = request;
}

/** @brief Emit one command icon and return the next OBJ tile index.
 *  @note Attribute fields use the existing two-word software OAM view.
 *  @address 0x08025518
 */
u16 BattleMenu_DrawCommandIcon(union SpriteOamEntry *oam, u8 x, u8 y, u16 tile,
    u8 shape, u8 size, u8 paletteOffset)
{
    union SpriteOamEntry sprite;

    sprite.fields.y = y;
    sprite.fields.affineMode = 0;
    sprite.fields.objMode = 0;
    sprite.fields.mosaic = 0;
    sprite.fields.bpp = 0;
    sprite.fields.shape = shape;
    sprite.fields.x = x;
    sprite.fields.affineIndexLow = 0;
    sprite.fields.hFlip = 0;
    sprite.fields.vFlip = 0;
    sprite.fields.size = size;
    sprite.fields.tile = tile;
    sprite.fields.priority = 0;
    sprite.fields.palette = paletteOffset + 13;
    *oam = sprite;
    tile += gBattleObjTileCounts[size + shape * 4];
    return tile;
}

/** @brief Clear the remembered perpendicular coordinates for target navigation.
 *  @note Both signed bytes use -1 as their unset value. */
void BattleMenu_ResetTargetMemory(void)
{
    sBattleTargetPreviousColumn = -1;
    sBattleTargetPreviousRowBits = -1;
}

/** @brief Animate the upper and lower arrows of a three-row list.
 *  @note A zero flag leaves both tiles and the shared timer untouched.
 *  @address 0x08025650
 */
void BattleMenu_DrawScrollArrows(u16 *tilemap, u8 flags)
{
    u16 *row = tilemap + 0x107;

    if (flags != 0)
    {
        sBattleMenuAnimationFrame = (sBattleMenuAnimationFrame + 1) % 16;
        if (flags & BATTLE_MENU_SCROLL_VISIBLE)
        {
            if (flags & BATTLE_MENU_SCROLL_UP)
                row[15] = BattleMenu_GetScrollArrowFrameInline(3) + 0xB9DE;
            else
                row[15] = 0xB001;
            row += 0xA0;
            if (flags & BATTLE_MENU_SCROLL_DOWN)
                row[15] = BattleMenu_GetScrollArrowFrameInline(3) + 0xB1DE;
            else
                row[15] = 0xB001;
        }
        else
        {
            row[15] = 0xB000;
            row = tilemap + 0x1A7;
            row[15] = 0xB000;
        }
    }
}

/** @brief Color visible skill rows by usability and current selection.
 *  @note Palette 11 is normal, 12 is disabled, and 13 is selected.
 *  @address 0x080256E4
 */
void BattleMenu_UpdateSkillListPalette(u16 *tilemap)
{
    u8 index;

    for (index = sBattleSkillScroll; index < sBattleSkillScroll + BATTLE_MENU_SKILL_PAGE_ROWS
        && index < sBattleSkillChoiceCount; index++)
    {
        s32 usable = sBattleSkillUsableMask >> index;
        u8 palette = 1;

        if (usable & 1)
            palette = index == sBattleSkillIndex ? 2 : 0;
        Battle_SetTilemapPalette(tilemap, palette + 11, 8,
            (index - sBattleSkillScroll) * 2 + 8, 9, 2);
    }
}

/** @brief Highlight the selected row of the visible item page.
 *  @note The signed loop bound is promoted from the byte-sized scroll position.
 *  @address 0x0802576C
 */
void BattleMenu_UpdateItemListPalette(u16 *tilemap)
{
    u8 index;

    for (index = sBattleMenuItemScroll; index < sBattleMenuItemScroll + BATTLE_MENU_SKILL_PAGE_ROWS
        && index < sBattleMenuItemCount; index++)
    {
        u8 palette = index == sBattleMenuItemIndex ? 2 : 0;

        Battle_SetTilemapPalette(tilemap, palette + 11, 8,
            (index - sBattleMenuItemScroll) * 2 + 8, 9, 2);
    }
}

/* Split read-only data, in ROM address order. */

/* ROM 0x08393A54..0x08393B14; .rodata. */
const struct BattleCommandIcon gBattleMenuCommandIcons[BATTLE_MENU_ICON_COUNT] __attribute__((aligned(1))) =
{
    { 0x00, 0x00, 0x01, 0x00 },
    { 0x04, 0x00, 0x01, 0x00 },
    { 0x08, 0x00, 0x01, 0x00 },
    { 0x0C, 0x00, 0x01, 0x00 },
    { 0x10, 0x00, 0x01, 0x00 },
    { 0x14, 0x00, 0x01, 0x00 },
    { 0x18, 0x00, 0x01, 0x00 },
    { 0x1C, 0x00, 0x01, 0x00 },
};

const union BattleCommandMenu gBattleMenuDescriptors[BATTLE_MENU_COMMAND_COUNT] __attribute__((aligned(1))) =
{
    { { 4, -1, 0x23, { 1, 2, 3, 4, -1 }, { 0x05, 0x06, 0x07, 0x04, 0xFF }, { 0x00, 0x00, 0x00 } } },
    { { 0, 0, 0x23, { -1, -1, -1, -1, -1 }, { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }, { 0x00, 0x00, 0x00 } } },
    { { 0, 0, 0x23, { -1, -1, -1, -1, -1 }, { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }, { 0x00, 0x00, 0x00 } } },
    { { 5, 0, 0x33, { 5, 6, 7, 8, 9 }, { 0x00, 0x01, 0x02, 0x03, 0x04 }, { 0x00, 0x00, 0x00 } } },
    { { 0, 0, 0x33, { -1, -1, -1, -1, -1 }, { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }, { 0x00, 0x00, 0x00 } } },
    { { 0, 3, 0x33, { -1, -1, -1, -1, -1 }, { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }, { 0x00, 0x00, 0x00 } } },
    { { 0, 3, 0x33, { -1, -1, -1, -1, -1 }, { 0x01, 0xFF, 0xFF, 0xFF, 0xFF }, { 0x00, 0x00, 0x00 } } },
    { { 0, 3, 0x33, { -1, -1, -1, -1, -1 }, { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }, { 0x00, 0x00, 0x00 } } },
    { { 0, 3, 0x33, { -1, -1, -1, -1, -1 }, { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }, { 0x00, 0x00, 0x00 } } },
    { { 0, 3, 0x33, { -1, -1, -1, -1, -1 }, { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }, { 0x00, 0x00, 0x00 } } },
};

/* ROM 0x0839DF1E..0x0839DF4B; .rodata.0039DF1E. */
const u8 gBattleMenuCommandHints[9][5] __attribute__((section(".rodata.0039DF1E"), aligned(1))) =
{
#include "lang/jp/battle_menu_command_hints.h"
};

/* ROM 0x0861A7A4..0x0861A7E4; .rodata.0061A7A4. */
const u8 gBattleMenuScrollArrowTiles[0x40] __attribute__((section(".rodata.0061A7A4"), aligned(1))) =
{
    0x11, 0x11, 0x11, 0x11, 0x86, 0x99, 0x99, 0x68, 0x75, 0x99, 0x99, 0x57, 0x61, 0x98, 0x89, 0x16, 0x11, 0x86,
    0x68, 0x11, 0x11, 0x61, 0x16, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x86, 0x99, 0x99, 0x68, 0x75, 0x99, 0x99, 0x57, 0x61, 0x98,
    0x89, 0x16, 0x11, 0x86, 0x68, 0x11, 0x11, 0x61, 0x16, 0x11,
};

/* ROM 0x0861C624..0x0861C664; .rodata.0061C624. */
const u16 gBattleMenuCommandPalettes[32] __attribute__((section(".rodata.0061C624"), aligned(1))) =
{
    0x0000, 0x0015, 0x45BF, 0x01D5, 0x8EBB, 0x939F, 0x318C, 0xC631, 0xE318, 0x5000, 0x6CE7, 0xFED6, 0x1DCA,
    0xAAF0, 0xC777, 0xFFFF, 0x0000, 0x000E, 0x28D8, 0x00EE, 0x01B3, 0x8276, 0x14A5, 0x2529, 0x39CE, 0x3400,
    0x5000, 0x61EF, 0x00E3, 0x8E09, 0xAA90, 0xD6B5,
};

/* End of split read-only data. */
