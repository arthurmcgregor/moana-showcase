#!/usr/bin/env bash
# Regenerate every font asset in the repo from one source family.
#
# Both tiers render Montserrat Medium with tabular figures. Neither renderer
# (lv_font_conv on the watch, M5GFX's VLW loader on the device) applies
# OpenType features, so 'tnum' is frozen into the TTF here — pyftfeatfreeze
# rewrites cmap to point at the .tf glyphs — and everything downstream just
# reads the default glyph for each codepoint.
#
# Usage:  tools/fonts/build.sh [workdir]
# Needs:  python3, node (npx), network on first run (font + tool download).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="${1:-${TMPDIR:-/tmp}/moana-fonts}"
VENV="$WORK/venv"
SRC="$WORK/Montserrat.ttf"
MEDIUM="$WORK/Montserrat-Medium.ttf"
TNUM="$WORK/Montserrat-Medium-tnum.ttf"

# The source font is an input like any dependency, and google/fonts moves. Every
# font asset in the repo was cut from this commit — the one that landed
# Montserrat 9.000 (2024-10-21) — and off `main` a regen a year from now could
# quietly reflow every screen on both the watch and the device with no diff to
# explain it. Same reasoning as the exact-version lib_deps pins.
MONTSERRAT_PIN=76fca9fd0bb4ea46583f92e978660f3984ab9442
MONTSERRAT_SHA256=0f7b311b2f3279e4eef9b2f968bcdbab6e28f4daeb1f049f4f278a902bcd82f7

mkdir -p "$WORK"

# ---- 1. source family -------------------------------------------------------
# Montserrat ships variable-only in google/fonts; instance the Medium (wght=500)
# that LVGL's built-in montserrat fonts are also cut from.
if [ ! -f "$VENV/bin/python" ]; then
  python3 -m venv "$VENV"
  "$VENV/bin/pip" -q install fonttools opentype-feature-freezer pillow
fi
# The workdir is a cache across runs, so a copy left by an older pin has to be
# spotted rather than reused — hence checking the file already there, not just
# the one just fetched.
if [ ! -f "$SRC" ] || ! echo "$MONTSERRAT_SHA256  $SRC" | shasum -a256 -c - >/dev/null 2>&1; then
  curl -sLf -o "$SRC" \
    "https://raw.githubusercontent.com/google/fonts/$MONTSERRAT_PIN/ofl/montserrat/Montserrat%5Bwght%5D.ttf"
  echo "$MONTSERRAT_SHA256  $SRC" | shasum -a256 -c - >/dev/null || {
    echo "Montserrat at $MONTSERRAT_PIN does not match the pinned checksum." >&2
    echo "Bump both together, and expect every generated font to change." >&2
    exit 1
  }
fi

"$VENV/bin/fonttools" varLib.instancer "$SRC" wght=500 -o "$MEDIUM" >/dev/null

# -S keeps the glyph names stable, -R renames the family so a frozen file is
# never mistaken for stock Montserrat in a font menu.
"$VENV/bin/pyftfeatfreeze" -f tnum -S -R "Montserrat/Montserrat Tnum" \
  "$MEDIUM" "$TNUM" >/dev/null

# Fail loudly if the freeze silently no-ops — every digit must share an advance.
"$VENV/bin/python" - "$TNUM" <<'PY'
import sys
from fontTools.ttLib import TTFont
f = TTFont(sys.argv[1]); cmap = f.getBestCmap(); hmtx = f["hmtx"]
adv = {d: hmtx[cmap[ord(d)]][0] for d in "0123456789"}
if len(set(adv.values())) != 1:
    sys.exit(f"tnum freeze failed, digit advances still vary: {adv}")
print(f"frozen: all digits {next(iter(set(adv.values())))}/{f['head'].unitsPerEm} em")
PY

# ---- 2. watch (LVGL) --------------------------------------------------------
# Digits-only for the SOG readout and the header clock; ASCII for everything
# else. bpp 4 matches the previous font_speed_170.c and is plenty on an AMOLED
# panel. The clock's range is the same trick as the speed readout: full ASCII at
# 56px is ~330KB of source for glyphs a clock never draws, so it gets digits,
# ':' and the '-' of the "--:--" placeholder and costs ~42KB instead.
lv_font() {  # size, range, out
  npx --yes lv_font_conv@1.5.3 \
    --font "$TNUM" --range "$2" --size "$1" --bpp 4 --format lvgl \
    --no-compress --lv-include lvgl.h \
    -o "$ROOT/watch/src/$3"
}
lv_font 170 '0x2D-0x2E,0x30-0x39' font_speed_170.c
lv_font  56 '0x2D,0x30-0x3A'      font_time_56.c
# ASCII plus U+2022, the bullet separating the two halves of the Stats session
# line. It is the only non-ASCII codepoint any label draws; add the glyph to
# whichever size needs it rather than to all of them, and remember that a
# character missing from the range renders as a hollow box, not as nothing.
lv_font  24 '0x20-0x7E,0x2022'    font_ui_24.c
lv_font  28 '0x20-0x7E'           font_ui_28.c
lv_font  40 '0x20-0x7E'           font_ui_40.c

# ---- 3. device (M5GFX VLW) --------------------------------------------------
# Sizes chosen to land near the Font0 cell heights they replace: setTextSize(1)
# was 8px, (2) 16px, (3) 24px, (4) 32px, and the speed screen's (17) was 136px.
# The 8px slot becomes 12px — Montserrat at 8px is a smudge, and the sites using
# it are fine print, not a size the layout depends on.
vlw() {  # size, charset, name
  "$VENV/bin/python" "$ROOT/tools/fonts/vlw_encode.py" "$TNUM" \
    --size "$1" --charset "$2" \
    --out "$WORK/$3.vlw" \
    --header "$ROOT/device/src/ui/fonts/$3.h" --name "$3"
}
vlw  12 ascii  montserrat_12
vlw  16 ascii  montserrat_16
vlw  24 ascii  montserrat_24
vlw  32 ascii  montserrat_32
vlw 136 digits montserrat_digits_136

echo
echo "device font flash cost:"
du -ch "$ROOT"/device/src/ui/fonts/*.h | tail -1
