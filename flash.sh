#!/bin/zsh
set -euo pipefail

ROOT="${0:A:h}"
SDK_PATH="${PICO_SDK_PATH:-$HOME/pico-sdk}"
TOOLCHAIN_PATH="${PICO_TOOLCHAIN_PATH:-$HOME/arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi}"
if [[ ! -x "$TOOLCHAIN_PATH/bin/arm-none-eabi-gcc" && -x "$HOME/arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi/bin/arm-none-eabi-gcc" ]]; then
    TOOLCHAIN_PATH="$HOME/arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi"
fi
BUILD_PATH="$ROOT/build"
UF2_PATH="$BUILD_PATH/pico_epaper_4color_example.uf2"
PICO_VOLUME="/Volumes/RPI-RP2"

if [[ -f "$ROOT/.env" ]]; then
    set -a
    source "$ROOT/.env"
    set +a
fi

INITIAL_TEXT=""
INITIAL_BG=1
INITIAL_ORIENTATION=0
while (( $# )); do
    case "$1" in
        --text|--word)
            [[ $# -ge 2 ]] || { print -u2 "$1 requires a value"; exit 1; }
            INITIAL_TEXT="$2"
            shift 2
            ;;
        --bg)
            [[ $# -ge 2 ]] || { print -u2 "--bg requires a color"; exit 1; }
            case "${2:l}" in
                black) INITIAL_BG=0 ;;
                white) INITIAL_BG=1 ;;
                yellow) INITIAL_BG=2 ;;
                red) INITIAL_BG=3 ;;
                *) print -u2 "--bg must be black, white, yellow, or red"; exit 1 ;;
            esac
            shift 2
            ;;
        --orientation)
            [[ $# -ge 2 ]] || { print -u2 "--orientation requires portrait or landscape"; exit 1; }
            case "${2:l}" in
                portrait) INITIAL_ORIENTATION=0 ;;
                landscape) INITIAL_ORIENTATION=1 ;;
                *) print -u2 "--orientation must be portrait or landscape"; exit 1 ;;
            esac
            shift 2
            ;;
        -h|--help)
            print "Usage: ./flash.sh [--text WORD|--word WORD] [--bg COLOR] [--orientation portrait|landscape]"
            exit 0
            ;;
        *)
            print -u2 "Unknown option: $1"
            exit 1
            ;;
    esac
done

if [[ -z "${WIFI_SSID:-}" && -z "${WIFI_SSID_2:-}" && -z "${WIFI_SSID_3:-}" && -z "${WIFI_SSID_4:-}" ]]; then
    print -u2 "Create .env with at least one WIFI_SSID/WIFI_PASSWORD pair before flashing."
    exit 1
fi

if [[ ! -d "$SDK_PATH" ]]; then
    print -u2 "Pico SDK not found: $SDK_PATH"
    exit 1
fi

if [[ ! -x "$TOOLCHAIN_PATH/bin/arm-none-eabi-gcc" ]]; then
    print -u2 "Arm toolchain not found: $TOOLCHAIN_PATH"
    exit 1
fi

export PICO_SDK_PATH="$SDK_PATH"
export PICO_TOOLCHAIN_PATH="$TOOLCHAIN_PATH"
export PATH="$TOOLCHAIN_PATH/bin:$PATH"

print "Building Pico W firmware..."
cmake -S "$ROOT" -B "$BUILD_PATH" \
    -DPICO_SDK_PATH="$PICO_SDK_PATH" \
    -DPICO_PLATFORM=rp2040 \
    -DPICO_BOARD=pico_w \
    -DWIFI_SSID="${WIFI_SSID:-}" \
    -DWIFI_PASSWORD="${WIFI_PASSWORD:-}" \
    -DWIFI_SSID_2="${WIFI_SSID_2:-}" \
    -DWIFI_PASSWORD_2="${WIFI_PASSWORD_2:-}" \
    -DWIFI_SSID_3="${WIFI_SSID_3:-}" \
    -DWIFI_PASSWORD_3="${WIFI_PASSWORD_3:-}" \
    -DWIFI_SSID_4="${WIFI_SSID_4:-}" \
    -DWIFI_PASSWORD_4="${WIFI_PASSWORD_4:-}" \
    -DDISPLAY_TEXT="$INITIAL_TEXT" \
    -DDISPLAY_BG="$INITIAL_BG" \
    -DDISPLAY_ORIENTATION="$INITIAL_ORIENTATION" \
    -G Ninja
cmake --build "$BUILD_PATH"

if [[ ! -f "$UF2_PATH" ]]; then
    print -u2 "Build did not produce: $UF2_PATH"
    exit 1
fi

if [[ ! -d "$PICO_VOLUME" ]]; then
    print "Hold BOOTSEL while reconnecting the Pico W. Waiting up to 60 seconds..."
    for ((second = 0; second < 60; second++)); do
        [[ -d "$PICO_VOLUME" ]] && break
        sleep 1
    done
fi

if [[ ! -d "$PICO_VOLUME" ]]; then
    print -u2 "Pico BOOTSEL drive not found at $PICO_VOLUME"
    exit 1
fi

print "Flashing $UF2_PATH to $PICO_VOLUME..."
cp -X "$UF2_PATH" "$PICO_VOLUME/"
sync
print "Flash complete. The Pico should reboot automatically."