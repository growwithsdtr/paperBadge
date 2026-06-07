#!/usr/bin/env python3
"""Build bilingual embedded PaperBadge fallback assets."""

from __future__ import annotations

import argparse
import json
import struct
from dataclasses import dataclass
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont, ImageOps
except ImportError as exc:  # pragma: no cover - user-facing setup guard
    raise SystemExit(
        "Pillow is required for bilingual badge generation. Install it in your active Python environment:\n"
        "  python3 -m pip install Pillow\n"
        "On this Mac, the repo-level environment already works with:\n"
        "  /Users/danieljimenez/AIDevelopment/.venv/bin/python tools/build_embedded_assets.py"
    ) from exc


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE_DIR = REPO_ROOT / "sample-data" / "paperbadge"
GENERATED_DIR = REPO_ROOT / "generated-assets" / "embedded"
HEADER_PATH = REPO_ROOT / "src" / "embedded_assets.h"
SCREEN_SIZE = (540, 960)

FULL_VARIANTS = {
    "badge.png",
    "badge_full.png",
    "full_badge.png",
    "badge_en.png",
    "complete_badge.png",
    "completebadge.png",
}
JAPANESE_VARIANTS = {
    "badge_ja.png",
    "badge_jp.png",
    "badge_japanese.png",
    "completebadgeja.png",
    "complete_badge_ja.png",
}
PROFILE_VARIANTS = {
    "profilephoto.png",
    "profile_photo.png",
    "photo.png",
    "portrait.png",
    "profile.png",
    "profilephoto.jpg",
    "profilephoto.jpeg",
    "profile_photo.jpg",
    "profile_photo.jpeg",
}
QR_VARIANTS = {
    "qr.png",
    "qr.jpg",
    "qr.jpeg",
    "qr.jpe",
    "linkedin_qr.png",
    "linkedinqr.png",
    "linkedin_qr.jpg",
    "linkedinqr.jpg",
}

EN_FONT_CANDIDATES = [
    "/Library/Fonts/Inter.ttf",
    "/Library/Fonts/Inter-Regular.ttf",
    "/System/Library/Fonts/HelveticaNeue.ttc",
    "/System/Library/Fonts/Helvetica.ttc",
    "/Library/Fonts/Arial Unicode.ttf",
]
JA_FONT_CANDIDATES = [
    "/Library/Fonts/NotoSansJP-Regular.otf",
    "/Library/Fonts/NotoSansJP-Regular.ttf",
    "/Library/Fonts/IBMPlexSansJP-Regular.ttf",
    "/System/Library/Fonts/ヒラギノ角ゴシック W6.ttc",
    "/System/Library/Fonts/ヒラギノ角ゴシック W5.ttc",
    "/System/Library/Fonts/Hiragino Sans GB.ttc",
    "/Library/Fonts/Arial Unicode.ttf",
]


@dataclass(frozen=True)
class ImageInfo:
    path: Path
    fmt: str
    width: int
    height: int
    size: int

    @property
    def name_key(self) -> str:
        return self.path.name.lower().replace("-", "_")

    @property
    def aspect(self) -> float:
        return self.width / self.height if self.height else 0.0

    @property
    def area(self) -> int:
        return self.width * self.height


@dataclass(frozen=True)
class GeneratedAsset:
    name: str
    source_name: str
    path: Path
    width: int
    height: int
    size: int


def parse_png_dimensions(path: Path) -> tuple[int, int] | None:
    with path.open("rb") as handle:
        header = handle.read(24)
    if len(header) < 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", header[16:24])


def parse_jpeg_dimensions(path: Path) -> tuple[int, int] | None:
    data = path.read_bytes()
    if len(data) < 4 or data[:2] != b"\xff\xd8":
        return None

    index = 2
    while index + 9 < len(data):
        if data[index] != 0xFF:
            index += 1
            continue
        while index < len(data) and data[index] == 0xFF:
            index += 1
        if index >= len(data):
            break
        marker = data[index]
        index += 1
        if marker in {0xD8, 0xD9, 0x01} or 0xD0 <= marker <= 0xD7:
            continue
        if index + 2 > len(data):
            break
        segment_length = struct.unpack(">H", data[index : index + 2])[0]
        if segment_length < 2 or index + segment_length > len(data):
            break
        if marker in {0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF}:
            height = struct.unpack(">H", data[index + 3 : index + 5])[0]
            width = struct.unpack(">H", data[index + 5 : index + 7])[0]
            return width, height
        index += segment_length
    return None


def inspect_image(path: Path) -> ImageInfo | None:
    suffix = path.suffix.lower()
    dims: tuple[int, int] | None = None
    fmt = ""

    if suffix == ".png":
        dims = parse_png_dimensions(path)
        fmt = "png"
    elif suffix in {".jpg", ".jpeg", ".jpe"}:
        dims = parse_jpeg_dimensions(path)
        fmt = "jpg"

    if not dims:
        return None

    width, height = dims
    return ImageInfo(path=path, fmt=fmt, width=width, height=height, size=path.stat().st_size)


def score_full_badge(info: ImageInfo) -> float:
    target_aspects = (SCREEN_SIZE[0] / SCREEN_SIZE[1], SCREEN_SIZE[1] / SCREEN_SIZE[0])
    aspect_score = max(0.0, 35.0 - min(abs(info.aspect - target) for target in target_aspects) * 120.0)
    area_score = min(info.area / (SCREEN_SIZE[0] * SCREEN_SIZE[1]), 4.0) * 10.0
    name_score = 65.0 if info.name_key in FULL_VARIANTS else 0.0
    return name_score + aspect_score + area_score


def score_japanese_badge(info: ImageInfo) -> float:
    return (85.0 if info.name_key in JAPANESE_VARIANTS else 0.0) + score_full_badge(info) * 0.25


def score_profile(info: ImageInfo) -> float:
    name_score = 60.0 if info.name_key in PROFILE_VARIANTS else 0.0
    portrait_score = 20.0 if 0.65 <= info.aspect <= 1.2 else 0.0
    size_score = max(0.0, 20.0 - abs(info.area - (220 * 262)) / 6000.0)
    return name_score + portrait_score + size_score


def score_qr(info: ImageInfo) -> float:
    name_score = 70.0 if info.name_key in QR_VARIANTS else 0.0
    square_score = max(0.0, 30.0 - abs(info.aspect - 1.0) * 100.0)
    size_score = min(info.area / (320 * 320), 4.0) * 5.0
    return name_score + square_score + size_score


def choose_asset(images: list[ImageInfo], scorer, exclude: set[Path] | None = None) -> ImageInfo | None:
    excluded = exclude or set()
    candidates = [image for image in images if image.path not in excluded]
    if not candidates:
        return None
    return max(candidates, key=scorer)


def load_font(candidates: list[str], size: int) -> ImageFont.FreeTypeFont:
    for candidate in candidates:
        path = Path(candidate)
        if path.exists():
            try:
                return ImageFont.truetype(str(path), size=size)
            except OSError:
                continue
    return ImageFont.load_default(size=size)


def text_width(draw: ImageDraw.ImageDraw, text: str, font: ImageFont.ImageFont) -> int:
    left, _, right, _ = draw.textbbox((0, 0), text, font=font)
    return right - left


def fit_font(draw: ImageDraw.ImageDraw, text: str, candidates: list[str], start: int, minimum: int, max_width: int):
    for size in range(start, minimum - 1, -2):
        font = load_font(candidates, size)
        if text_width(draw, text, font) <= max_width:
            return font
    return load_font(candidates, minimum)


def draw_centered(draw: ImageDraw.ImageDraw, y: int, text: str, font: ImageFont.ImageFont, fill=(0, 0, 0)) -> None:
    bbox = draw.textbbox((0, 0), text, font=font)
    width = bbox[2] - bbox[0]
    height = bbox[3] - bbox[1]
    draw.text(((SCREEN_SIZE[0] - width) / 2, y - height / 2 - bbox[1]), text, font=font, fill=fill)


def paste_contained(canvas: Image.Image, source: Image.Image, box: tuple[int, int, int, int]) -> None:
    image = ImageOps.contain(source.convert("RGBA"), (box[2] - box[0], box[3] - box[1]), Image.Resampling.LANCZOS)
    x = box[0] + (box[2] - box[0] - image.width) // 2
    y = box[1] + (box[3] - box[1] - image.height) // 2
    canvas.alpha_composite(image, (x, y))


def load_badge_json(path: Path | None) -> dict:
    if not path:
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def generate_english_badge(source: ImageInfo, output: Path) -> GeneratedAsset:
    output.parent.mkdir(parents=True, exist_ok=True)
    with Image.open(source.path) as image:
        badge = ImageOps.fit(image.convert("RGB"), SCREEN_SIZE, method=Image.Resampling.LANCZOS, centering=(0.5, 0.5))
        badge.save(output, format="PNG", optimize=True)
    info = inspect_image(output)
    assert info is not None
    return GeneratedAsset("badge_en", source.path.name, output, info.width, info.height, info.size)


def generate_japanese_badge(
    source: ImageInfo | None, profile: ImageInfo, qr: ImageInfo, badge_json: dict, output: Path
) -> GeneratedAsset:
    output.parent.mkdir(parents=True, exist_ok=True)

    if source:
        with Image.open(source.path) as image:
            badge = ImageOps.fit(
                image.convert("RGB"), SCREEN_SIZE, method=Image.Resampling.LANCZOS, centering=(0.5, 0.5)
            )
            badge.save(output, format="PNG", optimize=True)
        info = inspect_image(output)
        assert info is not None
        return GeneratedAsset("badge_ja", source.path.name, output, info.width, info.height, info.size)

    japanese = badge_json.get("japanese", {})
    name = japanese.get("name") or "ダニエル・ヒメネズ"
    title = japanese.get("title") or "AIプロダクトマネージャー"
    subtitle = japanese.get("subtitle") or "0→1 AI・SaaS・FinTech"
    location = japanese.get("location") or "東京拠点"
    footer = japanese.get("footer") or "LinkedInをスキャン"

    canvas = Image.new("RGBA", SCREEN_SIZE, (255, 255, 255, 255))
    draw = ImageDraw.Draw(canvas)

    with Image.open(profile.path) as profile_image:
        paste_contained(canvas, profile_image, (122, 42, 418, 344))

    line_color = (135, 135, 135)
    draw.line((74, 382, SCREEN_SIZE[0] - 74, 382), fill=line_color, width=2)

    name_font = fit_font(draw, name, JA_FONT_CANDIDATES, 42, 30, 468)
    title_font = fit_font(draw, title, JA_FONT_CANDIDATES, 30, 24, 468)
    subtitle_font = fit_font(draw, subtitle, JA_FONT_CANDIDATES, 28, 22, 468)
    location_font = fit_font(draw, location, JA_FONT_CANDIDATES, 25, 21, 468)
    footer_font = fit_font(draw, footer, JA_FONT_CANDIDATES, 25, 20, 468)

    draw_centered(draw, 436, name, name_font)
    draw_centered(draw, 493, title, title_font)
    draw_centered(draw, 538, subtitle, subtitle_font)
    draw_centered(draw, 578, location, location_font)

    draw.line((74, 612, SCREEN_SIZE[0] - 74, 612), fill=line_color, width=2)
    with Image.open(qr.path) as qr_image:
        qr_ready = ImageOps.fit(qr_image.convert("RGB"), (300, 300), method=Image.Resampling.NEAREST)
        canvas.paste(qr_ready, ((SCREEN_SIZE[0] - 300) // 2, 632))

    draw_centered(draw, 944, footer, footer_font, fill=(50, 50, 50))

    canvas.convert("RGB").save(output, format="PNG", optimize=True)
    info = inspect_image(output)
    assert info is not None
    return GeneratedAsset("badge_ja", "generated from badge.json", output, info.width, info.height, info.size)


def normalize_profile(source: ImageInfo, output: Path) -> GeneratedAsset:
    output.parent.mkdir(parents=True, exist_ok=True)
    with Image.open(source.path) as image:
        profile = ImageOps.contain(image.convert("RGB"), (220, 262), Image.Resampling.LANCZOS)
        canvas = Image.new("RGB", (220, 262), (255, 255, 255))
        canvas.paste(profile, ((220 - profile.width) // 2, (262 - profile.height) // 2))
        canvas.save(output, format="PNG", optimize=True)
    info = inspect_image(output)
    assert info is not None
    return GeneratedAsset("profile", source.path.name, output, info.width, info.height, info.size)


def normalize_qr(source: ImageInfo, output: Path) -> GeneratedAsset:
    output.parent.mkdir(parents=True, exist_ok=True)
    with Image.open(source.path) as image:
        qr = ImageOps.fit(image.convert("L"), (320, 320), method=Image.Resampling.NEAREST)
        qr = qr.point(lambda value: 255 if value > 180 else 0, mode="1").convert("RGB")
        qr.save(output, format="PNG", optimize=True)
    info = inspect_image(output)
    assert info is not None
    return GeneratedAsset("qr", source.path.name, output, info.width, info.height, info.size)


def c_array(name: str, data: bytes, indent: str = "  ") -> str:
    lines = [f"{indent}const uint8_t {name}[] PROGMEM = {{"]
    for start in range(0, len(data), 12):
        chunk = data[start : start + 12]
        values = ", ".join(f"0x{byte:02X}" for byte in chunk)
        suffix = "," if start + 12 < len(data) else ""
        lines.append(f"{indent}  {values}{suffix}")
    lines.append(f"{indent}}};")
    return "\n".join(lines)


def write_asset_block(asset: GeneratedAsset, symbol: str) -> list[str]:
    return [
        f'  constexpr const char* {symbol}Source = "{asset.source_name}";',
        f'  constexpr const char* {symbol}Generated = "{asset.path.name}";',
        f"  constexpr uint16_t {symbol}Width = {asset.width};",
        f"  constexpr uint16_t {symbol}Height = {asset.height};",
        c_array(f"{symbol}Png", asset.path.read_bytes()),
        f"  constexpr size_t {symbol}PngSize = sizeof({symbol}Png);",
        "",
    ]


def write_header(badge_en: GeneratedAsset, badge_ja: GeneratedAsset, profile: GeneratedAsset, qr: GeneratedAsset) -> None:
    total_size = badge_en.size + badge_ja.size + profile.size + qr.size
    parts = [
        "#pragma once",
        "",
        "#include <Arduino.h>",
        "",
        "namespace embedded_assets {",
        "",
        "  constexpr uint16_t kBadgeWidth = 540;",
        "  constexpr uint16_t kBadgeHeight = 960;",
        "",
    ]
    parts += write_asset_block(badge_en, "kBadgeEn")
    parts += write_asset_block(badge_ja, "kBadgeJa")
    parts += write_asset_block(profile, "kProfile")
    parts += write_asset_block(qr, "kQr")
    parts += [
        f"  constexpr size_t kEmbeddedPngTotalSize = {total_size};",
        "",
        "}  // namespace embedded_assets",
        "",
    ]
    HEADER_PATH.write_text("\n".join(parts), encoding="utf-8")


def describe_info(info: ImageInfo | None) -> str:
    if info is None:
        return "not found"
    return f"{info.path.name} ({info.fmt}, {info.width}x{info.height}, {info.size} bytes)"


def describe_generated(asset: GeneratedAsset) -> str:
    return f"{asset.path.name} ({asset.width}x{asset.height}, {asset.size} bytes; source: {asset.source_name})"


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate bilingual embedded PaperBadge assets.")
    parser.add_argument(
        "source_dir",
        nargs="?",
        type=Path,
        default=DEFAULT_SOURCE_DIR,
        help="Folder containing badge.json and local badge images.",
    )
    args = parser.parse_args()

    source_dir = args.source_dir.resolve()
    if not source_dir.exists():
        raise SystemExit(f"Source folder not found: {source_dir}")

    files = sorted(path for path in source_dir.iterdir() if path.is_file())
    images = [info for path in files if (info := inspect_image(path))]
    badge_json_path = source_dir / "badge.json"
    badge_json_path = badge_json_path if badge_json_path.exists() else None

    print(f"Inspecting: {source_dir}")
    print("Files found:")
    for path in files:
        info = inspect_image(path)
        if info:
            print(f"  - {path.name}: {info.fmt}, {info.width}x{info.height}, {info.size} bytes")
        else:
            print(f"  - {path.name}: {path.stat().st_size} bytes")

    if not images:
        raise SystemExit("No PNG/JPEG images found.")

    selected_ja = choose_asset(images, score_japanese_badge)
    if selected_ja and score_japanese_badge(selected_ja) < 85.0:
        selected_ja = None

    excluded = {selected_ja.path} if selected_ja else set()
    selected_en = choose_asset(images, score_full_badge, excluded)
    if selected_en is None:
        raise SystemExit("Could not identify an English/full badge image.")

    selected_profile = choose_asset(images, score_profile, {selected_en.path, *(set() if selected_ja is None else {selected_ja.path})})
    selected_qr = choose_asset(
        images,
        score_qr,
        {selected_en.path, *(set() if selected_ja is None else {selected_ja.path}), *(set() if selected_profile is None else {selected_profile.path})},
    )
    if selected_profile is None:
        raise SystemExit("Could not identify a profile photo.")
    if selected_qr is None:
        raise SystemExit("Could not identify a QR code.")

    print()
    print("Selected mapping:")
    print(f"  English full badge source file: {describe_info(selected_en)}")
    print(f"  Japanese full badge source file: {describe_info(selected_ja)}")
    print(f"  profile photo source file: {describe_info(selected_profile)}")
    print(f"  QR source file: {describe_info(selected_qr)}")
    print(f"  badge.json source file: {badge_json_path.name if badge_json_path else 'not found'}")

    badge_json = load_badge_json(badge_json_path)

    badge_en = generate_english_badge(selected_en, GENERATED_DIR / "badge_en.png")
    badge_ja = generate_japanese_badge(selected_ja, selected_profile, selected_qr, badge_json, GENERATED_DIR / "badge_ja.png")
    profile = normalize_profile(selected_profile, GENERATED_DIR / "profile.png")
    qr = normalize_qr(selected_qr, GENERATED_DIR / "qr.png")
    write_header(badge_en, badge_ja, profile, qr)

    print()
    print("Generated assets:")
    print(f"  badge_en.png: {describe_generated(badge_en)}")
    print(f"  badge_ja.png: {describe_generated(badge_ja)}")
    print(f"  profile.png: {describe_generated(profile)}")
    print(f"  qr.png: {describe_generated(qr)}")
    print(f"  header: {HEADER_PATH.relative_to(REPO_ROOT)} ({HEADER_PATH.stat().st_size} bytes)")
    print(f"  embedded PNG total: {badge_en.size + badge_ja.size + profile.size + qr.size} bytes")


if __name__ == "__main__":
    main()
