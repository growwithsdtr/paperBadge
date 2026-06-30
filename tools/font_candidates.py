#!/usr/bin/env python3
"""Download, subset, and embed PaperBadge Font Lab candidates.

The firmware cannot render arbitrary TTF/OTF files at runtime, so this tool
turns open-license candidate fonts into the same sparse 1 bpp glyph format used
by the built-in BIZ/IPA UI fonts. The generated C++ assets are intentionally
small: they cover the Font Lab physical-QA sample glyphs, not a whole Japanese
production font.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import urllib.request
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "generated-assets" / "fontlab"
LICENSES = OUT / "licenses"
SOURCE_DIR = OUT / "source"
SUBSET_DIR = OUT / "subset"
SRC_FONT_DIR = ROOT / "src" / "font"

WESTERN_SAMPLES = [
    "Daniel Jimenez",
    "Senior Technical PM | AI Products",
    "Practice 1/71",
    "Reader body text 1234567890",
    "The quick brown fox jumps over 12345.",
]

JAPANESE_SAMPLES = [
    "ひらがな：ちがう・にもつ・ひっこす",
    "カタカナ：ダニエル・ヒメネズ",
    "漢字：郵便局 荷物 違う 引っ越す",
    "N3 ・ W1D1 ・ 500問 ・ ぶんぽう",
    "「郵便局」の読み方として正しいものはどれですか。",
    "EN: Post office / JP: 郵便局",
]

PUNCTUATION = " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~、。！？「」『』（）［］【】・ー〜…"
SAMPLE_TEXT = "\n".join(WESTERN_SAMPLES + JAPANESE_SAMPLES) + "\n" + PUNCTUATION


@dataclass(frozen=True)
class Candidate:
    key: str
    display_name: str
    source: str
    weight: str
    url: str
    license_name: str
    license_url: str


CANDIDATES: list[Candidate] = [
    Candidate(
        "BIZUDPGothic-Regular",
        "BIZ UDPGothic",
        "Morisawa BIZ UD Gothic",
        "Regular",
        "https://github.com/googlefonts/morisawa-biz-ud-gothic/raw/main/fonts/ttf/BIZUDPGothic-Regular.ttf",
        "SIL OFL 1.1",
        "https://raw.githubusercontent.com/googlefonts/morisawa-biz-ud-gothic/main/OFL.txt",
    ),
    Candidate(
        "BIZUDPGothic-Bold",
        "BIZ UDPGothic",
        "Morisawa BIZ UD Gothic",
        "Bold",
        "https://github.com/googlefonts/morisawa-biz-ud-gothic/raw/main/fonts/ttf/BIZUDPGothic-Bold.ttf",
        "SIL OFL 1.1",
        "https://raw.githubusercontent.com/googlefonts/morisawa-biz-ud-gothic/main/OFL.txt",
    ),
    Candidate(
        "NotoSansJP-Regular",
        "Noto Sans JP",
        "Noto CJK",
        "Regular",
        "https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/Japanese/NotoSansCJKjp-Regular.otf",
        "SIL OFL 1.1",
        "https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/LICENSE",
    ),
    Candidate(
        "NotoSansJP-Medium",
        "Noto Sans JP",
        "Noto CJK",
        "Medium",
        "https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/Japanese/NotoSansCJKjp-Medium.otf",
        "SIL OFL 1.1",
        "https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/LICENSE",
    ),
    Candidate(
        "NotoSansJP-Bold",
        "Noto Sans JP",
        "Noto CJK",
        "Bold",
        "https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/Japanese/NotoSansCJKjp-Bold.otf",
        "SIL OFL 1.1",
        "https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/LICENSE",
    ),
    Candidate(
        "MPLUS1-Regular",
        "M PLUS 1p",
        "M PLUS Fonts",
        "Regular",
        "https://raw.githubusercontent.com/coz-m/MPLUS_FONTS/master/fonts/MPLUS1/ttf/MPLUS1-Regular.ttf",
        "SIL OFL 1.1",
        "https://raw.githubusercontent.com/coz-m/MPLUS_FONTS/master/OFL.txt",
    ),
    Candidate(
        "MPLUS1-Bold",
        "M PLUS 1p",
        "M PLUS Fonts",
        "Bold",
        "https://raw.githubusercontent.com/coz-m/MPLUS_FONTS/master/fonts/MPLUS1/ttf/MPLUS1-Bold.ttf",
        "SIL OFL 1.1",
        "https://raw.githubusercontent.com/coz-m/MPLUS_FONTS/master/OFL.txt",
    ),
    Candidate(
        "MPLUSRounded1c-Regular",
        "M PLUS Rounded 1c",
        "Google Fonts",
        "Regular",
        "https://github.com/google/fonts/raw/main/ofl/mplusrounded1c/MPLUSRounded1c-Regular.ttf",
        "SIL OFL 1.1",
        "https://raw.githubusercontent.com/google/fonts/main/ofl/mplusrounded1c/METADATA.pb",
    ),
    Candidate(
        "MPLUSRounded1c-Bold",
        "M PLUS Rounded 1c",
        "Google Fonts",
        "Bold",
        "https://github.com/google/fonts/raw/main/ofl/mplusrounded1c/MPLUSRounded1c-Bold.ttf",
        "SIL OFL 1.1",
        "https://raw.githubusercontent.com/google/fonts/main/ofl/mplusrounded1c/METADATA.pb",
    ),
    Candidate(
        "Inter-Regular",
        "Inter",
        "Google Fonts",
        "Regular",
        "https://github.com/google/fonts/raw/main/ofl/inter/Inter%5Bopsz,wght%5D.ttf",
        "SIL OFL 1.1",
        "https://raw.githubusercontent.com/google/fonts/main/ofl/inter/OFL.txt",
    ),
    Candidate(
        "SourceSerif4-Regular",
        "Source Serif 4",
        "Google Fonts",
        "Regular",
        "https://github.com/google/fonts/raw/main/ofl/sourceserif4/SourceSerif4%5Bopsz,wght%5D.ttf",
        "SIL OFL 1.1",
        "https://raw.githubusercontent.com/google/fonts/main/ofl/sourceserif4/OFL.txt",
    ),
]


def run(cmd: list[str]) -> None:
    subprocess.run(cmd, check=True)


def download(url: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists() and dest.stat().st_size > 0:
        return
    print(f"download {dest.name}")
    curl = shutil.which("curl")
    if curl:
        run([curl, "-L", "--fail", "--silent", "--show-error", "-o", str(dest), url])
        return
    with urllib.request.urlopen(url, timeout=120) as response:
        dest.write_bytes(response.read())


def suffix_for_url(url: str) -> str:
    clean = url.split("?", 1)[0]
    suffix = Path(clean).suffix
    return suffix or ".ttf"


def source_path(candidate: Candidate, out: Path) -> Path:
    return out / "source" / f"{candidate.key}{suffix_for_url(candidate.url)}"


def license_path(candidate: Candidate, out: Path) -> Path:
    safe = candidate.key.replace("/", "_")
    return out / "licenses" / f"{safe}.LICENSE.txt"


def build_codepoints(text: str) -> list[int]:
    cps = set()
    for ch in text:
        if ch == "\n":
            continue
        cps.add(ord(ch))
    # Include ASCII printable and common full-width punctuation so sample pages
    # can label font names/weights even when a row is added later.
    cps.update(range(0x20, 0x7F))
    cps.update(ord(ch) for ch in PUNCTUATION)
    return sorted(cp for cp in cps if cp <= 0xFFFF)


def subset_font(src: Path, dest: Path, text: str) -> bool:
    dest.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        sys.executable,
        "-m",
        "fontTools.subset",
        str(src),
        f"--text={text}",
        "--layout-features=*",
        "--no-hinting",
        "--output-file",
        str(dest),
    ]
    try:
        subprocess.run(cmd, check=True)
        return True
    except (FileNotFoundError, subprocess.CalledProcessError):
        print("warn: fonttools unavailable or failed; skipping outline subset", file=sys.stderr)
        return False


def rasterise_glyph(font, cp: int, width: int, height: int) -> bytes:
    from PIL import Image, ImageDraw

    img = Image.new("L", (width, height), color=0)
    draw = ImageDraw.Draw(img)
    draw.text((0, 0), chr(cp), font=font, fill=255, anchor="la")
    width_byte = (width + 7) // 8
    out = bytearray(width_byte * height)
    px = img.load()
    for y in range(height):
        for x in range(width):
            if px[x, y] >= 128:
                out[y * width_byte + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out)


def symbol_for_key(key: str) -> str:
    out = []
    for ch in key:
        out.append(ch if ch.isalnum() else "_")
    return "".join(out)


def emit_firmware_assets(candidates: list[Candidate], out: Path, text: str, width: int, height: int) -> dict[str, object]:
    try:
        from PIL import ImageFont
    except ImportError as exc:
        raise SystemExit("Pillow is required to emit firmware assets: python3 -m pip install pillow") from exc

    cps = build_codepoints(text)
    char_byte = ((width + 7) // 8) * height
    face_blobs: list[tuple[Candidate, str, bytes, int]] = []
    for candidate in candidates:
        path = source_path(candidate, out)
        font = ImageFont.truetype(str(path), size=height)
        blob = bytearray()
        missing = 0
        for cp in cps:
            try:
                glyph = rasterise_glyph(font, cp, width, height)
                if not any(glyph) and not chr(cp).isspace():
                    missing += 1
                blob.extend(glyph)
            except (OSError, ValueError):
                blob.extend(bytes(char_byte))
                missing += 1
        face_blobs.append((candidate, symbol_for_key(candidate.key), bytes(blob), missing))

    header = SRC_FONT_DIR / "font_lab_assets.h"
    source = SRC_FONT_DIR / "font_lab_assets.cpp"
    fingerprint = hashlib.sha256(
        ("\n".join(str(cp) for cp in cps) + "\n" + "\n".join(c.key for c in candidates)).encode("utf-8")
    ).hexdigest()[:16]

    header.write_text(
        "\n".join(
            [
                "// AUTO-GENERATED by tools/font_candidates.py --emit-firmware - do not edit.",
                f"// fingerprint: {fingerprint}",
                "",
                "#pragma once",
                "",
                "#include <cstdint>",
                "",
                "namespace ps3::font {",
                "",
                f"inline constexpr int kFontLabGlyphW = {width};",
                f"inline constexpr int kFontLabGlyphH = {height};",
                f"inline constexpr int kFontLabGlyphCount = {len(cps)};",
                f"inline constexpr int kFontLabBytesPerGlyph = {char_byte};",
                "",
                "struct FontLabFace {",
                "    const char* key;",
                "    const char* display_name;",
                "    const char* source;",
                "    const char* weight;",
                "    const char* license;",
                "    const uint8_t* glyphs;",
                "};",
                "",
                "extern const uint16_t kFontLabCodepoints[kFontLabGlyphCount];",
                "extern const FontLabFace kFontLabFaces[];",
                "extern const int kFontLabFaceCount;",
                "",
                "}  // namespace ps3::font",
                "",
            ]
        ),
        encoding="utf-8",
    )

    lines = [
        "// AUTO-GENERATED by tools/font_candidates.py --emit-firmware - do not edit.",
        f"// fingerprint: {fingerprint}",
        "",
        '#include "font_lab_assets.h"',
        "",
        "namespace ps3::font {",
        "",
        "const uint16_t kFontLabCodepoints[kFontLabGlyphCount] = {",
    ]
    chunk: list[str] = []
    for i, cp in enumerate(cps):
        chunk.append(f"0x{cp:04X}")
        if (i + 1) % 12 == 0 or i == len(cps) - 1:
            lines.append("    " + ", ".join(chunk) + ",")
            chunk = []
    lines.extend(["};", ""])
    for candidate, symbol, blob, _missing in face_blobs:
        lines.append(f"static const uint8_t kFontLabGlyphs_{symbol}[kFontLabGlyphCount * kFontLabBytesPerGlyph] = {{")
        byte_chunk: list[str] = []
        for i, b in enumerate(blob):
            byte_chunk.append(f"0x{b:02X}")
            if (i + 1) % 16 == 0 or i == len(blob) - 1:
                lines.append("    " + ", ".join(byte_chunk) + ",")
                byte_chunk = []
        lines.extend(["};", ""])
    lines.append("const FontLabFace kFontLabFaces[] = {")
    for candidate, symbol, _blob, _missing in face_blobs:
        lines.append(
            "    {"
            f"\"{candidate.key}\", \"{candidate.display_name}\", "
            f"\"{candidate.source}\", \"{candidate.weight}\", "
            f"\"{candidate.license_name}\", kFontLabGlyphs_{symbol}"
            "},"
        )
    lines.extend(
        [
            "};",
            "const int kFontLabFaceCount = sizeof(kFontLabFaces) / sizeof(kFontLabFaces[0]);",
            "",
            "}  // namespace ps3::font",
            "",
        ]
    )
    source.write_text("\n".join(lines), encoding="utf-8")

    return {
        "glyph_count": len(cps),
        "glyph_width": width,
        "glyph_height": height,
        "bytes_per_face": len(cps) * char_byte,
        "header": str(header.relative_to(ROOT)),
        "source": str(source.relative_to(ROOT)),
        "missing_by_face": {c.key: missing for c, _symbol, _blob, missing in face_blobs},
    }


def write_license_readme(out: Path, candidates: list[Candidate]) -> None:
    LICENSES.mkdir(parents=True, exist_ok=True)
    lines = [
        "# PaperBadge Font Lab Candidate Licenses",
        "",
        "Font Lab candidate bitmaps are derived from the open-license sources below.",
        "Full upstream license files are downloaded next to this README by `tools/font_candidates.py --download`.",
        "",
    ]
    for c in candidates:
        lines.append(f"- {c.key}: {c.license_name} — {c.url}")
    lines.append("")
    (out / "licenses" / "README.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--download", action="store_true", help="download candidate fonts and licenses")
    parser.add_argument("--subset", action="store_true", help="create outline TTF/OTF subsets when fonttools is available")
    parser.add_argument("--emit-firmware", action="store_true", help="generate src/font/font_lab_assets.{h,cpp}")
    parser.add_argument("--text", default=SAMPLE_TEXT, help="glyph sample text for subsetting and firmware assets")
    parser.add_argument("--out", type=Path, default=OUT, help="output directory for downloaded sources and manifest")
    parser.add_argument("--width", type=int, default=24, help="glyph cell width")
    parser.add_argument("--height", type=int, default=24, help="glyph cell height")
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "source").mkdir(parents=True, exist_ok=True)
    (args.out / "subset").mkdir(parents=True, exist_ok=True)
    (args.out / "licenses").mkdir(parents=True, exist_ok=True)

    manifest: dict[str, object] = {
        "sample_text": args.text,
        "sample_glyph_count": len(build_codepoints(args.text)),
        "candidates": [],
    }

    for candidate in CANDIDATES:
        src = source_path(candidate, args.out)
        lic = license_path(candidate, args.out)
        if args.download or args.subset or args.emit_firmware:
            download(candidate.url, src)
            download(candidate.license_url, lic)
        subset = args.out / "subset" / f"{candidate.key}.subset{src.suffix}"
        subset_ok = False
        if args.subset:
            subset_ok = subset_font(src, subset, args.text)
        manifest["candidates"].append(
            {
                "key": candidate.key,
                "display_name": candidate.display_name,
                "source": candidate.source,
                "weight": candidate.weight,
                "font_file": str(src.relative_to(ROOT)),
                "license": candidate.license_name,
                "license_file": str(lic.relative_to(ROOT)),
                "subset_file": str(subset.relative_to(ROOT)),
                "subset_generated": subset_ok,
                "firmware_visible": args.emit_firmware,
            }
        )

    write_license_readme(args.out, CANDIDATES)
    if args.emit_firmware:
        manifest["firmware_assets"] = emit_firmware_assets(
            CANDIDATES, args.out, args.text, args.width, args.height
        )

    manifest_path = args.out / "fontlab_manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(manifest_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
