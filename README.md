# WeAct 2.9-inch 4-Color E-Paper for Pico W


License and credits

This project is licensed under the GNU General Public License, version 3 or (at your option) any later version (GPL-3.0-or-later). See LICENSE.

It builds on:

rp2350-epaper by 8dcc (GPL-3.0-or-later), the source of the e-paper driver (epaper_display*.c) and the font table. Copyright 2026 8dcc.
GxEPD2 by Jean-Marc Zingg (GPL-3.0), origin of the GDEY029F51H / JD79667 init sequence.
Adafruit-GFX-Library classic 5x7 font (BSD, Copyright (c) 2012 Adafruit Industries).

Changes made in this repository: Pico W web UI and HTTP server (main.c)


Firmware for the WeAct Studio 2.9-inch 4-color e-paper display:

- Controller: JD79667 / GDEY029F51H
- Resolution: 168 x 384 pixels
- Colors: black, white, yellow, red
- Board: Raspberry Pi Pico W (RP2040)

The Pico starts the web server automatically whenever it receives power. After
connecting to Wi-Fi, it prints its IP address over USB serial.

## Wiring

Use 3.3 V power and the following Pico W GPIOs:

| Display | Pico W | GPIO |
| --- | --- | --- |
| VCC | 3V3 (pin 36) | - |
| GND | GND (pin 38) | - |
| SDA / DIN | pin 15 | GP11 |
| SCL / CLK | pin 14 | GP10 |
| CS | pin 12 | GP9 |
| D/C | pin 11 | GP8 |
| RES | pin 16 | GP12 |
| BUSY | pin 17 | GP13 |

The display is write-only; no MISO connection is needed.

## Wi-Fi Setup

Copy the template and edit the local file:

```sh
cp .env.example .env
```

Set at least `WIFI_SSID` and `WIFI_PASSWORD`. Up to four networks can be
configured; the Pico cycles through them and retries until one connects:

```sh
WIFI_SSID=home-network
WIFI_PASSWORD=home-password
WIFI_SSID_2=phone-hotspot
WIFI_PASSWORD_2=hotspot-password
```

`.env` is ignored by git. Credentials are compiled into the firmware.

## Build And Flash

On an Apple Silicon Mac, set the SDK and Arm toolchain paths:

```sh
export PICO_SDK_PATH="$HOME/pico-sdk"
export PICO_TOOLCHAIN_PATH="$HOME/arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi"
export PATH="$PICO_TOOLCHAIN_PATH/bin:$PATH"
```

Hold BOOTSEL while connecting the Pico W, then run:

```sh
./flash.sh
```

The script builds the firmware, waits for `/Volumes/RPI-RP2`, and copies the
UF2. After that, normal USB power starts the application automatically; BOOTSEL
is only needed for future flashing.

## Boot Message

A boot message can be compiled into the firmware. It is centered and scaled to
fit the selected orientation:

```sh
./flash.sh --text friedrich --bg red --orientation landscape
```

Supported backgrounds are `black`, `white`, `yellow`, and `red`. Supported
orientations are `portrait` and `landscape`; portrait is the default.

Without `--text`, the Pico shows `Web UI:` and its assigned IP address on the
display after connecting.

## Web Editor

Open the IP address printed by the Pico over USB serial. The web editor lets
you choose:

- Text and text color
- Text size
- Background color
- Portrait or landscape orientation
- A live preview before writing to the display

The display currently performs a full-frame refresh. The 4-color panel refresh
can take several seconds and may flash during the update.

USB serial on macOS typically appears as:

```sh
screen /dev/cu.usbmodem11301 115200
```
