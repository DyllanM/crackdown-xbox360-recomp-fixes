# Button glyphs

Modern button prompts that replace the game's 22-28 px Xbox 360 button glyphs at runtime, plus
keyboard/mouse keycaps for a keyboard prompt mode.

| File | What it is |
| --- | --- |
| `svg/*.svg` | The sources: one 128x128 SVG per glyph (`pad-*` controller, `key-*` keycaps, `mouse-*` mouse). |
| `button_glyphs.dds` | The sheet the runtime loads: 2048x1280, uncompressed A8R8G8B8, one mip, straight (non-premultiplied) alpha. Fully transparent pixels are black. |
| `button_glyphs.toml` | Manifest: sheet size and the pixel rectangle of every glyph. `[[controller]]` entries are in font atlas order (`char` U+0190 + n, 40 entries); `[[key]]` entries are named by the keybind names of rexglue's `src/ui/keybinds.cpp`, plus the prompt-only `WheelUp`, `WheelDown`, `MouseMove`, `WASD` and `Arrows`. |
| `preview.png` | The sheet over mid-grey, for humans. |

The art is original to this project, drawn as SVG here. It uses no platform logos: `<LIVE>` is a generic globe,
and `<BLANK2>`/`<BLANK3>` are transparent.

## Regenerating

After editing or adding an SVG (new glyphs also go in the lists at the top of the tool):

    python tools/make-button-glyphs.py
    python tools/make-button-glyphs.py --browser /path/to/chrome

It needs Microsoft Edge or Chrome to rasterize the SVGs; the rest is the Python standard library. The outputs are
committed, so building the port needs neither. Labels use Segoe UI Bold (Arial as fallback), so render on Windows
to reproduce the committed sheet exactly.
