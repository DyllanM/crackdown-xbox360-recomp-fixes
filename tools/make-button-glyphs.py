#!/usr/bin/env python3
"""Renders packaging/glyphs/svg/*.svg into the button glyph sheet: button_glyphs.dds, button_glyphs.toml and preview.png.

Developer tool: the generated files are committed, so building the port needs no SVG converter.
Needs Microsoft Edge or Chrome for the rasterization; the rest is the Python standard library.
The sheet is a grid of 128x128 cells: the 40 controller glyphs first, in font atlas order (char U+0190 + n),
then the keyboard/mouse keycaps starting on a fresh row. The DDS is uncompressed A8R8G8B8, one mip level,
straight (non-premultiplied) alpha. Refuses to run when a manifest name is unknown or duplicated, or when an
SVG is missing or unreferenced. Key names are the keybind names of rexglue's src/ui/keybinds.cpp
(checked against out/rexglue-sdk-src when present) plus a few prompt-only names (EXTRA_KEYS).

  python tools/make-button-glyphs.py
  python tools/make-button-glyphs.py --browser /path/to/chrome
"""
import argparse
import base64
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
GLYPHS = ROOT / "packaging" / "glyphs"
SVGS = GLYPHS / "svg"
SDK_KEYBINDS = ROOT / "out" / "rexglue-sdk-src" / "src" / "ui" / "keybinds.cpp"
CELL = 128
COLUMNS = 16
FIRST_CHAR = 0x0190
BROWSERS = (
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    "microsoft-edge", "google-chrome", "chromium", "chromium-browser",
)

# Controller glyphs in font atlas order: entry n replaces the glyph of char U+0190 + n.
# Slot 8 (BACK) has no token string of its own in the game's table; integrators should key off `char`.
CONTROLLER = (
    ("<A>", "pad-a"), ("<B>", "pad-b"), ("<X>", "pad-x"), ("<Y>", "pad-y"), ("<LIVE>", "pad-live"),
    ("<BLANK2>", "pad-blank2"), ("<BLANK3>", "pad-blank3"), ("<S>", "pad-menu"), ("<BACK>", "pad-view"),
    ("<LB>", "pad-lb"), ("<RB>", "pad-rb"), ("<LT>", "pad-lt"), ("<RT>", "pad-rt"),
    ("<L>", "pad-stick-l"), ("<R>", "pad-stick-r"),
    ("<LUL>", "pad-stick-l-ul"), ("<LL>", "pad-stick-l-l"), ("<LDL>", "pad-stick-l-dl"), ("<LD>", "pad-stick-l-d"),
    ("<LDR>", "pad-stick-l-dr"), ("<LR>", "pad-stick-l-r"), ("<LUR>", "pad-stick-l-ur"), ("<LU>", "pad-stick-l-u"),
    ("<RUL>", "pad-stick-r-ul"), ("<RL>", "pad-stick-r-l"), ("<RDL>", "pad-stick-r-dl"), ("<RD>", "pad-stick-r-d"),
    ("<RDR>", "pad-stick-r-dr"), ("<RR>", "pad-stick-r-r"), ("<RUR>", "pad-stick-r-ur"), ("<RU>", "pad-stick-r-u"),
    ("<DUL>", "pad-dpad-ul"), ("<DL>", "pad-dpad-l"), ("<DDL>", "pad-dpad-dl"), ("<DD>", "pad-dpad-d"),
    ("<DDR>", "pad-dpad-dr"), ("<DR>", "pad-dpad-r"), ("<DUR>", "pad-dpad-ur"), ("<DU>", "pad-dpad-u"),
    ("<D>", "pad-dpad"),
)
CONTROLLER_COUNT = 40

# Every name of kKeyNames in rexglue src/ui/keybinds.cpp (F13-F24 have no glyph).
KEYBIND_NAMES = frozenset(
    [f"F{i}" for i in range(1, 25)] + list("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")
    + ["Backtick", "Minus", "Plus", "Comma", "Period", "Semicolon", "Slash", "Backslash", "LBracket", "RBracket",
       "Quote", "Escape", "Return", "Space", "Tab", "Backspace", "Delete", "Insert", "Home", "End", "PageUp",
       "PageDown", "Left", "Right", "Up", "Down", "Shift", "Control", "Alt"]
    + [f"Numpad{i}" for i in range(10)]
    + ["NumpadEnter", "NumpadPlus", "NumpadMinus", "NumpadStar", "NumpadSlash", "PrintScreen", "Pause",
       "CapsLock", "NumLock", "ScrollLock", "LMB", "RMB", "MMB"])
# Prompt-only glyphs with no keybind of their own.
EXTRA_KEYS = frozenset(("WheelUp", "WheelDown", "MouseMove", "WASD", "Arrows"))

MOUSE_FILES = {"LMB": "mouse-lmb", "RMB": "mouse-rmb", "MMB": "mouse-mmb", "WheelUp": "mouse-wheelup",
               "WheelDown": "mouse-wheeldown", "MouseMove": "mouse-move"}
KEYS = tuple(
    list("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") + [f"F{i}" for i in range(1, 13)]
    + ["Backtick", "Minus", "Plus", "Comma", "Period", "Semicolon", "Slash", "Backslash", "LBracket", "RBracket",
       "Quote", "Escape", "Return", "Space", "Tab", "Backspace", "Delete", "Insert", "Home", "End", "PageUp",
       "PageDown", "Left", "Right", "Up", "Down", "Shift", "Control", "Alt", "CapsLock"]
    + [f"Numpad{i}" for i in range(10)]
    + ["NumpadEnter", "NumpadPlus", "NumpadMinus", "NumpadStar", "NumpadSlash", "PrintScreen", "Pause", "NumLock",
       "ScrollLock", "LMB", "RMB", "MMB", "WheelUp", "WheelDown", "MouseMove", "WASD", "Arrows"])


def key_file(name):
    return MOUSE_FILES.get(name, "key-" + name.lower())


def find_browser(requested):
    for candidate in (requested,) if requested else BROWSERS:
        found = shutil.which(candidate)
        if found:
            return found
    sys.exit("No Edge or Chrome found: pass --browser")


def check_manifest():
    tokens = [token for token, _ in CONTROLLER]
    if len(tokens) != CONTROLLER_COUNT or len(set(tokens)) != len(tokens):
        sys.exit(f"Controller list must hold {CONTROLLER_COUNT} unique tokens")
    known = set(KEYBIND_NAMES)
    if SDK_KEYBINDS.exists():
        sdk = set(re.findall(r'\{"([^"]+)", VirtualKey::', SDK_KEYBINDS.read_text(encoding="utf-8")))
        if sdk:
            known = sdk
    seen = set()
    for name in KEYS:
        if name in seen:
            sys.exit(f"Duplicate key name {name}")
        if name not in known and name not in EXTRA_KEYS:
            sys.exit(f"Unknown key name {name}: not in keybinds.cpp nor EXTRA_KEYS")
        seen.add(name)
    files = [svg for _, svg in CONTROLLER] + [key_file(name) for name in KEYS]
    on_disk = {path.stem for path in SVGS.glob("*.svg")}
    missing = [name for name in files if name not in on_disk]
    unused = sorted(on_disk - set(files))
    if missing or unused:
        sys.exit(f"SVG mismatch: missing {missing}, unreferenced {unused}")
    return files


def layout(count):
    """Cell (column, row) of every entry: controller glyphs first, keycaps from the next full row."""
    key_row = -(-CONTROLLER_COUNT // COLUMNS)
    cells = [(n % COLUMNS, n // COLUMNS) for n in range(CONTROLLER_COUNT)]
    cells += [(n % COLUMNS, key_row + n // COLUMNS) for n in range(count - CONTROLLER_COUNT)]
    return cells, key_row + -(-(count - CONTROLLER_COUNT) // COLUMNS)


def render(browser, files, cells, width, height, work):
    images = []
    for name, (column, row) in zip(files, cells):
        uri = base64.b64encode((SVGS / f"{name}.svg").read_bytes()).decode()
        images.append(f'<img src="data:image/svg+xml;base64,{uri}" width="{CELL}" height="{CELL}" '
                      f'style="position:absolute;left:{column * CELL}px;top:{row * CELL}px">')
    page = work / "sheet.html"
    page.write_text('<!doctype html><body style="margin:0;background:transparent">' + "".join(images),
                    encoding="utf-8")
    out = work / "sheet.png"
    subprocess.run(
        [browser, "--headless", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
         "--default-background-color=00000000", f"--user-data-dir={work / 'profile'}",
         f"--window-size={width},{height}", f"--screenshot={out}", page.as_uri()],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)
    png = out.read_bytes()
    size = struct.unpack(">IIBBBBB", png[16:29])
    if (size[0], size[1], size[2], size[3], size[6]) != (width, height, 8, 6, 0):
        sys.exit(f"Sheet render is not an 8-bit RGBA {width}x{height} non-interlaced PNG")
    return png


def rgba_pixels(png):
    """Decodes an 8-bit RGBA non-interlaced PNG into one bytearray, row-major."""
    width, height = struct.unpack(">II", png[16:24])
    data, pos = b"", 8
    while pos < len(png):
        length, kind = struct.unpack(">I4s", png[pos:pos + 8])
        if kind == b"IDAT":
            data += png[pos + 8:pos + 8 + length]
        pos += 12 + length
    raw, stride = zlib.decompress(data), width * 4
    pixels, prev = bytearray(), bytearray(stride)
    for y in range(height):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if kind == 1:
            for x in range(4, stride):
                line[x] = (line[x] + line[x - 4]) & 255
        elif kind == 2:
            line = bytearray((a + b) & 255 for a, b in zip(line, prev))
        elif kind == 3:
            for x in range(stride):
                line[x] = (line[x] + ((line[x - 4] if x >= 4 else 0) + prev[x]) // 2) & 255
        elif kind == 4:
            for x in range(stride):
                left = line[x - 4] if x >= 4 else 0
                up = prev[x]
                corner = prev[x - 4] if x >= 4 else 0
                p = left + up - corner
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                line[x] = (line[x] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 255
        pixels += line
        prev = line
    return pixels


def build_dds(rgba, width, height):
    bgra = bytearray(len(rgba))
    bgra[0::4], bgra[1::4], bgra[2::4], bgra[3::4] = rgba[2::4], rgba[1::4], rgba[0::4], rgba[3::4]
    # DDSD_CAPS|HEIGHT|WIDTH|PITCH|PIXELFORMAT|MIPMAPCOUNT; DDPF_RGB|DDPF_ALPHAPIXELS; DDSCAPS_TEXTURE.
    header = struct.pack("<4s7I44x", b"DDS ", 124, 0x2100F, height, width, width * 4, 0, 1)
    header += struct.pack("<8I", 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    header += struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    return header + bytes(bgra)


def build_preview(rgba, width, height, grey=0x80):
    alpha = rgba[3::4]
    out = bytearray(width * height * 3)
    for channel in range(3):
        out[channel::3] = bytes((c * a + grey * (255 - a) + 127) // 255 for c, a in zip(rgba[channel::4], alpha))
    stride = width * 3
    raw = b"".join(b"\0" + bytes(out[y * stride:(y + 1) * stride]) for y in range(height))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def build_toml(cells, width, height):
    lines = [
        "# Generated by tools/make-button-glyphs.py from packaging/glyphs/svg; do not edit by hand.",
        "# Rectangles are pixels in button_glyphs.dds. Alpha is straight (not premultiplied).",
        "", "[sheet]", 'file = "button_glyphs.dds"', 'format = "A8R8G8B8"', 'alpha = "straight"',
        f"cell = {CELL}", f"columns = {COLUMNS}", f"width = {width}", f"height = {height}", "",
        f"# {CONTROLLER_COUNT} controller glyphs in font atlas order: entry n replaces char U+0190 + n.",
        "# <BACK> (U+0198) has no token string of its own in the game; match on `char`.",
    ]
    for n, (token, svg) in enumerate(CONTROLLER):
        column, row = cells[n]
        lines += ["", "[[controller]]", f'token = "{token}"', f"char = 0x{FIRST_CHAR + n:04X}", f'svg = "{svg}.svg"',
                  f"x = {column * CELL}", f"y = {row * CELL}", f"w = {CELL}", f"h = {CELL}"]
    lines += ["", "# Keyboard/mouse keycaps; `name` is the keybind name of rexglue src/ui/keybinds.cpp,",
              "# except the prompt-only names " + ", ".join(sorted(EXTRA_KEYS)) + "."]
    for n, name in enumerate(KEYS):
        column, row = cells[CONTROLLER_COUNT + n]
        lines += ["", "[[key]]", f'name = "{name}"', f'svg = "{key_file(name)}.svg"',
                  f"x = {column * CELL}", f"y = {row * CELL}", f"w = {CELL}", f"h = {CELL}"]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--browser", help="Edge or Chrome executable")
    browser = find_browser(parser.parse_args().browser)
    files = check_manifest()
    cells, rows = layout(len(files))
    width, height = COLUMNS * CELL, rows * CELL
    with tempfile.TemporaryDirectory() as work:
        png = render(browser, files, cells, width, height, pathlib.Path(work))
    rgba = rgba_pixels(png)
    (GLYPHS / "button_glyphs.dds").write_bytes(build_dds(rgba, width, height))
    (GLYPHS / "button_glyphs.toml").write_bytes(build_toml(cells, width, height).encode("utf-8"))
    (GLYPHS / "preview.png").write_bytes(build_preview(rgba, width, height))
    print(f"Wrote {width}x{height} sheet ({CONTROLLER_COUNT} controller + {len(KEYS)} key glyphs) to {GLYPHS}")


if __name__ == "__main__":
    main()
