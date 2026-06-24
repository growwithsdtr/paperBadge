#!/usr/bin/env bash
# prepare_jp_fonts.sh — Download and subset BIZ UDPGothic Bold for PaperBadge Font Lab.
# License: BIZ UDPGothic is SIL OFL 1.1 (free to embed in firmware).
# Output: tools/BIZUDPGothic-Bold-subset.ttf
# After running this script, run tools/subset_jp_font.py to generate a C array for embedding.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_TTF="$SCRIPT_DIR/BIZUDPGothic-Bold-subset.ttf"
FULL_TTF="$SCRIPT_DIR/BIZUDPGothic-Bold.ttf"

echo "=== PaperBadge JP font prep ==="
echo ""

# Step 1: download BIZ UDPGothic Bold from Google Fonts API
if [ ! -f "$FULL_TTF" ]; then
  echo "[1/3] Downloading BIZ UDPGothic Bold from Google Fonts..."
  # Google Fonts CSS API — extract the TTF URL for the Japanese subset
  FONT_URL="https://fonts.gstatic.com/s/bizudpgothic/v15/vejMV_T8J5PCRSP2t3-uBWhlN0qYMbJxGgBvXYHg-Q.ttf"
  curl -L --fail -o "$FULL_TTF" "$FONT_URL"
  echo "    Saved: $FULL_TTF"
else
  echo "[1/3] Found existing: $FULL_TTF (skipping download)"
fi

# Step 2: install fonttools if needed
echo "[2/3] Checking fonttools..."
if ! python3 -c "import fonttools" 2>/dev/null; then
  if ! pip3 show fonttools >/dev/null 2>&1; then
    echo "    Installing fonttools..."
    pip3 install fonttools brotli
  fi
fi
echo "    fonttools OK"

# Step 3: subset the font
echo "[3/3] Subsetting font for PaperBadge N3 vocab..."
python3 "$SCRIPT_DIR/subset_jp_font.py" "$FULL_TTF" "$OUT_TTF"

echo ""
echo "=== Done ==="
echo "Output: $OUT_TTF"
echo ""
echo "Next step: integrate the TTF subset into firmware."
echo "  Option A: Use LGFX loadFont() from SD: copy to /paperbadge/fonts/jp_body.ttf"
echo "  Option B: Convert to C array with tools/ttf_to_c_array.py (not yet implemented)"
echo "  Option C: Use LGFX VLW format via fontconvert — see NOTES/JP_FONT_ASSET_NOTES.md"
