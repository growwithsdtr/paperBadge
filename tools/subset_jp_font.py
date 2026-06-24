#!/usr/bin/env python3
"""subset_jp_font.py — Subset a Japanese TTF/OTF to PaperBadge N3 codepoints.

Usage:
  python3 tools/subset_jp_font.py <input.ttf> <output.ttf>

Requires: fonttools  (pip install fonttools brotli)

Codepoints included:
  - All Hiragana     U+3040-U+309F
  - All Katakana     U+30A0-U+30FF
  - CJK Punctuation  U+3000-U+303F
  - ASCII            U+0020-U+007E
  - N3 vocab kanji and key characters used in PaperBadge questions
"""

import sys
import unicodedata

try:
    from fontTools.ttLib import TTFont
    from fontTools.subset import Subsetter, Options
except ImportError:
    print("ERROR: fonttools not installed. Run: pip install fonttools brotli")
    sys.exit(1)


def build_codepoints():
    cps = set()

    # ASCII printable
    for cp in range(0x0020, 0x007F):
        cps.add(cp)

    # Hiragana
    for cp in range(0x3040, 0x30A0):
        cps.add(cp)

    # Katakana
    for cp in range(0x30A0, 0x3100):
        cps.add(cp)

    # CJK Symbols and Punctuation
    for cp in range(0x3000, 0x3040):
        cps.add(cp)

    # Halfwidth/fullwidth forms
    for cp in range(0xFF00, 0xFF60):
        cps.add(cp)

    # PaperBadge N3 vocabulary kanji (hand-curated from embedded question set)
    n3_kanji = (
        "郵便局荷物違引越子供手紙送受取場所読方正覚える使週間日本語質問答問題例文"
        "意味言葉文法正確間違正しい難しい易しい大切時間毎朝夜昼午前午後今日明日昨日"
        "来週先週今週来月先月今月今年去年来年友達家族仕事会社学校駅電車バス車歩く"
        "走買食飲見聞書話起寝出入帰持来行会作知思待理由説明理解勉強練習試験点数"
        "結果答合正誤選択肢選問解"
    )
    for ch in n3_kanji:
        cps.add(ord(ch))

    return cps


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} input.ttf output.ttf")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]

    codepoints = build_codepoints()
    print(f"Subsetting {input_path}")
    print(f"  Target codepoints: {len(codepoints)}")

    options = Options()
    options.layout_features = []  # drop layout features to reduce size
    options.name_IDs = [1, 2, 4]  # keep family/style/full name only
    options.drop_tables = ["DSIG"]
    options.hinting = False       # no hinting needed for embedded bitmap-like rendering

    font = TTFont(input_path)
    subsetter = Subsetter(options=options)
    subsetter.populate(unicodes=codepoints)
    subsetter.subset(font)
    font.save(output_path)

    import os
    size_kb = os.path.getsize(output_path) / 1024
    print(f"  Output: {output_path}  ({size_kb:.1f} KB)")
    if size_kb > 200:
        print(f"  WARNING: subset is {size_kb:.1f} KB — consider reducing N3 kanji list to stay under 200 KB")
    else:
        print(f"  Size OK: under 200 KB guardrail")

    print("")
    print("Next steps:")
    print("  1. Copy the subset TTF to SD: /paperbadge/fonts/jp_body.ttf")
    print("  2. Or convert to C array for firmware embedding (see JP_FONT_ASSET_NOTES.md)")


if __name__ == "__main__":
    main()
