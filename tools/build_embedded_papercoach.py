#!/usr/bin/env python3
"""Build embedded PaperCoach deck header from generated JSON."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = REPO_ROOT / "generated" / "papercoach" / "interview_cards.json"
HEADER_PATH = REPO_ROOT / "src" / "embedded_papercoach_deck.h"


def raw_string(text: Any) -> str:
    value = "" if text is None else str(text)
    for index in range(100):
        delim = f"PB{index}"
        if f"){delim}\"" not in value:
            return f'R"{delim}({value}){delim}"'
    return json.dumps(value, ensure_ascii=False)


def main() -> None:
    if not SOURCE_PATH.exists():
        raise SystemExit(f"Missing generated deck: {SOURCE_PATH}. Run tools/convert_prep_sheet.py first.")

    payload = json.loads(SOURCE_PATH.read_text(encoding="utf-8"))
    cards = payload.get("cards", [])
    if len(cards) < 50:
        raise SystemExit(f"Refusing to embed only {len(cards)} cards; expected at least 50.")

    lines: list[str] = [
        "#pragma once",
        "",
        "#include <Arduino.h>",
        "",
        "namespace embedded_papercoach {",
        "",
        "struct Card {",
        "  const char* id;",
        "  const char* sectionId;",
        "  const char* section;",
        "  const char* number;",
        "  const char* title;",
        "  bool mustMaster;",
        "  const char* theme;",
        "  const char* spoken;",
        "  const char* anchor;",
        "  const char* confidence;",
        "  const char* watch;",
        "};",
        "",
        f"constexpr size_t kCardCount = {len(cards)};",
        f"constexpr size_t kMustMasterCount = {payload.get('must_master_count', 0)};",
        f"constexpr size_t kSourceJsonBytes = {SOURCE_PATH.stat().st_size};",
        f'constexpr const char* kSourcePath = "{payload.get("source_path", "")}";',
        "",
        "const Card kCards[] PROGMEM = {",
    ]

    for card in cards:
        lines.extend(
            [
                "  {",
                f"    {raw_string(card.get('id'))},",
                f"    {raw_string(card.get('section_id'))},",
                f"    {raw_string(card.get('section'))},",
                f"    {raw_string(card.get('number'))},",
                f"    {raw_string(card.get('title'))},",
                f"    {'true' if card.get('must_master') else 'false'},",
                f"    {raw_string(card.get('theme'))},",
                f"    {raw_string(card.get('spoken'))},",
                f"    {raw_string(card.get('anchor'))},",
                f"    {raw_string(card.get('confidence'))},",
                f"    {raw_string(card.get('watch'))},",
                "  },",
            ]
        )

    lines.extend(
        [
            "};",
            "",
            "}  // namespace embedded_papercoach",
            "",
        ]
    )

    HEADER_PATH.write_text("\n".join(lines), encoding="utf-8")
    print(f"Embedded PaperCoach cards: {len(cards)}")
    print(f"Must-master cards: {payload.get('must_master_count', 0)}")
    print(f"Source JSON bytes: {SOURCE_PATH.stat().st_size}")
    print(f"Header: {HEADER_PATH.relative_to(REPO_ROOT)} ({HEADER_PATH.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
