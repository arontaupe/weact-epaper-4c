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

#ifndef EPAPER_DISPLAY_2IN9_H_
#define EPAPER_DISPLAY_2IN9_H_ 1

#include <stdint.h>
#include <stdbool.h>

#include "epaper_display.h"

/*
 * Initialization function for a 2.9" E-Paper Display.
 */
bool epd_2in9_init_display(const epd_ctx_t* ctx);

/*
 * Model-specific control functions. See the 'epaper_display.h' header for more
 * information.
 */
void epd_2in9_reset(const epd_ctx_t* ctx);
void epd_2in9_flush(const epd_ctx_t* ctx);
void epd_2in9_sleep(const epd_ctx_t* ctx);

/*
 * Model-specific drawing functions. See the 'epaper_display.h' header for more
 * information.
 */
void epd_2in9_clear(const epd_ctx_t* ctx, uint8_t color);
void epd_2in9_draw_pixel(const epd_ctx_t* ctx, uint16_t x, uint16_t y, uint8_t color);
void epd_2in9_draw_line(const epd_ctx_t* ctx,
                        uint16_t x0,
                        uint16_t y0,
                        uint16_t x1,
                        uint16_t y1,
                        uint8_t color);
void epd_2in9_draw_rect(const epd_ctx_t* ctx,
                        uint16_t x,
                        uint16_t y,
                        uint16_t width,
                        uint16_t height,
                        uint8_t color);
void epd_2in9_draw_filled_rect(const epd_ctx_t* ctx,
                               uint16_t x,
                               uint16_t y,
                               uint16_t width,
                               uint16_t height,
                               uint8_t color);
void epd_2in9_draw_char(const epd_ctx_t* ctx,
                        uint16_t x,
                        uint16_t y,
                        char c,
                        uint8_t color);
void epd_2in9_draw_str(const epd_ctx_t* ctx,
                       uint16_t x,
                       uint16_t y,
                       const char* str,
                       uint8_t color);

#endif /* EPAPER_DISPLAY_2IN9_H_ */
