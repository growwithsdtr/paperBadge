#!/usr/bin/env python3
"""Build embedded PaperCoach deck header from generated JSON."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = REPO_ROOT / "generated" / "papercoach" / "interview_cards.json"
DRILLS_PATH = REPO_ROOT / "generated" / "papercoach" / "drills.json"
GLOSSARY_PATH = REPO_ROOT / "generated" / "papercoach" / "glossary.json"
HEADER_PATH = REPO_ROOT / "src" / "embedded_papercoach_deck.h"
MAX_EMBEDDED_OPTIONS = 4


def raw_string(text: Any) -> str:
    value = "" if text is None else str(text)
    for index in range(100):
        delim = f"PB{index}"
        if f"){delim}\"" not in value:
            return f'R"{delim}({value}){delim}"'
    return json.dumps(value, ensure_ascii=False)


def embedded_options_for(drill: dict[str, Any]) -> tuple[list[str], int, bool]:
    """Limit options while keeping the correct answer addressable."""
    source_options = list(drill.get("options", []))
    if not source_options:
        return [], 0, False

    source_correct = int(drill.get("correct_index", 0))
    if source_correct < 0 or source_correct >= len(source_options):
        return source_options[:MAX_EMBEDDED_OPTIONS], source_correct, False

    if len(source_options) <= MAX_EMBEDDED_OPTIONS:
        return source_options, source_correct, False

    keep_indices: list[int] = []
    for index in range(len(source_options)):
        if index == source_correct:
            continue
        keep_indices.append(index)
        if len(keep_indices) >= MAX_EMBEDDED_OPTIONS - 1:
            break

    if source_correct not in keep_indices:
        keep_indices.append(source_correct)
    keep_indices = sorted(keep_indices)

    selected_options = [source_options[index] for index in keep_indices]
    remapped_correct = keep_indices.index(source_correct)
    return selected_options, remapped_correct, True


def main() -> None:
    if not SOURCE_PATH.exists():
        raise SystemExit(f"Missing generated deck: {SOURCE_PATH}. Run tools/convert_prep_sheet.py first.")
    if not DRILLS_PATH.exists():
        raise SystemExit(f"Missing generated drills: {DRILLS_PATH}. Run tools/convert_prep_sheet.py first.")
    if not GLOSSARY_PATH.exists():
        raise SystemExit(f"Missing generated glossary: {GLOSSARY_PATH}.")

    payload = json.loads(SOURCE_PATH.read_text(encoding="utf-8"))
    drills_payload = json.loads(DRILLS_PATH.read_text(encoding="utf-8"))
    glossary_payload = json.loads(GLOSSARY_PATH.read_text(encoding="utf-8"))
    cards = payload.get("cards", [])
    drills = drills_payload.get("drills", [])
    glossary = glossary_payload.get("terms", [])
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
        "struct Drill {",
        "  const char* id;",
        "  const char* type;",
        "  const char* cardId;",
        "  const char* prompt;",
        "  const char* options[4];",
        "  uint8_t optionCount;",
        "  uint8_t correctIndex;",
        "  const char* explanation;",
        "};",
        "",
        "struct GlossaryTerm {",
        "  const char* category;",
        "  const char* term;",
        "  const char* definition;",
        "  const char* interviewImportance;",
        "  const char* example;",
        "};",
        "",
        f"constexpr size_t kCardCount = {len(cards)};",
        f"constexpr size_t kMustMasterCount = {payload.get('must_master_count', 0)};",
        f"constexpr size_t kDrillCount = {len(drills)};",
        f"constexpr size_t kGlossaryCount = {len(glossary)};",
        f"constexpr size_t kSourceJsonBytes = {SOURCE_PATH.stat().st_size};",
        f"constexpr size_t kDrillsJsonBytes = {DRILLS_PATH.stat().st_size};",
        f"constexpr size_t kGlossaryJsonBytes = {GLOSSARY_PATH.stat().st_size};",
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
            "const Drill kDrills[] PROGMEM = {",
        ]
    )

    remapped_drills: list[tuple[str, int, int, int, int]] = []
    invalid_source_keys: list[tuple[str, int, int]] = []

    for drill in drills:
        source_options = list(drill.get("options", []))
        source_correct = int(drill.get("correct_index", 0))
        options, correct_index, remapped = embedded_options_for(drill)
        option_count = len(options)
        if option_count > 0 and (correct_index < 0 or correct_index >= option_count):
            invalid_source_keys.append((str(drill.get("id", "")), option_count, correct_index))
        if remapped:
            remapped_drills.append((str(drill.get("id", "")), len(source_options), source_correct, option_count, correct_index))
        while len(options) < MAX_EMBEDDED_OPTIONS:
            options.append("")
        lines.extend(
            [
                "  {",
                f"    {raw_string(drill.get('id'))},",
                f"    {raw_string(drill.get('type'))},",
                f"    {raw_string(drill.get('card_id'))},",
                f"    {raw_string(drill.get('prompt'))},",
                "    {",
                f"      {raw_string(options[0])},",
                f"      {raw_string(options[1])},",
                f"      {raw_string(options[2])},",
                f"      {raw_string(options[3])},",
                "    },",
                f"    {option_count},",
                f"    {correct_index},",
                f"    {raw_string(drill.get('explanation'))},",
                "  },",
            ]
        )

    lines.extend(
        [
            "};",
            "",
            "const GlossaryTerm kGlossaryTerms[] PROGMEM = {",
        ]
    )

    for term in glossary:
        lines.extend(
            [
                "  {",
                f"    {raw_string(term.get('category'))},",
                f"    {raw_string(term.get('term'))},",
                f"    {raw_string(term.get('definition'))},",
                f"    {raw_string(term.get('interview_importance') or term.get('why_it_matters') or term.get('why'))},",
                f"    {raw_string(term.get('example'))},",
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
    print(f"Embedded PaperCoach drills: {len(drills)}")
    print(f"Drill option remaps: {len(remapped_drills)}")
    for drill_id, original_count, original_correct, embedded_count, embedded_correct in remapped_drills:
        print(
            "  remapped "
            f"{drill_id}: original options={original_count} correct={original_correct} "
            f"embedded options={embedded_count} correct={embedded_correct}"
        )
    if invalid_source_keys:
        print(f"Invalid source drill keys after remap: {len(invalid_source_keys)}")
        for drill_id, option_count, correct_index in invalid_source_keys:
            print(f"  invalid {drill_id}: optionCount={option_count} correctIndex={correct_index}")
    print(f"Embedded PaperCoach glossary terms: {len(glossary)}")
    print(f"Source JSON bytes: {SOURCE_PATH.stat().st_size}")
    print(f"Drills JSON bytes: {DRILLS_PATH.stat().st_size}")
    print(f"Glossary JSON bytes: {GLOSSARY_PATH.stat().st_size}")
    print(f"Header: {HEADER_PATH.relative_to(REPO_ROOT)} ({HEADER_PATH.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
