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

#include <stdint.h>

/*
 * Width of the internal bitmap font.
 */
#define FONT_WIDTH 5

/*
 * Height of the internal bitmap font.
 */
#define FONT_HEIGHT 7

/*----------------------------------------------------------------------------*/

/*
 * Get the glyph for the internal 5x7 font. The glyph is a pointer to an array
 * of FONT_WIDTH bytes, where each byte contains FONT_HEIGHT bits of bitmap
 * data about the specified character.
 */
const uint8_t* font_get_glyph(char c);

#endif /* FONT_H_ */
