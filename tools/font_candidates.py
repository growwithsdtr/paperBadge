#!/usr/bin/env python3
"""Download and subset PaperBadge Font Lab candidates.

This tool is intentionally Mac/host-side. The normal firmware embeds only the
current small XTEink subsets; use this helper to build a separate Font Lab asset
set without requiring manual SD font copying.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "generated-assets" / "fontlab"
LICENSES = OUT / "licenses"

SAMPLE_TEXT = (
    "Daniel Jimenez Senior Technical PM AI Products Practice 1/71 "
    "Reader body text 1234567890 The quick brown fox jumps over 12345. "
    "ひらがな：ちがう・にもつ・ひっこす "
    "カタカナ：ダニエル・ヒメネズ "
    "漢字：郵便局 荷物 違う 引っ越す "
    "N3 W1D1 500問 ぶんぽう 郵便局の読み方として正しいものはどれですか "
    "EN Post office JP"
)

CANDIDATES = [
    {
        "name": "NotoSansJP-Regular",
        "url": "https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/Japanese/NotoSansCJKjp-Regular.otf",
        "license": "OFL-1.1",
    },
    {
        "name": "NotoSansJP-Medium",
        "url": "https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/Japanese/NotoSansCJKjp-Medium.otf",
        "license": "OFL-1.1",
    },
    {
        "name": "BIZUDPGothic-Regular",
        "url": "https://github.com/googlefonts/morisawa-biz-ud-gothic/raw/main/fonts/ttf/BIZUDPGothic-Regular.ttf",
        "license": "OFL-1.1",
    },
    {
        "name": "MPLUS1p-Regular",
        "url": "https://github.com/coz-m/MPLUS_FONTS/raw/master/fonts/ttf/Mplus1p-Regular.ttf",
        "license": "OFL-1.1",
    },
    {
        "name": "MPLUSRounded1c-Regular",
        "url": "https://github.com/coz-m/MPLUS_FONTS/raw/master/fonts/ttf/MPLUSRounded1c-Regular.ttf",
        "license": "OFL-1.1",
    },
]


def download(url: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists() and dest.stat().st_size > 0:
        return
    print(f"download {dest.name}")
    with urllib.request.urlopen(url, timeout=60) as response:
        dest.write_bytes(response.read())


def subset_font(src: Path, dest: Path, text: str) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        sys.executable,
        "-m",
        "fontTools.subset",
        str(src),
        f"--text={text}",
        "--layout-features=*",
        "--output-file",
        str(dest),
    ]
    try:
        subprocess.run(cmd, check=True)
    except (FileNotFoundError, subprocess.CalledProcessError) as exc:
        raise SystemExit(
            "fonttools subset failed. Install with: python3 -m pip install fonttools"
        ) from exc


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--download", action="store_true", help="download candidate fonts")
    parser.add_argument("--subset", action="store_true", help="subset candidates to Font Lab glyphs")
    parser.add_argument("--text", default=SAMPLE_TEXT, help="glyph sample text for subsetting")
    parser.add_argument("--out", type=Path, default=OUT, help="output directory")
    args = parser.parse_args()

    fonts_dir = args.out / "source"
    subset_dir = args.out / "subset"
    LICENSES.mkdir(parents=True, exist_ok=True)
    manifest = []

    for candidate in CANDIDATES:
        suffix = Path(candidate["url"]).suffix
        src = fonts_dir / f"{candidate['name']}{suffix}"
        if args.download or args.subset:
            download(candidate["url"], src)
        if args.subset:
            subset_font(src, subset_dir / f"{candidate['name']}.subset{suffix}", args.text)
        manifest.append(
            {
                "name": candidate["name"],
                "source": str(src.relative_to(ROOT)),
                "license": candidate["license"],
                "subset": str((subset_dir / f"{candidate['name']}.subset{suffix}").relative_to(ROOT)),
            }
        )

    (args.out / "fontlab_manifest.json").write_text(
        json.dumps({"candidates": manifest}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    (LICENSES / "README.md").write_text(
        "Font candidates are open-license fonts. Verify upstream license files "
        "before embedding candidates in normal firmware.\n",
        encoding="utf-8",
    )
    print(args.out / "fontlab_manifest.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
