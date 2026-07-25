# claude-dude daemon

A small Windows tray app that reads your local Claude usage/quota and pushes it to the
paired claude-dude device over BLE, independent of whether Claude Desktop is open.

## What it does

1. Reads your Claude OAuth access token from the local credentials file Claude Code /
   Claude Desktop already maintains (`~/.claude/.credentials.json` by default — see
   `credentials.py` for the full search order). **This token never leaves your machine
   except in a request to `api.anthropic.com`.** It is never logged, written elsewhere,
   or committed anywhere.
2. Every 60 seconds, makes a minimal `POST /v1/messages` call (`max_tokens: 1`) and reads
   the `anthropic-ratelimit-unified-*` response headers for session (5h) and weekly (7d)
   usage percentage and reset countdowns.
3. Connects to your `Claude-XXXX` device as a BLE central and writes a `cmd:"usage"` line
   — see [../docs/PROTOCOL.md](../docs/PROTOCOL.md) for the exact shape.
4. Shows a tray icon: **green** connected, **amber** searching for the device, **red**
   an error (auth failure, no credentials file, no GATT access — see the note below).

**Honest caveat:** step 2 is a real API call, not a free status check — Anthropic doesn't
expose rate-limit headers any other way. `max_tokens: 1` keeps the cost and latency
negligible, but it isn't literally zero, and it happens every 60 seconds whether or not
you're actively using Claude.

**Known limitation — one connection at a time.** The original design called for the
daemon to connect as a *second* BLE central alongside Claude Desktop's own connection, so
usage data and live session status could both stay fresh simultaneously. Tested on
Windows: it doesn't work that way. Once Desktop holds the connection, Windows exposes only
the device's standard GAP/GATT services to a second local app — the ESP32's own NUS
service is invisible, so the daemon can't write to it (shows red, "connected" briefly then
errors on write). With Desktop fully closed, the daemon connects and works normally. In
practice: **live session status when Desktop's open, usage data when it's closed, not
both at once.** See the "Two BLE centrals" row in `../PLAN.md`'s Risks table.

## Setup

Requires Python 3.11+ on Windows (native path — not a WSL mount; BLE needs the Windows
Bluetooth stack via `bleak`'s WinRT backend).

```powershell
cd daemon
python -m venv .venv
.venv\Scripts\pip install -r requirements.txt
.venv\Scripts\python daemon.py
```

Pair the device with Claude Desktop first (Developer → Open Hardware Buddy → Connect) so
it's bonded — the daemon connects to whatever's advertising as `Claude-*`, but writes to
the NUS characteristics require the OS-level encrypted bond Desktop's pairing already
established.

### Run on login

```powershell
.venv\Scripts\python install_autostart.py
```

Drops a launcher in your Startup folder that runs the daemon hidden (no console window)
using this same venv's `pythonw.exe`. To remove it: `install_autostart.py --uninstall`.

## Troubleshooting

- **Red icon, "no Claude credentials file found"** — you haven't signed into Claude Code
  or Claude Desktop on this machine, or `CLAUDE_CREDENTIALS_PATH`/`CLAUDE_CONFIG_DIR`
  point somewhere unexpected.
- **Red icon, "unauthorized"** — the access token expired. Open Claude Code or Claude
  Desktop once to refresh it; the daemon re-reads the file on every poll, it doesn't
  cache a stale token.
- **Amber forever** — the device isn't advertising (powered off, out of range) or isn't
  named `Claude-*`. Check `pio device monitor` for `[ble] advertising as '...'`.
