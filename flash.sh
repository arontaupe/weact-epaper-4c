#!/bin/zsh
#
# Build and flash the Pico W e-paper firmware.
#
#   ./flash.sh [--text WORD] [--bg COLOR] [--orientation portrait|landscape]
#              [--clean] [--build-only] [--bootsel] [--monitor] [--verbose]
#
# Settings come from .env (KEY=VALUE per line; read as data, never executed):
#   WIFI_SSID, WIFI_PASSWORD, WIFI_SSID_2..4, WIFI_PASSWORD_2..4
#   AP_SSID, AP_PASSWORD   (optional: fallback access point when no Wi-Fi works)
#   PICO_SDK_PATH, PICO_TOOLCHAIN_PATH, PICO_BOARD, PICO_PLATFORM   (optional)
# Real environment variables work for the PICO_* settings; .env wins for Wi-Fi.
set -euo pipefail

ROOT="${0:A:h}"
BUILD_PATH="$ROOT/build"
TARGET_NAME="pico_epaper_4color_example"
FLASH_VOLUMES="${FLASH_VOLUMES:-/Volumes/RPI-RP2 /Volumes/RP2350}"
FLASH_WAIT_SECONDS="${FLASH_WAIT_SECONDS:-60}"

if [[ -t 1 ]]; then
    BOLD=$'\e[1m'; RED=$'\e[31m'; GRN=$'\e[32m'; YLW=$'\e[33m'; DIM=$'\e[2m'; RST=$'\e[0m'
else
    BOLD=""; RED=""; GRN=""; YLW=""; DIM=""; RST=""
fi
info() { print -r -- "${BOLD}==>${RST} $*"; }
ok()   { print -r -- "${GRN}ok${RST}  $*"; }
warn() { print -u2 -r -- "${YLW}warning:${RST} $*"; }
die()  { print -u2 -r -- "${RED}error:${RST} $*"; exit 1; }

trap 'print -u2 ""; print -u2 "Interrupted."; exit 130' INT

usage() {
    cat <<'EOF'
Usage: ./flash.sh [options]

  --text WORD, --word WORD   text to show on boot (instead of the IP address)
  --bg COLOR                 boot background: black, white, yellow or red
  --orientation MODE         boot orientation: portrait or landscape (default: landscape)
  --clean                    delete the build directory first
  --build-only               build, do not flash
  --bootsel                  skip picotool, flash via the BOOTSEL drive
  --monitor                  open the USB serial console after flashing
  --verbose                  show the full build output
  -h, --help                 this help

Wi-Fi settings live in .env, for example:
  WIFI_SSID=My Network
  WIFI_PASSWORD='pass word with $pecial \ characters'

Fallback access point (used when none of the networks can be joined):
  AP_SSID=epaper-setup
  AP_PASSWORD=epaper1234        (8 to 63 characters)
EOF
}

# ---- arguments ---------------------------------------------------------------

INITIAL_TEXT=""
INITIAL_BG=1
INITIAL_ORIENTATION=1
CLEAN=0
BUILD_ONLY=0
FORCE_BOOTSEL=0
MONITOR=0
VERBOSE=0

while (( $# )); do
    case "$1" in
        --text|--word)
            [[ $# -ge 2 ]] || die "$1 requires a value"
            INITIAL_TEXT="$2"
            shift 2
            ;;
        --bg)
            [[ $# -ge 2 ]] || die "--bg requires a color"
            case "${2:l}" in
                black) INITIAL_BG=0 ;;
                white) INITIAL_BG=1 ;;
                yellow) INITIAL_BG=2 ;;
                red) INITIAL_BG=3 ;;
                *) die "--bg must be black, white, yellow, or red" ;;
            esac
            shift 2
            ;;
        --orientation)
            [[ $# -ge 2 ]] || die "--orientation requires portrait or landscape"
            case "${2:l}" in
                portrait) INITIAL_ORIENTATION=0 ;;
                landscape) INITIAL_ORIENTATION=1 ;;
                *) die "--orientation must be portrait or landscape" ;;
            esac
            shift 2
            ;;
        --clean) CLEAN=1; shift ;;
        --build-only) BUILD_ONLY=1; shift ;;
        --bootsel) FORCE_BOOTSEL=1; shift ;;
        --monitor) MONITOR=1; shift ;;
        --verbose|-v) VERBOSE=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) die "Unknown option: $1 (try --help)" ;;
    esac
done

# ---- .env: parsed as data, never sourced ---------------------------------------
# Accepted lines: KEY=VALUE, KEY="VALUE", KEY='VALUE', optional "export ", "#"
# comment lines, CRLF endings. Values are taken literally: no $expansion, no
# backslash escapes, no inline comments (a '#' may be part of a password). Quotes
# only protect leading and trailing spaces.

typeset -A ENV_VALUES
ENV_FILE="$ROOT/.env"

load_env() {
    setopt local_options extended_glob
    local file="$1" line key value first last n=0
    [[ -f "$file" ]] || return 0
    local mode
    if [[ "$OSTYPE" == darwin* ]]; then
        mode="$(stat -f '%Lp' "$file" 2>/dev/null || true)"
    else
        mode="$(stat -c '%a' "$file" 2>/dev/null || true)"
    fi
    if [[ -n "$mode" && "${mode[-2,-1]}" != "00" ]]; then
        warn "$file is readable by other users (it holds Wi-Fi passwords). Run: chmod 600 ${file:t}"
    fi
    while IFS= read -r line || [[ -n "$line" ]]; do
        n=$(( n + 1 ))
        line="${line%$'\r'}"
        [[ "$line" =~ '^[[:space:]]*(#.*)?$' ]] && continue
        if ! [[ "$line" =~ '^[[:space:]]*(export[[:space:]]+)?([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*=(.*)$' ]]; then
            warn "${file:t} line $n is not KEY=VALUE; ignored"
            continue
        fi
        key="${match[2]}"
        value="${match[3]}"
        value="${value##[[:space:]]#}"
        value="${value%%[[:space:]]#}"
        if (( ${#value} >= 2 )); then
            first="${value[1]}"
            last="${value[-1]}"
            if [[ "$first" == "$last" && ( "$first" == '"' || "$first" == "'" ) ]]; then
                value="${value[2,-2]}"
            fi
        fi
        case "$key" in
            WIFI_SSID|WIFI_PASSWORD|WIFI_SSID_[234]|WIFI_PASSWORD_[234]|AP_SSID|AP_PASSWORD|PICO_SDK_PATH|PICO_TOOLCHAIN_PATH|PICO_BOARD|PICO_PLATFORM)
                ENV_VALUES[$key]="$value"
                ;;
            *)
                warn "${file:t}: unknown key $key ignored"
                ;;
        esac
    done < "$file"
}

# cfg KEY [default]: .env first, then the real environment, then the default.
cfg() {
    local key="$1" default="${2-}"
    if (( ${+ENV_VALUES[$key]} )); then
        print -rn -- "${ENV_VALUES[$key]}"
    elif [[ -n "${(P)key-}" ]]; then
        print -rn -- "${(P)key}"
    else
        print -rn -- "$default"
    fi
}

# cfg_env KEY [default]: the real environment wins over .env (for tool paths).
cfg_env() {
    local key="$1" default="${2-}"
    if [[ -n "${(P)key-}" ]]; then
        print -rn -- "${(P)key}"
    elif (( ${+ENV_VALUES[$key]} )); then
        print -rn -- "${ENV_VALUES[$key]}"
    else
        print -rn -- "$default"
    fi
}

byte_length() { print -rn -- "$1" | LC_ALL=C wc -c | tr -d ' '; }

load_env "$ENV_FILE"

WIFI_SSID="$(cfg WIFI_SSID)";     WIFI_PASSWORD="$(cfg WIFI_PASSWORD)"
WIFI_SSID_2="$(cfg WIFI_SSID_2)"; WIFI_PASSWORD_2="$(cfg WIFI_PASSWORD_2)"
WIFI_SSID_3="$(cfg WIFI_SSID_3)"; WIFI_PASSWORD_3="$(cfg WIFI_PASSWORD_3)"
WIFI_SSID_4="$(cfg WIFI_SSID_4)"; WIFI_PASSWORD_4="$(cfg WIFI_PASSWORD_4)"
AP_SSID="$(cfg AP_SSID)";         AP_PASSWORD="$(cfg AP_PASSWORD)"
[[ -n "$AP_SSID" ]] || AP_SSID="epaper-setup"
[[ -n "$AP_PASSWORD" ]] || AP_PASSWORD="epaper1234"
BOARD="$(cfg_env PICO_BOARD pico_w)"
PLATFORM="$(cfg_env PICO_PLATFORM rp2040)"

# The Pico can always open its own access point, so no network at all is fine.
if [[ -z "$WIFI_SSID$WIFI_SSID_2$WIFI_SSID_3$WIFI_SSID_4" ]]; then
    warn "No Wi-Fi network configured; the Pico will start its own access point \"$AP_SSID\"."
fi

# Show what will be compiled in (never the password itself) and catch typos early.
info "Wi-Fi networks (tried in this order):"
typeset -a ssids passwords
ssids=("$WIFI_SSID" "$WIFI_SSID_2" "$WIFI_SSID_3" "$WIFI_SSID_4")
passwords=("$WIFI_PASSWORD" "$WIFI_PASSWORD_2" "$WIFI_PASSWORD_3" "$WIFI_PASSWORD_4")
for i in 1 2 3 4; do
    [[ -n "${ssids[$i]}" ]] || continue
    ssid_bytes="$(byte_length "${ssids[$i]}")"
    pw_bytes="$(byte_length "${passwords[$i]}")"
    if [[ -z "${passwords[$i]}" ]]; then
        print -r -- "    $i. \"${ssids[$i]}\"  (open network, no password)"
    else
        print -r -- "    $i. \"${ssids[$i]}\"  (password: $pw_bytes bytes)"
        (( pw_bytes >= 8 && pw_bytes <= 63 )) || warn "network $i: a WPA password has 8 to 63 characters, this one has $pw_bytes"
    fi
    (( ssid_bytes <= 32 )) || warn "network $i: an SSID has at most 32 bytes, this one has $ssid_bytes"
done
for i in 2 3 4; do
    if [[ -z "${ssids[$i]}" && -n "${passwords[$i]}" ]]; then
        warn "WIFI_PASSWORD_$i is set but WIFI_SSID_$i is empty; that entry is ignored"
    fi
done

# Fallback access point: CMake refuses values outside these limits, so say so here.
ap_ssid_bytes="$(byte_length "$AP_SSID")"
ap_pw_bytes="$(byte_length "$AP_PASSWORD")"
(( ap_ssid_bytes >= 1 && ap_ssid_bytes <= 32 )) || die "AP_SSID must be 1 to 32 bytes, this one has $ap_ssid_bytes"
(( ap_pw_bytes >= 8 && ap_pw_bytes <= 63 )) || die "AP_PASSWORD must be 8 to 63 bytes (WPA2), this one has $ap_pw_bytes"
info "Fallback access point: \"$AP_SSID\"  (password: $ap_pw_bytes bytes), web UI at http://192.168.4.1/"

# Old CMakeLists passed credentials as -D compile flags, which breaks on ; " and \.
if ! grep -q 'wifi_config.h' "$ROOT/CMakeLists.txt" 2>/dev/null; then
    if [[ "${WIFI_SSID}${WIFI_PASSWORD}${WIFI_SSID_2}${WIFI_PASSWORD_2}${WIFI_SSID_3}${WIFI_PASSWORD_3}${WIFI_SSID_4}${WIFI_PASSWORD_4}" == *[\;\"\\]* ]]; then
        warn "CMakeLists.txt passes credentials as compile flags; ; \" and \\ will be mangled. Use the generated wifi_config.h version."
    fi
fi

# ---- tools ---------------------------------------------------------------------

SDK_PATH="$(cfg_env PICO_SDK_PATH "$HOME/pico-sdk")"
[[ -d "$SDK_PATH" ]] || die "Pico SDK not found: $SDK_PATH (set PICO_SDK_PATH)"
[[ -f "$SDK_PATH/pico_sdk_init.cmake" ]] || die "$SDK_PATH does not look like the Pico SDK (pico_sdk_init.cmake missing)"

command -v cmake >/dev/null 2>&1 || die "cmake not found (brew install cmake)"

find_toolchain() {
    setopt local_options null_glob
    local candidate gcc_path
    candidate="$(cfg_env PICO_TOOLCHAIN_PATH)"
    if [[ -n "$candidate" ]]; then
        if [[ -x "$candidate/bin/arm-none-eabi-gcc" ]]; then
            print -r -- "$candidate"; return 0
        fi
        warn "PICO_TOOLCHAIN_PATH=$candidate has no bin/arm-none-eabi-gcc; searching elsewhere" >&2
    fi
    gcc_path="$(command -v arm-none-eabi-gcc 2>/dev/null || true)"
    if [[ -n "$gcc_path" ]]; then
        print -r -- "${gcc_path:h:h}"; return 0
    fi
    local -a dirs
    dirs=( "$HOME"/arm-gnu-toolchain-*(/On) /Applications/ArmGNUToolchain/*/arm-none-eabi(/On) )
    for candidate in $dirs; do
        if [[ -x "$candidate/bin/arm-none-eabi-gcc" ]]; then
            print -r -- "$candidate"; return 0
        fi
    done
    return 1
}
TOOLCHAIN_PATH="$(find_toolchain)" || die "Arm toolchain (arm-none-eabi-gcc) not found. Install it (brew install --cask gcc-arm-embedded) or set PICO_TOOLCHAIN_PATH."

export PICO_SDK_PATH="$SDK_PATH"
export PICO_TOOLCHAIN_PATH="$TOOLCHAIN_PATH"
export PATH="$TOOLCHAIN_PATH/bin:$PATH"

# Keep the generator of an existing build directory; otherwise prefer Ninja.
GENERATOR=""
if [[ -f "$BUILD_PATH/CMakeCache.txt" ]]; then
    GENERATOR="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "$BUILD_PATH/CMakeCache.txt")"
fi
if [[ -z "$GENERATOR" ]]; then
    if command -v ninja >/dev/null 2>&1; then GENERATOR="Ninja"; else GENERATOR="Unix Makefiles"; fi
fi
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || print 4)"

# ---- build -----------------------------------------------------------------------

# run_step LABEL LOGFILE COMMAND...: quiet unless it fails (then: error lines + log path).
run_step() {
    local label="$1" log="$2" excerpt
    shift 2
    if (( VERBOSE )); then
        "$@"
        return
    fi
    if "$@" >"$log" 2>&1; then
        return 0
    fi
    print -u2 ""
    print -u2 -r -- "${RED}$label failed.${RST} Full log: $log"
    excerpt="$(grep -E -B1 -A4 'error:|undefined reference|CMake Error|FAILED' "$log" | head -60 || true)"
    if [[ -n "$excerpt" ]]; then
        print -u2 -r -- "$excerpt"
    else
        tail -30 "$log" >&2
    fi
    return 1
}

if (( CLEAN )); then
    info "Removing $BUILD_PATH"
    rm -rf "$BUILD_PATH"
    GENERATOR="$(command -v ninja >/dev/null 2>&1 && print Ninja || print 'Unix Makefiles')"
fi
mkdir -p "$BUILD_PATH"

SECONDS=0
info "Configuring ($BOARD / $PLATFORM, $GENERATOR)..."
run_step "Configure" "$BUILD_PATH/configure.log" \
    cmake -S "$ROOT" -B "$BUILD_PATH" -G "$GENERATOR" \
        -DPICO_SDK_PATH="$PICO_SDK_PATH" \
        -DPICO_PLATFORM="$PLATFORM" \
        -DPICO_BOARD="$BOARD" \
        -DWIFI_SSID="$WIFI_SSID" \
        -DWIFI_PASSWORD="$WIFI_PASSWORD" \
        -DWIFI_SSID_2="$WIFI_SSID_2" \
        -DWIFI_PASSWORD_2="$WIFI_PASSWORD_2" \
        -DWIFI_SSID_3="$WIFI_SSID_3" \
        -DWIFI_PASSWORD_3="$WIFI_PASSWORD_3" \
        -DWIFI_SSID_4="$WIFI_SSID_4" \
        -DWIFI_PASSWORD_4="$WIFI_PASSWORD_4" \
        -DAP_SSID="$AP_SSID" \
        -DAP_PASSWORD="$AP_PASSWORD" \
        -DDISPLAY_TEXT="$INITIAL_TEXT" \
        -DDISPLAY_BG="$INITIAL_BG" \
        -DDISPLAY_ORIENTATION="$INITIAL_ORIENTATION" \
    || exit 1

info "Building..."
run_step "Build" "$BUILD_PATH/build.log" \
    cmake --build "$BUILD_PATH" --parallel "$JOBS" \
    || exit 1

UF2_PATH="$BUILD_PATH/$TARGET_NAME.uf2"
if [[ ! -f "$UF2_PATH" ]]; then
    setopt local_options null_glob
    uf2_candidates=( "$BUILD_PATH"/*.uf2(.om) )
    (( $#uf2_candidates )) || die "Build did not produce a .uf2 in $BUILD_PATH"
    UF2_PATH="${uf2_candidates[1]}"
fi
ok "Built ${UF2_PATH:t} ($(wc -c < "$UF2_PATH" | tr -d ' ') bytes) in ${SECONDS}s"

if (( BUILD_ONLY )); then
    exit 0
fi

# ---- flash -----------------------------------------------------------------------

find_volume() {
    local volume
    for volume in ${=FLASH_VOLUMES}; do
        if [[ -d "$volume" ]]; then
            print -r -- "$volume"
            return 0
        fi
    done
    return 1
}

# With USB stdio the firmware accepts a reboot request, so no BOOTSEL replug is needed.
flash_with_picotool() {
    local tool output
    tool="$(command -v picotool 2>/dev/null || true)"
    if [[ -z "$tool" && -x "$BUILD_PATH/_deps/picotool/picotool" ]]; then
        tool="$BUILD_PATH/_deps/picotool/picotool"
    fi
    [[ -n "$tool" ]] || return 1
    info "Flashing with picotool..."
    if output="$("$tool" load -f -x "$UF2_PATH" 2>&1)"; then
        return 0
    fi
    print -r -- "${DIM}    picotool: ${output##*$'\n'}${RST}"
    return 1
}

flash_with_bootsel() {
    local volume="" waited=0 limit=$(( FLASH_WAIT_SECONDS * 2 ))
    if ! volume="$(find_volume)"; then
        info "Hold BOOTSEL while plugging the board in (waiting up to ${FLASH_WAIT_SECONDS}s)..."
        while (( waited < limit )); do
            if volume="$(find_volume)"; then break; fi
            sleep 0.5
            waited=$(( waited + 1 ))
        done
    fi
    [[ -n "$volume" ]] || die "No BOOTSEL drive found (looked for: $FLASH_VOLUMES)"
    info "Copying to $volume..."
    if [[ "$OSTYPE" == darwin* ]]; then
        cp -X "$UF2_PATH" "$volume/"  # -X: no extended attributes, which the drive rejects
    else
        cp "$UF2_PATH" "$volume/"
    fi
    sync
    waited=0
    while [[ -d "$volume" ]] && (( waited < 30 )); do
        sleep 0.5
        waited=$(( waited + 1 ))
    done
    if [[ -d "$volume" ]]; then
        warn "The drive is still mounted; the board may not have rebooted. Replug it if nothing happens."
    fi
}

if (( FORCE_BOOTSEL )) || ! flash_with_picotool; then
    flash_with_bootsel
fi
ok "Flashed. The board reboots on its own; it takes a moment to join Wi-Fi (or open \"$AP_SSID\")."

# ---- serial console --------------------------------------------------------------

if (( MONITOR )); then
    command -v screen >/dev/null 2>&1 || { warn "screen not found; open the USB serial port with your terminal tool"; exit 0; }
    setopt null_glob
    typeset -a ports
    info "Waiting for the USB serial port..."
    for i in {1..40}; do
        ports=( /dev/cu.usbmodem*(N) /dev/ttyACM*(N) )
        (( $#ports )) && break
        sleep 0.5
    done
    (( $#ports )) || { warn "No serial port appeared (is USB stdio enabled in CMakeLists.txt?)"; exit 0; }
    info "Opening ${ports[1]} at 115200 baud (quit with Ctrl-A, then K)"
    exec screen "${ports[1]}" 115200
fi