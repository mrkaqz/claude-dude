"""pystray status icon: green connected / amber searching / red error."""

from PIL import Image, ImageDraw

import pystray

_COLORS = {
    "connected": (52, 199, 89),
    "searching": (255, 159, 10),
    "error": (255, 69, 58),
}


def _make_image(color: tuple[int, int, int]) -> Image.Image:
    size = 64
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    pad = 6
    draw.ellipse((pad, pad, size - pad, size - pad), fill=color)
    return img


class StatusTray:
    def __init__(self, on_quit):
        self._on_quit = on_quit
        self._icon = pystray.Icon(
            "claude-dude",
            _make_image(_COLORS["searching"]),
            "claude-dude: searching for device",
            menu=pystray.Menu(pystray.MenuItem("Quit", self._quit)),
        )

    def _quit(self, icon, item):
        self._on_quit()
        icon.stop()

    def set_status(self, status: str) -> None:
        color = _COLORS.get(status, _COLORS["error"])
        self._icon.icon = _make_image(color)
        self._icon.title = f"claude-dude: {status}"

    def run(self) -> None:
        """Blocks — call from the main thread."""
        self._icon.run()

    def stop(self) -> None:
        self._icon.stop()
