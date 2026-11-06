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

#ifndef FONT_H_
#define FONT_H_ 1

#include <stddef.h>
#include <stdint.h>

/* Glyph cell: FONT_WIDTH columns, FONT_HEIGHT rows (bit 0 = top row). */
#define FONT_WIDTH 5
#define FONT_HEIGHT 8

/* Columns advance by FONT_WIDTH plus one blank column. */
#define FONT_ADVANCE (FONT_WIDTH + 1)

/*
 * Glyph for a Unicode codepoint: FONT_WIDTH column bytes. Unknown codepoints
 * return a hollow box, never NULL.
 */
const uint8_t *font_get_glyph_cp(uint32_t codepoint);

/* Legacy API: the byte is treated as a Latin-1 codepoint (not UTF-8). */
const uint8_t *font_get_glyph(char c);

/*
 * Decode one UTF-8 sequence from a NUL-terminated string. Returns the number of
 * bytes consumed (0 at the terminator). Invalid input yields U+FFFD and
 * consumes one byte, so decoding always makes progress.
 */
size_t font_utf8_decode(const char *str, uint32_t *codepoint);

/*
 * Number of codepoints in the first 'byte_length' bytes of 'str'. The byte
 * after the range must not be a UTF-8 continuation byte (a '\n' or NUL is fine).
 */
size_t font_utf8_count(const char *str, size_t byte_length);

#endif /* FONT_H_ */
