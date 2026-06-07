#!/usr/bin/env python3
"""Prepare a PaperBadge SD-card folder from sample data."""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SAMPLE_ROOT = REPO_ROOT / "sample-data"
DEFAULT_OUTPUT = REPO_ROOT / "dist" / "PAPERSD"
BADGE_JSON = SAMPLE_ROOT / "paperbadge" / "badge.json"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Copy PaperBadge sample assets into an SD-card-ready folder."
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help=f"Destination folder, such as /Volumes/PAPERSD (default: {DEFAULT_OUTPUT})",
    )
    return parser.parse_args()


def validate_badge_json() -> None:
    with BADGE_JSON.open("r", encoding="utf-8") as file:
        json.load(file)


def copy_sample_data(destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)

    for item in SAMPLE_ROOT.iterdir():
        target = destination / item.name
        if item.is_dir():
            shutil.copytree(item, target, dirs_exist_ok=True)
        else:
            shutil.copy2(item, target)


def main() -> None:
    args = parse_args()
    destination = args.output.expanduser().resolve()

    validate_badge_json()
    copy_sample_data(destination)

    print(f"Prepared PaperBadge SD folder: {destination}")
    print(f"Expected badge JSON: {destination / 'paperbadge' / 'badge.json'}")


if __name__ == "__main__":
    main()
