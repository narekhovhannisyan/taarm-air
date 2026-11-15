#!/usr/bin/env bash
# Flash taarm-thermo. Buttonless boards: use jumper wires (see README).
set -euo pipefail
export PATH="${HOME}/.platformio/penv/bin:${PATH}"
cd "$(dirname "$0")/.."

PORT="${UPLOAD_PORT:-/dev/cu.usbserial-140}"
BEFORE="${UPLOAD_BEFORE:-default-reset}"

echo "Building..."
pio run

echo
echo "Uploading to ${PORT} (before=${BEFORE})"
echo
echo "Buttonless download mode:"
echo "  1) Jumper GPIO9 -> GND  (leave connected)"
echo "  2) Unplug USB, plug back in  (or briefly short EN->GND then release EN)"
echo "  3) Run:  UPLOAD_BEFORE=no-reset ./scripts/flash.sh"
echo "  4) After success: remove GPIO9 jumper, unplug/replug USB"
echo

if [[ "${BEFORE}" == "no-reset" ]]; then
  # Board already waiting in ROM download mode
  "$HOME/.platformio/packages/tool-esptool/esptool" \
    --chip esp32c3 \
    --port "${PORT}" \
    --baud 115200 \
    --before no-reset \
    --after hard-reset \
    write-flash -z \
    --flash-mode dio --flash-freq 80m --flash-size 4MB \
    0x0000 .pio/build/esp32-c3-devkitc-02/bootloader.bin \
    0x8000 .pio/build/esp32-c3-devkitc-02/partitions.bin \
    0xe000 "$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" \
    0x10000 .pio/build/esp32-c3-devkitc-02/firmware.bin
else
  pio run -t upload --upload-port "${PORT}"
fi

echo "Done. Monitor with: pio device monitor --port ${PORT}"
