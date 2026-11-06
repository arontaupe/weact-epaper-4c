/**
 * WeAct 2.9" 4-color e-paper (black / white / yellow / red), JD79667, 168 x 384 px.
 * Init sequence and data format follow the GxEPD2 driver for GDEY029F51H.
 * Needs a Calliope mini with enough RAM for a 16 KB frame buffer (mini 2 / mini 3).
 * The frame buffer uses one compact 16 KB allocation to avoid the overhead of
 * many smaller MakeCode heap allocations.
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

    const BUF_SIZE = ROW_BYTES * NATIVE_H // 16128 bytes, 2 bits per pixel

    let csPin: DigitalPin = DigitalPin.P0
    let dcPin: DigitalPin = DigitalPin.P1
    let rstPin: DigitalPin = DigitalPin.P2
    let busyPin: DigitalPin = DigitalPin.P3
    let rotation = 0
    let buf: Buffer = null
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
        if (!buf) {
            buf = control.createBuffer(BUF_SIZE)
            buf.fill(0x55) // white
        }
    }

    function putNative(px: number, py: number, c: number) {
        let idx = py * ROW_BYTES + (px >> 2)
        let shift = 6 - 2 * (px & 3)
        let b = buf.getUint8(idx)
        b = (b & (255 - (3 << shift))) | (c << shift)
        buf.setUint8(idx, b)
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
        buf.fill(color * 0x55)
    }

    /** Set one pixel. */
    function setPixel(x: number, y: number, color: Color) {
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
                        for (let sy = 0; sy < size; sy++) {
                            for (let sx = 0; sx < size; sx++) {
                                setPixel(cx + col * size + sx, cy + row * size + sy, color)
                            }
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
        for (let i = 0; i < BUF_SIZE; i++) {
            pins.spiWrite(buf.getUint8(i))
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