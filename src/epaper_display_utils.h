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

#ifndef EPAPER_DISPLAY_UTILS_H_
#define EPAPER_DISPLAY_UTILS_H_ 1

#include <stddef.h>
#include <stdio.h>

#include "epaper_display.h"

/*
 * Log a message to standard output.
 */
#define EPD_LOG(...)                                                           \
    do {                                                                       \
        printf("[EPD] ");                                                      \
        printf(__VA_ARGS__);                                                   \
        putchar('\n');                                                         \
    } while (0)

/*----------------------------------------------------------------------------*/

/*
 * Write the specified byte array to through the SPI pins associated to the
 * specified E-Paper Display context.
 */
void epd_utils_spi_write(const epd_ctx_t* ctx, const uint8_t* data, size_t len);

/*
 * Write the specified command byte to through the SPI pins associated to the
 * specified E-Paper Display context.
 */
void epd_utils_send_command(const epd_ctx_t* ctx, uint8_t cmd);

/*
 * Write the specified data byte to through the SPI pins associated to the
 * specified E-Paper Display context.
 *
 * FIXME: Rename to 'epd_utils_send_data_byte'.
 */
void epd_utils_send_data(const epd_ctx_t* ctx, uint8_t data);

/*
 * Write the specified data buffer to through the SPI pins associated to the
 * specified E-Paper Display context.
 */
void epd_utils_send_data_buffer(const epd_ctx_t* ctx,
                                const uint8_t* data,
                                size_t len);

/*
 * Wait until the display associated to the specified E-Paper Display context is
 * no longer busy. That is, while the stored "busy" pin is 1.
 */
void epd_utils_wait_until_idle(const epd_ctx_t* ctx);

#endif /* EPAPER_DISPLAY_UTILS_H_ */
