/*
 * Copyright 2026 8dcc
 *
 * This file is part of rp2350-epaper.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * 5x8 bitmap font, extended for German text (modified from the original 5x7
 * table: shifted down one row, ASCII fixes, German characters added).
 *
 * Glyph format: FONT_WIDTH columns, left to right. In each byte, bit 0 is the
 * top row and bit 7 the bottom row.
 *
 * Cell layout (8 rows):
 *   row 0      accent dots of the capital umlauts; blank otherwise (line gap)
 *   rows 1-7   capitals and ascenders, baseline on row 7
 *   rows 3-7   lowercase x-height
 * The capital umlauts are drawn 6 rows high (rows 2-7) so the dots on row 0
 * stay separated from the letter. There are no true descenders: g, j, p, q
 * and y use the classic 5x7 shapes.
 *
 * Text is UTF-8: use font_utf8_decode() and font_get_glyph_cp().
 *
 * Generated from the original table by gen_font.py; edit the glyphs there or
 * here, but keep the format above.
 */

#include <stddef.h>
#include <stdint.h>

#include "font.h"

/* Printable ASCII, U+0020 .. U+007E. */
static const uint8_t font_ascii[][FONT_WIDTH] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
    {0x00, 0x00, 0xBE, 0x00, 0x00}, /* ! */
    {0x00, 0x0E, 0x00, 0x0E, 0x00}, /* " */
    {0x28, 0xFE, 0x28, 0xFE, 0x28}, /* # */
    {0x48, 0x54, 0xFE, 0x54, 0x24}, /* $ */
    {0x46, 0x26, 0x10, 0xC8, 0xC4}, /* % */
    {0x6C, 0x92, 0xAA, 0x44, 0xA0}, /* & */
    {0x00, 0x0A, 0x06, 0x00, 0x00}, /* ' */
    {0x00, 0x38, 0x44, 0x82, 0x00}, /* ( */
    {0x00, 0x82, 0x44, 0x38, 0x00}, /* ) */
    {0x28, 0x10, 0x7C, 0x10, 0x28}, /* * */
    {0x10, 0x10, 0x7C, 0x10, 0x10}, /* + */
    {0x00, 0xA0, 0x60, 0x00, 0x00}, /* , */
    {0x10, 0x10, 0x10, 0x10, 0x10}, /* - */
    {0x00, 0xC0, 0xC0, 0x00, 0x00}, /* . */
    {0x40, 0x20, 0x10, 0x08, 0x04}, /* / */
    {0x7C, 0xA2, 0x92, 0x8A, 0x7C}, /* 0 */
    {0x00, 0x84, 0xFE, 0x80, 0x00}, /* 1 */
    {0x84, 0xC2, 0xA2, 0x92, 0x8C}, /* 2 */
    {0x42, 0x82, 0x8A, 0x96, 0x62}, /* 3 */
    {0x30, 0x28, 0x24, 0xFE, 0x20}, /* 4 */
    {0x4E, 0x8A, 0x8A, 0x8A, 0x72}, /* 5 */
    {0x78, 0x94, 0x92, 0x92, 0x60}, /* 6 */
    {0x02, 0xE2, 0x12, 0x0A, 0x06}, /* 7 */
    {0x6C, 0x92, 0x92, 0x92, 0x6C}, /* 8 */
    {0x0C, 0x92, 0x92, 0x52, 0x3C}, /* 9 */
    {0x00, 0x6C, 0x6C, 0x00, 0x00}, /* : */
    {0x00, 0xAC, 0x6C, 0x00, 0x00}, /* ; */
    {0x10, 0x28, 0x44, 0x82, 0x00}, /* < */
    {0x28, 0x28, 0x28, 0x28, 0x28}, /* = */
    {0x00, 0x82, 0x44, 0x28, 0x10}, /* > */
    {0x04, 0x02, 0xA2, 0x12, 0x0C}, /* ? */
    {0x64, 0x92, 0xF2, 0x82, 0x7C}, /* @ */
    {0xFC, 0x22, 0x22, 0x22, 0xFC}, /* A */
    {0xFE, 0x92, 0x92, 0x92, 0x6C}, /* B */
    {0x7C, 0x82, 0x82, 0x82, 0x44}, /* C */
    {0xFE, 0x82, 0x82, 0x44, 0x38}, /* D */
    {0xFE, 0x92, 0x92, 0x92, 0x82}, /* E */
    {0xFE, 0x12, 0x12, 0x12, 0x02}, /* F */
    {0x7C, 0x82, 0x92, 0x92, 0xF4}, /* G */
    {0xFE, 0x10, 0x10, 0x10, 0xFE}, /* H */
    {0x00, 0x82, 0xFE, 0x82, 0x00}, /* I */
    {0x40, 0x80, 0x82, 0x7E, 0x02}, /* J */
    {0xFE, 0x10, 0x28, 0x44, 0x82}, /* K */
    {0xFE, 0x80, 0x80, 0x80, 0x80}, /* L */
    {0xFE, 0x04, 0x18, 0x04, 0xFE}, /* M */
    {0xFE, 0x08, 0x10, 0x20, 0xFE}, /* N */
    {0x7C, 0x82, 0x82, 0x82, 0x7C}, /* O */
    {0xFE, 0x12, 0x12, 0x12, 0x0C}, /* P */
    {0x7C, 0x82, 0xA2, 0x42, 0xBC}, /* Q */
    {0xFE, 0x12, 0x32, 0x52, 0x8C}, /* R */
    {0x8C, 0x92, 0x92, 0x92, 0x62}, /* S */
    {0x02, 0x02, 0xFE, 0x02, 0x02}, /* T */
    {0x7E, 0x80, 0x80, 0x80, 0x7E}, /* U */
    {0x3E, 0x40, 0x80, 0x40, 0x3E}, /* V */
    {0x7E, 0x80, 0x70, 0x80, 0x7E}, /* W */
    {0xC6, 0x28, 0x10, 0x28, 0xC6}, /* X */
    {0x0E, 0x10, 0xE0, 0x10, 0x0E}, /* Y */
    {0xC2, 0xA2, 0x92, 0x8A, 0x86}, /* Z */
    {0x00, 0xFE, 0x82, 0x82, 0x00}, /* [ */
    {0x04, 0x08, 0x10, 0x20, 0x40}, /* \ */
    {0x00, 0x82, 0x82, 0xFE, 0x00}, /* ] */
    {0x08, 0x04, 0x02, 0x04, 0x08}, /* ^ */
    {0x80, 0x80, 0x80, 0x80, 0x80}, /* _ */
    {0x00, 0x02, 0x04, 0x08, 0x00}, /* ` */
    {0x40, 0xA8, 0xA8, 0xA8, 0xF0}, /* a */
    {0xFE, 0x90, 0x88, 0x88, 0x70}, /* b */
    {0x70, 0x88, 0x88, 0x88, 0x40}, /* c */
    {0x70, 0x88, 0x88, 0x90, 0xFE}, /* d */
    {0x70, 0xA8, 0xA8, 0xA8, 0x30}, /* e */
    {0x10, 0xFC, 0x12, 0x02, 0x04}, /* f */
    {0x18, 0xA4, 0xA4, 0xA4, 0x7C}, /* g */
    {0xFE, 0x10, 0x08, 0x08, 0xF0}, /* h */
    {0x00, 0x88, 0xFA, 0x80, 0x00}, /* i */
    {0x40, 0x80, 0x88, 0x7A, 0x00}, /* j */
    {0xFE, 0x20, 0x50, 0x88, 0x00}, /* k */
    {0x00, 0x82, 0xFE, 0x80, 0x00}, /* l */
    {0xF8, 0x08, 0x30, 0x08, 0xF0}, /* m */
    {0xF8, 0x10, 0x08, 0x08, 0xF0}, /* n */
    {0x70, 0x88, 0x88, 0x88, 0x70}, /* o */
    {0xF8, 0x28, 0x28, 0x28, 0x10}, /* p */
    {0x10, 0x28, 0x28, 0x30, 0xF8}, /* q */
    {0xF8, 0x10, 0x08, 0x08, 0x10}, /* r */
    {0x90, 0xA8, 0xA8, 0xA8, 0x40}, /* s */
    {0x08, 0x7E, 0x88, 0x80, 0x40}, /* t */
    {0x78, 0x80, 0x80, 0x40, 0xF8}, /* u */
    {0x38, 0x40, 0x80, 0x40, 0x38}, /* v */
    {0x78, 0x80, 0x60, 0x80, 0x78}, /* w */
    {0x88, 0x50, 0x20, 0x50, 0x88}, /* x */
    {0x18, 0xA0, 0xA0, 0xA0, 0x78}, /* y */
    {0x88, 0xC8, 0xA8, 0x98, 0x88}, /* z */
    {0x00, 0x10, 0x6C, 0x82, 0x00}, /* { */
    {0x00, 0x00, 0xFE, 0x00, 0x00}, /* | */
    {0x00, 0x82, 0x6C, 0x10, 0x00}, /* } */
    {0x20, 0x10, 0x10, 0x20, 0x10}, /* ~ */
};

/* Hollow box shown for codepoints the font does not have. */
static const uint8_t font_fallback[FONT_WIDTH] = {0xFE, 0x82, 0x82, 0x82, 0xFE};

typedef struct
{
    uint32_t codepoint;
    uint8_t columns[FONT_WIDTH];
} font_extra_t;

/* Non-ASCII glyphs. The art next to each entry shows rows 0-7, top to bottom. */
static const font_extra_t font_extra[] = {
    /* U+00E4 a-umlaut
     *   .....
     *   .#.#.
     *   .....
     *   .###.
     *   ....#
     *   .####
     *   #...#
     *   .####
     */
    {0x00E4, {0x40, 0xAA, 0xA8, 0xAA, 0xF0}},
    /* U+00F6 o-umlaut
     *   .....
     *   .#.#.
     *   .....
     *   .###.
     *   #...#
     *   #...#
     *   #...#
     *   .###.
     */
    {0x00F6, {0x70, 0x8A, 0x88, 0x8A, 0x70}},
    /* U+00FC u-umlaut
     *   .....
     *   .#.#.
     *   .....
     *   #...#
     *   #...#
     *   #...#
     *   #..##
     *   .##.#
     */
    {0x00FC, {0x78, 0x82, 0x80, 0x42, 0xF8}},
    /* U+00C4 A-umlaut
     *   .#.#.
     *   .....
     *   .###.
     *   #...#
     *   #...#
     *   #####
     *   #...#
     *   #...#
     */
    {0x00C4, {0xF8, 0x25, 0x24, 0x25, 0xF8}},
    /* U+00D6 O-umlaut
     *   .#.#.
     *   .....
     *   .###.
     *   #...#
     *   #...#
     *   #...#
     *   #...#
     *   .###.
     */
    {0x00D6, {0x78, 0x85, 0x84, 0x85, 0x78}},
    /* U+00DC U-umlaut
     *   .#.#.
     *   .....
     *   #...#
     *   #...#
     *   #...#
     *   #...#
     *   #...#
     *   .###.
     */
    {0x00DC, {0x7C, 0x81, 0x80, 0x81, 0x7C}},
    /* U+00DF sharp s
     *   .....
     *   .###.
     *   #...#
     *   #...#
     *   #.##.
     *   #...#
     *   #...#
     *   #.##.
     */
    {0x00DF, {0xFC, 0x02, 0x92, 0x92, 0x6C}},
    /* U+201E low double quote
     *   .....
     *   .....
     *   .....
     *   .....
     *   .....
     *   ##.##
     *   .#..#
     *   #..#.
     */
    {0x201E, {0xA0, 0x60, 0x00, 0xA0, 0x60}},
    /* U+201C left double quote
     *   .....
     *   .#..#
     *   #..#.
     *   ##.##
     *   .....
     *   .....
     *   .....
     *   .....
     */
    {0x201C, {0x0C, 0x0A, 0x00, 0x0C, 0x0A}},
    /* U+201D right double quote
     *   .....
     *   ##.##
     *   .#..#
     *   #..#.
     *   .....
     *   .....
     *   .....
     *   .....
     */
    {0x201D, {0x0A, 0x06, 0x00, 0x0A, 0x06}},
    /* U+201A low single quote
     *   .....
     *   .....
     *   .....
     *   .....
     *   .....
     *   .##..
     *   ..#..
     *   .#...
     */
    {0x201A, {0x00, 0xA0, 0x60, 0x00, 0x00}},
    /* U+2018 left single quote
     *   .....
     *   ...#.
     *   ..#..
     *   ..##.
     *   .....
     *   .....
     *   .....
     *   .....
     */
    {0x2018, {0x00, 0x00, 0x0C, 0x0A, 0x00}},
    /* U+2019 right single quote / apostrophe
     *   .....
     *   .##..
     *   ..#..
     *   .#...
     *   .....
     *   .....
     *   .....
     *   .....
     */
    {0x2019, {0x00, 0x0A, 0x06, 0x00, 0x00}},
    /* U+00AB left guillemet
     *   .....
     *   .....
     *   .....
     *   ..#.#
     *   .#.#.
     *   #.#..
     *   .#.#.
     *   ..#.#
     */
    {0x00AB, {0x20, 0x50, 0xA8, 0x50, 0x88}},
    /* U+00BB right guillemet
     *   .....
     *   .....
     *   .....
     *   #.#..
     *   .#.#.
     *   ..#.#
     *   .#.#.
     *   #.#..
     */
    {0x00BB, {0x88, 0x50, 0xA8, 0x50, 0x20}},
    /* U+20AC euro
     *   .....
     *   ..###
     *   .#...
     *   ####.
     *   .#...
     *   ####.
     *   .#...
     *   ..###
     */
    {0x20AC, {0x28, 0x7C, 0xAA, 0xAA, 0x82}},
    /* U+00B0 degree
     *   .....
     *   .##..
     *   #..#.
     *   #..#.
     *   .##..
     *   .....
     *   .....
     *   .....
     */
    {0x00B0, {0x0C, 0x12, 0x12, 0x0C, 0x00}},
    /* U+2026 ellipsis
     *   .....
     *   .....
     *   .....
     *   .....
     *   .....
     *   .....
     *   .....
     *   #.#.#
     */
    {0x2026, {0x80, 0x00, 0x80, 0x00, 0x80}},
    /* U+2022 bullet
     *   .....
     *   .....
     *   .....
     *   .###.
     *   .###.
     *   .###.
     *   .....
     *   .....
     */
    {0x2022, {0x00, 0x38, 0x38, 0x38, 0x00}},
};

/*----------------------------------------------------------------------------*/

const uint8_t *font_get_glyph_cp(uint32_t codepoint)
{
    if (codepoint >= 0x20 && codepoint <= 0x7E)
        return font_ascii[codepoint - 0x20];

    /* Control characters (tab, CR, ...) render as blank. */
    if (codepoint < 0x20 || codepoint == 0x7F)
        return font_ascii[0];

    switch (codepoint)
    {
    case 0x00A0: /* no-break space */
        return font_ascii[0];
    case 0x2013: /* en dash */
    case 0x2014: /* em dash */
    case 0x2212: /* minus sign */
        /* The hyphen already spans the whole 5-column cell. */
        return font_ascii['-' - 0x20];
    default:
        break;
    }

    for (size_t i = 0; i < sizeof(font_extra) / sizeof(font_extra[0]); i++)
        if (font_extra[i].codepoint == codepoint)
            return font_extra[i].columns;

    return font_fallback;
}

/* Legacy single-byte API: the byte is taken as a Latin-1 codepoint. */
const uint8_t *font_get_glyph(char c)
{
    return font_get_glyph_cp((uint8_t)c);
}

/*----------------------------------------------------------------------------*/

size_t font_utf8_decode(const char *str, uint32_t *codepoint)
{
    const uint8_t *p = (const uint8_t *)str;
    size_t length;
    uint32_t value;
    uint32_t minimum;

    if (p[0] == 0)
    {
        *codepoint = 0;
        return 0;
    }
    if (p[0] < 0x80)
    {
        *codepoint = p[0];
        return 1;
    }

    if ((p[0] & 0xE0) == 0xC0)
    {
        length = 2;
        value = p[0] & 0x1F;
        minimum = 0x80;
    }
    else if ((p[0] & 0xF0) == 0xE0)
    {
        length = 3;
        value = p[0] & 0x0F;
        minimum = 0x800;
    }
    else if ((p[0] & 0xF8) == 0xF0)
    {
        length = 4;
        value = p[0] & 0x07;
        minimum = 0x10000;
    }
    else
    {
        *codepoint = 0xFFFD;
        return 1;
    }

    for (size_t i = 1; i < length; i++)
    {
        /* Also stops at the string terminator, never reading past it. */
        if ((p[i] & 0xC0) != 0x80)
        {
            *codepoint = 0xFFFD;
            return 1;
        }
        value = (value << 6) | (p[i] & 0x3F);
    }

    if (value < minimum || value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF))
    {
        *codepoint = 0xFFFD;
        return 1;
    }

    *codepoint = value;
    return length;
}

size_t font_utf8_count(const char *str, size_t byte_length)
{
    size_t count = 0;
    size_t offset = 0;
    while (offset < byte_length)
    {
        uint32_t codepoint;
        size_t consumed = font_utf8_decode(str + offset, &codepoint);
        if (consumed == 0)
            break;
        offset += consumed;
        count++;
    }
    return count;
}
