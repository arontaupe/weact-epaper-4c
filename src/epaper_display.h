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

#ifndef EPAPER_DISPLAY_H_
#define EPAPER_DISPLAY_H_ 1

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Color definitions for an E-Paper Display.
 */
enum EEpdColors
{
    EPD_COLOR_BLACK = 0,
    EPD_COLOR_WHITE = 1,
    EPD_COLOR_YELLOW = 2,
    EPD_COLOR_RED = 3,
};

/*
 * Enumeration with all currently supported display models.
 */
enum EEpdModels
{
    EPD_MODEL_2IN9,
};

/*----------------------------------------------------------------------------*/

typedef struct epd_pin_config epd_pin_config_t;
typedef struct epd_display_funcs epd_display_funcs_t;
typedef struct epd_ctx epd_ctx_t;

/*
 * Structure containing the pins of the display.
 */
struct epd_pin_config
{
    uint8_t sck;
    uint8_t mosi;
    uint8_t cs;
    uint8_t dc;
    uint8_t res;
    uint8_t busy;
};

/*
 * Structure containing all model-specific functions for an E-Paper Display.
 */
struct epd_display_funcs
{
    /* Initialization functions */
    bool (*init_display)(const epd_ctx_t *ctx);

    /* Control functions */
    void (*reset)(const epd_ctx_t *ctx);
    void (*flush)(const epd_ctx_t *ctx);
    void (*sleep)(const epd_ctx_t *ctx);

    /* Drawing functions */
    void (*clear)(const epd_ctx_t *ctx, uint8_t color);
    void (*draw_pixel)(const epd_ctx_t *ctx,
                       uint16_t x,
                       uint16_t y,
                       uint8_t color);
    void (*draw_line)(const epd_ctx_t *ctx,
                      uint16_t x0,
                      uint16_t y0,
                      uint16_t x1,
                      uint16_t y1,
                      uint8_t color);
    void (*draw_rect)(const epd_ctx_t *ctx,
                      uint16_t x,
                      uint16_t y,
                      uint16_t width,
                      uint16_t height,
                      uint8_t color);
    void (*draw_filled_rect)(const epd_ctx_t *ctx,
                             uint16_t x,
                             uint16_t y,
                             uint16_t width,
                             uint16_t height,
                             uint8_t color);
    void (*draw_char)(const epd_ctx_t *ctx,
                      uint16_t x,
                      uint16_t y,
                      char c,
                      uint8_t color);
    void (*draw_str)(const epd_ctx_t *ctx,
                     uint16_t x,
                     uint16_t y,
                     const char *str,
                     uint8_t color);
};

/*
 * Structure containing all context information for drawing in an E-Paper
 * Display.
 */
struct epd_ctx
{
    /* SPI pins used for accessing the display, independently of the model */
    epd_pin_config_t pins;

    /* Identificator for the display model of the current context */
    enum EEpdModels model;

    /*
     * Width and height of the display. Assigned in 'epd_init', depending on the
     * selected model.
     */
    size_t width, height;

    /*
     * Framebuffer with packed pixel information. Allocated dynamically in
     * 'epd_init', depending on the selected model.
     */
    uint8_t *framebuffer;

    /* Size of the allocated framebuffer, in bytes */
    size_t framebuffer_size;

    /*
     * List of functions that affect this specific model. Assigned in
     * 'epd_init', depending on the selected model.
     */
    epd_display_funcs_t display_funcs;
};

/*----------------------------------------------------------------------------*/

/*
 * Initialize an E-Paper Display context with the specific SPI pin
 * configuration, and the specific board model.
 */
bool epd_init(epd_ctx_t *ctx,
              const epd_pin_config_t *pin_config,
              enum EEpdModels model);

/*
 * Reset the display
 */
static inline void epd_reset(const epd_ctx_t *ctx)
{
    ctx->display_funcs.reset(ctx);
}

/*
 * Update the display with current framebuffer content
 */
static inline void epd_flush(const epd_ctx_t *ctx)
{
    ctx->display_funcs.flush(ctx);
}

/*
 * Put display into deep sleep mode
 */
static inline void epd_sleep(const epd_ctx_t *ctx)
{
    ctx->display_funcs.sleep(ctx);
}

/*
 * Clear the display to specified color
 *
 * FIXME: Use 'enum EEpdColors' instead of 'uint8_t'.
 */
static inline void epd_clear(const epd_ctx_t *ctx, uint8_t color)
{
    ctx->display_funcs.clear(ctx, color);
}

/*
 * Set a pixel at (x, y) to specified color
 */
static inline void epd_draw_pixel(const epd_ctx_t *ctx,
                                  uint16_t x,
                                  uint16_t y,
                                  uint8_t color)
{
    ctx->display_funcs.draw_pixel(ctx, x, y, color);
}

/*
 * Draw a line from (x0, y0) to (x1, y1)
 */
static inline void epd_draw_line(const epd_ctx_t *ctx,
                                 uint16_t x0,
                                 uint16_t y0,
                                 uint16_t x1,
                                 uint16_t y1,
                                 uint8_t color)
{
    ctx->display_funcs.draw_line(ctx, x0, y0, x1, y1, color);
}

/*
 * Draw a rectangle
 */
static inline void epd_draw_rect(const epd_ctx_t *ctx,
                                 uint16_t x,
                                 uint16_t y,
                                 uint16_t width,
                                 uint16_t height,
                                 uint8_t color)
{
    ctx->display_funcs.draw_rect(ctx, x, y, width, height, color);
}

/*
 * Draw a filled rectangle
 */
static inline void epd_draw_filled_rect(const epd_ctx_t *ctx,
                                        uint16_t x,
                                        uint16_t y,
                                        uint16_t width,
                                        uint16_t height,
                                        uint8_t color)
{
    ctx->display_funcs.draw_filled_rect(ctx, x, y, width, height, color);
}

/*
 * Draw a character at (x, y)
 */
static inline void epd_draw_char(const epd_ctx_t *ctx,
                                 uint16_t x,
                                 uint16_t y,
                                 char c,
                                 uint8_t color)
{
    ctx->display_funcs.draw_char(ctx, x, y, c, color);
}

/*
 * Draw a string at (x, y)
 */
static inline void epd_draw_str(const epd_ctx_t *ctx,
                                uint16_t x,
                                uint16_t y,
                                const char *str,
                                uint8_t color)
{
    ctx->display_funcs.draw_str(ctx, x, y, str, color);
}

#endif /* EPAPER_DISPLAY_H_ */
