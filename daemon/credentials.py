"""Locates and reads the local Claude OAuth token.

Read locally, sent only to api.anthropic.com — never logged, never written
anywhere else. See PLAN.md's "Credentials path order" reference notes.
"""

import json
import os
from pathlib import Path


def _candidate_paths():
    env_path = os.environ.get("CLAUDE_CREDENTIALS_PATH")
    if env_path:
        yield Path(env_path)

    config_dir = os.environ.get("CLAUDE_CONFIG_DIR")
    if config_dir:
        yield Path(config_dir) / ".credentials.json"

    yield Path.home() / ".claude" / ".credentials.json"

    local_appdata = os.environ.get("LOCALAPPDATA")
    if local_appdata:
        yield Path(local_appdata) / "Claude" / ".credentials.json"

    appdata = os.environ.get("APPDATA")
    if appdata:
        yield Path(appdata) / "Claude" / ".credentials.json"


def find_credentials_path() -> Path | None:
    for path in _candidate_paths():
        if path.is_file():
            return path
    return None


class CredentialsError(RuntimeError):
    pass


def read_oauth() -> dict:
    """Returns the claudeAiOauth object: accessToken, subscriptionType, etc."""
    path = find_credentials_path()
    if not path:
        raise CredentialsError("no Claude credentials file found")

    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as e:
        raise CredentialsError(f"could not read {path}: {e}") from e

    # "may be top-level or nested" per PLAN.md — this file's shape has it
    # under claudeAiOauth, but tolerate a flatter shape too.
    oauth = data.get("claudeAiOauth", data)
    if not oauth.get("accessToken"):
        raise CredentialsError(f"no accessToken in {path}")
    return oauth
