#!/usr/bin/env python3
"""Build embedded PaperBadge fallback assets from sample-data/paperbadge."""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE_DIR = REPO_ROOT / "sample-data" / "paperbadge"
GENERATED_DIR = REPO_ROOT / "generated-assets" / "embedded"
HEADER_PATH = REPO_ROOT / "src" / "embedded_assets.h"

FULL_VARIANTS = {
    "badge.png",
    "badge_full.png",
    "full_badge.png",
    "badge_en.png",
    "complete_badge.png",
    "completebadge.png",
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


@dataclass(frozen=True)
class ImageInfo:
    path: Path
    fmt: str
    width: int
    height: int
    size: int

    @property
    def name_key(self) -> str:
        return self.path.name.lower()

    @property
    def aspect(self) -> float:
        return self.width / self.height if self.height else 0.0

    @property
    def area(self) -> int:
        return self.width * self.height


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
        if marker in {
            0xC0,
            0xC1,
            0xC2,
            0xC3,
            0xC5,
            0xC6,
            0xC7,
            0xC9,
            0xCA,
            0xCB,
            0xCD,
            0xCE,
            0xCF,
        }:
            if segment_length >= 7:
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
    target_aspects = (540 / 960, 960 / 540)
    aspect_score = max(0.0, 35.0 - min(abs(info.aspect - target) for target in target_aspects) * 120.0)
    area_score = min(info.area / (540 * 960), 4.0) * 10.0
    name_score = 60.0 if info.name_key in FULL_VARIANTS else 0.0
    return name_score + aspect_score + area_score


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


def choose_asset(images: list[ImageInfo], scorer, exclude: set[Path]) -> ImageInfo | None:
    candidates = [image for image in images if image.path not in exclude]
    if not candidates:
        return None
    return max(candidates, key=scorer)


def run_sips(source: Path, output: Path, width: int | None = None, height: int | None = None, max_edge: int | None = None) -> None:
    if not shutil.which("sips"):
        raise SystemExit(
            "macOS 'sips' was not found. This script uses sips for PNG normalization; no Pillow install is required."
        )

    output.parent.mkdir(parents=True, exist_ok=True)
    command = ["sips", "-s", "format", "png"]
    if width is not None and height is not None:
        command += ["-z", str(height), str(width)]
    elif max_edge is not None:
        command += ["-Z", str(max_edge)]
    command += [str(source), "--out", str(output)]
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def c_array(name: str, data: bytes, indent: str = "  ") -> str:
    lines = [f"{indent}const uint8_t {name}[] PROGMEM = {{"]
    for start in range(0, len(data), 12):
        chunk = data[start : start + 12]
        values = ", ".join(f"0x{byte:02X}" for byte in chunk)
        suffix = "," if start + 12 < len(data) else ""
        lines.append(f"{indent}  {values}{suffix}")
    lines.append(f"{indent}}};")
    return "\n".join(lines)


def write_header(full: ImageInfo, profile: ImageInfo | None, qr: ImageInfo | None) -> None:
    total_size = full.size + (profile.size if profile else 0) + (qr.size if qr else 0)
    parts = [
        "#pragma once",
        "",
        "#include <Arduino.h>",
        "",
        "namespace embedded_assets {",
        "",
        f'  constexpr const char* kFullBadgeSource = "{full.path.name}";',
        f"  constexpr uint16_t kFullBadgeWidth = {full.width};",
        f"  constexpr uint16_t kFullBadgeHeight = {full.height};",
        c_array("kFullBadgePng", full.path.read_bytes()),
        "  constexpr size_t kFullBadgePngSize = sizeof(kFullBadgePng);",
        "",
    ]

    if profile:
        parts += [
            f'  constexpr const char* kProfileSource = "{profile.path.name}";',
            f"  constexpr uint16_t kProfileWidth = {profile.width};",
            f"  constexpr uint16_t kProfileHeight = {profile.height};",
            c_array("kProfilePng", profile.path.read_bytes()),
            "  constexpr size_t kProfilePngSize = sizeof(kProfilePng);",
            "",
        ]
    else:
        parts += [
            '  constexpr const char* kProfileSource = "";',
            "  constexpr uint16_t kProfileWidth = 0;",
            "  constexpr uint16_t kProfileHeight = 0;",
            "  constexpr const uint8_t* kProfilePng = nullptr;",
            "  constexpr size_t kProfilePngSize = 0;",
            "",
        ]

    if qr:
        parts += [
            f'  constexpr const char* kQrSource = "{qr.path.name}";',
            f"  constexpr uint16_t kQrWidth = {qr.width};",
            f"  constexpr uint16_t kQrHeight = {qr.height};",
            c_array("kQrPng", qr.path.read_bytes()),
            "  constexpr size_t kQrPngSize = sizeof(kQrPng);",
            "",
        ]
    else:
        parts += [
            '  constexpr const char* kQrSource = "";',
            "  constexpr uint16_t kQrWidth = 0;",
            "  constexpr uint16_t kQrHeight = 0;",
            "  constexpr const uint8_t* kQrPng = nullptr;",
            "  constexpr size_t kQrPngSize = 0;",
            "",
        ]

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


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate src/embedded_assets.h from local PaperBadge sample assets.")
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
    badge_json = source_dir / "badge.json"
    badge_json = badge_json if badge_json.exists() else None

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

    selected_full = choose_asset(images, score_full_badge, set())
    if selected_full is None:
        raise SystemExit("Could not identify a full badge image.")
    selected_profile = choose_asset(images, score_profile, {selected_full.path})
    selected_qr = choose_asset(images, score_qr, {selected_full.path, *(set() if selected_profile is None else {selected_profile.path})})

    print()
    print("Selected mapping:")
    print(f"  full badge source file: {describe_info(selected_full)}")
    print(f"  profile photo source file: {describe_info(selected_profile)}")
    print(f"  QR source file: {describe_info(selected_qr)}")
    print(f"  badge.json source file: {badge_json.name if badge_json else 'not found'}")

    full_output = GENERATED_DIR / "embedded_full_badge.png"
    profile_output = GENERATED_DIR / "embedded_profile.png"
    qr_output = GENERATED_DIR / "embedded_qr.png"

    run_sips(selected_full.path, full_output, width=540, height=960)
    normalized_full = inspect_image(full_output)
    if normalized_full is None:
        raise SystemExit("Failed to normalize full badge image.")

    normalized_profile = None
    if selected_profile:
        run_sips(selected_profile.path, profile_output, max_edge=262)
        normalized_profile = inspect_image(profile_output)

    normalized_qr = None
    if selected_qr:
        run_sips(selected_qr.path, qr_output, width=320, height=320)
        normalized_qr = inspect_image(qr_output)

    write_header(normalized_full, normalized_profile, normalized_qr)

    print()
    print("Generated assets:")
    print(f"  full badge: {describe_info(normalized_full)}")
    print(f"  profile: {describe_info(normalized_profile)}")
    print(f"  QR: {describe_info(normalized_qr)}")
    print(f"  header: {HEADER_PATH.relative_to(REPO_ROOT)} ({HEADER_PATH.stat().st_size} bytes)")
    print(f"  embedded PNG total: {normalized_full.size + (normalized_profile.size if normalized_profile else 0) + (normalized_qr.size if normalized_qr else 0)} bytes")


if __name__ == "__main__":
    main()
