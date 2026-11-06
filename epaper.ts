/**
 * WeAct 2.9" 4-color e-paper (black / white / yellow / red), JD79667, 168 x 384 px.
 * Init sequence and data format follow the GxEPD2 driver for GDEY029F51H.
 * Needs a Calliope mini with enough RAM for a 16 KB frame buffer (mini 2 / mini 3).
 * The frame buffer is split into small strips, because one 16 KB allocation
 * fails on the MakeCode heap (error 022).
 */
//% color="#2E7D32" weight=60 icon="\uf109" block="E-Paper"
namespace epaper {
    export enum Color {
        //% block="black"
        Black = 0,
        //% block="white"
        White = 1,
        //% block="yellow"
        Yellow = 2,
        //% block="red"
        Red = 3
    }

    export enum Rotation {
        //% block="portrait"
        Portrait = 0,
        //% block="landscape"
        Landscape = 1,
        //% block="portrait (upside down)"
        PortraitFlipped = 2,
        //% block="landscape (upside down)"
        LandscapeFlipped = 3
    }

    // native panel size
    const NATIVE_W = 168
    const NATIVE_H = 384
    const ROW_BYTES = 42 // 168 px / 4 px per byte

    // Keep each allocation small enough for the MakeCode heap.
    const STRIP_ROWS = 8
    const STRIP_BYTES = ROW_BYTES * STRIP_ROWS // 336
    const NUM_STRIPS = NATIVE_H / STRIP_ROWS // 48

    let csPin: DigitalPin = DigitalPin.P8
    let dcPin: DigitalPin = DigitalPin.P9
    let rstPin: DigitalPin = DigitalPin.P10
    let busyPin: DigitalPin = DigitalPin.P12
    let rotation = 0
    let strips: Buffer[] = null
    let ready = false

    // ---------------------------------------------------------------- low level

    function cmd(c: number) {
        pins.digitalWritePin(dcPin, 0)
        pins.digitalWritePin(csPin, 0)
        pins.spiWrite(c)
        pins.digitalWritePin(csPin, 1)
    }

    function dat(d: number) {
        pins.digitalWritePin(dcPin, 1)
        pins.digitalWritePin(csPin, 0)
        pins.spiWrite(d)
        pins.digitalWritePin(csPin, 1)
    }

    // BUSY is active LOW on this panel: wait until the pin goes high again (max ~40 s)
    function waitBusy() {
        basic.pause(2)
        let n = 0
        while (pins.digitalReadPin(busyPin) == 0 && n < 4000) {
            basic.pause(10)
            n++
        }
    }

    function hwReset() {
        pins.digitalWritePin(rstPin, 1)
        basic.pause(20)
        pins.digitalWritePin(rstPin, 0)
        basic.pause(2)
        pins.digitalWritePin(rstPin, 1)
        basic.pause(2)
        waitBusy()
    }

    function setWindow(partial: number) {
        cmd(0x83)
        dat(0)
        dat(0)
        dat((NATIVE_W - 1) >> 8)
        dat((NATIVE_W - 1) & 255)
        dat(0)
        dat(0)
        dat((NATIVE_H - 1) >> 8)
        dat((NATIVE_H - 1) & 255)
        dat(partial)
    }

    function initController() {
        cmd(0x4D); dat(0x78)
        cmd(0x00); dat(0x0F); dat(0x29)             // panel setting
        cmd(0x01); dat(0x07); dat(0x00)             // power setting
        cmd(0x03); dat(0x10); dat(0x54); dat(0x44)  // power off sequence
        cmd(0x06)                                   // booster soft start
        dat(0x05); dat(0x00); dat(0x3F); dat(0x0A); dat(0x25); dat(0x12); dat(0x1A)
        cmd(0x50); dat(0x37)                        // VCOM and data interval
        cmd(0x60); dat(0x02); dat(0x02)             // TCON
        cmd(0x61)                                   // resolution
        dat(NATIVE_W >> 8); dat(NATIVE_W & 255)
        dat(NATIVE_H >> 8); dat(NATIVE_H & 255)
        cmd(0xE7); dat(0x1C)
        cmd(0xE3); dat(0x22)
        cmd(0xB4); dat(0xD0)
        cmd(0xB5); dat(0x03)
        cmd(0xE9); dat(0x01)
        cmd(0x30); dat(0x08)                        // PLL
        cmd(0x04)                                   // power on
        waitBusy()
    }

    function ensureBuffer() {
        if (!strips) {
            strips = []
            for (let i = 0; i < NUM_STRIPS; i++) {
                let s = control.createBuffer(STRIP_BYTES)
                s.fill(0x55) // white
                strips.push(s)
            }
        }
    }

    function putNative(px: number, py: number, c: number) {
        let s = strips[py >> 3]
        let idx = (py & 7) * ROW_BYTES + (px >> 2)
        let shift = 6 - 2 * (px & 3)
        let b = s.getUint8(idx)
        b = (b & (255 - (3 << shift))) | (c << shift)
        s.setUint8(idx, b)
    }

    // ---------------------------------------------------------------- setup

    /**
     * Connect the e-paper display. Wire the SPI clock (SCL) and data (SDA) pins
     * to the Calliope SPI pins (see README), and the four pins below to any free pins.
     * The display runs on 3.3 V only!
     * @param cs chip select (CS)
     * @param dc data/command (D/C)
     * @param rst reset (RES)
     * @param busy busy (BUSY)
     */
    //% blockId=epaper_connect block="e-paper connect|CS %cs|D/C %dc|RES %rst|BUSY %busy"
    //% weight=100 group="Setup"
    export function connect(cs: DigitalPin, dc: DigitalPin, rst: DigitalPin, busy: DigitalPin) {
        csPin = cs
        dcPin = dc
        rstPin = rst
        busyPin = busy
        pins.digitalWritePin(csPin, 1)
        pins.spiFormat(8, 0)
        pins.spiFrequency(4000000)
        ensureBuffer()
        ready = true
    }

    /**
     * Choose the orientation of your drawing area. Try the values until text reads upright.
     */
    //% blockId=epaper_rotation block="e-paper set orientation %rot"
    //% weight=90 group="Setup"
    export function setRotation(rot: Rotation) {
        rotation = rot
    }

    /** Width of the drawing area in pixels (depends on the orientation). */
    //% blockId=epaper_width block="e-paper width"
    //% weight=80 group="Setup"
    export function width(): number {
        return (rotation & 1) == 0 ? NATIVE_W : NATIVE_H
    }

    /** Height of the drawing area in pixels (depends on the orientation). */
    //% blockId=epaper_height block="e-paper height"
    //% weight=79 group="Setup"
    export function height(): number {
        return (rotation & 1) == 0 ? NATIVE_H : NATIVE_W
    }

    // ---------------------------------------------------------------- drawing

    /** Fill the whole drawing area with one color (only in memory until you call "refresh"). */
    //% blockId=epaper_clear block="e-paper clear with %color"
    //% weight=70 group="Draw"
    export function clear(color: Color) {
        ensureBuffer()
        for (let i = 0; i < NUM_STRIPS; i++) {
            strips[i].fill(color * 0x55)
        }
    }

    /** Set one pixel. */
    //% blockId=epaper_setpixel block="e-paper plot x %x y %y color %color"
    //% weight=65 group="Draw"
    export function setPixel(x: number, y: number, color: Color) {
        ensureBuffer()
        if (x < 0 || y < 0 || x >= width() || y >= height()) return
        let px = x
        let py = y
        if (rotation == 1) {
            px = NATIVE_W - 1 - y
            py = x
        } else if (rotation == 2) {
            px = NATIVE_W - 1 - x
            py = NATIVE_H - 1 - y
        } else if (rotation == 3) {
            px = y
            py = NATIVE_H - 1 - x
        }
        putNative(px, py, color)
    }

    /** Draw a line. */
    //% blockId=epaper_line block="e-paper line from x %x0 y %y0 to x %x1 y %y1 color %color"
    //% weight=60 group="Draw"
    export function drawLine(x0: number, y0: number, x1: number, y1: number, color: Color) {
        let dx = Math.abs(x1 - x0)
        let dy = -Math.abs(y1 - y0)
        let sx = x0 < x1 ? 1 : -1
        let sy = y0 < y1 ? 1 : -1
        let err = dx + dy
        while (true) {
            setPixel(x0, y0, color)
            if (x0 == x1 && y0 == y1) break
            let e2 = 2 * err
            if (e2 >= dy) {
                err += dy
                x0 += sx
            }
            if (e2 <= dx) {
                err += dx
                y0 += sy
            }
        }
    }

    /** Draw a rectangle outline. */
    //% blockId=epaper_rect block="e-paper rectangle x %x y %y width %w height %h color %color"
    //% weight=55 group="Draw"
    export function drawRect(x: number, y: number, w: number, h: number, color: Color) {
        if (w <= 0 || h <= 0) return
        drawLine(x, y, x + w - 1, y, color)
        drawLine(x, y + h - 1, x + w - 1, y + h - 1, color)
        drawLine(x, y, x, y + h - 1, color)
        drawLine(x + w - 1, y, x + w - 1, y + h - 1, color)
    }

    /** Draw a filled rectangle. */
    //% blockId=epaper_fillrect block="e-paper filled rectangle x %x y %y width %w height %h color %color"
    //% weight=54 group="Draw"
    export function fillRect(x: number, y: number, w: number, h: number, color: Color) {
        for (let j = 0; j < h; j++) {
            for (let i = 0; i < w; i++) {
                setPixel(x + i, y + j, color)
            }
        }
    }

    /** Draw a circle outline around the center (cx, cy). */
    //% blockId=epaper_circle block="e-paper circle center x %cx y %cy radius %r color %color"
    //% weight=50 group="Draw"
    export function drawCircle(cx: number, cy: number, r: number, color: Color) {
        let x = r
        let y = 0
        let err = 1 - r
        while (x >= y) {
            setPixel(cx + x, cy + y, color)
            setPixel(cx + y, cy + x, color)
            setPixel(cx - y, cy + x, color)
            setPixel(cx - x, cy + y, color)
            setPixel(cx - x, cy - y, color)
            setPixel(cx - y, cy - x, color)
            setPixel(cx + y, cy - x, color)
            setPixel(cx + x, cy - y, color)
            y++
            if (err < 0) {
                err += 2 * y + 1
            } else {
                x--
                err += 2 * (y - x) + 1
            }
        }
    }

    /** Draw a filled circle around the center (cx, cy). */
    //% blockId=epaper_fillcircle block="e-paper filled circle center x %cx y %cy radius %r color %color"
    //% weight=49 group="Draw"
    export function fillCircle(cx: number, cy: number, r: number, color: Color) {
        for (let j = -r; j <= r; j++) {
            for (let i = -r; i <= r; i++) {
                if (i * i + j * j <= r * r) {
                    setPixel(cx + i, cy + j, color)
                }
            }
        }
    }

    /**
     * Draw text (ASCII only) with the top-left corner at x, y.
     * Size 1 = 5x7 pixels per letter, 2 = double, and so on. "\n" starts a new line.
     * @param size scale factor, eg: 1
     */
    //% blockId=epaper_text block="e-paper text %text|x %x y %y color %color size %size"
    //% size.min=1 size.max=8 size.defl=1
    //% weight=45 group="Draw"
    export function drawText(text: string, x: number, y: number, color: Color, size: number = 1) {
        if (size < 1) size = 1
        let cx = x
        let cy = y
        for (let n = 0; n < text.length; n++) {
            let ch = text.charCodeAt(n)
            if (ch == 10) {
                cx = x
                cy += 8 * size
                continue
            }
            if (ch < 32 || ch > 126) ch = 63 // "?"
            let base = (ch - 32) * 5
            for (let col = 0; col < 5; col++) {
                let bits = font5x7.getUint8(base + col)
                for (let row = 0; row < 7; row++) {
                    if ((bits >> row) & 1) {
                        if (size == 1) {
                            setPixel(cx + col, cy + row, color)
                        } else {
                            fillRect(cx + col * size, cy + row * size, size, size, color)
                        }
                    }
                }
            }
            cx += 6 * size
        }
    }

    // ---------------------------------------------------------------- output

    /**
     * Send the drawing to the display and refresh it.
     * This takes about 20 to 25 seconds, the screen flickers in all colors meanwhile.
     */
    //% blockId=epaper_update block="e-paper refresh display"
    //% weight=40 group="Display"
    export function update() {
        if (!ready) return
        ensureBuffer()
        hwReset()
        initController()
        setWindow(1)
        cmd(0x10)
        pins.digitalWritePin(dcPin, 1)
        pins.digitalWritePin(csPin, 0)
        for (let s = 0; s < NUM_STRIPS; s++) {
            let b = strips[s]
            for (let i = 0; i < STRIP_BYTES; i++) {
                pins.spiWrite(b.getUint8(i))
            }
        }
        pins.digitalWritePin(csPin, 1)
        setWindow(0)
        cmd(0x50); dat(0x37)
        cmd(0x12); dat(0x00) // display refresh
        basic.pause(1)
        waitBusy()
        cmd(0x02); dat(0x00) // power off the panel voltages
        waitBusy()
    }

    /**
     * Put the display controller into deep sleep. The picture stays visible.
     * The next "refresh display" wakes it up automatically.
     */
    //% blockId=epaper_sleep block="e-paper deep sleep"
    //% weight=35 group="Display" advanced=true
    export function sleep() {
        if (!ready) return
        cmd(0x07); dat(0xA5)
    }
}