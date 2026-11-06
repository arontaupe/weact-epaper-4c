# WeAct 2.9" 4-color e-paper for Calliope mini

MakeCode extension for the WeAct 2.9" black / white / yellow / red SPI e-paper
module (controller JD79667, 168 x 384 px).

> **Status: 0.1.0, not yet tested on real hardware.** The init sequence and pixel
> format come from the GxEPD2 driver for the GDEY029F51H panel (same controller and
> resolution). The code was checked in a simulation only.

## Requirements

- Calliope mini **2.x or 3**.
- The display runs on **3.3 V only**, power and data.

## Wiring

Use the Calliope mini V3 pinout reference: [official technical details and
pinout](https://calliope.cc/en/calliope-mini/tech-facts?version=v3), including the
[official V3 pinout legend SVG](https://calliope.cc/media/pages/calliope-mini/technische-daten/c40cb48fa1-1697810645/calliopev3_legende.svg).
The display
uses 3.3 V logic, so do not connect it to the Calliope mini `5V` pin.

For the pin assignments used by the example in this extension, connect every
wire as follows:

| WeAct e-paper pin | Calliope mini V3 pin | Purpose |
| --- | --- | --- |
| VCC | `3V` | 3.3 V power |
| GND | `GND` | Ground |
| SCL / CLK | `P13` | SPI clock (`SCK`) |
| SDA / DIN | `P15` | SPI data from Calliope (`MOSI`) |
| CS | `P0` | Chip select |
| D/C | `P1` | Data/command select |
| RES | `P2` | Display reset |
| BUSY | `P3` | Display busy status |

`P14` (`MISO`) is not connected because this display is write-only. On the
Calliope mini header, `SCL` and `SDA` on the e-paper module mean SPI clock and
SPI data here; they are not an I2C connection. Keep the four control pins
consistent with the `epaper.connect(CS, D/C, RES, BUSY)` block below.

If you choose different control pins, pass those pins to `epaper.connect`.
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

`refresh display` streams the text frame to the panel and takes **20 to 25 seconds**.
Do not refresh more often than every few minutes.

## Colors

black, white, yellow, red. Text is ASCII only (5x7 font, scale 1 to 8).

## Credits

The 5x7 font is the classic font from Adafruit-GFX-Library (BSD license,
Copyright (c) 2012 Adafruit Industries). Init sequence derived from GxEPD2
by Jean-Marc Zingg (GPL-3.0), so check licensing before publishing.

## Supported targets

- for PXT/calliope

<https://calliope.cc/media/pages/calliope-mini/technische-daten/b18aad5ac3-1695728195/calliopev3_front.svg>
