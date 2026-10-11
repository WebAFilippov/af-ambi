# AmbiLight — ESP8266 firmware (WeMos D1 Mini V2)

Receives LED frames over WiFi (UDP) and drives a WS2815 strip.

## Why the UART driver

A normal FastLED sketch bit-bangs the strip with interrupts disabled. For a
512-LED strip that is ~15 ms per `show()`, during which the ESP8266 cannot
service WiFi — packets drop, the stack stalls, and the strip flickers.

This firmware defines `FASTLED_ESP8266_UART` (set in `platformio.ini`), which
selects `UARTController_ESP8266`: WS2812 bits are encoded into UART bytes and
the **hardware UART1** produces the waveform at 3.2 Mbps (4 UART bits per
1.25 µs LED bit). The CPU stays free for WiFi.

Cost: the data pin is fixed to **GPIO2** (UART1 TX-only), which is `D4` on the
D1 Mini.

## Wiring

| D1 Mini | WS2815 strip | Notes |
|---|---|---|
| `D4` (GPIO2) | `DATA` | data line — via level shifter |
| `GND` | `GND` | common ground is mandatory |
| — | `+12V` | from a **separate 12 V supply** |
| — | `BI` (backup) | tie to `GND`, or bridge as the datasheet says |

**Level shift.** WS2815 expects 5 V logic; GPIO2 is 3.3 V. It usually works but
is marginal. Use a 74HCT125/74AHCT125 shifter, or power a small sacrificial
pixel at 5 V as a buffer.

**Power.** WS2815 is 12 V, up to ~60 mA per pixel at full white. Do **not**
power the strip from the D1 Mini regulator. Use a separate 12 V supply rated
for the strip length (512 pixels ≈ 30 A worst case), with common ground.

## Build & upload

PlatformIO is not on PATH on this machine; use the penv copy:

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"

# build
& $pio run -e esp8266-d1-mini

# first flash (USB)
& $pio run -e esp8266-d1-mini -t upload

# serial monitor
& $pio device monitor -b 115200
```

## First boot & Wi-Fi setup

1. On first boot the board sees no stored credentials and raises an access
   point **`AmbiLight-Setup`**.
2. Connect a phone/PC to it; a captive portal opens at `192.168.4.1`.
3. Pick your network, enter the password, save.
4. The board reboots onto your network and advertises **`ambilight.local`**.

To reconfigure later: open `http://ambilight.local`, use "Forget Wi-Fi &
reboot", then repeat the setup flow.

## OTA updates

Open `http://ambilight.local`, choose the built
`.pio/build/esp8266-d1-mini/firmware.bin`, upload. Credentials are
`admin` / `ambilight` (the HTTP auth on `/update`).

## Frame protocol

Binary UDP datagrams on port 7777 (see `protocol.h`):

| offset | field | size |
|---|---|---|
| 0 | magic `0xAA 0x55` | 2 |
| 2 | version (`1`) | 1 |
| 3 | flags | 1 |
| 4 | seq (LE) | 4 |
| 8 | count (LE) | 2 |
| 10 | R,G,B per LED | count×3 |

- `flags & 0x01` (`kFlagClear`) blanks the strip.
- `flags & 0x02` (`kFlagDiscover`) is a PC→board probe; the board replies
  `kFlagAnnounce` with a JSON status payload.
- `flags & 0x04` (`kFlagAnnounce`) is the board→PC reply.

The Electron app (`electron-app/protocol.js`) mirrors this. Keep them in sync.
