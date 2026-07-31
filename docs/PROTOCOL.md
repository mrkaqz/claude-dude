# Protocol extensions (this fork only)

This device speaks the same Nordic UART BLE protocol as upstream
[claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) — see
[`REFERENCE.md`](../REFERENCE.md), which is unmodified from upstream and remains the source
of truth for the heartbeat snapshot, turn events, permission decisions, pairing, and the
`name`/`owner`/`unpair`/`status` commands.

This file documents the one thing this fork adds on top of that: a command to trigger a
persona state directly, since the states that were originally triggered by the M5StickC
Plus's IMU (shake → `dizzy`, a physical interaction → `heart`) have no equivalent sensor on
the T-Display-S3. See [`NOTICE`](../NOTICE) for the full list of what changed in this port
and why.

## `cmd:"persona"`

```json
{ "cmd": "persona", "name": "dizzy" }
{ "cmd": "persona", "name": "heart", "ms": 3000 }
```

| Field  | Required | Meaning                                                              |
| ------ | -------- | --------------------------------------------------------------------|
| `name` | yes      | one of `sleep`, `idle`, `busy`, `attention`, `celebrate`, `dizzy`, `heart` |
| `ms`   | no       | how long to hold the state before returning to normal, default 2000 |

Ack, same shape as every other command in `REFERENCE.md`:

```json
{ "ack": "persona", "ok": true }
```

`ok:false` means `name` was missing or didn't match one of the seven persona states — the
device didn't change anything.

Behaves exactly like the one-shot states upstream already had for level-up (`celebrate`):
it's shown for the requested duration, then the display reverts to whatever the heartbeat
data would normally derive (see `derive()` in `src/main.cpp`). It doesn't persist and isn't
acked as "connected" or any kind of session state — it's purely a display trigger.
