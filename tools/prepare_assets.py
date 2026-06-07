#!/usr/bin/env python3
"""Prepare PaperBadge image assets inside a paperbadge SD-card folder."""

from __future__ import annotations

import argparse
from pathlib import Path

try:
    from PIL import Image
except ImportError as exc:  # pragma: no cover - depends on local environment
    raise SystemExit(
        "Pillow is required for image conversion. Install it with:\n"
        "  python3 -m pip install Pillow"
    ) from exc


PHOTO_INPUT_NAMES = (
    "profilePhoto.png",
    "profilePhoto.jpg",
    "profilePhoto.jpeg",
)
QR_INPUT_NAMES = (
    "qr.JPG",
    "qr.jpg",
    "qr.jpeg",
    "qr.png",
)
PHOTO_OUTPUT_NAME = "profile_photo.png"
QR_OUTPUT_NAME = "qr.png"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert PaperBadge photo and QR assets in a paperbadge folder."
    )
    parser.add_argument(
        "folder",
        type=Path,
        help="Path to the SD card paperbadge folder, such as /Volumes/PAPERSD/paperbadge.",
    )
    return parser.parse_args()


def first_existing(folder: Path, names: tuple[str, ...]) -> Path | None:
    for name in names:
        candidate = folder / name
        if candidate.exists():
            return candidate
    return None


def center_crop_square(image: Image.Image) -> Image.Image:
    width, height = image.size
    side = min(width, height)
    left = (width - side) // 2
    top = (height - side) // 2
    return image.crop((left, top, left + side, top + side))


def save_profile_photo(source: Path, destination: Path) -> None:
    with Image.open(source) as image:
        prepared = center_crop_square(image.convert("L"))
        prepared = prepared.resize((220, 220), Image.Resampling.LANCZOS)
        prepared.save(destination, format="PNG", optimize=True)


def save_qr(source: Path, destination: Path) -> None:
    with Image.open(source) as image:
        prepared = image.convert("L")
        prepared = prepared.resize((320, 320), Image.Resampling.NEAREST)
        prepared = prepared.point(lambda pixel: 255 if pixel > 160 else 0, mode="1")
        prepared.save(destination, format="PNG", optimize=True)


def print_output(label: str, path: Path) -> None:
    with Image.open(path) as image:
        print(f"{label}: {path}")
        print(f"  size: {image.size[0]}x{image.size[1]}")
        print(f"  bytes: {path.stat().st_size}")


def main() -> None:
    args = parse_args()
    folder = args.folder.expanduser().resolve()
    folder.mkdir(parents=True, exist_ok=True)

    photo_source = first_existing(folder, PHOTO_INPUT_NAMES)
    qr_source = first_existing(folder, QR_INPUT_NAMES)

    if photo_source:
        photo_output = folder / PHOTO_OUTPUT_NAME
        save_profile_photo(photo_source, photo_output)
        print_output("profile photo", photo_output)
    else:
        print("profile photo: no profilePhoto.png/.jpg/.jpeg found")

    if qr_source:
        qr_output = folder / QR_OUTPUT_NAME
        save_qr(qr_source, qr_output)
        print_output("QR", qr_output)
    else:
        print("QR: no qr.JPG/.jpg/.jpeg/.png found")

    print("Original files were left untouched.")


if __name__ == "__main__":
    main()
