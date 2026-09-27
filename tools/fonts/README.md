# Fonts

Both screens render **Montserrat Medium with tabular figures**. One source TTF,
two generated forms:

| Tier | Renderer | Assets |
|---|---|---|
| `watch/` | LVGL | `watch/src/font_speed_170.c`, `font_ui_{24,28,40}.c` |
| `device/` | M5GFX (VLW) | `device/src/ui/fonts/montserrat_{12,16,24,32}.h`, `montserrat_digits_136.h` |

Regenerate everything:

```bash
tools/fonts/build.sh          # writes straight into watch/src and device/src/ui/fonts
```

Needs `python3` and `node`; first run downloads the font, creates a venv, and
pulls `lv_font_conv` via `npx`.

## Why the TTF is "frozen"

Montserrat's default digits are proportional — `1` has roughly half the advance
of `0`. Any readout whose digits change width shifts sideways as it updates:
`9.9 → 10.1` moved the watch's SOG number by ~50 px.

The fix is the OpenType `tnum` feature, which swaps in tabular digit glyphs that
share one advance. Neither renderer here can apply it: `lv_font_conv` and
M5GFX's VLW loader both read `cmap` and rasterise the default glyph for each
codepoint — no GSUB, no shaping. So `build.sh` applies the substitution to the
font binary up front with `pyftfeatfreeze -f tnum`, which rewrites `cmap` to
point at the `.tf` glyphs. Downstream tools then pick up tabular digits without
knowing features exist. The script asserts every digit shares an advance and
fails the build if the freeze silently no-ops.

Frozen digits are 700/1000 em — slightly wider than the widest proportional
digit (`0`, at 667), so tabular strings are a little wider overall. At the
watch's 170 px that puts `12.3` at 396 px inside a 410 px panel.

## Sizes

Watch sizes match what the LVGL built-ins were doing (`lv_font_montserrat_24`
etc., now unused). Device sizes replace `setTextSize()` multiples of the old 5×7
bitmap `Font0` — 8/16/24/32 px cells — except the 8 px slot, which becomes 12 px
because Montserrat at 8 px is a smudge. See `device/include/ui/text_style.h`.

The device's speed font is digits-only (`0-9`, `.`, `:`, `-`, space): a full
ASCII set at 136 px would cost ~700 KB of flash for glyphs nothing draws.

## Tools

- `build.sh` — the whole pipeline; the only thing you normally run
- `vlw_encode.py` — TTF → VLW + C header. Format notes are in its docstring
- `vlw_preview.py` — decodes a `.vlw` and renders a sample string to PNG.
  Written against the format spec independently of the encoder, so it catches a
  systematic misread rather than agreeing with one:

```bash
python3 vlw_preview.py montserrat_24.vlw --text "SOG 12.3 kn" --out check.png
```
