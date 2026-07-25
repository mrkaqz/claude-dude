"""Polls api.anthropic.com for rate-limit usage via the response headers on
a minimal /v1/messages call. See docs/PROTOCOL.md for the cmd:"usage" shape
this feeds, and PLAN.md's "Rate-limit headers" reference notes for the
header names (including the enterprise -overage- fallback).

This makes a real, tiny (max_tokens:1) API call every poll — negligible
cost, but not zero. See daemon/README.md.
"""

import time
from dataclasses import dataclass

import requests

API_URL = "https://api.anthropic.com/v1/messages"
API_VERSION = "2023-06-01"
OAUTH_BETA = "oauth-2025-04-20"
PROBE_MODEL = "claude-haiku-4-5-20251001"   # cheapest/fastest model — this call exists only to read headers


@dataclass
class UsageSnapshot:
    ok: bool
    session_pct: int = 0
    session_reset_s: int = 0
    week_pct: int = 0
    week_reset_s: int = 0
    plan: str = ""
    err: str = ""


def _pct(headers: dict, key: str) -> float | None:
    v = headers.get(key)
    if v is None:
        return None
    try:
        # Headers report a 0..1 fraction.
        return float(v) * 100.0
    except ValueError:
        return None


def _reset_seconds(headers: dict, key: str, now: float) -> int | None:
    v = headers.get(key)
    if v is None:
        return None
    try:
        reset_epoch = float(v)
    except ValueError:
        return None
    return max(0, int(reset_epoch - now))


def poll_usage(access_token: str, plan_hint: str = "") -> UsageSnapshot:
    headers = {
        "anthropic-version": API_VERSION,
        "anthropic-beta": OAUTH_BETA,
        "authorization": f"Bearer {access_token}",
        "content-type": "application/json",
    }
    body = {
        "model": PROBE_MODEL,
        "max_tokens": 1,
        "messages": [{"role": "user", "content": "."}],
    }

    try:
        resp = requests.post(API_URL, headers=headers, json=body, timeout=15)
    except requests.RequestException as e:
        return UsageSnapshot(ok=False, err=f"network: {e}")

    h = resp.headers
    now = time.time()

    session_pct = _pct(h, "anthropic-ratelimit-unified-5h-utilization")
    week_pct = _pct(h, "anthropic-ratelimit-unified-7d-utilization")
    session_reset_s = _reset_seconds(h, "anthropic-ratelimit-unified-5h-reset", now)
    week_reset_s = _reset_seconds(h, "anthropic-ratelimit-unified-7d-reset", now)

    # Enterprise fallback per PLAN.md's reference notes.
    if session_pct is None:
        session_pct = _pct(h, "anthropic-ratelimit-unified-overage-utilization")
        session_reset_s = _reset_seconds(h, "anthropic-ratelimit-unified-overage-reset", now)

    if resp.status_code == 401:
        return UsageSnapshot(ok=False, err="unauthorized (token expired?)")
    if resp.status_code == 429 and session_pct is None:
        # Rate-limited and headers absent — still an error, not usage data.
        return UsageSnapshot(ok=False, err="rate limited, no usage headers")
    if session_pct is None or week_pct is None:
        return UsageSnapshot(ok=False, err=f"missing rate-limit headers (http {resp.status_code})")

    return UsageSnapshot(
        ok=True,
        session_pct=round(session_pct),
        session_reset_s=session_reset_s or 0,
        week_pct=round(week_pct),
        week_reset_s=week_reset_s or 0,
        plan=plan_hint,
    )
