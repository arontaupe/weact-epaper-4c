/**
 * WeAct 2.9" 4-color e-paper (black / white / yellow / red), JD79667, 168 x 384 px.
 * Init sequence and data format follow the GxEPD2 driver for GDEY029F51H.
 * Streams a text frame directly over SPI, so it does not allocate a full
 * 16 KB framebuffer in the MakeCode heap.
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

    let csPin: DigitalPin = DigitalPin.P0
    let dcPin: DigitalPin = DigitalPin.P1
    let rstPin: DigitalPin = DigitalPin.P2
    let busyPin: DigitalPin = DigitalPin.P3
    let rotation = 0
    let background: Color = Color.White
    let textValue = ""
    let textX = 0
    let textY = 0
    let textColor: Color = Color.Black
    let textSize = 1
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

    function textPixel(x: number, y: number): Color {
        let cx = textX
        let cy = textY
        for (let n = 0; n < textValue.length; n++) {
            let ch = textValue.charCodeAt(n)
            if (ch == 10) {
                cx = textX
                cy += 8 * textSize
                continue
            }
            if (ch < 32 || ch > 126) ch = 63
            if (x >= cx && x < cx + 5 * textSize && y >= cy && y < cy + 7 * textSize) {
                let col = (x - cx) / textSize >> 0
                let row = (y - cy) / textSize >> 0
                let bits = font5x7.getUint8((ch - 32) * 5 + col)
                if ((bits >> row) & 1) return textColor
            }
            cx += 6 * textSize
        }
        return background
    }

    function logicalPixel(nativeX: number, nativeY: number): Color {
        let x = nativeX
        let y = nativeY
        if (rotation == 1) {
            x = nativeY
            y = NATIVE_W - 1 - nativeX
        } else if (rotation == 2) {
            x = NATIVE_W - 1 - nativeX
            y = NATIVE_H - 1 - nativeY
        } else if (rotation == 3) {
            x = NATIVE_H - 1 - nativeY
            y = nativeX
        }
        return textPixel(x, y)
    }

    function streamByte(nativeX: number, nativeY: number) {
        let value = logicalPixel(nativeX, nativeY) << 6
        value |= logicalPixel(nativeX + 1, nativeY) << 4
        value |= logicalPixel(nativeX + 2, nativeY) << 2
        value |= logicalPixel(nativeX + 3, nativeY)
        pins.spiWrite(value)
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
        background = color
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
        textValue = text
        textX = x
        textY = y
        textColor = color
        textSize = size
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
        hwReset()
        initController()
        setWindow(1)
        cmd(0x10)
        pins.digitalWritePin(dcPin, 1)
        pins.digitalWritePin(csPin, 0)
        for (let y = 0; y < NATIVE_H; y++) {
            for (let x = 0; x < NATIVE_W; x += 4) {
                streamByte(x, y)
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