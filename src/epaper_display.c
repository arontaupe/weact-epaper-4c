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

#include "epaper_display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#include "epaper_display_2in9.h"
#include "epaper_display_utils.h"

/*----------------------------------------------------------------------------*/

/*
 * The driver always uses the SPI1 controller. Pins can only be routed to it
 * from fixed GPIOs: SCK on 10, 14 or 26, and MOSI (TX) on 11, 15 or 27.
 * (RP2040 and RP2350A; RP2350B's extra GPIOs are not covered here.)
 */
static bool epd_spi1_pins_valid(const epd_pin_config_t *pins)
{
    const bool sck_ok = pins->sck == 10 || pins->sck == 14 || pins->sck == 26;
    const bool mosi_ok =
        pins->mosi == 11 || pins->mosi == 15 || pins->mosi == 27;
    return sck_ok && mosi_ok;
}

/*
 * Initialize the SPI pins associated to the specified E-Paper Display context.
 */
static bool epd_init_spi(const epd_ctx_t *ctx)
{
    if (!epd_spi1_pins_valid(&ctx->pins))
    {
        EPD_LOG("SCK (%u) / MOSI (%u) are not valid SPI1 pins. "
                "Use SCK 10/14/26 with MOSI 11/15/27.",
                (unsigned)ctx->pins.sck,
                (unsigned)ctx->pins.mosi);
        return false;
    }

    /*
     * Initialize the SPI1 controller with the specified baud rate.
     */
    spi_init(spi1, 4000000);

    /*
     * Initialize the clock and MOSI pins as SPI, binding them to the SPI1
     * controller we just initialized.
     *
     * This controller which will be used to send the actual data+commands to
     * the E-Paper Display.
     */
    gpio_set_function(ctx->pins.sck, GPIO_FUNC_SPI);
    gpio_set_function(ctx->pins.mosi, GPIO_FUNC_SPI);

    /* Initialize chip select pin */
    gpio_init(ctx->pins.cs);
    gpio_set_dir(ctx->pins.cs, GPIO_OUT);
    gpio_put(ctx->pins.cs, 1);

    /*
     * Initialize Data/Command (D/C) pin.
     *
     * We will set this pin high or low to let the display module know whether
     * or not the information we are sending through SPI is data (high) or a
     * command (low).
     */
    gpio_init(ctx->pins.dc);
    gpio_set_dir(ctx->pins.dc, GPIO_OUT);

    /*
     * Initialize reset pin. The display resets if this pin changes from high to
     * low.
     */
    gpio_init(ctx->pins.res);
    gpio_set_dir(ctx->pins.res, GPIO_OUT);

    /*
     * Initialize busy pin. The display holds this pin low whenever it's busy
     * (refreshing, powering on/off) and releases it high when it can accept
     * data/commands again. See 'epd_utils_wait_until_idle'.
     */
    gpio_init(ctx->pins.busy);
    gpio_set_dir(ctx->pins.busy, GPIO_IN);

    return true;
}

/*
 * Initialize the device-specific display functions depending on the stored
 * model.
 */
static bool epd_init_model_properties(epd_ctx_t *ctx)
{
    switch (ctx->model)
    {
    case EPD_MODEL_2IN9:
    {
        ctx->width = 168;
        ctx->height = 384;

        /* The 4-color display stores two bits per pixel. */
        ctx->framebuffer_size = ctx->width * ctx->height / 4;
        ctx->framebuffer = malloc(ctx->framebuffer_size);
        if (ctx->framebuffer == NULL)
            return false;

        ctx->display_funcs.init_display = epd_2in9_init_display;
        ctx->display_funcs.reset = epd_2in9_reset;
        ctx->display_funcs.flush = epd_2in9_flush;
        ctx->display_funcs.sleep = epd_2in9_sleep;
        ctx->display_funcs.clear = epd_2in9_clear;
        ctx->display_funcs.draw_pixel = epd_2in9_draw_pixel;
        ctx->display_funcs.draw_line = epd_2in9_draw_line;
        ctx->display_funcs.draw_rect = epd_2in9_draw_rect;
        ctx->display_funcs.draw_filled_rect = epd_2in9_draw_filled_rect;
        ctx->display_funcs.draw_char = epd_2in9_draw_char;
        ctx->display_funcs.draw_str = epd_2in9_draw_str;
    }
    break;

    default:
    {
        EPD_LOG("Unsupported 'model' member (%d).", ctx->model);
        return false;
    }
    }

    return true;
}

/*----------------------------------------------------------------------------*/

bool epd_init(epd_ctx_t *ctx,
              const epd_pin_config_t *pin_config,
              enum EEpdModels model)
{
    /*
     * Copy the user pin configuration, and initialize the pins.
     */
    memcpy(&ctx->pins, pin_config, sizeof(epd_pin_config_t));
    if (!epd_init_spi(ctx))
        return false;

    /*
     * Assign the current display model, and assign its specific data/functions
     * in the context.
     */
    ctx->model = model;
    if (!epd_init_model_properties(ctx))
        return false;

    /*
     * Initialize the specific display model.
     */
    if (!ctx->display_funcs.init_display(ctx))
    {
        epd_deinit(ctx);
        return false;
    }

    return true;
}

void epd_deinit(epd_ctx_t *ctx)
{
    if (ctx == NULL)
        return;

    free(ctx->framebuffer);
    ctx->framebuffer = NULL;
    ctx->framebuffer_size = 0;
    spi_deinit(spi1);
}
