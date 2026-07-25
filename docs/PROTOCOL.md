# Wire protocol

claude-dude speaks Nordic UART Service (NUS) BLE to **two independent centrals**:

| UUID | Role |
|---|---|
| `6e400001-b5a3-f393-e0a9-e50e24dcca9e` | NUS service |
| `6e400002-b5a3-f393-e0a9-e50e24dcca9e` | RX — writes from a central to the device |
| `6e400003-b5a3-f393-e0a9-e50e24dcca9e` | TX — notifications from the device |

One UTF-8 JSON object per line (`\n`-terminated), same framing both centrals use.

## Claude Desktop → device (unchanged from claude-desktop-buddy)

Hardware Buddy is a developer-mode feature, not a supported product surface. This shape is
observed behavior, not a stable contract — unknown fields must be ignored, not treated as
errors.

```json
{"total":3,"running":1,"waiting":1,"msg":"approve: Bash",
 "entries":["10:42 git push","10:41 yarn test"],
 "tokens":184502,"tokens_today":31200,
 "prompt":{"id":"req_abc123","tool":"Bash","hint":"rm -rf /tmp/foo"}}
{"cmd":"status"}
{"cmd":"name","name":"Clawd"}
{"cmd":"owner","name":"Felix"}
{"cmd":"unpair"}
{"time":[1775731234,-25200]}
```

Sent roughly every 10s (heartbeat) plus immediately on any state change.

claude-dude never sends `{"cmd":"permission",...}` back — it is display-only, there is no
button input to act on a prompt with.

## claude-dude daemon → device (new, this project)

```json
{"cmd":"usage","session_pct":45,"session_reset_s":7200,
 "week_pct":28,"week_reset_s":432000,"plan":"pro","ok":true}
```

- `session_pct` / `week_pct` — integer 0-100, from the `anthropic-ratelimit-unified-5h-utilization`
  / `-7d-utilization` response headers on `api.anthropic.com/v1/messages`.
- `session_reset_s` / `week_reset_s` — seconds remaining until reset, derived from the
  `-5h-reset` / `-7d-reset` epoch headers at poll time. The device counts these down locally
  between polls (daemon polls every 60s).
- `ok` — `false` when the last poll failed (auth, network, header shape changed). When
  `false`, the device shows the last good snapshot marked stale, never a blank screen.
- `err` — present only when `ok:false`; a short human-readable reason shown on the idle
  screen.

Key names are deliberately explicit and spelled out, unlike Clawdmeter's abbreviated field
names — this schema was designed independently for this project.

## Screen state machine

Driven entirely by the heartbeat and the usage snapshot; there are no buttons.

| State | Condition | Shows |
|---|---|---|
| `ATTENTION` | `prompt` present, or `waiting > 0` | tool + hint, attention badge, counts |
| `ACTIVE` | `running > 0` | `msg` summary, total/running/waiting, working sprite |
| `IDLE` | no running/waiting for > 30s | session + weekly usage bars, resets, tokens today, sleeping sprite |
| `NO-DESKTOP` | no Desktop heartbeat for > 30s (heartbeat interval is ~10s) | idle screen + small "desktop offline" marker |

A watchdog forces a return to `IDLE` if a non-idle state persists without a fresh heartbeat —
without buttons there is no manual recovery, so no state may wedge indefinitely.
