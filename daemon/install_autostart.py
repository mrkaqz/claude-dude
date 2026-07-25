"""One-time setup: drops a launcher into the Windows Startup folder so the
daemon runs on login. Run this from the venv you want the daemon to use
(it captures sys.executable) — and from a native Windows path, not a WSL
mount, per PLAN.md's Phase 5 note.

    python install_autostart.py            # install
    python install_autostart.py --uninstall
"""

import os
import sys
from pathlib import Path

STARTUP_DIR = Path(os.environ["APPDATA"]) / "Microsoft" / "Windows" / "Start Menu" / "Programs" / "Startup"
LAUNCHER_NAME = "claude-dude-daemon.vbs"


def install() -> None:
    daemon_dir = Path(__file__).resolve().parent
    daemon_py = daemon_dir / "daemon.py"

    pythonw = Path(sys.executable).with_name("pythonw.exe")
    if not pythonw.exists():
        pythonw = Path(sys.executable)   # falls back to a visible console window

    # A .vbs wrapper avoids a console window flashing on every login —
    # pythonw.exe alone still briefly shows one on some Windows builds.
    vbs = (
        'Set shell = CreateObject("WScript.Shell")\n'
        f'shell.Run """{pythonw}"" ""{daemon_py}""", 0, False\n'
    )
    STARTUP_DIR.mkdir(parents=True, exist_ok=True)
    launcher_path = STARTUP_DIR / LAUNCHER_NAME
    launcher_path.write_text(vbs, encoding="utf-8")
    print(f"Installed: {launcher_path}")
    print(f"Runs: {pythonw} {daemon_py}")


def uninstall() -> None:
    launcher_path = STARTUP_DIR / LAUNCHER_NAME
    if launcher_path.exists():
        launcher_path.unlink()
        print(f"Removed {launcher_path}")
    else:
        print("No autostart launcher found.")


if __name__ == "__main__":
    if "--uninstall" in sys.argv:
        uninstall()
    else:
        install()
