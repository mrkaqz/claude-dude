# claude-dude — a Claude status + quota display for the LilyGO T-Display-S3

Target repo: **https://github.com/mrkaqz/claude-dude** (exists, currently empty)

## Context

You have a LilyGO T-Display-S3 (non-touch) and you want one device that does the useful half
of two existing projects:

- **When Claude is working** — show what it's doing now, how many sessions are
  total / running / waiting, and anything needing your attention (like
  [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy)).
- **When it's idle** — show usage and remaining quota (like
  [Clawdmeter](https://github.com/HermannBjorgvin/Clawdmeter)).

This is **not** a merge of the two codebases. It's a new project that forks one and
reimplements the other, because of two constraints found during research:

**Licensing.** buddy is **MIT** (Copyright 2026 Anthropic, PBC) — forkable with attribution.
Clawdmeter has **no LICENSE file** (GitHub's `license` field is `null`), so it is
all-rights-reserved by default. We therefore fork buddy's firmware and **reimplement the
usage daemon clean-room**. That's not a real obstacle: the technique is just calling
`api.anthropic.com/v1/messages` with your own OAuth token and reading the
`anthropic-ratelimit-unified-*` response headers — ordinary use of Anthropic's API, not
Clawdmeter's IP. We copy no Clawdmeter code and none of its Clawd pixel art.

**Transport.** buddy's wire protocol is Nordic UART + line-delimited JSON dispatched on a
`cmd` field. That means our own daemon can connect as a *second BLE central* and push
`{"cmd":"usage",...}` down the same characteristic the Desktop app uses. One BLE stack
(Bluedroid, inherited from buddy and untouched), one service, no protocol merge.

### Decisions taken

| | |
|---|---|
| Orientation | **Landscape 320×170** — the board's natural orientation, and the only one where a status line + total/running/waiting row + two usage bars fit comfortably |
| Idle screen | Usage + reset countdowns + **original pixel art** (new mascot, not buddy's characters, not Clawd) |
| Buttons | **None. The device is display-only.** No approve/deny, no screen cycling — screens switch automatically |
| Base | Fork `anthropics/claude-desktop-buddy` (MIT) |
| Daemon | New, clean-room Python |

Because there are no buttons, a permission prompt is **shown, not actioned** — the device
tells you Claude needs you, you act on the PC. Screen selection is entirely automatic.

### Target hardware (verified)

| | |
|---|---|
| SoC | ESP32-S3R8 — 16 MB flash, 8 MB **OPI** PSRAM (`memory_type = qio_opi`) |
| PlatformIO board | `lilygo-t-display-s3` |
| Panel | ST7789, **170×320 native, 8-bit parallel (i80)** → used at rotation 1/3 for 320×170 |
| Data pins | D0–D7 = `39,40,41,42,45,46,47,48` |
| Control | `WR=8  RD=9  DC=7  CS=6  RST=5  BL=38` (backlight active HIGH) |
| Power enable | **`GPIO15` must be driven HIGH** or the panel stays dark |
| Battery | ADC on `GPIO4`, 2:1 divider |
| Absent | no touch, no IMU, no PMU, no buzzer, no external RTC |
| USB | native CDC → `-DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1` |

Toolchain already present on this machine: `pio`, Python 3.13, git, esptool. `COM16` is the
likely board port.

---

## Architecture

```
   Claude Desktop  ──BLE(NUS)──┐
   (Hardware Buddy window)     │
                               ├──►  claude-dude  (ESP32-S3, 320×170)
   claude-dude daemon ─BLE(NUS)┘        automatic screen state machine
   (our Python tray app)
        │
        └─► api.anthropic.com/v1/messages  (reads rate-limit headers)
```

Two BLE centrals, one peripheral, one Nordic UART service:

| UUID | Role |
|---|---|
| `6e400001-b5a3-f393-e0a9-e50e24dcca9e` | NUS service |
| `6e400002-…` | RX — desktop→device **and** daemon→device |
| `6e400003-…` | TX — device→desktop |

**Desktop → device** (existing, unchanged): heartbeat every ~10 s plus on change —
`{"total":3,"running":1,"waiting":1,"msg":"approve: Bash","entries":[…],"tokens":184502,
"tokens_today":31200,"prompt":{"id":"req_abc123","tool":"Bash","hint":"rm -rf /tmp/foo"}}`,
plus `{"time":[epoch,tzoffset]}` and `{"cmd":"owner","name":"…"}` on connect.

**Daemon → device** (new, ours):

```json
{"cmd":"usage","session_pct":45,"session_reset_s":7200,
 "week_pct":28,"week_reset_s":432000,"plan":"pro","ok":true}
```

Deliberately explicit key names rather than Clawdmeter's abbreviations. `ok:false` carries a
`"err"` string so the device can show *why* usage is stale instead of showing nothing.

A useful property of this split: the usage screen keeps working when Claude Desktop is
closed, because the daemon is an independent connection.

### Screen state machine

Driven by the heartbeat, no user input:

| State | Condition | Shows |
|---|---|---|
| **ATTENTION** | `prompt` present, or `waiting > 0` | tool + hint, attention badge, counts |
| **ACTIVE** | `running > 0` | `msg` summary, total/running/waiting, working sprite |
| **IDLE** | no running/waiting for > 30 s | session + weekly bars, resets, tokens today, sleeping sprite |
| **NO-DESKTOP** | no heartbeat > 30 s (keepalive is 10 s) | idle screen + small "desktop offline" marker |

A watchdog forces a return to IDLE if any non-idle state persists without a fresh heartbeat —
without buttons there is no manual recovery, so no state may be able to wedge.

### Screen layouts (320×170)

```
+----------------------------------------+     +----------------------------------------+
|  * Bash: rm -rf /tmp/foo       [!]     |     |  session  45%  ###########-----  2h00  |
|  ------------------------------------  |     |  weekly   28%  #######---------  5d00  |
|   3 total    1 running    1 waiting    |     |                          (~)           |
|                                        |     |  idle 12m              31.2k tok today |
+----------------------------------------+     +----------------------------------------+
          ACTIVE / ATTENTION                                    IDLE
```

---

## Build plan

### Phase 0 — Prove the board first

Do this before writing any project code; it de-risks everything downstream.

1. `mkdir C:\Users\RWongmalasit\projects` and clone the empty `mrkaqz/claude-dude`.
2. Confirm the port by diffing `Get-PnpDevice -Class Ports` across a replug. Expect `COM16`.
3. Flash LilyGO's own `factory` example from
   [Xinyuan-LilyGO/T-Display-S3](https://github.com/Xinyuan-LilyGO/T-Display-S3) to prove
   panel, pin map and USB CDC. **`GPIO15` power-enable is the #1 cause of a black screen.**
4. Record `esptool flash_id` output (flash size, PSRAM detect) so partition and
   `memory_type` settings are grounded in fact.

### Phase 1 — Scaffold the repo

Fork buddy's `src/` as the base and lay out:

```
claude-dude/
├── LICENSE              MIT, preserving "Copyright 2026 Anthropic, PBC" + your line
├── NOTICE               derived-from attribution; Clawdmeter credited as inspiration only
├── README.md            what it is, wiring, flashing, daemon setup
├── platformio.ini
├── docs/PROTOCOL.md     the cmd:"usage" extension, documented
├── src/
│   ├── main.cpp         from buddy, heavily reworked (see Phase 3)
│   ├── ble_bridge.*     from buddy, UNCHANGED
│   ├── character.*      from buddy, art swapped
│   ├── hw.h / hw.cpp    NEW — T-Display-S3 board shim
│   ├── appstate.h/.cpp  NEW — screen state machine
│   ├── usage.h/.cpp     NEW — usage snapshot + cmd:"usage" parser
│   ├── ui_active.cpp    NEW — active / attention screen
│   └── ui_idle.cpp      NEW — usage screen
├── art/                 NEW — original sprite source + generator
└── daemon/              NEW — clean-room Python daemon + Windows tray
```

Preserving buddy's MIT notice and adding a NOTICE file is required, not optional — it's the
condition on which we're allowed to fork it at all.

### Phase 2 — Board shim (`src/hw.h` / `hw.cpp`)

buddy has **no HAL**: ~50 `M5.*` call sites are scattered through a 44 KB `main.cpp`. The move
is to introduce the shim it never had. M5StickCPlus's LCD class derives from **TFT_eSPI**, so
`M5.Lcd.*` is already a TFT_eSPI call — expose a global `TFT_eSPI tft` and the display half is
a rename.

| M5 API | Replacement |
|---|---|
| `M5.begin()` | `pinMode(15,OUTPUT); digitalWrite(15,HIGH); tft.init(); tft.setRotation(1); ledcAttach(38,…)` |
| `M5.Lcd.*` | `tft.*` — drop-in |
| `TFT_eSprite` | unchanged |
| `M5.update()`, `M5.BtnA/BtnB` | **deleted** — no button input |
| `M5.Axp.ScreenBreath(n)` | `ledcWrite` on `GPIO38` |
| `M5.Axp.PowerOff()` | `esp_deep_sleep_start()` |
| `M5.Axp.GetBatVoltage()` | `analogReadMilliVolts(4) * 2 / 1000.0` |
| `M5.Axp.GetBatCurrent/GetVBusVoltage/GetTempInAXP192` | drop from the status payload |
| `M5.Imu.*`, `isFaceDown()`, `checkShake()` | **deleted** — no IMU on this board |
| `M5.Beep.*` | deleted |
| `M5.Rtc.*` | **not a loss** — Desktop sends `{"time":[epoch,tz]}` on connect → `settimeofday()` |

`platformio.ini`: `board = lilygo-t-display-s3`, `board_build.arduino.memory_type = qio_opi`,
16 MB partitions, `littlefs`; drop `m5stack/M5StickCPlus`, add `bodmer/TFT_eSPI`, keep
`bitbank2/AnimatedGIF` + `bblanchon/ArduinoJson`. Configure TFT_eSPI **entirely via
`build_flags`** (`-DUSER_SETUP_LOADED -DST7789_DRIVER -DTFT_PARALLEL_8_BIT` + the pin defines,
mirroring `User_Setups/Setup206_LilyGo_T_Display_S3.h`) so a `.pio` wipe can't lose the config.
Drop buddy's `board_build.f_cpu = 160000000L` (an M5 battery tweak).

### Phase 3 — Rewrite the UI for landscape

This is the largest chunk of new code. buddy hardcodes `const int W = 135, H = 240;` portrait
with magic numbers throughout (`CX = W/2`, `y = 70`, `H - AREA`) across `drawMenu()`,
`drawInfo()`, `drawClock()`. We are not adapting those — we're replacing them with
`ui_active.cpp` / `ui_idle.cpp` built for 320×170 and driven by `appstate`.

Keep from buddy: `ble_bridge.*` verbatim, the character/GIF rendering path
(`AnimatedGIF` + `TFT_eSprite`), and `stats.h` / `xfer.h`. Delete: the menu system, clock
mode, the button-driven navigation, and the permission *decision* path (`{"cmd":"permission"}`
is never sent — display only).

`usage.cpp` parses `cmd:"usage"`, holds the last good snapshot with a staleness timestamp, and
renders "resets in 2h00" / "5d00" countdowns locally so the bars keep counting down between
the daemon's 60 s polls.

### Phase 4 — Original art

You chose original art, so this is a real task, not a copy. Scope it small:

- **Three states**: `working` (active), `alert` (attention), `sleeping` (idle). One or two
  frames each; the sprite is a small accent in the corner, not the centrepiece — the layouts
  above give it roughly 48×48 px.
- **Pipeline**: author as GIFs, reuse buddy's `tools/prep_character.py` (MIT) to normalise
  scale, load via the existing `AnimatedGIF` path. No new firmware machinery.
- **Sequencing**: generate a placeholder sprite set procedurally (Python + PIL) in `art/` so
  the firmware is testable end-to-end from day one, then swap in the final art as a drop-in
  asset. Do not block the build on drawing.

### Phase 5 — The daemon (`daemon/`, clean-room Python)

New code, written from the documented API behaviour — no Clawdmeter source consulted while
writing it.

- **Token**: read `%USERPROFILE%\.claude\.credentials.json`, take
  `claudeAiOauth.accessToken`. Never leaves the machine except to `api.anthropic.com`.
- **Poll** every 60 s: minimal `POST https://api.anthropic.com/v1/messages` with
  `anthropic-version: 2023-06-01`, `anthropic-beta: oauth-2025-04-20`,
  `Authorization: Bearer …`, `max_tokens: 1`.
- **Read headers**: `anthropic-ratelimit-unified-5h-utilization` / `-5h-reset` and
  `-7d-utilization` / `-7d-reset`. Reset headers are epoch → seconds remaining.
- **Push** the `cmd:"usage"` line to NUS RX via `bleak`, targeting the bonded `Claude-XXXX`
  device. Exponential backoff, split into slow-search (device absent) and fast-reconnect
  (link dropped) regimes.
- **Tray**: `pystray` status icon (green connected / amber searching / red error) + autostart
  via a Startup shortcut. Must run from a **native Windows path, not a WSL share**.
- Honest caveat for the README: this makes a real (tiny, `max_tokens:1`) API call every
  60 s to read the headers. Negligible, but not zero.

### Phase 6 — Publish

`git init`, commit, push to `mrkaqz/claude-dude`. The repo is **public** — the README must
state that `.credentials.json` is read locally and never committed, and `.gitignore` must
cover `daemon/config`, `.venv/`, `.pio/`.

---

## Risks

| Risk | Handling |
|---|---|
| **Two BLE centrals at once** (Desktop + daemon) — the top technical unknown | **Tested, does not hold.** Confirmed on real hardware: with Claude Desktop connected, the daemon (bleak/WinRT) can still open a connection, but Windows only exposes the standard GAP/GATT services to it — the ESP32's own NUS service is invisible, so no write is possible. With Desktop fully closed, the daemon connects and writes fine — each side works alone, they just can't hold the link simultaneously. NimBLE's `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` (3) and restarting advertising on every `onConnect` (not just `onDisconnect`) rule out the device side; this is a Windows BLE central limitation, not a firmware bug. **Accepted for now:** live session status when Desktop's open, usage data when it's closed, not both at once — the Wi-Fi fallback below remains available if this needs revisiting. |
| ST7789 **35 px GRAM offset** | 240×320 controller showing a 170-wide window. Verify with a full-screen fill + 1 px border touching all four edges *before* trusting any layout. |
| Hardware Buddy is a **developer-mode feature**, not a supported product surface | The wire protocol can change without notice. Pin `docs/PROTOCOL.md` to the observed shape and treat unknown fields as ignorable. |
| **No buttons** → no manual recovery | The watchdog reverting to IDLE is a correctness requirement, not a nicety. |
| `oauth-2025-04-20` beta header / header names change | Daemon must degrade to `ok:false` + `err` and the device must show stale-with-reason, never a blank or frozen screen. |
| Art becomes the critical path | Placeholder-first sequencing in Phase 4 exists specifically to prevent this. |

## Verification

Each gate must pass before the next:

1. **Board alive** — LilyGO `factory` example displays; `pio device monitor -p COM16` shows CDC output.
2. **Panel correct** — full-screen R/G/B fill + 1 px border on all four edges at rotation 1. Catches the 35 px offset and rotation errors.
3. **Compiles** — `pio run` clean on the forked tree.
4. **BLE to Desktop** — Claude Desktop → Developer Mode → Developer → Open Hardware Buddy → Connect → `Claude-XXXX`. Heartbeat visibly updates counts on screen.
5. **Two centrals** — daemon connects *while* Desktop is connected; both keep delivering for 10+ minutes without dropping. **This is the make-or-break test** — run it before finishing the UI.
6. **Usage accuracy** — session % and weekly % on screen match `/status` in Claude Code.
7. **State machine** — start a long Claude Code task and watch ACTIVE appear; trigger a permission prompt and watch ATTENTION show the tool name; let it settle and watch IDLE return after 30 s. Then close Claude Desktop and confirm the usage screen survives.
8. **Watchdog** — kill the Desktop connection mid-ATTENTION and confirm the device falls back to IDLE rather than wedging.
9. **Cold boot** — power-cycle with both hosts running; device reconnects unattended with no button press available.

## Open questions to settle during the build

- Does `Arduino_ESP32LCD8`-class 8-bit parallel throughput matter here, or is TFT_eSPI's parallel path fast enough at 320×170? (Expected: fine — we're drawing text and bars, not video.)
- Is the PSRAM on your specific unit actually `qio_opi`? Confirm from Phase 0 output rather than assuming.
- Worth upstreaming the T-Display-S3 board shim to buddy as a PR once it works? Its README explicitly anticipates forks that "swap those drivers."

---

## Reference material gathered during research

Kept here so a later session doesn't have to re-derive it.

**buddy wire protocol** (`anthropics/claude-desktop-buddy`, `REFERENCE.md`) — NUS,
UTF-8 JSON, one object per line, `\n` terminated.

Desktop → device:
```json
{"total":3,"running":1,"waiting":1,"msg":"approve: Bash",
 "entries":["10:42 git push","10:41 yarn test"],
 "tokens":184502,"tokens_today":31200,
 "prompt":{"id":"req_abc123","tool":"Bash","hint":"rm -rf /tmp/foo"}}
{"cmd":"status"}   {"cmd":"name","name":"Clawd"}   {"cmd":"owner","name":"Felix"}
{"cmd":"unpair"}   {"time":[1775731234,-25200]}
```
Device → desktop:
```json
{"cmd":"permission","id":"req_abc123","decision":"once"|"deny"}   ← we never send this
{"ack":"status","ok":true,"data":{"bat":{…},"sys":{…},"stats":{…}}}
{"evt":"turn","role":"assistant","content":[…]}
```

**Rate-limit headers** — `anthropic-ratelimit-unified-5h-utilization` / `-5h-reset`,
`anthropic-ratelimit-unified-7d-utilization` / `-7d-reset`; enterprise fallback is
`-overage-utilization` / `-overage-reset`. Reset values are epoch seconds.

**Credentials path order** — `CLAUDE_CREDENTIALS_PATH` env override, then
`CLAUDE_CONFIG_DIR/.credentials.json`, then `~/.claude/.credentials.json`,
`LOCALAPPDATA/Claude/.credentials.json`, `APPDATA/Claude/.credentials.json`.
Token lives at `claudeAiOauth.accessToken` (may be top-level or nested).

**Upstream repos** — buddy: `anthropics/claude-desktop-buddy` (MIT).
Clawdmeter: `HermannBjorgvin/Clawdmeter` (**no license** — reference for behaviour only,
never for code). Board reference: `Xinyuan-LilyGO/T-Display-S3`, pin map in
`lib/TFT_eSPI/User_Setups/Setup206_LilyGo_T_Display_S3.h`.
