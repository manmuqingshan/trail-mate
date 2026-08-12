#!/ usr / bin / env python3
"""Draw T-Deck Pro EPD UI artwork and export it as LVGL I1 image descriptors.

This is deliberately an *authoring* tool, not a colour-to-monochrome converter:
every icon below is drawn from its UI meaning at its final device dimensions.
The output uses only opaque black and transparency, so edges are exact pixels,
not a thresholded or dithered derivative of another asset.

Run from the repository root:
    python scripts/generate_tdeckpro_epd_assets.py

Outputs:
    images/tdeckpro_epd/                         -- inspectable 1-bit PNG art
    modules/ui_shared/src/ui/assets/tdeckpro_epd -- LVGL I1 descriptor includes

The existing descriptors retain their public symbols.  Their C source includes
the EPD descriptor only when ARDUINO_T_DECK_PRO is selected, leaving all colour
targets byte-for-byte untouched in the #else branch.
"""

from __future__ import annotations

import math
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from PIL import Image, ImageDraw, ImageFont

from png_to_lvgl import I1, save_mono_png, write_lvgl_image


ROOT = Path(__file__).resolve().parents[1]
PNG_DIR = ROOT / "images" / "tdeckpro_epd"
LVGL_DIR = ROOT / "modules" / "ui_shared" / "src" / "ui" / "assets" / "tdeckpro_epd"
C_ASSET_DIR = ROOT / "modules" / "ui_shared" / "src" / "ui" / "assets"
ECHO_LITE_LOGO_CPP = ROOT / "modules" / "ui_mono" / "src" / "assets" / "trailmate_sleep_logo.cpp"
BLACK = (0, 0, 0, 255)


def _font_path() -> str:
    candidates = (
        Path(r"C:\Windows\Fonts\arialbd.ttf"),
        Path(r"C:\Windows\Fonts\segoeuib.ttf"),
        Path(r"C:\Windows\Fonts\arial.ttf"),
    )
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    raise RuntimeError("A Windows TrueType font is required to draw the EPD labels")


FONT_PATH = _font_path()


class Ink:
    """Native-pixel monochrome icon authoring in a 64-unit layout grid.

    Coordinates scale with the asset, but structural strokes do not.  The
    former generator scaled every requested 3/4/5-unit outline independently;
    at small sizes that rounded into a distracting mixture of one- and
    two-pixel edges.  T-Deck Pro assets instead use one intentional native
    stroke weight per icon class: two pixels for the main artwork and one for
    the compact 24-pixel status row.
    """

    def __init__(self, width: int, height: int) -> None:
        self.image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
        self.draw = ImageDraw.Draw(self.image)
        self.width = width
        self.height = height
        self.scale = min(width, height) / 64.0
        self.offset_x = (width - 64.0 * self.scale) / 2.0
        self.offset_y = (height - 64.0 * self.scale) / 2.0

    def point(self, x: float, y: float) -> tuple[int, int]:
        return (
            int(round(self.offset_x + x * self.scale)),
            int(round(self.offset_y + y * self.scale)),
        )

    def pixels(self, value: float) -> int:
        """Scale a dimension such as font size or corner radius to pixels."""
        return max(1, int(round(value * self.scale)))

    def stroke(self, value: float) -> int:
        """Return the deliberate native-pixel structural line weight."""
        del value
        return 1 if min(self.width, self.height) <= 24 else 2

    def line(self, points: list[tuple[float, float]], width: float = 3) -> None:
        self.draw.line([self.point(x, y) for x, y in points], fill=BLACK, width=self.stroke(width), joint="curve")

    def polygon(self, points: list[tuple[float, float]], *, fill: bool = False, width: float = 3) -> None:
        points_px = [self.point(x, y) for x, y in points]
        if fill:
            self.draw.polygon(points_px, fill=BLACK)
        else:
            self.draw.line(points_px + [points_px[0]], fill=BLACK, width=self.stroke(width), joint="curve")

    def ellipse(self, box: tuple[float, float, float, float], *, fill: bool = False, width: float = 3) -> None:
        coords = (*self.point(box[0], box[1]), *self.point(box[2], box[3]))
        if fill:
            self.draw.ellipse(coords, fill=BLACK)
        else:
            self.draw.ellipse(coords, outline=BLACK, width=self.stroke(width))

    def rectangle(
        self,
        box: tuple[float, float, float, float],
        *,
        fill: bool = False,
        width: float = 3,
        radius: float = 0,
    ) -> None:
        coords = (*self.point(box[0], box[1]), *self.point(box[2], box[3]))
        if radius:
            self.draw.rounded_rectangle(
                coords,
                radius=self.pixels(radius),
                fill=BLACK if fill else None,
                outline=None if fill else BLACK,
                width=self.stroke(width),
            )
        elif fill:
            self.draw.rectangle(coords, fill=BLACK)
        else:
            self.draw.rectangle(coords, outline=BLACK, width=self.stroke(width))

    def arc(self, box: tuple[float, float, float, float], start: float, end: float, width: float = 3) -> None:
        coords = (*self.point(box[0], box[1]), *self.point(box[2], box[3]))
        self.draw.arc(coords, start=start, end=end, fill=BLACK, width=self.stroke(width))

    def text_center(self, value: str, x: float, y: float, logical_size: float, *, anchor: str = "mm") -> None:
        font = ImageFont.truetype(FONT_PATH, max(6, self.pixels(logical_size)))
        self.draw.text(self.point(x, y), value, font=font, fill=BLACK, anchor=anchor)

    def text_fit(self, value: str, left: int, top: int, right: int, bottom: int, max_size: int) -> None:
        """Place legible real text inside an exact pixel rectangle."""
        for size in range(max_size, 5, -1):
            font = ImageFont.truetype(FONT_PATH, size)
            bbox = self.draw.textbbox((0, 0), value, font=font)
            if bbox[2] - bbox[0] <= right - left and bbox[3] - bbox[1] <= bottom - top:
                x = left + ((right - left) - (bbox[2] - bbox[0])) // 2
                y = top + ((bottom - top) - (bbox[3] - bbox[1])) // 2 - bbox[1]
                self.draw.text((x, y), value, font=font, fill=BLACK)
                return
        raise RuntimeError(f"'{value}' does not fit {right - left}x{bottom - top}")


def gear(ink: Ink, cx: float = 32, cy: float = 32, outer: float = 25, inner: float = 20) -> None:
    points: list[tuple[float, float]] = []
    for index in range(24):
        radius = outer if index % 3 in (0, 1) else inner
        angle = math.radians(index * 15.0 - 90.0)
        points.append((cx + math.cos(angle) * radius, cy + math.sin(angle) * radius))
    ink.polygon(points, width=3)
    ink.ellipse((cx - 8, cy - 8, cx + 8, cy + 8), width=3)


def draw_alert(ink: Ink) -> None:
    ink.polygon([(8, 27), (18, 27), (30, 17), (30, 47), (18, 37), (8, 37)], fill=True)
    ink.arc((25, 20, 45, 44), -55, 55, 4)
    ink.arc((28, 24, 39, 40), -55, 55, 3)


def draw_aprs(ink: Ink) -> None:
    ink.line([(32, 48), (32, 19)], 4)
    ink.polygon([(25, 25), (32, 13), (39, 25)], width=3)
    ink.arc((14, 14, 50, 50), 215, 325, 3)
    ink.arc((8, 8, 56, 56), 220, 320, 3)
    ink.ellipse((28, 44, 36, 52), fill=True)


def draw_area_cleared(ink: Ink) -> None:
    ink.ellipse((8, 8, 56, 56), width=5)
    ink.line([(19, 32), (28, 41), (46, 21)], 5)


def draw_basecamp(ink: Ink) -> None:
    ink.line([(7, 49), (28, 18), (57, 49)], 4)
    ink.line([(28, 18), (28, 49)], 3)
    ink.line([(18, 49), (28, 36), (39, 49)], 3)
    ink.line([(8, 50), (56, 50)], 3)
    ink.line([(45, 17), (45, 33)], 3)
    ink.polygon([(45, 17), (56, 21), (45, 25)], fill=True)


def draw_chat(ink: Ink) -> None:
    ink.rectangle((8, 13, 47, 39), radius=5, width=4)
    ink.polygon([(20, 39), (18, 48), (29, 39)], fill=True)
    ink.rectangle((24, 30, 57, 51), radius=5, width=4)
    ink.polygon([(44, 51), (48, 57), (36, 51)], fill=True)
    ink.ellipse((18, 23, 22, 27), fill=True)
    ink.ellipse((28, 23, 32, 27), fill=True)
    ink.ellipse((38, 23, 42, 27), fill=True)


def draw_contact(ink: Ink) -> None:
    ink.ellipse((19, 8, 45, 34), width=4)
    ink.arc((11, 25, 53, 62), 195, 345, 4)
    ink.line([(12, 46), (12, 55), (52, 55), (52, 46)], 4)
    ink.ellipse((10, 27, 20, 37), width=3)
    ink.ellipse((44, 27, 54, 37), width=3)


def draw_extension(ink: Ink) -> None:
    ink.line(
        [
            (10, 19),
            (23, 19),
            (23, 11),
            (34, 11),
            (34, 19),
            (47, 19),
            (47, 30),
            (55, 30),
            (55, 42),
            (47, 42),
            (47, 53),
            (34, 53),
            (34, 45),
            (23, 45),
            (23, 53),
            (10, 53),
            (10, 42),
            (18, 42),
            (18, 30),
            (10, 30),
            (10, 19),
        ],
        3,
    )


def draw_fsk(ink: Ink) -> None:
    ink.rectangle((3, 10, 61, 54), radius=8, width=4)
    ink.text_center("FSK", 32, 32, 21)


def draw_good_find(ink: Ink) -> None:
    ink.ellipse((11, 10, 42, 41), width=4)
    ink.line([(34, 34), (54, 54)], 5)
    ink.line([(18, 26), (24, 32), (35, 19)], 4)


def draw_gps(ink: Ink) -> None:
    ink.polygon([(7, 19), (24, 13), (40, 19), (57, 13), (57, 48), (40, 54), (24, 48), (7, 54)], width=3)
    ink.line([(24, 13), (24, 48)], 3)
    ink.line([(40, 19), (40, 54)], 3)
    ink.ellipse((25, 16, 39, 30), width=3)
    ink.line([(32, 29), (32, 43)], 4)
    ink.polygon([(27, 29), (37, 29), (32, 43)], fill=True)


def draw_gps_topbar(ink: Ink) -> None:
    ink.ellipse((19, 5, 45, 31), width=6)
    ink.ellipse((29, 15, 35, 21), fill=True)
    ink.polygon([(21, 27), (43, 27), (32, 55)], fill=True)


def draw_usb(ink: Ink) -> None:
    ink.rectangle((21, 15, 43, 49), radius=4, width=4)
    ink.rectangle((24, 5, 40, 17), width=3)
    ink.rectangle((27, 8, 31, 12), fill=True)
    ink.rectangle((34, 8, 38, 12), fill=True)
    ink.line([(32, 23), (32, 39)], 3)
    ink.line([(32, 23), (25, 30)], 3)
    ink.line([(32, 23), (39, 30)], 3)
    ink.ellipse((22, 29, 28, 35), fill=True)
    ink.rectangle((37, 29, 42, 34), fill=True)
    ink.polygon([(32, 39), (27, 34), (37, 34)], fill=True)


def draw_logo(ink: Ink) -> None:
    """Reuse the existing T-Echo Lite Trail Mate brand mark without redesign."""
    source_width, source_height, source_stride = 184, 116, 23
    encoded = ECHO_LITE_LOGO_CPP.read_text(encoding="utf-8")
    bitmap = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", encoded))
    if len(bitmap) != source_stride * source_height:
        raise RuntimeError("unexpected T-Echo Lite Trail Mate logo bitmap size")

    original = Image.new("RGBA", (source_width, source_height), (0, 0, 0, 0))
    pixels = original.load()
    for y in range(source_height):
        for x in range(source_width):
            if bitmap[y * source_stride + x // 8] & (0x80 >> (x % 8)):
                pixels[x, y] = BLACK

    # Keep the T-Echo Lite mark at its authored 184x116 pixels. Scaling a
    # one-bit reference by 1.25 made the logo's deliberate strokes alternate
    # between one and two pixels on the EPD; the surrounding 243x160 canvas
    # provides the required splash layout without resampling the mark itself.
    ink.image.alpha_composite(original, ((ink.width - original.width) // 2, (ink.height - original.height) // 2))


def draw_lora(ink: Ink) -> None:
    # Text is intentionally the primary silhouette: this is the discriminator
    # the previous converted icon lost.
    ink.text_fit("LoRa", 1, 6, ink.width - 1, ink.height - 4, max(9, ink.height - 4))


def draw_message(ink: Ink) -> None:
    ink.rectangle((5, 11, 59, 45), radius=8, width=4)
    ink.polygon([(18, 45), (15, 55), (28, 45)], fill=True)
    for x in (21, 32, 43):
        ink.ellipse((x - 3, 25, x + 3, 31), fill=True)


def draw_nomad(ink: Ink) -> None:
    ink.ellipse((8, 8, 56, 56), width=3)
    ink.line([(32, 11), (42, 42), (22, 51), (32, 11)], 3)
    ink.polygon([(32, 13), (38, 38), (32, 34)], fill=True)
    ink.polygon([(32, 51), (26, 26), (32, 30)], width=3)
    ink.text_center("N", 32, 5, 11, anchor="ms")


def draw_radar(ink: Ink) -> None:
    ink.arc((7, 7, 57, 57), 205, 45, 4)
    ink.arc((16, 16, 48, 48), 205, 45, 3)
    ink.line([(32, 32), (50, 17)], 4)
    ink.ellipse((28, 28, 36, 36), fill=True)
    ink.line([(9, 54), (55, 54)], 3)


def draw_rally(ink: Ink) -> None:
    ink.line([(15, 8), (15, 56)], 4)
    ink.polygon([(17, 11), (53, 18), (43, 33), (17, 27)], fill=True)
    ink.line([(10, 56), (22, 56)], 4)


def draw_rf(ink: Ink) -> None:
    ink.line([(32, 47), (32, 25)], 4)
    ink.polygon([(25, 47), (39, 47), (32, 59)], width=3)
    ink.arc((18, 12, 46, 40), 205, 335, 4)
    ink.arc((9, 3, 55, 49), 205, 335, 4)
    ink.ellipse((28, 21, 36, 29), fill=True)


def draw_room(ink: Ink) -> None:
    ink.ellipse((15, 6, 49, 40), width=4)
    ink.ellipse((27, 18, 37, 28), fill=True)
    ink.polygon([(17, 34), (47, 34), (32, 58)], fill=True)


def draw_route(ink: Ink) -> None:
    ink.ellipse((7, 39, 19, 51), width=4)
    ink.ellipse((45, 12, 57, 24), width=4)
    ink.line([(17, 45), (27, 36), (31, 40), (41, 28), (47, 19)], 3)
    ink.ellipse((27, 33, 33, 39), fill=True)


def draw_satellite(ink: Ink) -> None:
    ink.polygon([(26, 23), (39, 10), (53, 24), (40, 39)], width=4)
    ink.rectangle((7, 17, 26, 34), width=3)
    ink.rectangle((40, 29, 57, 46), width=3)
    ink.line([(15, 18), (22, 34)], 2)
    ink.line([(7, 25), (26, 25)], 2)
    ink.line([(41, 37), (56, 37)], 2)
    ink.line([(49, 30), (49, 46)], 2)
    ink.arc((40, 39, 67, 66), 205, 270, 3)
    ink.arc((35, 34, 76, 75), 205, 270, 3)


def draw_setting(ink: Ink) -> None:
    gear(ink)


def draw_shutdown(ink: Ink) -> None:
    ink.arc((10, 9, 54, 59), 35, 325, 5)
    ink.line([(32, 6), (32, 31)], 5)


def draw_sos(ink: Ink) -> None:
    ink.ellipse((6, 6, 58, 58), width=4)
    ink.text_center("SOS", 32, 32, 20)


def draw_sstv(ink: Ink) -> None:
    ink.rectangle((7, 14, 57, 49), radius=4, width=4)
    ink.line([(12, 42), (21, 34), (28, 39), (37, 22), (51, 32)], 3)
    ink.line([(17, 54), (47, 54)], 4)
    ink.line([(32, 49), (32, 54)], 3)
    ink.line([(17, 10), (11, 4)], 3)
    ink.line([(47, 10), (53, 4)], 3)


def draw_team(ink: Ink) -> None:
    ink.ellipse((24, 8, 40, 24), fill=True)
    ink.ellipse((8, 16, 23, 31), fill=True)
    ink.ellipse((41, 16, 56, 31), fill=True)
    ink.arc((18, 22, 46, 57), 190, 350, 5)
    ink.arc((4, 27, 29, 55), 195, 342, 4)
    ink.arc((35, 27, 60, 55), 198, 345, 4)
    ink.line([(8, 51), (56, 51)], 4)


def draw_team_topbar(ink: Ink) -> None:
    ink.ellipse((8, 8, 28, 28), fill=True)
    ink.ellipse((31, 11, 49, 29), fill=True)
    ink.arc((3, 20, 33, 48), 195, 345, 4)
    ink.arc((25, 22, 55, 49), 195, 345, 4)


def draw_tracker(ink: Ink) -> None:
    ink.ellipse((10, 10, 29, 42), fill=True)
    ink.ellipse((35, 23, 54, 55), fill=True)
    for x, y in ((13, 5), (20, 3), (27, 6), (37, 17), (44, 15), (51, 19)):
        ink.ellipse((x, y, x + 5, y + 6), fill=True)


def draw_tracker_topbar(ink: Ink) -> None:
    ink.ellipse((11, 6, 28, 34), fill=True)
    ink.ellipse((34, 28, 50, 55), fill=True)
    ink.ellipse((12, 2, 17, 8), fill=True)
    ink.ellipse((18, 0, 23, 6), fill=True)
    ink.ellipse((34, 24, 39, 30), fill=True)
    ink.ellipse((40, 22, 45, 28), fill=True)


def draw_walkie_monitor(ink: Ink) -> None:
    ink.rectangle((6, 14, 58, 49), radius=5, width=4)
    ink.line([(16, 37), (23, 28), (30, 37), (38, 22), (48, 32)], 3)
    ink.line([(14, 43), (50, 43)], 3)


def draw_walkie_talkie(ink: Ink) -> None:
    ink.line([(35, 12), (43, 4)], 4)
    ink.rectangle((19, 15, 45, 55), radius=4, width=4)
    ink.rectangle((25, 23, 39, 31), width=2)
    for y in (37, 42, 47):
        ink.line([(25, y), (39, y)], 2)
    ink.ellipse((26, 51, 32, 57), fill=True)
    ink.ellipse((35, 51, 41, 57), fill=True)
    ink.arc((38, 3, 62, 27), 205, 305, 3)


def draw_wifi(ink: Ink) -> None:
    ink.arc((5, 5, 59, 59), 215, 325, 5)
    ink.arc((14, 14, 50, 50), 215, 325, 5)
    ink.arc((23, 23, 41, 41), 215, 325, 4)
    ink.ellipse((28, 46, 36, 54), fill=True)


DrawFn = Callable[[Ink], None]


@dataclass(frozen=True)
class Asset:
    file_stem: str
    symbol: str
    width: int
    height: int
    draw: DrawFn


ASSETS = (
    Asset("alert", "alert", 32, 32, draw_alert),
    Asset("aprs", "aprs", 48, 48, draw_aprs),
    Asset("AreaCleared", "AreaCleared", 48, 41, draw_area_cleared),
    Asset("BaseCamp", "BaseCamp", 48, 48, draw_basecamp),
    Asset("Chat", "Chat", 48, 48, draw_chat),
    Asset("contact", "contact", 48, 48, draw_contact),
    Asset("ext", "ext", 48, 48, draw_extension),
    Asset("fsk_mod_topbar", "fsk_mod_topbar", 34, 24, draw_fsk),
    Asset("GoodFind", "GoodFind", 48, 48, draw_good_find),
    Asset("gps", "gps_icon", 48, 48, draw_gps),
    Asset("gps_topbar", "gps_topbar", 24, 24, draw_gps_topbar),
    Asset("img_usb", "img_usb", 48, 48, draw_usb),
    Asset("logo", "logo", 243, 160, draw_logo),
    Asset("lora_mod_topbar", "lora_mod_topbar", 40, 24, draw_lora),
    Asset("message_topbar", "message_topbar", 24, 24, draw_message),
    Asset("nomad", "nomad", 48, 48, draw_nomad),
    Asset("radar", "radar", 48, 48, draw_radar),
    Asset("rally", "rally", 48, 48, draw_rally),
    Asset("rf", "rf", 48, 48, draw_rf),
    Asset("room-24px", "room_24px", 24, 24, draw_room),
    Asset("route_topbar", "route_topbar", 24, 24, draw_route),
    Asset("Satellite", "Satellite", 48, 48, draw_satellite),
    Asset("Setting", "Setting", 48, 48, draw_setting),
    Asset("shutdown", "shutdown", 48, 48, draw_shutdown),
    Asset("sos", "sos", 48, 48, draw_sos),
    Asset("sstv", "sstv", 48, 48, draw_sstv),
    Asset("team", "team_icon", 48, 48, draw_team),
    Asset("team_topbar", "team_topbar", 24, 24, draw_team_topbar),
    Asset("tracker", "tracker_icon", 48, 48, draw_tracker),
    Asset("tracker_topbar", "tracker_topbar", 24, 24, draw_tracker_topbar),
    Asset("walkie_monitor_topbar", "walkie_monitor_topbar", 24, 24, draw_walkie_monitor),
    Asset("walkie_talkie", "walkie_talkie", 48, 48, draw_walkie_talkie),
    Asset("wifi_topbar", "wifi_topbar", 24, 24, draw_wifi),
)


def write_platform_include(asset: Asset) -> None:
    """Install an exact platform guard while preserving the colour C body."""
    source_path = C_ASSET_DIR / f"{asset.file_stem}.c"
    raw = source_path.read_bytes()
    expected = f"const lv_image_dsc_t {asset.symbol} =".encode("utf-8")
    if expected not in raw:
        raise RuntimeError(f"{source_path} does not export expected symbol {asset.symbol}")

    first_line = b"#if defined(ARDUINO_T_DECK_PRO)"
    if raw.startswith(first_line):
        expected_include = f'#include "tdeckpro_epd/{asset.file_stem}.inc"'.encode("utf-8")
        if expected_include not in raw.splitlines()[:3]:
            raise RuntimeError(f"{source_path} has an unexpected platform wrapper")
        return

    newline = b"\r\n" if b"\r\n" in raw else b"\n"
    wrapper = first_line + newline
    wrapper += f'#include "tdeckpro_epd/{asset.file_stem}.inc"'.encode("utf-8") + newline
    wrapper += b"#else" + newline
    wrapper += raw
    if not raw.endswith((b"\n", b"\r")):
        wrapper += newline
    wrapper += b"#endif // defined(ARDUINO_T_DECK_PRO)" + newline
    source_path.write_bytes(wrapper)


def make_preview(images: list[tuple[Asset, Image.Image]]) -> None:
    """Create an inspectable contact sheet; it is not linked into firmware."""
    columns = 5
    cell_width, cell_height = 170, 135
    rows = math.ceil(len(images) / columns)
    preview = Image.new("RGB", (columns * cell_width, rows * cell_height), "white")
    preview_draw = ImageDraw.Draw(preview)
    label_font = ImageFont.truetype(FONT_PATH, 13)
    for index, (asset, image) in enumerate(images):
        col, row = index % columns, index // columns
        left, top = col * cell_width, row * cell_height
        max_width, max_height = 122, 88
        scale = min(max_width / image.width, max_height / image.height, 2.0)
        scaled = image.resize((round(image.width * scale), round(image.height * scale)), Image.Resampling.NEAREST)
        mask = scaled.getchannel("A")
        x = left + (cell_width - scaled.width) // 2
        y = top + 8 + (max_height - scaled.height) // 2
        preview.paste((0, 0, 0), (x, y), mask)
        text = f"{asset.file_stem}  {asset.width}x{asset.height}"
        bbox = preview_draw.textbbox((0, 0), text, font=label_font)
        preview_draw.text(
            (left + (cell_width - (bbox[2] - bbox[0])) // 2, top + 106),
            text,
            font=label_font,
            fill="black",
        )
    preview.save(PNG_DIR / "contact_sheet.png")


def validate(asset: Asset, image_path: Path, descriptor_path: Path) -> None:
    with Image.open(image_path) as image:
        if image.size != (asset.width, asset.height) or image.mode != "P":
            raise RuntimeError(f"{image_path} is not the expected 1-bit palette image")
    descriptor = descriptor_path.read_text(encoding="utf-8")
    for fragment in (
        f"const lv_image_dsc_t {asset.symbol}",
        "LV_COLOR_FORMAT_I1",
        f".header.w = {asset.width}",
        f".header.h = {asset.height}",
    ):
        if fragment not in descriptor:
            raise RuntimeError(f"{descriptor_path} is missing {fragment!r}")


def main() -> None:
    PNG_DIR.mkdir(parents=True, exist_ok=True)
    LVGL_DIR.mkdir(parents=True, exist_ok=True)
    preview_images: list[tuple[Asset, Image.Image]] = []

    for asset in ASSETS:
        ink = Ink(asset.width, asset.height)
        asset.draw(ink)
        png_path = PNG_DIR / f"{asset.file_stem}.png"
        descriptor_path = LVGL_DIR / f"{asset.file_stem}.inc"
        save_mono_png(ink.image, png_path)
        write_lvgl_image(ink.image, descriptor_path, asset.symbol, I1)
        write_platform_include(asset)
        validate(asset, png_path, descriptor_path)
        preview_images.append((asset, ink.image))
        print(f"generated {asset.file_stem}: {asset.width}x{asset.height}")

    make_preview(preview_images)
    print(f"Generated {len(ASSETS)} original 1-bit T-Deck Pro assets.")


if __name__ == "__main__":
    main()
