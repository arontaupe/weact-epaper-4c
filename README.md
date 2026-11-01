# WeAct 2.9" 4-color e-paper for Calliope mini

MakeCode extension for the WeAct 2.9" black / white / yellow / red SPI e-paper
module (controller JD79667, 168 x 384 px).

> **Status: 0.1.0, not yet tested on real hardware.** The init sequence and pixel
> format come from the GxEPD2 driver for the GDEY029F51H panel (same controller and
> resolution). The code was checked in a simulation only.

## Requirements
- Calliope mini **2.x or 3** (the frame buffer needs 16 KB of RAM; the mini 1 is too small).
- The display runs on **3.3 V only**, power and data.

## Wiring
| Display | Calliope mini |
|---|---|
| VCC | 3V |
| GND | GND |
| SCL (clock) | SPI clock pin |
| SDA (data) | SPI MOSI pin |
| CS | any free pin (e.g. P0) |
| D/C | any free pin (e.g. P1) |
| RES | any free pin (e.g. P2) |
| BUSY | any free pin (e.g. P3) |

If your SPI wires are not on the default SPI pins, set them first with the
`set SPI pins` block (Advanced > Pins).

## Blocks
```blocks
epaper.connect(DigitalPin.P0, DigitalPin.P1, DigitalPin.P2, DigitalPin.P3)
epaper.setRotation(epaper.Rotation.Landscape)
epaper.clear(epaper.Color.White)
epaper.drawText("Hello!", 10, 10, epaper.Color.Red, 3)
epaper.update()
```
Drawing only changes the memory buffer. `refresh display` sends it to the panel
and takes **20 to 25 seconds**. Do not refresh more often than every few minutes.

## Colors
black, white, yellow, red. Text is ASCII only (5x7 font, scale 1 to 8).

## Credits
The 5x7 font is the classic font from Adafruit-GFX-Library (BSD license,
Copyright (c) 2012 Adafruit Industries). Init sequence derived from GxEPD2
by Jean-Marc Zingg (GPL-3.0), so check licensing before publishing.

## Supported targets
* for PXT/calliope
