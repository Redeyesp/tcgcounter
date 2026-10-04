#!/usr/bin/env bash
# ============================================================================
#  package_firmware.sh — turn a PlatformIO build into release files.
#
#    bash scripts/package_firmware.sh [env]      (default env: cyd)
#
#  Needs: a finished `pio run -e <env>` and esptool v5 on PATH.
#  Produces:
#    dist/  download bundle (separate parts, fresh-install image, notes)
#    site/  GitHub Pages web flasher (ESP Web Tools page + manifest + parts)
#  Used by .github/workflows/build.yml, and works the same on a PC.
#
#  Flash layout (default 4 MB partition table):
#    0x1000  bootloader     0x8000  partition table
#    0x9000  NVS  <-- saved game + touch calibration live here
#    0xE000  otadata (boot_app0)    0x10000 application
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

ENV="${1:-cyd}"
BUILD=".pio/build/${ENV}"
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

for f in "${BUILD}/bootloader.bin" "${BUILD}/partitions.bin" "${BUILD}/firmware.bin" "${BOOT_APP0}"; do
  [ -f "$f" ] || { echo "ERROR: missing $f (run: pio run -e ${ENV})" >&2; exit 1; }
done

rm -rf dist site
mkdir -p dist site/firmware

# ---- Download bundle ---------------------------------------------------------
# The four parts a normal Arduino/PlatformIO upload writes. Flashing them at
# their own offsets leaves NVS (saved game + calibration) untouched.
cp "${BUILD}/bootloader.bin" dist/bootloader.bin
cp "${BUILD}/partitions.bin" dist/partitions.bin
cp "${BOOT_APP0}"            dist/boot_app0.bin
cp "${BUILD}/firmware.bin"   "dist/${NAME}-app.bin"

# Single image for a FRESH install. Its padding covers 0x9000-0xDFFF (NVS),
# so flashing it resets the saved game AND the touch calibration.
esptool --chip esp32 merge-bin -o "dist/${NAME}-merged-fresh-install.bin" \
  --flash-mode keep --flash-freq keep --flash-size keep \
  0x1000  "${BUILD}/bootloader.bin" \
  0x8000  "${BUILD}/partitions.bin" \
  0xe000  "${BOOT_APP0}" \
  0x10000 "${BUILD}/firmware.bin"

{
  echo "TCG Counter v${VERSION}  (commit ${COMMIT}, built ${DATE})"
  echo "Board: ESP32-2432S028R \"Cheap Yellow Display\""
  echo
  echo "EASIEST: the web flasher on this repo's GitHub Pages site (Chrome/Edge on a PC)."
  echo
  echo "Commands work with esptool v4 and v5 (also the one in an ESP-IDF shell)."
  echo "Type each command on ONE line. Replace COM5 with your port (Device Manager)."
  echo
  echo "FRESH INSTALL - resets saved game + touch calibration (use this the first time):"
  echo "  esptool --chip esp32 --port COM5 --baud 460800 write_flash 0x0 ${NAME}-merged-fresh-install.bin"
  echo
  echo "UPDATE - keeps saved game + touch calibration:"
  echo "  esptool --chip esp32 --port COM5 --baud 460800 write_flash 0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 ${NAME}-app.bin"
  echo
  echo "\"Failed to connect\": hold BOOT, tap RST, release BOOT, run the command again."
  echo "After flashing, tap RST once."
} > dist/FLASHING.txt

# ---- Web flasher (ESP Web Tools) ----------------------------------------------
# Uses the separate parts, NOT the merged image, so "Install" without ticking
# "Erase device" keeps the saved game and calibration.
cp dist/bootloader.bin dist/partitions.bin dist/boot_app0.bin site/firmware/
cp "dist/${NAME}-app.bin" site/firmware/app.bin

{
  echo '{'
  echo '  "name": "TCG Counter",'
  echo "  \"version\": \"${VERSION}\","
  echo '  "new_install_prompt_erase": true,'
  echo '  "builds": ['
  echo '    {'
  echo '      "chipFamily": "ESP32",'
  echo '      "parts": ['
  echo '        { "path": "firmware/bootloader.bin", "offset": 4096 },'
  echo '        { "path": "firmware/partitions.bin", "offset": 32768 },'
  echo '        { "path": "firmware/boot_app0.bin",  "offset": 57344 },'
  echo '        { "path": "firmware/app.bin",        "offset": 65536 }'
  echo '      ]'
  echo '    }'
  echo '  ]'
  echo '}'
} > site/manifest.json

sed -e "s|@VERSION@|${VERSION}|g" -e "s|@COMMIT@|${COMMIT}|g" \
    -e "s|@DATE@|${DATE}|g" -e "s|@REPO@|${REPO}|g" \
    flasher/index.html > site/index.html
if [ -f docs/ui_preview.png ]; then cp docs/ui_preview.png site/; fi

echo "Packaged ${NAME} (${COMMIT})"
ls -l dist site/firmware
