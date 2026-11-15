#!/usr/bin/env bash
# Alternate flash strategies for bare ESP32-C3-WROOM-02 + CH340E
set -u
export PATH="${HOME}/.platformio/penv/bin:${PATH}"
cd "$(dirname "$0")/.."

PORT="${UPLOAD_PORT:-$(ls /dev/cu.usbserial-* 2>/dev/null | head -1)}"
ESPTOOL="${HOME}/.platformio/packages/tool-esptool/esptool"

if [[ -z "${PORT}" ]]; then
  echo "No /dev/cu.usbserial-* found"
  exit 1
fi

echo "Port: ${PORT}"
echo "Building..."
pio run || exit 1

echo
echo "Strategy A: many DTR/RTS reset patterns + sync (wire DTR->IO9 and RTS->EN if possible)"
echo "Strategy B: 90s retry — while this runs, briefly short EN to GND then release (IO9 held to GND)"
echo

python3 - "$PORT" << 'PY'
import sys, time, serial

port = sys.argv[1]
ser = serial.Serial(port, 115200, timeout=0.05)

def pulse(dtr, rts, hold=0.05):
    ser.setDTR(dtr)
    ser.setRTS(rts)
    time.sleep(hold)

patterns = [
    # classic Espressif
    [(False, True, 0.1), (True, False, 0.05), (False, False, 0.05)],
    # inverted
    [(True, False, 0.1), (False, True, 0.05), (True, True, 0.05)],
    # EN-only pulse (RTS)
    [(False, True, 0.15), (False, False, 0.05)],
    [(True, True, 0.15), (True, False, 0.05)],
    # boot strap held longer
    [(True, True, 0.2), (True, False, 0.2), (False, False, 0.05)],
]

# esptool sync frame-ish: SLIP 0xc0 0x00 ... but ROM expects its own protocol.
# Just clear buffers and leave line idle after each pattern.
for i, steps in enumerate(patterns):
    print(f"pattern {i+1}/{len(patterns)}")
    ser.reset_input_buffer()
    for dtr, rts, hold in steps:
        pulse(dtr, rts, hold)
    time.sleep(0.15)
    data = ser.read(256)
    if data:
        print("rx after pattern:", data[:80])
ser.close()
print("DTR/RTS patterns done")
PY

detect() {
  local before="$1"
  "$ESPTOOL" --chip esp32c3 --port "$PORT" --baud 115200 --before "$before" chip-id
}

set +e
for before in no-reset default-reset usb-reset; do
  echo "=== detect before=${before} ==="
  if detect "$before"; then
    echo "Detected — uploading (before=${before})"
    if [[ "$before" == "no-reset" ]]; then
      UPLOAD_PORT="$PORT" UPLOAD_BEFORE=no-reset ./scripts/flash.sh
    else
      pio run -t upload --upload-port "$PORT"
    fi
    exit $?
  fi
done

echo
echo "=== 90s retry: pulse EN now (IO9 must stay grounded) ==="
deadline=$((SECONDS + 90))
while (( SECONDS < deadline )); do
  if detect no-reset; then
    echo "Detected during retry — uploading"
    UPLOAD_PORT="$PORT" UPLOAD_BEFORE=no-reset ./scripts/flash.sh
    exit $?
  fi
  # also try a quick RTS/DTR nudge each loop
  python3 - "$PORT" << 'PY'
import sys, time, serial
ser = serial.Serial(sys.argv[1], 115200)
ser.setDTR(True); ser.setRTS(True); time.sleep(0.05)
ser.setDTR(True); ser.setRTS(False); time.sleep(0.05)
ser.setDTR(False); time.sleep(0.02)
ser.close()
PY
  sleep 1
done

echo
echo "Still no response."
echo "Next hardware option (often works when UART0 fails on bare C3):"
echo "  Native USB: ESP GPIO19 (D+) and GPIO18 (D-) + GND to a USB cable data lines,"
echo "  keep 3V3/GND/EN powered from CH340 or external 3.3V, IO9->GND, power cycle,"
echo "  then look for a new /dev/cu.usbmodem* port and flash that."
exit 2
