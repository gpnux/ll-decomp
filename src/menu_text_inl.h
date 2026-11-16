#ifndef MENU_TEXT_INL_H
#define MENU_TEXT_INL_H

#include "menu.h"

/**
 * @brief 按菜单文本坐标和调色板编号写入一个图块（内联试验助手）
 *
 * @note x/y 为八位坐标，行跨度三十二个半字；基值为 (palette << 12) + 240，再加 tile；无独立 ROM 入口。
 * @address N/A（私有内联助手，无独立 ROM 入口）
 */
static inline void SetTile(u8 x, u8 y, u16 tile, u16 palette)
{
    u16 base = (palette << 12) + 0x240;
    // s32 xByteOffset = x * 2;

    *(u16*) MENU_TEXT_ADDR(x, y) = base + tile;

    // *(u16 *)((u32)MENU_TEXT_ADDR(0, 0) + xByteOffset + y * 64) = tileBase + tileOffset;
}



/**
 * @brief 按菜单文本坐标写入一个 tile（内联）
 *
 * @note x/y 为八位图块坐标，行跨度为三十二个半字；写入 tileBase 加 tileOffset 的低十六位。
 * @address N/A（私有内联助手，无独立 ROM 入口）
 */
static inline void Menu_SetTextTileInline(u8 x, u8 y, u16 tileOffset, u16 tileBase)
{
    s32 xByteOffset = x * 2;

    *(u16 *)((u32)MENU_TEXT_ADDR(0, 0) + xByteOffset + y * 64) = tileBase + tileOffset;
}

/**
 * @brief 从给定缓冲地址清除菜单文本矩形（内联）
 *
 * @note 宽高为八位，行列计数为十六位；填 B001，每行前进三十二个半字；无独立 ROM 入口。
 * @address N/A（私有内联助手，无独立 ROM 入口）
 */
static inline void Menu_ClearTextBufferInline(u16 *buf, u8 w, u8 h)
{
    u16 x, y;
    u16 *temp_buf;
    for (y = 0; y < h; y++)
    {
        temp_buf = buf;

        for (x = 0; x < w; x++)
        {
            *buf++ = 0xB001;
        }
        buf = temp_buf + 0x20;
    }
}

/* The one-pass block retains agbcc's loop allocation when this is inlined. */
/**
 * @brief 按文本坐标清除菜单矩形区域（内联）
 *
 * @note 宽高在循环比较时截断为十六位；填 B001，行跨度三十二个半字；同类公开入口为 0x080165C8。
 * @address N/A（私有内联助手，无独立 ROM 入口）
 */
static inline void Menu_ClearTextRectInline(u8 x, u8 y, u32 w, u32 h)
{
    do
    {
        u16 *ptr = MENU_TEXT_ADDR(x, y), *start;
        u32 width = w, height = h;
        u16 row, column;

        for (row = 0; row < (u16)height; row++)
        {
            start = ptr;
            for (column = 0; column < (u16)width; column++)
                *ptr++ = 0xB001;
            start += 32;
            ptr = start;
        }
    } while (0);
}

/* These helpers also have public ROM entries. Some callers expand them. */
/**
 * @brief 将一个普通、扩展或空字形写成上下两行 tile（内联）
 *
 * @note 下行距上行三十二个半字；零字形清空两格；低字节 FE 的高字节选择扩展字形；本内联副本没有独立 ROM 地址。
 * @address 0x08016368（对应公开入口）
 */
static inline void Menu_DrawGlyphInline(u16 *dest, u16 glyph, u8 palette)
{
    u16 paletteBits;
    u16 tile;

    if (glyph != 0)
    {
        if ((glyph & 0xFF) == 0xFE)
        {
            paletteBits = palette << 12;
            tile = ((glyph & 0xFF00) >> 7) + 0x280;
            *dest = paletteBits + tile;
            dest += 32;
            tile++;
            *dest = paletteBits | tile;
        }
        else
        {
            paletteBits = palette << 12;
            tile = glyph * 2;
            *dest = paletteBits + tile;
            dest += 32;
            tile++;
            *dest = paletteBits + tile;
        }
    }
    else
    {
        paletteBits = palette << 12;
        *dest = paletteBits + 1;
        dest += 32;
        *dest = paletteBits + 1;
    }
}

/**
 * @brief 按坐标和调色板记录绘制一组菜单文字（内联）
 *
 * @note 记录格式为 x,y,palette,text...FF，整个布局以 x=FF 结束；FE 后读取扩展字节；本内联副本没有独立 ROM 地址。
 * @address 0x080163CC（对应公开入口）
 */
static inline void Menu_DrawLayoutInline(const u8 *layout)
{
    u16 glyph;
    u8 x;
    u8 y;
    u8 palette;
    u16 *dest;

    while ((x = *layout++) != 0xFF)
    {
        y = *layout++;
        palette = *layout++;
        dest = MENU_TEXT_ADDR(x, y);
        while ((glyph = *layout++) != 0xFF)
        {
            if (glyph == 0xFE)
            {
                glyph = *layout++;
                glyph = (glyph << 8) | 0xFE;
                Menu_DrawGlyph(dest, glyph, palette);
            }
            else
                Menu_DrawGlyph(dest, glyph, palette);
            dest++;
        }
    }
}

/**
 * @brief 绘制或擦除带有文本坐标的单条菜单文字（内联）
 *
 * @note 记录为 x,y,text...FF；visible=0 按八位长度计数并用 palette 11 擦除。
 * @address 0x0801654C（对应公开入口）
 */
static inline void Menu_SetTextVisibleInline(const u8 *text, u8 palette, u8 visible)
{
    u8 x = *text++;
    u8 y = *text++;
    u16 *dest = MENU_TEXT_ADDR(x, y);

    if (visible == 0)
    {
        u8 length = 0;

        while (*text != 0xFF)
        {
            text++;
            length++;
        }
        while (length != 0)
        {
            Menu_DrawGlyph(dest++, 0, 11);
            length--;
        }
    }
    else
    {
        while (*text != 0xFF)
            Menu_DrawGlyph(dest++, *text++, palette);
    }
}

/**
 * @brief 在指定坐标绘制物品名称（内联）
 *
 * @note 编号零不绘制；物品名称占八字节、以零结束；palette 为调色板编号；本内联副本没有独立 ROM 地址。
 * @address 0x080166A4（对应公开入口）
 */
static inline void Menu_DrawItemNameInline(u8 itemId, u8 x, u8 y, u8 palette)
{
    const u8 *text;
    u16 *dest;
    u8 length;
    u16 glyph;

    if (itemId != PARTY_NO_ITEM)
    {
        text = gMenuItemNames[itemId];
        dest = MENU_TEXT_ADDR(x, y);
        for (length = 0; length <= 7 && (glyph = *text++) != 0; length++)
        {
            Menu_DrawGlyph(dest++, glyph, palette);
        }
    }
}

/**
 * @brief 在指定坐标绘制技能名称（内联）
 *
 * @note 技能编号从一开始，FF 不绘制；名称最多八字节、以零结束；本内联副本没有独立 ROM 地址。
 * @address 0x080166FC（对应公开入口）
 */
static inline void Menu_DrawSkillNameInline(u8 skillId, u8 x, u8 y, u8 palette)
{
    const u8 *text;
    u16 *dest;
    u8 length;
    u16 glyph;

    if (skillId != PARTY_NO_SKILL)
    {
        skillId--;
        text = gMenuSkillNames[skillId];
        dest = MENU_TEXT_ADDR(x, y);
        /* Keep the text terminator check in the body: agbcc's nested-loop
         * allocation differs when this test is folded into the for condition. */
        for (length = 0; length <= 7; length++)
        {
            glyph = *text++;
            if (glyph == 0)
                break;
            Menu_DrawGlyph(dest++, glyph, palette);
        }
    }
}

/* Draw the name and return the row's value column. */
/**
 * @brief 绘制角色技能行的名称并返回数值列地址（内联）
 *
 * @note 按十三格间距排成两列，行坐标从六开始；属于角色面板展开逻辑，无独立 ROM 入口。
 * @address N/A（私有内联助手，无独立 ROM 入口）
 */
static inline u16 *Menu_DrawCharacterSkillNameRowInline(u8 skillId, u16 rowIndex)
{
    u8 nameX = (rowIndex & 1) * 13 + 3;
    u8 nameY = (rowIndex & ~1) + 6;
    u8 valueX;

    Menu_DrawSkillNameInline(skillId, nameX, nameY, 11);
    valueX = (rowIndex & 1) * 13 + 13;
    return MENU_TEXT_ADDR(valueX, (rowIndex & ~1) + 6);
}

/**
 * @brief 查找并绘制场景名称，返回文字结束位置（内联）
 *
 * @note sceneId 按连续 FF 结尾名称编号；FE 后读取扩展字节；返回下一可写半字地址；本内联副本没有独立 ROM 地址。
 * @address 0x08016460（对应公开入口）
 */
static inline u16 *Menu_DrawSceneNameInline(u16 *dest, u16 sceneId, u8 palette)
{
    const u8 *text = gMenuSceneNames;
    u16 index = 0;

    while (index != sceneId)
    {
        if (*text++ == 0xFF)
            index++;
    }
    while (*text != 0xFF)
    {
        if (*text == 0xFE)
        {
            text++;
            Menu_DrawGlyph(dest++, (*text << 8) | 0xFE, palette);
        }
        else
            Menu_DrawGlyph(dest++, *text, palette);
        text++;
    }
    return dest;
}

/**
 * @brief 绘制场景名称并追加菜单右括号（内联）
 *
 * @note 返回位置由 Menu_DrawSceneName 得到；仅追加右括号 C9，括号固定使用 palette 11；本内联副本没有独立 ROM 地址。
 * @address 0x08016444（对应公开入口）
 */
static inline void Menu_DrawSceneNameWithClosingBracketInline(u16 *dest, u16 sceneId, u8 palette)
{
    Menu_DrawGlyph(Menu_DrawSceneName(dest, sceneId, palette), 0xC9, 11);
}

/**
 * @brief 比较存档头的十二字节签名（内联）
 *
 * @note 匹配返回零，不匹配返回一；调用者传入槽头指针，本助手不查询槽编号。
 * @address N/A（私有内联助手，无独立 ROM 入口）
 */
static inline u8 Save_CompareSlotSignature(const u8 *text)
{
    u8 index = 0;
    do
    {
        if (*text != gSaveSlotSignature[index])
            return 1;
        text++;
        index++;
    } while (index <= 11);
    return 0;
}

#endif
