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

#include <stdint.h>
#include <stddef.h>
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

void epd_utils_wait_until_idle(const epd_ctx_t *ctx)
{
    while (gpio_get(ctx->pins.busy) == 0)
        sleep_ms(10);
}
