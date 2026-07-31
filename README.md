# claude-dude

A desk buddy that watches your Claude Desktop sessions — sleeps when nothing's happening,
wakes up when you start working, gets impatient when approvals pile up. This is a port of
[anthropics/claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy)
(MIT licensed) from the M5StickC Plus to the **LilyGO T-Display-S3**, in landscape:

```
+--------------------------------------------------+
|                        |  Buddy's name            |
|                        |  busy                     |
|      pet / GIF         |  ------------------------ |
|      (left half)       |  mood   ♥ ♥ ♡ ♡            |
|                        |  fed    ●●●●●○○○○○         |
|                        |  energy ▉▉▉▉▉               |
|                        |  [Lv 3]                    |
|                        |  tokens 128.4K              |
|                        |  today  4.2K                 |
+--------------------------------------------------+
```

Pet on the left, stats stacked on the right — the T-Display-S3's USB-C port sits on the
short edge, so landscape keeps the cable from sticking straight down out of a desk mount.

## Status

**Compiles clean, not yet verified on real hardware.** The port (board shim, landscape
layout, BLE stack, character rendering) was written and built successfully with
`pio run`, but the board was disconnected when that happened, so nothing in this list has
actually been checked against a physical device yet:

- [ ] Panel initializes correctly, no GRAM offset/rotation issues
- [ ] All 7 persona states render without overlap
- [ ] BLE pairing with Claude Desktop
- [ ] Heartbeat data drives state changes (busy/attention/idle)
- [ ] Watchdog recovers from a dropped connection
- [ ] `cmd:"persona"` reaches dizzy/heart/celebrate
- [ ] bufo animates correctly in the pet column; species switching works

If you're picking this up: flash it, work through that list, and delete this section.

## Hardware

| | |
|---|---|
| Board | [LilyGO T-Display-S3](https://github.com/Xinyuan-LilyGO/T-Display-S3) |
| SoC | ESP32-S3, 16MB flash, 8MB octal PSRAM |
| Panel | ST7789, 320×170 landscape, 8-bit parallel |
| Buttons | present but not used — see below |

This board has **no IMU, no power-management chip, no RTC, no buzzer** — all present on the
M5StickC Plus upstream targets. `src/hw.h`/`src/hw.cpp` is the board shim that replaces
those API calls; see [NOTICE](NOTICE) for the full list of what that meant dropping or
replacing.

### No buttons

The buttons aren't reachable in the enclosure this is built for, so the UI is entirely
automatic — no menu, no button-driven approve/deny. Screen selection follows the persona
state machine, derived purely from the heartbeat Claude Desktop sends. A pending permission
prompt is **shown, not actioned** — you approve or deny it on the desktop, same as if the
device weren't there. The states that used to be IMU-triggered (`dizzy` on a shake, `heart`
on interaction) are reachable over BLE instead — see
[`docs/PROTOCOL.md`](docs/PROTOCOL.md).

## Building

Requires [PlatformIO](https://platformio.org/).

```sh
pio run                                # build
pio run -t uploadfs --upload-port COM16  # flash the character pack (LittleFS)
pio run -t upload --upload-port COM16    # flash firmware
pio device monitor -p COM16              # serial console
```

Adjust the port for your machine. `pio run -t uploadfs` only needs re-running when
`data/characters/` changes — the character pack lives in flash, not in the firmware image.

### Pairing

In Claude Desktop: **Help → Troubleshooting → Enable Developer Mode**, then
**Developer → Open Hardware Buddy… → Connect**, and pick `Claude-XXXX` from the list. The
device shows a 6-digit passkey to enter on the desktop side.

Pair from inside Claude Desktop itself, not through your OS's Bluetooth settings — whichever
app grabs the connection first tends to hold onto it, and Desktop's own "Connect" button
can silently no-op if something else paired first.

## Characters

Ships with [bufo](https://bufo.zone) (`characters/bufo/`, third-party art — see
[`characters/bufo/README.md`](characters/bufo/README.md)) plus 18 built-in ASCII-art
species (`src/buddies/`) that need no character pack at all. Switch between them with
`{"cmd":"species","idx":N}` (`0xFF` for the installed GIF pack), or drop a new GIF character
pack onto the Hardware Buddy window in Claude Desktop — see
[`tools/prep_character.py`](tools/prep_character.py) for how a source pack gets prepped into
device-ready GIFs.

## Protocol

The BLE wire protocol is documented in [`REFERENCE.md`](REFERENCE.md) — unchanged from
upstream, since it's genuinely hardware-agnostic. The one addition this fork makes
(`cmd:"persona"`, for triggering states that used to come from the IMU) is in
[`docs/PROTOCOL.md`](docs/PROTOCOL.md).

## License

MIT, inherited from upstream — see [LICENSE](LICENSE). [NOTICE](NOTICE) documents
everything this fork changed and why, plus the third-party bufo art's separate licensing.
