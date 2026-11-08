#!/bin/bash
# Tests for flash.sh using shim versions of cmake, picotool, screen and a fake SDK/toolchain.
set -u
PASS=0; FAIL=0
ok()   { PASS=$((PASS+1)); echo "  ok   $1"; }
bad()  { FAIL=$((FAIL+1)); echo "  FAIL $1"; }
check(){ if eval "$2"; then ok "$1"; else bad "$1  [$2]"; fi; }

T=$(mktemp -d); export HOME=$T/home; mkdir -p "$HOME"
PROJ=$T/proj; mkdir -p "$PROJ"; cp /home/claude/flash/flash.sh "$PROJ/"
SDK=$HOME/pico-sdk; mkdir -p "$SDK"; touch "$SDK/pico_sdk_init.cmake"
mk_tc() { mkdir -p "$1/bin"; printf '#!/bin/sh\nexit 0\n' > "$1/bin/arm-none-eabi-gcc"; chmod +x "$1/bin/arm-none-eabi-gcc"; }
mk_tc "$HOME/arm-gnu-toolchain-13.2.rel1-old"
mk_tc "$HOME/arm-gnu-toolchain-14.3.rel1-new"
SHIM=$T/shim; mkdir -p "$SHIM"
cat > "$SHIM/cmake" <<'EOF'
#!/bin/sh
{ printf 'PATH_FIRST=%s\n' "$(echo "$PATH" | cut -d: -f1)"; printf 'TOOLCHAIN=%s\n' "${PICO_TOOLCHAIN_PATH:-}"; printf '%s\0' "$@"; printf '\n--END--\n'; } >> "$SHIM_LOG"
case "$1" in
  -S) while [ $# -gt 0 ]; do [ "$1" = -B ] && B=$2; shift; done
      mkdir -p "$B"; echo "CMAKE_GENERATOR:INTERNAL=${SHIM_GEN:-Ninja}" > "$B/CMakeCache.txt"
      [ "${SHIM_FAIL_CONFIGURE:-0}" = 1 ] && { echo "CMake Error at CMakeLists.txt:9: boom"; exit 1; }
      exit 0;;
  --build) B=$2
      if [ "${SHIM_FAIL_BUILD:-0}" = 1 ]; then echo "[1/2] Building x.c.o"; echo "/p/src/x.c:10:5: error: expected ';' before '}' token"; echo "FAILED: x.c.o"; exit 1; fi
      echo UF2DATA > "$B/pico_epaper_4color_example.uf2"; exit 0;;
esac
EOF
printf '#!/bin/sh\nexit 0\n' > "$SHIM/ninja"
cat > "$SHIM/picotool" <<'EOF'
#!/bin/sh
echo "picotool $*" >> "$SHIM_LOG.picotool"
if [ "${SHIM_PICOTOOL:-ok}" = ok ]; then echo "Loading into Flash: [==============================]  100%"; exit 0; fi
echo "No accessible RP-series devices in BOOTSEL mode were found."; exit 1
EOF
printf '#!/bin/sh\necho "screen $*" >> "$SHIM_LOG.screen"\nexit 0\n' > "$SHIM/screen"
chmod +x "$SHIM"/*
export SHIM_LOG
BASEPATH="/usr/bin:/bin:/usr/local/bin"

# run NAME [args...]: fresh log, captures $OUT and $RC. PATH includes the shims unless NOPICOTOOL=1.
run() {
  SHIM_LOG=$T/log.$RANDOM; export SHIM_LOG; : > "$SHIM_LOG"
  local pt="$SHIM"
  if [ "${NOPICOTOOL:-0}" = 1 ]; then pt=$T/shim_nopt; mkdir -p "$pt"; cp "$SHIM/cmake" "$SHIM/ninja" "$SHIM/screen" "$pt/"; fi
  OUT=$(cd "$PROJ" && PATH="$pt:$BASEPATH" ./flash.sh "$@" 2>&1); RC=$?
}
dargs() { python3 - "$SHIM_LOG" <<'EOF'
import sys
data=open(sys.argv[1],'rb').read().split(b'--END--')[0]
parts=[p for p in data.split(b'\0')]
for p in parts:
    s=p.decode('utf-8','replace')
    if s.startswith('-D'): print(s)
EOF
}

write_env() { printf "$1" > "$PROJ/.env"; chmod 600 "$PROJ/.env"; }
STD_ENV='WIFI_SSID=HomeNet\nWIFI_PASSWORD=correct horse\n'

echo "== .env parsing (quotes, specials, CRLF, comments, unknown keys) =="
write_env '# comment\n\nWIFI_SSID=Büro WLAN\nWIFI_PASSWORD='"'"'pa ss;wo"rd\\n$HOME`x`#1'"'"'\nexport WIFI_SSID_2="Second Net"\nWIFI_PASSWORD_2="  spaced  "\nUNKNOWN_KEY=1\nthis is not valid\nWIFI_PASSWORD_3=short\n'
run --build-only
D=$(dargs)
check "exit 0" '[ $RC -eq 0 ]'
check "ssid with umlaut+space literal"  'echo "$D" | grep -qxF -- "-DWIFI_SSID=Büro WLAN"'
check "password: specials literal (; \" \\n \$HOME backticks #)" 'echo "$D" | grep -qxF -- "-DWIFI_PASSWORD=pa ss;wo\"rd\\n\$HOME\`x\`#1"'
check "export prefix + double quotes"   'echo "$D" | grep -qxF -- "-DWIFI_SSID_2=Second Net"'
check "inner spaces kept in quotes"     'echo "$D" | grep -qxF -- "-DWIFI_PASSWORD_2=  spaced  "'
check "unknown key warned"              'echo "$OUT" | grep -q "unknown key UNKNOWN_KEY"'
check "invalid line warned"             'echo "$OUT" | grep -q "line 8 is not KEY=VALUE"'
check "password without SSID warned"    'echo "$OUT" | grep -q "WIFI_PASSWORD_3 is set but WIFI_SSID_3 is empty"'
check "password never printed"          '! echo "$OUT" | grep -q "pa ss;wo"'
check "password length shown"           'echo "$OUT" | grep -q "password: 23 bytes"'
printf 'WIFI_SSID=CrLf Net\r\nWIFI_PASSWORD=pw12345678\r\n' > "$PROJ/.env"; chmod 600 "$PROJ/.env"
run --build-only; D=$(dargs)
check "CRLF line endings stripped" 'echo "$D" | grep -qxF -- "-DWIFI_PASSWORD=pw12345678" && echo "$D" | grep -qxF -- "-DWIFI_SSID=CrLf Net"'

echo "== validation and warnings =="
write_env 'WIFI_SSID=Net\nWIFI_PASSWORD=short\n'; run --build-only
check "short WPA password warned" 'echo "$OUT" | grep -q "8 to 63 characters, this one has 5"'
write_env 'WIFI_SSID=OpenNet\n'; run --build-only
check "open network accepted" '[ $RC -eq 0 ] && echo "$OUT" | grep -q "open network"'
rm -f "$PROJ/.env"; run --build-only
check "no .env -> clear error, exit 1" '[ $RC -eq 1 ] && echo "$OUT" | grep -q "No Wi-Fi network configured"'
write_env "$STD_ENV"; chmod 644 "$PROJ/.env"; run --build-only
check "world-readable .env warned" 'echo "$OUT" | grep -q "readable by other users"'
write_env "$STD_ENV"

echo "== options -> cmake arguments =="
run --build-only --text "Grüße \"Welt\"" --bg RED --orientation Landscape; D=$(dargs)
check "text passed literally"  'echo "$D" | grep -qxF -- "-DDISPLAY_TEXT=Grüße \"Welt\""'
check "bg red = 3, landscape = 1" 'echo "$D" | grep -qxF -- "-DDISPLAY_BG=3" && echo "$D" | grep -qxF -- "-DDISPLAY_ORIENTATION=1"'
run --build-only --word hi; D=$(dargs)
check "--word alias"  'echo "$D" | grep -qxF -- "-DDISPLAY_TEXT=hi"'
check "board/platform defaults" 'echo "$D" | grep -qxF -- "-DPICO_BOARD=pico_w" && echo "$D" | grep -qxF -- "-DPICO_PLATFORM=rp2040"'
PICO_BOARD=pico2_w PICO_PLATFORM=rp2350 run --build-only; D=$(dargs)
check "PICO_BOARD/PLATFORM from environment" 'echo "$D" | grep -qxF -- "-DPICO_BOARD=pico2_w" && echo "$D" | grep -qxF -- "-DPICO_PLATFORM=rp2350"'
for a in "--bg purple" "--orientation sideways" "--nope" "--text" "--bg"; do
  run $a; check "bad option '$a' -> exit 1" '[ $RC -eq 1 ]'
done
run --help; check "--help exit 0" '[ $RC -eq 0 ] && echo "$OUT" | grep -q "Usage:"'

echo "== tool discovery =="
run --build-only
LAST=$(grep -a '^TOOLCHAIN=' "$SHIM_LOG" | head -1)
check "newest ~/arm-gnu-toolchain-* chosen" 'echo "$LAST" | grep -q "14.3.rel1-new"'
check "toolchain bin first on PATH" 'grep -aq "PATH_FIRST=.*14.3.rel1-new/bin" "$SHIM_LOG"'
PICO_TOOLCHAIN_PATH="$HOME/arm-gnu-toolchain-13.2.rel1-old" run --build-only
check "PICO_TOOLCHAIN_PATH override" 'grep -aq "TOOLCHAIN=.*13.2.rel1-old" "$SHIM_LOG"'
PICO_TOOLCHAIN_PATH=/nonexistent run --build-only
check "bad PICO_TOOLCHAIN_PATH warns, falls back" '[ $RC -eq 0 ] && echo "$OUT" | grep -q "has no bin/arm-none-eabi-gcc"'
PICO_SDK_PATH=/nonexistent run --build-only
check "missing SDK -> error" '[ $RC -eq 1 ] && echo "$OUT" | grep -q "Pico SDK not found"'
mkdir -p "$T/notsdk"; PICO_SDK_PATH="$T/notsdk" run --build-only
check "dir that is not the SDK -> error" '[ $RC -eq 1 ] && echo "$OUT" | grep -q "does not look like the Pico SDK"'
rm -rf "$PROJ/build"; run --build-only
check "Ninja chosen for a fresh build dir" 'grep -a -q -- "-GNinja" "$SHIM_LOG" || tr "\0" "\n" < "$SHIM_LOG" | grep -qx "Ninja"'
rm -rf "$PROJ/build"; SHIM_GEN="Unix Makefiles" run --build-only; run --build-only
check "existing build dir keeps its generator" 'tr "\0" "\n" < "$SHIM_LOG" | grep -qx "Unix Makefiles"'

echo "== build failures are readable =="
rm -rf "$PROJ/build"; SHIM_FAIL_BUILD=1 run
check "build failure -> exit 1, error line shown" '[ $RC -eq 1 ] && echo "$OUT" | grep -q "error: expected" && echo "$OUT" | grep -q "Build failed"'
check "no flash attempted after failed build" '[ ! -f "$SHIM_LOG.picotool" ]'
check "full log path printed" 'echo "$OUT" | grep -q "build.log"'
rm -rf "$PROJ/build"; SHIM_FAIL_CONFIGURE=1 run
check "configure failure -> exit 1" '[ $RC -eq 1 ] && echo "$OUT" | grep -q "Configure failed" && echo "$OUT" | grep -q "CMake Error"'

echo "== --clean and --build-only =="
mkdir -p "$PROJ/build"; touch "$PROJ/build/stale-marker"; run --clean --build-only
check "--clean removed the old build dir" '[ ! -f "$PROJ/build/stale-marker" ] && [ $RC -eq 0 ]'
check "--build-only does not flash" '[ ! -f "$SHIM_LOG.picotool" ]'

echo "== flashing =="
SHIM_PICOTOOL=ok run
check "picotool path: loaded with -f -x" '[ $RC -eq 0 ] && grep -q "picotool load -f -x .*pico_epaper_4color_example.uf2" "$SHIM_LOG.picotool" && echo "$OUT" | grep -q "Flashed"'
check "no BOOTSEL wait needed"         '! echo "$OUT" | grep -q "Hold BOOTSEL"'
VOL=$T/vol/RPI-RP2
# board in BOOTSEL mode; a background job "reboots" it once the UF2 has been copied
reboot_when_copied() { ( for _ in $(seq 1 100); do [ -f "$VOL/pico_epaper_4color_example.uf2" ] && { sleep 0.3; rm -rf "$VOL"; exit; }; sleep 0.1; done ) & }
mkdir -p "$VOL"; reboot_when_copied
FLASH_VOLUMES="$VOL" SHIM_PICOTOOL=fail run
check "picotool fails -> falls back to BOOTSEL copy" '[ $RC -eq 0 ] && echo "$OUT" | grep -q "Copying to" && echo "$OUT" | grep -q "Flashed"'
check "picotool error line shown (dimmed)" 'echo "$OUT" | grep -q "No accessible RP-series"'
check "no replug warning after clean reboot" '! echo "$OUT" | grep -q "still mounted"'
mkdir -p "$VOL"; reboot_when_copied
FLASH_VOLUMES="$VOL" NOPICOTOOL=1 run
check "no picotool installed -> BOOTSEL path" '[ $RC -eq 0 ] && echo "$OUT" | grep -q "Copying to"'
mkdir -p "$VOL"; reboot_when_copied
FLASH_VOLUMES="$VOL" SHIM_PICOTOOL=ok run --bootsel
check "--bootsel skips picotool even if present" '[ $RC -eq 0 ] && [ ! -f "$SHIM_LOG.picotool" ] && echo "$OUT" | grep -q "Copying to"'
FLASH_VOLUMES="$T/vol/RP2350 $T/vol/RPI-RP2" ; mkdir -p "$T/vol/RP2350"; ( for _ in $(seq 1 100); do [ -f "$T/vol/RP2350/pico_epaper_4color_example.uf2" ] && { sleep 0.3; rm -rf "$T/vol/RP2350"; exit; }; sleep 0.1; done ) &
FLASH_VOLUMES="$T/vol/RPI-RP2 $T/vol/RP2350" SHIM_PICOTOOL=fail run
check "either volume name accepted (RP2350)" '[ $RC -eq 0 ] && echo "$OUT" | grep -q "RP2350"'
rm -rf "$T/vol"; FLASH_WAIT_SECONDS=1 FLASH_VOLUMES="$VOL" SHIM_PICOTOOL=fail run
check "no board -> waits, then clear error" '[ $RC -eq 1 ] && echo "$OUT" | grep -q "Hold BOOTSEL" && echo "$OUT" | grep -q "No BOOTSEL drive found"'
mkdir -p "$VOL"   # drive that never disappears
FLASH_VOLUMES="$VOL" SHIM_PICOTOOL=fail run
check "drive stays mounted -> replug hint" '[ $RC -eq 0 ] && echo "$OUT" | grep -q "still mounted"'
rm -rf "$T/vol"

echo "== monitor =="
SHIM_PICOTOOL=ok run --monitor
check "--monitor without a serial port: hint, no crash" '[ $RC -eq 0 ] && echo "$OUT" | grep -q "No serial port appeared"'

echo
echo "passed: $PASS   failed: $FAIL"
[ $FAIL -eq 0 ]
