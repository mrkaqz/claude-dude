# claude-dude

A Claude status + quota display for the [LilyGO T-Display-S3](https://github.com/Xinyuan-LilyGO/T-Display-S3)
(non-touch). One small screen, two jobs:

- **When Claude is working** — what it's doing, how many sessions are total / running /
  waiting, and anything that needs you (a permission prompt, shown not actioned — there are
  no buttons, you act on the PC).
- **When it's idle** — usage and remaining quota, with countdowns to the next reset.

Screen selection is fully automatic; the device has no buttons.

This is a fork of [anthropics/claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy)
(MIT) for the BLE bridge and character rendering path, plus a clean-room usage daemon written
against Anthropic's public API behavior. See [NOTICE](NOTICE) for the full attribution and
[docs/PROTOCOL.md](docs/PROTOCOL.md) for the wire protocol.

## Status

Under active development. Board bring-up verified on real hardware (ESP32-S3, 16MB flash,
8MB octal PSRAM). Firmware and daemon are being built out — see `PLAN.md` in the repo history
for the phased build plan.

## Hardware

| | |
|---|---|
| Board | LilyGO T-Display-S3, non-touch |
| SoC | ESP32-S3R8 — 16MB flash, 8MB OPI PSRAM |
| Panel | ST7789, 170×320 native, 8-bit parallel, used at 320×170 (rotation 1) |
| Buttons | none — display only |

## Building

Requires [PlatformIO](https://platformio.org/).

```sh
pio run                       # build
pio run -t upload -p COM16    # flash (adjust port)
pio device monitor -p COM16   # serial console
```

## Daemon

`daemon/` is a small Windows tray app that reads your local Claude OAuth token
(`~/.claude/.credentials.json` or the Claude Desktop equivalent — **read locally, never
transmitted anywhere except to `api.anthropic.com`, and never committed**), polls the
`/v1/messages` rate-limit headers every 60s, and pushes a `cmd:"usage"` line to the device
over BLE. See [daemon/README.md](daemon/README.md) for setup (once written).

## License

MIT — see [LICENSE](LICENSE). Derived-from attribution in [NOTICE](NOTICE).
