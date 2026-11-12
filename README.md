# RFBlinds

Control 433 MHz motorised blinds from an **ESP32**, with no changes to the
blinds. The ESP32 sends the same radio frames as the original 4-channel
remote through a cheap 433 MHz transmitter module. You can drive it from:

- a **web page** at `http://rfblinds.local/` (works on a phone or desktop),
- a **REST API** for Home Assistant, Node-RED, scripts and similar tools,
- **USB serial**, with the same 1-byte protocol as the earlier Arduino Nano
  version, so existing host scripts keep working.

The RF protocol was reverse engineered from SDR recordings of the remote.
See [`rf-analysis/`](rf-analysis/README.md).

```
RFBlinds/
├── .devcontainer/              Docker dev environment with all build tools
├── src/
│   ├── blinds/                 ESP32 firmware (Arduino sketch)
│   │   ├── blinds.ino          serial, web UI, REST API, OTA
│   │   ├── config.h            pins, timing, blind names/IDs
│   │   ├── secrets.example.h   copy to secrets.h for Wi-Fi credentials
│   │   ├── rf_protocol.h       frame encoder (pure C++, unit-tested)
│   │   └── rf_transmitter.*    RMT-based 433 MHz output
│   ├── build.sh                command-line build / flash / monitor
│   ├── platformio.ini          PlatformIO build
│   ├── test/                   host-side encoder tests
│   └── tools/blinds_serial.py  send commands over USB
└── rf-analysis/                SDR captures, decoder script, protocol notes
```

## Hardware

| Part | Notes |
|---|---|
| ESP32 dev board | Any ESP32 / S2 / S3 / C3 board with Arduino support |
| 433 MHz ASK transmitter | FS1000A, or a better module such as STX882 / H34A |
| Antenna | 17.3 cm of straight wire (a quarter wavelength) on the module's ANT pad |

### Wiring

| Transmitter pin | ESP32 |
|---|---|
| DATA | **GPIO 4** (set by `RF_TX_PIN` in `config.h`) |
| VCC | **5V / VIN** for more range (3.3 V works, with less range) |
| GND | GND |

The module's DATA input accepts the ESP32's 3.3 V logic level, even when the
module runs on 5 V. The on-board LED (GPIO 2) blinks while a command is being
sent. Set `STATUS_LED_PIN` to `-1` if your board has no LED there.

## Building and flashing

### Arduino IDE

1. In **Boards Manager**, install **esp32 by Espressif Systems**. Core 2.x
   and 3.x both work.
2. Open `src/blinds/blinds.ino`.
3. Optional, for Wi-Fi: copy `secrets.example.h` to `secrets.h` next to it
   and fill in your SSID and password. Without `secrets.h` the board runs
   with USB serial only.
4. Select your board (for example *ESP32 Dev Module*) and its port, then
   click **Upload**.
5. Open the Serial Monitor at **57600 baud** to see the IP address.

### Dev container (all tools preinstalled)

`.devcontainer/` defines a container with everything needed to build, test
and flash: PlatformIO, arduino-cli, g++, Python with numpy and pyserial. You
need Docker and the VS Code **Dev Containers** extension.

1. Open the `RFBlinds` folder in VS Code and run **Dev Containers: Reopen in
   Container**.
2. On first start, the container downloads the ESP32 toolchain into a Docker
   volume, which takes several minutes, then runs the tests. Rebuilding the
   container reuses the volume, so the toolchain is only downloaded once.
3. In the container terminal, use `src/build.sh` as described below.
   PlatformIO is the default there. Add `-b arduino-cli` to use arduino-cli
   instead.

To flash from the container, the board's USB port must be visible on the
Docker host as `/dev/ttyUSB*` or `/dev/ttyACM*`:

- **Linux**: works as is.
- **Windows (WSL 2)**: attach the board to WSL first with
  [usbipd-win](https://learn.microsoft.com/windows/wsl/connect-usb)
  (`usbipd attach --wsl --busid <id>`), and run Docker inside WSL.

### Command line (no IDE)

`src/build.sh` builds, flashes and monitors the firmware from a terminal.
It uses **arduino-cli** if it is installed, otherwise **PlatformIO**.
Install either one:

```bash
src/build.sh                        # compile
```

```bash
src/build.sh upload -p /dev/ttyUSB0 # compile and flash
```

```bash
src/build.sh monitor                # serial monitor at 57600 baud
```

```bash
src/build.sh flash                  # flash, then open the monitor
```

| Option | Meaning |
|---|---|
| `-p`, `--port` | Serial port (`/dev/ttyUSB0`, `COM3`). Detected automatically if omitted |
| `-b`, `--backend` | Force `arduino-cli` or `pio` |
| `--fqbn` | arduino-cli board, default `esp32:esp32:esp32` (ESP32 Dev Module) |
| `-e`, `--env` | PlatformIO environment from `platformio.ini`, default `esp32dev` |

Run `src/build.sh --help` for every command. The same `secrets.h` rules
apply as in the IDE.


## Configuration (`src/blinds/config.h`)

| Setting | Default | Meaning |
|---|---|---|
| `RF_TX_PIN` | `4` | GPIO wired to the transmitter's DATA pin |
| `STATUS_LED_PIN` | `2` | LED that blinks while sending, `-1` = none |
| `RF_USE_RMT` | `1` | Use the RMT peripheral for exact timing. `0` = bit-bang |
| `RF_UNIT_US` | `306` | Protocol time unit, measured from the remote |
| `RF_GAP_US` | `23600` | Gap between repeated frames (46 ms period) |
| `RF_REPEATS` | `5` | Frames per command |
| `BLINDS[]` | 4 blinds, `0x11`–`0x14` | Names and RF IDs shown in the UI/API |
| `SERIAL_BAUD` | `57600` | USB serial speed |

In `secrets.h`: `WIFI_SSID`, `WIFI_PASSWORD`, and optionally `HOSTNAME`
(default `rfblinds`), `OTA_PASSWORD` (turns on over-the-air updates) and
`API_TOKEN` (requires a token on API calls).

## Usage

### Web page

Open `http://rfblinds.local/` (or the IP shown on the serial monitor). Each
blind, and an *All blinds* row, has **Open / Stop / Close** buttons.

### REST API

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/api/blinds` | List the configured blinds |
| `GET` / `POST` | `/api/blind?id=<1-4\|all>&cmd=<open\|stop\|close>` | Send a command |
| `GET` / `POST` | `/api/blind?code=0x11&cmd=open` | Send to a raw RF ID |

`up` and `down` are also accepted for `cmd`. If `API_TOKEN` is set, add
`&token=...` or send an `X-Token` header.

Examples:

```bash
curl -X POST "http://rfblinds.local/api/blind?id=1&cmd=open"
```

```bash
curl -X POST "http://rfblinds.local/api/blind?id=all&cmd=close"
```

A successful call returns:

```json
{"ok":true,"blind":"Blind 1","cmd":"open"}
```

### Home Assistant

```yaml
rest_command:
  blind_command:
    url: "http://rfblinds.local/api/blind?id={{ blind }}&cmd={{ cmd }}"
    method: post

cover:
  - platform: template
    covers:
      living_room_blind:
        friendly_name: "Living room blind"
        open_cover:
          service: rest_command.blind_command
          data: { blind: 1, cmd: open }
        close_cover:
          service: rest_command.blind_command
          data: { blind: 1, cmd: close }
        stop_cover:
          service: rest_command.blind_command
          data: { blind: 1, cmd: stop }
```

### USB serial (compatible with the Arduino Nano version)

Each command is a single byte at 57600 baud:

```
bit 7..6  command   0 = open, 1 = stop, 2 = close
bit 5..0  blind ID  0x11 .. 0x14
```

For example, `0x11` opens blind 1, `0x51` stops it and `0x91` closes it.
Sending `0xFF` turns on debug output, which prints each code as it is sent.

```bash
pip install pyserial
```

```bash
python3 src/tools/blinds_serial.py /dev/ttyUSB0 1 open
```

```bash
python3 src/tools/blinds_serial.py /dev/ttyUSB0 all close --debug
```

## RF protocol in short

Each frame is 24 bits, `[blind ID][0x65][0x30 + cmd]`, sent MSB first after
a 1T start pulse, with T = 306 µs. Each bit is LOW then HIGH: a 0 is
LOW 2T + HIGH 1T and a 1 is LOW 1T + HIGH 2T. The frame repeats every
~46 ms. The full analysis, captures and decoder are in
[`rf-analysis/README.md`](rf-analysis/README.md).

## Tests

These run on a PC and need no ESP32. They need `g++`, `python3` and `numpy`.

```bash
src/test/run_tests.sh
```

The script unit-tests the frame encoder and checks that its codes match the
original Nano firmware. It then renders every command as a simulated SDR
recording and decodes it with `rf-analysis/decode_blinds.py`, to check that
the ESP32 sends exactly what the remote sends.