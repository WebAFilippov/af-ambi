# AmbiLight

Screen-capture ambilight for Windows, streaming over WiFi to a WS2815 strip.

Three parts:

| Component | Path | What it does |
|---|---|---|
| Screen capture | `capture.cpp` | DXGI Desktop Duplication + compute shader; averages perimeter zones to RGB |
| Desktop app | `electron-app/` | Electron UI, monitor/LED config, UDP streaming to the strip |
| Strip firmware | `src/esp8266/` | ESP8266 D1 Mini V2, WS2815 over hardware UART, WiFi + captive portal + OTA |
| Legacy firmware | `src/esp32-c3/` | ESP32-C3 over USB CDC (older transport) |

## Data flow

```
capture.exe ──(JSON lines: zone colours)──▶ electron main.js
                                              │  resample zones → N LEDs
                                              ▼
                                     UDP :7777 (0xAA55 frame)
                                              │
                                              ▼
                              ESP8266 ──▶ WS2815 strip (GPIO2 / D4)
```

The strip's LED count is independent of the capture zone count; the app
resamples one onto the other (`electron-app/resample.js`).

## Firmware

See [`src/esp8266/README.md`](src/esp8266/README.md) for wiring, protocol and
build notes. In short:

- First boot raises a `AmbiLight-Setup` access point for Wi-Fi setup.
- Afterwards the board reconnects automatically and advertises
  `ambilight.local` (mDNS) plus a UDP announce reply.
- `http://ambilight.local` gives a status/config/OTA page.

## Build

```powershell
# capture.exe (Visual Studio 18 2026, x64)
powershell -File build.ps1

# firmware (PlatformIO is not on PATH; use the penv copy)
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
& $pio run -e esp8266-d1-mini -d src

# app
cd electron-app
yarn start
```

> PlatformIO writes to `%USERPROFILE%\.platformio` by default. If that is not
> writable (sandboxed/CI), point it at the workspace copy:
> `$env:PLATFORMIO_CORE_DIR = "$PWD\.pio-core"`.
