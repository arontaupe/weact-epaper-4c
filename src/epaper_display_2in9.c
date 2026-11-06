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

#include "epaper_display_2in9.h"

#include "font.h"

#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"

#include "epaper_display_utils.h"

/* Display commands */
#define EPD_CMD_POWER_SETTING 0x01
#define EPD_CMD_BOOSTER_SOFT_START_CONTROL 0x06
#define EPD_CMD_DEEP_SLEEP_MODE 0x07
#define EPD_CMD_PANEL_SETTING 0x00
#define EPD_CMD_DISPLAY_REFRESH 0x12
#define EPD_CMD_DATA_START_TRANSMISSION 0x10
#define EPD_CMD_VCOM_AND_DATA_INTERVAL 0x50
#define EPD_CMD_RESOLUTION_SETTING 0x61
#define EPD_CMD_POWER_ON 0x04
#define EPD_CMD_POWER_OFF 0x02
#define EPD_CMD_PARTIAL_WINDOW 0x83

/* Deep sleep control code for the JD79667 (GDEY029F51H reference driver). */
#define EPD_DEEP_SLEEP_CODE 0xA5

/* Typical full refresh takes ~21 s; give the idle wait plenty of headroom. */
#define EPD_REFRESH_START_DELAY_MS 1

/*
 * Set by epd_2in9_sleep(): after deep sleep the controller needs a hardware
 * reset before it accepts commands again. Single-display driver, so a
 * file-scope flag is enough.
 */
static bool panel_asleep = false;

/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/

/*
 * Send the full register setup and power the panel on. Mirrors the GxEPD2
 * GDEY029F51H '_InitDisplay()': it is re-run before every update because the
 * panel is powered off again after each refresh. The hardware reset is only
 * needed on first init and after deep sleep.
 *
 * Does not touch the framebuffer.
 */
static void epd_2in9_init_registers(const epd_ctx_t *ctx, bool hardware_reset)
{
    if (hardware_reset)
        epd_2in9_reset(ctx);
    epd_utils_send_command(ctx, 0x4D);
    epd_utils_send_data(ctx, 0x78);
    epd_utils_send_command(ctx, EPD_CMD_PANEL_SETTING);
    epd_utils_send_data(ctx, 0x0F);
    epd_utils_send_data(ctx, 0x29);
    epd_utils_send_command(ctx, EPD_CMD_POWER_SETTING);
    epd_utils_send_data(ctx, 0x07);
    epd_utils_send_data(ctx, 0x00);
    epd_utils_send_command(ctx, 0x03);
    epd_utils_send_data(ctx, 0x10);
    epd_utils_send_data(ctx, 0x54);
    epd_utils_send_data(ctx, 0x44);
    epd_utils_send_command(ctx, EPD_CMD_BOOSTER_SOFT_START_CONTROL);
    epd_utils_send_data(ctx, 0x05);
    epd_utils_send_data(ctx, 0x00);
    epd_utils_send_data(ctx, 0x3F);
    epd_utils_send_data(ctx, 0x0A);
    epd_utils_send_data(ctx, 0x25);
    epd_utils_send_data(ctx, 0x12);
    epd_utils_send_data(ctx, 0x1A);
    epd_utils_send_command(ctx, EPD_CMD_VCOM_AND_DATA_INTERVAL);
    epd_utils_send_data(ctx, 0x37);
    epd_utils_send_command(ctx, 0x60);
    epd_utils_send_data(ctx, 0x02);
    epd_utils_send_data(ctx, 0x02);
    epd_utils_send_command(ctx, EPD_CMD_RESOLUTION_SETTING);
    epd_utils_send_data(ctx, ctx->width >> 8);
    epd_utils_send_data(ctx, ctx->width & 0xFF);
    epd_utils_send_data(ctx, ctx->height >> 8);
    epd_utils_send_data(ctx, ctx->height & 0xFF);
    epd_utils_send_command(ctx, 0xE7);
    epd_utils_send_data(ctx, 0x1C);
    epd_utils_send_command(ctx, 0xE3);
    epd_utils_send_data(ctx, 0x22);
    epd_utils_send_command(ctx, 0xB4);
    epd_utils_send_data(ctx, 0xD0);
    epd_utils_send_command(ctx, 0xB5);
    epd_utils_send_data(ctx, 0x03);
    epd_utils_send_command(ctx, 0xE9);
    epd_utils_send_data(ctx, 0x01);
    epd_utils_send_command(ctx, 0x30);
    epd_utils_send_data(ctx, 0x08);
    epd_utils_send_command(ctx, EPD_CMD_POWER_ON);
    epd_utils_wait_until_idle(ctx);
}

bool epd_2in9_init_display(const epd_ctx_t *ctx)
{
    epd_2in9_init_registers(ctx, true);
    panel_asleep = false;
    /* Start from an all-white framebuffer (color 1 in every 2-bit pixel). */
    memset(ctx->framebuffer, 0x55, ctx->framebuffer_size);
    return true;
}

void epd_2in9_reset(const epd_ctx_t *ctx)
{
    gpio_put(ctx->pins.res, 1);
    sleep_ms(200);
    gpio_put(ctx->pins.res, 0);
    sleep_ms(5);
    gpio_put(ctx->pins.res, 1);
    sleep_ms(200);
}

void epd_2in9_flush(const epd_ctx_t *ctx)
{
    const uint16_t last_x = ctx->width - 1;
    const uint16_t last_y = ctx->height - 1;

    /*
     * The panel is powered off after every refresh (see below), so bring it
     * back up first. After deep sleep this includes a hardware reset.
     */
    epd_2in9_init_registers(ctx, panel_asleep);
    panel_asleep = false;

    /* RAM window = whole screen, partial-mode flag set while writing data. */
    epd_utils_send_command(ctx, EPD_CMD_PARTIAL_WINDOW);
    epd_utils_send_data(ctx, 0);
    epd_utils_send_data(ctx, 0);
    epd_utils_send_data(ctx, last_x >> 8);
    epd_utils_send_data(ctx, last_x & 0xFF);
    epd_utils_send_data(ctx, 0);
    epd_utils_send_data(ctx, 0);
    epd_utils_send_data(ctx, last_y >> 8);
    epd_utils_send_data(ctx, last_y & 0xFF);
    epd_utils_send_data(ctx, 1);
    epd_utils_send_command(ctx, EPD_CMD_DATA_START_TRANSMISSION);
    epd_utils_send_data_buffer(ctx, ctx->framebuffer, ctx->framebuffer_size);

    /* Full-screen window with the partial flag cleared => full refresh. */
    epd_utils_send_command(ctx, EPD_CMD_PARTIAL_WINDOW);
    epd_utils_send_data(ctx, 0x00);
    epd_utils_send_data(ctx, 0x00);
    epd_utils_send_data(ctx, last_x >> 8);
    epd_utils_send_data(ctx, last_x & 0xFF);
    epd_utils_send_data(ctx, 0x00);
    epd_utils_send_data(ctx, 0x00);
    epd_utils_send_data(ctx, last_y >> 8);
    epd_utils_send_data(ctx, last_y & 0xFF);
    epd_utils_send_data(ctx, 0x00);
    epd_utils_send_command(ctx, EPD_CMD_VCOM_AND_DATA_INTERVAL);
    epd_utils_send_data(ctx, 0x37);
    epd_utils_send_command(ctx, EPD_CMD_DISPLAY_REFRESH);
    epd_utils_send_data(ctx, 0x00);
    /* BUSY may take a moment to assert after the refresh command. */
    sleep_ms(EPD_REFRESH_START_DELAY_MS);
    epd_utils_wait_until_idle(ctx);

    /* Power the panel off so the image is not held under bias. */
    epd_utils_send_command(ctx, EPD_CMD_POWER_OFF);
    epd_utils_send_data(ctx, 0x00);
    epd_utils_wait_until_idle(ctx);
}

/*
 * Put the controller into deep sleep. 'epd_2in9_flush' already powers the
 * panel off after each refresh. The next flush performs the hardware reset
 * needed to wake the controller.
 */
void epd_2in9_sleep(const epd_ctx_t *ctx)
{
    epd_utils_send_command(ctx, EPD_CMD_DEEP_SLEEP_MODE);
    epd_utils_send_data(ctx, EPD_DEEP_SLEEP_CODE);
    panel_asleep = true;
}

void epd_2in9_clear(const epd_ctx_t *ctx, uint8_t color)
{
    if (color > EPD_COLOR_RED)
    {
        EPD_LOG("Invalid color enumerator (%d).", color);
        return;
    }
    uint8_t fill_value = (color << 6) | (color << 4) | (color << 2) | color;
    memset(ctx->framebuffer, fill_value, ctx->framebuffer_size);
}

void epd_2in9_draw_pixel(const epd_ctx_t *ctx,
                         uint16_t x,
                         uint16_t y,
                         uint8_t color)
{
    if (x >= ctx->width || y >= ctx->height)
        return;

    if (color > EPD_COLOR_RED)
    {
        EPD_LOG("Invalid color enumerator (%d).", color);
        return;
    }
    uint32_t addr = (x / 4) + y * (ctx->width / 4);
    uint8_t shift = 6 - ((x % 4) * 2);
    ctx->framebuffer[addr] = (ctx->framebuffer[addr] & ~(0x03 << shift)) |
                             ((color & 0x03) << shift);
}

void epd_2in9_draw_line(const epd_ctx_t *ctx,
                        uint16_t x0,
                        uint16_t y0,
                        uint16_t x1,
                        uint16_t y1,
                        uint8_t color)
{
    int16_t dx = abs(x1 - x0);
    int16_t dy = abs(y1 - y0);
    int16_t sx = x0 < x1 ? 1 : -1;
    int16_t sy = y0 < y1 ? 1 : -1;
    int16_t err = dx - dy;

    for (;;)
    {
        epd_2in9_draw_pixel(ctx, x0, y0, color);

        if (x0 == x1 && y0 == y1)
            break;

        int16_t e2 = 2 * err;
        if (e2 > -dy)
        {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void epd_2in9_draw_rect(const epd_ctx_t *ctx,
                        uint16_t x,
                        uint16_t y,
                        uint16_t width,
                        uint16_t height,
                        uint8_t color)
{
    if (width == 0 || height == 0)
        return;

    epd_2in9_draw_line(ctx, x, y, x + width - 1, y, color);
    epd_2in9_draw_line(ctx,
                       x + width - 1,
                       y,
                       x + width - 1,
                       y + height - 1,
                       color);
    epd_2in9_draw_line(ctx,
                       x + width - 1,
                       y + height - 1,
                       x,
                       y + height - 1,
                       color);
    epd_2in9_draw_line(ctx, x, y + height - 1, x, y, color);
}

void epd_2in9_draw_filled_rect(const epd_ctx_t *ctx,
                               uint16_t x,
                               uint16_t y,
                               uint16_t width,
                               uint16_t height,
                               uint8_t color)
{
    for (uint16_t i = 0; i < height; i++)
        for (uint16_t j = 0; j < width; j++)
            epd_2in9_draw_pixel(ctx, x + j, y + i, color);
}

/*
 * Draw one glyph (FONT_WIDTH column bytes, bit 0 = top row) at (x, y).
 */
static void epd_2in9_draw_glyph(const epd_ctx_t *ctx,
                                uint16_t x,
                                uint16_t y,
                                const uint8_t *glyph,
                                uint8_t color)
{
    for (uint8_t glyph_x = 0; glyph_x < FONT_WIDTH; glyph_x++)
    {
        const uint8_t line = glyph[glyph_x];
        for (uint8_t glyph_y = 0; glyph_y < FONT_HEIGHT; glyph_y++)
            if (line & (1u << glyph_y))
                epd_2in9_draw_pixel(ctx, x + glyph_x, y + glyph_y, color);
    }
}

/* Single-byte API: the byte is treated as a Latin-1 codepoint, not UTF-8. */
void epd_2in9_draw_char(const epd_ctx_t *ctx,
                        uint16_t x,
                        uint16_t y,
                        char c,
                        uint8_t color)
{
    epd_2in9_draw_glyph(ctx, x, y, font_get_glyph(c), color);
}

/* The string is UTF-8, so umlauts, ß, quotes and € draw as single glyphs. */
void epd_2in9_draw_str(const epd_ctx_t *ctx,
                       uint16_t x,
                       uint16_t y,
                       const char *str,
                       uint8_t color)
{
    uint16_t cur_x = x;
    for (;;)
    {
        uint32_t codepoint;
        const size_t consumed = font_utf8_decode(str, &codepoint);
        if (consumed == 0)
            break;
        epd_2in9_draw_glyph(ctx, cur_x, y, font_get_glyph_cp(codepoint), color);
        cur_x += FONT_ADVANCE;
        str += consumed;
    }
}
