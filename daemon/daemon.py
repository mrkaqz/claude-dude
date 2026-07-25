"""claude-dude usage daemon: entry point.

Reads the local Claude OAuth token, polls api.anthropic.com's rate-limit
headers every 60s, and pushes the result to the paired claude-dude device
as a second BLE central (see PLAN.md's two-centrals architecture note).
Runs a pystray tray icon on the main thread; the poll/BLE loop runs on a
background thread with its own asyncio event loop.
"""

import asyncio
import json
import threading

from ble_push import BlePusher
from credentials import CredentialsError, read_oauth
from tray import StatusTray
from usage_poll import UsageSnapshot, poll_usage

POLL_INTERVAL_S = 60


def _plan_hint(oauth: dict) -> str:
    return oauth.get("subscriptionType") or oauth.get("rateLimitTier") or ""


async def _poll_once(pusher: BlePusher) -> None:
    try:
        oauth = read_oauth()
        snap = poll_usage(oauth["accessToken"], plan_hint=_plan_hint(oauth))
    except CredentialsError as e:
        snap = UsageSnapshot(ok=False, err=str(e))

    payload = {"cmd": "usage", "ok": snap.ok}
    if snap.ok:
        payload.update(
            session_pct=snap.session_pct,
            session_reset_s=snap.session_reset_s,
            week_pct=snap.week_pct,
            week_reset_s=snap.week_reset_s,
            plan=snap.plan,
        )
    else:
        payload["err"] = snap.err

    await pusher.send_line(json.dumps(payload))


async def _run(pusher: BlePusher, stop_flag: threading.Event) -> None:
    stop_event = asyncio.Event()

    async def watch_stop_flag():
        while not stop_flag.is_set():
            await asyncio.sleep(0.5)
        stop_event.set()

    watcher = asyncio.create_task(watch_stop_flag())
    try:
        while not stop_event.is_set():
            await _poll_once(pusher)
            try:
                await asyncio.wait_for(stop_event.wait(), timeout=POLL_INTERVAL_S)
            except asyncio.TimeoutError:
                pass
    finally:
        watcher.cancel()
        await pusher.close()


def main() -> None:
    stop_flag = threading.Event()
    tray = StatusTray(on_quit=stop_flag.set)
    pusher = BlePusher(on_status_change=tray.set_status)

    thread = threading.Thread(target=lambda: asyncio.run(_run(pusher, stop_flag)), daemon=True)
    thread.start()

    tray.run()   # blocks on the main thread until Quit is clicked
    stop_flag.set()
    thread.join(timeout=5)


if __name__ == "__main__":
    main()
