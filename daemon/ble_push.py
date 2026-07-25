"""Pushes cmd:"usage" lines to the bonded claude-dude device over BLE.

Connects as a second NUS central alongside Claude Desktop's own connection
(see PLAN.md's "two BLE centrals" architecture note). Two backoff regimes,
both exponential with a cap:

- slow-search: device not currently known/visible — don't hammer the radio
  scanning for a device that isn't there.
- fast-reconnect: we had a working connection and it just dropped — that's
  often a transient radio blip, worth retrying quickly for a bit before
  falling back to a fresh scan.
"""

import asyncio
import time

from bleak import BleakClient, BleakScanner

NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
DEVICE_NAME_PREFIX = "Claude-"

FAST_RECONNECT_BASE_S = 1.0
FAST_RECONNECT_MAX_S = 10.0
FAST_RECONNECT_BUDGET_S = 30.0   # give up on the known address after this long

SLOW_SEARCH_BASE_S = 15.0
SLOW_SEARCH_MAX_S = 120.0

SCAN_TIMEOUT_S = 8.0


class BlePusher:
    def __init__(self, on_status_change=None):
        self.client: BleakClient | None = None
        self.status = "searching"   # searching | connected | error
        self._on_status_change = on_status_change
        self._known_address: str | None = None
        self._slow_search_attempt = 0
        self._next_slow_search_at = 0.0

    def _set_status(self, status: str) -> None:
        if status != self.status:
            self.status = status
            if self._on_status_change:
                self._on_status_change(status)

    async def _scan_for_device(self):
        devices = await BleakScanner.discover(timeout=SCAN_TIMEOUT_S)
        for d in devices:
            if d.name and d.name.startswith(DEVICE_NAME_PREFIX):
                return d
        return None

    async def _try_connect(self, address: str) -> bool:
        try:
            client = BleakClient(address)
            await client.connect()
            self.client = client
            self._known_address = address
            self._set_status("connected")
            return True
        except Exception:
            return False

    async def _fast_reconnect(self) -> bool:
        deadline = time.monotonic() + FAST_RECONNECT_BUDGET_S
        attempt = 0
        while time.monotonic() < deadline:
            if await self._try_connect(self._known_address):
                return True
            attempt += 1
            delay = min(FAST_RECONNECT_BASE_S * (2 ** attempt), FAST_RECONNECT_MAX_S)
            await asyncio.sleep(delay)
        self._known_address = None
        return False

    async def ensure_connected(self) -> bool:
        if self.client and self.client.is_connected:
            return True
        self.client = None

        if self._known_address:
            self._set_status("searching")
            if await self._fast_reconnect():
                return True
            # Fast-reconnect exhausted — fall through to a slow search,
            # resetting its backoff since this is a fresh cause.
            self._slow_search_attempt = 0
            self._next_slow_search_at = 0.0

        now = time.monotonic()
        if now < self._next_slow_search_at:
            self._set_status("searching")
            return False

        self._set_status("searching")
        device = await self._scan_for_device()
        if not device:
            self._slow_search_attempt += 1
            delay = min(SLOW_SEARCH_BASE_S * (2 ** self._slow_search_attempt), SLOW_SEARCH_MAX_S)
            self._next_slow_search_at = now + delay
            return False

        self._slow_search_attempt = 0
        return await self._try_connect(device.address)

    async def send_line(self, line: str) -> bool:
        if not await self.ensure_connected():
            return False
        try:
            await self.client.write_gatt_char(NUS_RX_UUID, (line + "\n").encode("utf-8"), response=True)
            return True
        except Exception:
            self._set_status("error")
            try:
                await self.client.disconnect()
            except Exception:
                pass
            self.client = None
            return False

    async def close(self) -> None:
        if self.client:
            try:
                await self.client.disconnect()
            except Exception:
                pass
            self.client = None
