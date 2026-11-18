# taarm-thermo — ESP32-C3 thermostat

Cooling thermostat on **ESP32-C3-WROOM-02** with a **10 kΩ NTC**, **Thermalright TL-C14C** PWM fan, **Auto/Manual** modes, USB serial debug, and **BLE** phone control.

## Hardware wiring

| Function | Board pin | Notes |
| --- | --- | --- |
| NTC sense | **GPIO4** | ADC; see divider below |
| Fan PWM | **GPIO6** | 25 kHz via N-FET (AO3400A); do not wire 5 V PWM to the pin |
| Fan tach | **GPIO7** | Open-collector, internal pull-up |
| GND | GND | Must share with 12 V PSU ground |

### NTC 10 kΩ divider

```
3V3 —— 10k fixed ——●—— NTC 10k —— GND
                   │
                 GPIO4
```

Assume β = 3950 K (change with serial `beta <n>` if needed).

### TL-C14C fan (4-pin)

| Fan pin | Wire (typical) | Connect to |
| --- | --- | --- |
| 1 GND | Black | ESP32 GND **and** 12 V PSU GND |
| 2 +12 V | Yellow | External **12 V** supply (~0.13 A) — **not** USB |
| 3 Tach | Green | GPIO7 |
| 4 PWM | pin 4 | AO3400A drain; gate←GPIO6 (+100k to GND), source←GND |

Never put 12 V on ESP32 pins.

## Firmware (PlatformIO)

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -t upload
pio device monitor
```

Upload/monitor port defaults to `/dev/cu.usbserial-140` in [`platformio.ini`](platformio.ini).

If upload fails with `No serial data received` (common on buttonless CH340 boards where DTR/RTS are not wired to the chip):

**Enter download mode with jumper wires**

1. Connect **GPIO9 → GND** (this is the “BOOT” strap; leave it connected)
2. Do **not** ground GPIO8 (GPIO8 must stay high)
3. Power-cycle: unplug USB and plug it back in  
   (or briefly short **EN → GND**, then release EN so the chip restarts)
4. Flash without toggling reset:

```bash
UPLOAD_BEFORE=no-reset ./scripts/flash.sh
```

5. After a successful flash: **remove the GPIO9–GND jumper**, unplug/replug USB so it boots normally

You should see ROM text like `waiting for download` on serial at 115200 if download mode worked.

### Alternate flash methods (if UART still silent)

**A. Auto-reset wires (recommended on CH340E)**

Your adapter has unused **DTR** / often **RTS**. Connect:

| CH340E | ESP32-C3 |
| --- | --- |
| DTR | IO9 |
| RTS | EN |

Keep EN also pulled to 3V3 with a **10k resistor** (not a hard short only), then run `./scripts/flash.sh` (default-reset).

**B. Native USB (ESP32-C3 USB-Serial/JTAG)**

| USB cable | ESP32-C3 |
| --- | --- |
| D+ | IO19 |
| D- | IO18 |
| GND | GND |

Power still from 3V3/GND/EN. Ground IO9, power-cycle, then look for a new `/dev/cu.usbmodem*` and flash that port.

**C. Helper script**

```bash
./scripts/flash_alt.sh
```

While it retries, briefly short **EN→GND** then release (keep IO9 grounded).

### Serial commands (115200 baud)

| Command | Effect |
| --- | --- |
| `status` | Print temp / setpoint / mode / duty / rpm |
| `set 25` | Setpoint °C (15–40) |
| `mode auto` / `mode manual` | Control mode |
| `duty 40` | Manual fan % (0–100) |
| `beta 3950` | NTC β constant |

Status also prints once per second.

### Control behavior

- **Auto**: below setpoint − 0.5 °C → 0% duty; above setpoint ramps ~30–100% over 4 °C  
- **Manual**: app/serial duty is applied directly  
- Defaults: Auto, setpoint 28 °C, manual duty 50%

## BLE GATT

Device name: **`taarm-thermo`**

Base UUID pattern: `a1b3XXXX-c3d4-4e5f-8a9b-0c1d2e3f4a5b`

| Characteristic | UUID | Format | Access |
| --- | --- | --- | --- |
| Service | `a1b30001-…` | — | — |
| Temperature | `a1b30002-…` | float32 LE °C | read + notify |
| Setpoint | `a1b30003-…` | float32 LE °C | read + write |
| Mode | `a1b30004-…` | uint8 `0=auto`, `1=manual` | read + write |
| Fan duty | `a1b30005-…` | uint8 0–100 | read + write + notify |
| RPM | `a1b30006-…` | uint16 LE | read + notify |

You can exercise the same UUIDs with **nRF Connect** before using the app.

## Mobile app (`app/`)

Expo + `react-native-ble-plx` (requires a **development build** — BLE is not available in Expo Go).

```bash
cd app
npm install
npx expo run:ios      # or: npx expo run:android
```

App features:

- Scan / connect to `taarm-thermo`
- Live temperature and RPM
- Target temperature stepper (writes setpoint)
- Auto / Manual toggle
- Fan % control in Manual mode

## Project layout

```
platformio.ini
include/thermostat_state.h
include/ble_service.h
src/main.cpp
src/ble_service.cpp
app/                 # Expo phone UI
README.md
```
