#!/usr/bin/env bash
# ============================================================================
#  package_firmware.sh — turn the PlatformIO builds into release files.
#
#    bash scripts/package_firmware.sh
#
#  Needs: a finished `pio run` (all envs) and esptool v5 on PATH.
#  Board variants packaged (PlatformIO env -> label used in file names):
#    cyd         -> ili9341   original CYD, micro-USB
#    cyd_st7789  -> st7789    newer CYD revisions (USB-C / two-USB)
#  Produces:
#    dist/  download bundle (shared parts, per-variant app + fresh-install image)
#    site/  GitHub Pages web flasher (ESP Web Tools page + manifests + parts)
#  Used by .github/workflows/build.yml, and works the same on a PC.
#
#  Flash layout (default 4 MB partition table):
#    0x1000  bootloader     0x8000  partition table
#    0x9000  NVS  <-- saved game + touch calibration live here
#    0xE000  otadata (boot_app0)    0x10000 application
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

VARIANTS="cyd:ili9341 cyd_st7789:st7789"
PIO_HOME="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
BOOT_APP0="${PIO_HOME}/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

VERSION="$(sed -n 's/^#define FW_VERSION *"\([^"]*\)".*/\1/p' TCGCounter/Config.h)"
COMMIT="$(git rev-parse --short HEAD 2>/dev/null || echo local)"
DATE="$(date -u +%Y-%m-%d)"
REPO="${GITHUB_REPOSITORY:-local}"
NAME="TCGCounter-v${VERSION}"

[ -n "${VERSION}" ] || { echo "ERROR: FW_VERSION not found in TCGCounter/Config.h" >&2; exit 1; }

# A release tag must match the version compiled into the firmware.
if [ "${GITHUB_REF_TYPE:-}" = "tag" ] && [ "${GITHUB_REF_NAME:-}" != "v${VERSION}" ]; then
  echo "ERROR: tag ${GITHUB_REF_NAME} does not match FW_VERSION ${VERSION} in Config.h" >&2
  echo "       Bump FW_VERSION (or tag v${VERSION}) and try again." >&2
  exit 1
fi

[ -f "${BOOT_APP0}" ] || { echo "ERROR: missing ${BOOT_APP0}" >&2; exit 1; }
for v in ${VARIANTS}; do
  env="${v%%:*}"
  for f in bootloader.bin partitions.bin firmware.bin; do
    [ -f ".pio/build/${env}/${f}" ] || { echo "ERROR: missing .pio/build/${env}/${f} (run: pio run)" >&2; exit 1; }
  done
done

rm -rf dist site
mkdir -p dist site/firmware

# ---- Shared parts (identical for every display variant) ---------------------
FIRST_ENV="${VARIANTS%%:*}"
cp ".pio/build/${FIRST_ENV}/bootloader.bin" dist/bootloader.bin
cp ".pio/build/${FIRST_ENV}/partitions.bin" dist/partitions.bin
cp "${BOOT_APP0}"                           dist/boot_app0.bin

for v in ${VARIANTS}; do
  env="${v%%:*}"; label="${v##*:}"; build=".pio/build/${env}"
  cmp -s "${build}/bootloader.bin" dist/bootloader.bin || { echo "ERROR: bootloader differs for ${env}" >&2; exit 1; }
  cmp -s "${build}/partitions.bin" dist/partitions.bin || { echo "ERROR: partitions differ for ${env}" >&2; exit 1; }

  # Application only, flashed at 0x10000 together with the shared parts:
  # leaves NVS (saved game + calibration) untouched.
  cp "${build}/firmware.bin" "dist/${NAME}-${label}-app.bin"

  # Single image for a FRESH install. Its padding covers 0x9000-0xDFFF (NVS),
  # so flashing it resets the saved game AND the touch calibration.
  esptool --chip esp32 merge-bin -o "dist/${NAME}-${label}-merged-fresh-install.bin" \
    --flash-mode keep --flash-freq keep --flash-size keep \
    0x1000  "${build}/bootloader.bin" \
    0x8000  "${build}/partitions.bin" \
    0xe000  "${BOOT_APP0}" \
    0x10000 "${build}/firmware.bin"

  # ---- Web flasher manifest for this variant (separate parts, NOT the merged
  # image, so "Install" without ticking "Erase device" keeps NVS) ------------
  cp "${build}/firmware.bin" "site/firmware/app-${label}.bin"
  {
    echo '{'
    echo "  \"name\": \"TCG Counter (${label})\","
    echo "  \"version\": \"${VERSION}\","
    echo '  "new_install_prompt_erase": true,'
    echo '  "builds": ['
    echo '    {'
    echo '      "chipFamily": "ESP32",'
    echo '      "parts": ['
    echo '        { "path": "firmware/bootloader.bin", "offset": 4096 },'
    echo '        { "path": "firmware/partitions.bin", "offset": 32768 },'
    echo '        { "path": "firmware/boot_app0.bin",  "offset": 57344 },'
    echo "        { \"path\": \"firmware/app-${label}.bin\", \"offset\": 65536 }"
    echo '      ]'
    echo '    }'
    echo '  ]'
    echo '}'
  } > "site/manifest-${label}.json"
done
cp dist/bootloader.bin dist/partitions.bin dist/boot_app0.bin site/firmware/

{
  echo "TCG Counter v${VERSION}  (commit ${COMMIT}, built ${DATE})"
  echo "Board: ESP32-2432S028R \"Cheap Yellow Display\""
  echo
  echo "WHICH FILE? Pick the display controller of your board:"
  echo "  ili9341 - original CYD (micro-USB)"
  echo "  st7789  - newer revisions (USB-C, or micro-USB + USB-C)"
  echo "  Not sure? The serial log (115200) prints a 'controller ID' line at boot."
  echo "  Mirrored / sideways picture with swapped colours = wrong variant."
  echo
  echo "EASIEST: the web flasher on this repo's GitHub Pages site (Chrome/Edge on a PC)."
  echo
  echo "Commands work with esptool v4 and v5 (also the one in an ESP-IDF shell)."
  echo "Type each command on ONE line. Replace COM5 with your port, st7789 with your variant."
  echo "If the board doesn't enter flash mode by itself: hold BOOT, tap RST, release BOOT,"
  echo "then add  --before no_reset  right after the --baud value."
  echo
  echo "FRESH INSTALL - resets saved game + touch calibration (use this the first time):"
  echo "  esptool --chip esp32 --port COM5 --baud 460800 write_flash 0x0 ${NAME}-st7789-merged-fresh-install.bin"
  echo
  echo "UPDATE - keeps saved game + touch calibration:"
  echo "  esptool --chip esp32 --port COM5 --baud 460800 write_flash 0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 ${NAME}-st7789-app.bin"
  echo
  echo "After flashing, tap RST once."
} > dist/FLASHING.txt

sed -e "s|@VERSION@|${VERSION}|g" -e "s|@COMMIT@|${COMMIT}|g" \
    -e "s|@DATE@|${DATE}|g" -e "s|@REPO@|${REPO}|g" \
    flasher/index.html > site/index.html
if [ -f docs/ui_preview.png ]; then cp docs/ui_preview.png site/; fi

echo "Packaged ${NAME} (${COMMIT})"
ls -l dist site site/firmware
