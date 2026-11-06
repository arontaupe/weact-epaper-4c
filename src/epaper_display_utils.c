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

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#include "epaper_display.h"

void epd_utils_spi_write(const epd_ctx_t *ctx,
                         const uint8_t *data,
                         size_t len)
{
    gpio_put(ctx->pins.cs, 0);
    spi_write_blocking(spi1, data, len);
    gpio_put(ctx->pins.cs, 1);
}

void epd_utils_send_command(const epd_ctx_t *ctx, uint8_t cmd)
{
    gpio_put(ctx->pins.dc, 0);
    epd_utils_spi_write(ctx, &cmd, 1);
}

void epd_utils_send_data(const epd_ctx_t *ctx, uint8_t data)
{
    gpio_put(ctx->pins.dc, 1);
    epd_utils_spi_write(ctx, &data, 1);
}

void epd_utils_send_data_buffer(const epd_ctx_t *ctx,
                                const uint8_t *data,
                                size_t len)
{
    gpio_put(ctx->pins.dc, 1);
    epd_utils_spi_write(ctx, data, len);
}

/*
 * The panel holds BUSY low while it is working and releases it (high) when it
 * is ready for commands. A full refresh takes about 21 s, so the default
 * timeout leaves headroom without hanging forever on a wiring fault.
 */
#define EPD_BUSY_TIMEOUT_MS 30000
#define EPD_BUSY_POLL_MS 10

/*
 * Called on every poll while waiting for the panel. The default does nothing;
 * define a non-weak 'epd_idle_hook' in the application to keep other work
 * running during the long refresh (e.g. 'cyw43_arch_poll()' for lwIP).
 *
 * The hook must not draw to or flush the display.
 */
__attribute__((weak)) void epd_idle_hook(void)
{
}

/*
 * Wait until BUSY is released. Returns false (after logging) on timeout.
 */
bool epd_utils_wait_until_idle_timeout(const epd_ctx_t *ctx,
                                       uint32_t timeout_ms)
{
    const absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (gpio_get(ctx->pins.busy) == 0)
    {
        if (time_reached(deadline))
        {
            printf("EPD: timed out waiting for BUSY after %u ms "
                   "(check wiring, 3.3 V supply and BUSY pin).\n",
                   (unsigned)timeout_ms);
            return false;
        }
        epd_idle_hook();
        sleep_ms(EPD_BUSY_POLL_MS);
    }
    return true;
}

void epd_utils_wait_until_idle(const epd_ctx_t *ctx)
{
    (void)epd_utils_wait_until_idle_timeout(ctx, EPD_BUSY_TIMEOUT_MS);
}
