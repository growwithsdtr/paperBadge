#!/usr/bin/env python3
"""Convert the real PaperCoach Markdown prep sheet into firmware-ready JSON."""

from __future__ import annotations

import argparse
import json
import re
import shutil
from collections import Counter
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE_DIR = REPO_ROOT / "sample-data" / "papercoach"
GENERATED_DIR = REPO_ROOT / "generated" / "papercoach"
DIST_DECK_DIR = REPO_ROOT / "dist" / "sdcard" / "papercoach" / "decks"

SECTION_RE = re.compile(r"^##\s+([A-J])\.\s+(.+?)\s*$")
CARD_RE = re.compile(r"^###\s+(\d+[a-z]?)\.\s+(.+?)\s*$", re.IGNORECASE)
FIELD_RE = re.compile(r"^\*\*(Spoken|Anchor|Confidence|Watch)\.\*\*\s*(.*)$")
THEME_RE = re.compile(r"^\*Theme:\s*(.+?)\*\s*$")
QUESTION_RE = re.compile(r'^\*\*(\d+)\.\s+(?:\((.+?)\)\s*)?"(.+?)"\*\*\s+[—-]\s+(.+?)\s*$')


def choose_markdown(path_arg: Path | None) -> Path:
    if path_arg:
        path = path_arg.resolve()
        if not path.exists():
            raise SystemExit(f"Markdown file not found: {path}")
        return path

    candidates = sorted(DEFAULT_SOURCE_DIR.glob("*.md"))
    if not candidates:
        raise SystemExit(f"No Markdown files found under {DEFAULT_SOURCE_DIR}")
    likely = [path for path in candidates if "interview" in path.name.lower() and "prep" in path.name.lower()]
    if len(likely) == 1:
        return likely[0]
    if len(candidates) == 1:
        return candidates[0]

    names = "\n".join(f"  - {path}" for path in candidates)
    raise SystemExit(f"Multiple Markdown files found; choose one explicitly:\n{names}")


def clean_inline(text: str) -> str:
    text = text.strip()
    text = re.sub(r"\s+", " ", text)
    text = text.replace("**", "")
    text = re.sub(r"(?<!\w)\*(.+?)\*(?!\w)", r"\1", text)
    return text.strip()


def slug_words(text: str) -> list[str]:
    words = re.findall(r"[A-Za-z][A-Za-z0-9+-]{2,}", text.lower())
    stop = {
        "and",
        "the",
        "for",
        "with",
        "your",
        "you",
        "how",
        "why",
        "what",
        "our",
        "this",
        "that",
        "from",
    }
    return [word for word in words if word not in stop][:8]


def card_id(section_id: str, number: str) -> str:
    match = re.match(r"^(\d+)([a-z]?)$", number, re.IGNORECASE)
    if not match:
        return f"{section_id}{number}"
    digits, suffix = match.groups()
    return f"{section_id}{int(digits):02d}{suffix}"


def infer_tags(section_title: str, title: str, theme: str, confidence: str, must_master: bool) -> list[str]:
    tags = set(slug_words(section_title) + slug_words(title) + slug_words(theme))
    if must_master:
        tags.add("must_master")
    if confidence:
        tags.add(re.sub(r"[^a-z0-9]+", "_", confidence.lower()).strip("_"))
    return sorted(tags)


def parse_card_block(
    section_id: str,
    section_title: str,
    number: str,
    raw_title: str,
    block_lines: list[str],
) -> dict[str, Any]:
    must_master = "★" in raw_title
    title = clean_inline(raw_title.replace("★", ""))
    fields: dict[str, str] = {}
    theme = ""
    current_field: str | None = None

    for line in block_lines:
        stripped = line.strip()
        if not stripped:
            continue
        theme_match = THEME_RE.match(stripped)
        if theme_match:
            theme = clean_inline(theme_match.group(1))
            current_field = None
            continue
        field_match = FIELD_RE.match(stripped)
        if field_match:
            current_field = field_match.group(1).lower()
            fields[current_field] = clean_inline(field_match.group(2))
            continue
        if current_field:
            fields[current_field] = clean_inline(fields[current_field] + " " + stripped)

    spoken = fields.get("spoken", "")
    anchor = fields.get("anchor", "")
    confidence = fields.get("confidence", "")
    watch = fields.get("watch", "")
    return {
        "id": card_id(section_id, number),
        "section_id": section_id,
        "section": section_title,
        "number": number,
        "title": title,
        "must_master": must_master,
        "theme": theme,
        "spoken": spoken,
        "anchor": anchor,
        "confidence": confidence,
        "watch": watch,
        "tags": infer_tags(section_title, title, theme, confidence, must_master),
        "source_text": "\n".join(block_lines).strip(),
    }


def parse_questions_to_ask(lines: list[str]) -> list[dict[str, Any]]:
    cards: list[dict[str, Any]] = []
    in_part_2 = False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("# PART 2"):
            in_part_2 = True
            continue
        if not in_part_2:
            continue
        match = QUESTION_RE.match(stripped)
        if not match:
            continue
        number, prefix, question, guidance = match.groups()
        title = clean_inline(f"{prefix}: {question}" if prefix else question)
        spoken = clean_inline(guidance)
        cards.append(
            {
                "id": f"Q{int(number):02d}",
                "section_id": "Q",
                "section": "Questions to ask interviewer",
                "number": number,
                "title": title,
                "must_master": number == "1",
                "theme": "Questions to ask interviewer",
                "spoken": spoken,
                "anchor": "",
                "confidence": "Perspective",
                "watch": "Use selectively by interviewer type.",
                "tags": infer_tags("Questions to ask interviewer", title, "Questions", "Perspective", number == "1"),
                "source_text": stripped,
            }
        )
    return cards


def parse_cards(markdown: str) -> list[dict[str, Any]]:
    lines = markdown.splitlines()
    cards: list[dict[str, Any]] = []
    current_section_id = ""
    current_section_title = ""
    pending: tuple[str, str, list[str]] | None = None

    def flush_pending() -> None:
        nonlocal pending
        if pending and current_section_id:
            number, raw_title, block_lines = pending
            cards.append(parse_card_block(current_section_id, current_section_title, number, raw_title, block_lines))
        pending = None

    for line in lines:
        section_match = SECTION_RE.match(line)
        if section_match:
            flush_pending()
            current_section_id, current_section_title = section_match.groups()
            current_section_title = clean_inline(current_section_title)
            continue

        card_match = CARD_RE.match(line)
        if card_match and current_section_id:
            flush_pending()
            number, raw_title = card_match.groups()
            pending = (number, raw_title, [])
            continue

        if pending:
            pending[2].append(line)

    flush_pending()
    cards.extend(parse_questions_to_ask(lines))
    return cards


def parse_table(lines: list[str], start_index: int) -> tuple[list[dict[str, str]], int]:
    header = [clean_inline(cell) for cell in lines[start_index].strip().strip("|").split("|")]
    rows: list[dict[str, str]] = []
    index = start_index + 2
    while index < len(lines) and lines[index].strip().startswith("|"):
        cells = [clean_inline(cell) for cell in lines[index].strip().strip("|").split("|")]
        if len(cells) == len(header):
            rows.append(dict(zip(header, cells, strict=True)))
        index += 1
    return rows, index


def extract_metadata(markdown: str) -> dict[str, Any]:
    lines = markdown.splitlines()
    metadata: dict[str, Any] = {
        "maturity_precision": [],
        "metric_defense": [],
        "phrases_to_retire": [],
        "locked_figures": [],
        "confidence_map": [],
    }

    current_heading = ""
    index = 0
    while index < len(lines):
        stripped = lines[index].strip()
        if stripped.startswith("## "):
            current_heading = clean_inline(stripped.lstrip("# "))
        if stripped.startswith("|") and index + 1 < len(lines) and set(lines[index + 1].strip()) <= {"|", "-", " "}:
            rows, index = parse_table(lines, index)
            heading_lower = current_heading.lower()
            if "maturity precision" in heading_lower:
                metadata["maturity_precision"] = rows
            elif "metric defense" in heading_lower:
                metadata["metric_defense"] = rows
            elif "phrases to retire" in heading_lower:
                metadata["phrases_to_retire"] = rows
            elif "locked figures" in heading_lower:
                metadata["locked_figures"] = rows
            continue
        if current_heading.lower().startswith("confidence map") and stripped.startswith("- "):
            metadata["confidence_map"].append(clean_inline(stripped[2:]))
        index += 1

    return metadata


def build_glossary(metadata: dict[str, Any], cards: list[dict[str, Any]]) -> dict[str, Any]:
    terms: list[dict[str, str]] = []
    for row in metadata.get("phrases_to_retire", []):
        retired = next(iter(row.values()), "")
        replacement = list(row.values())[1] if len(row) > 1 else ""
        if retired and replacement:
            terms.append({"term": retired, "definition": f"Prefer: {replacement}", "source": "phrases_to_retire"})
    for row in metadata.get("locked_figures", []):
        values = list(row.values())
        if len(values) >= 2:
            terms.append({"term": values[0], "definition": " | ".join(values[1:]), "source": "locked_figures"})
    for card in cards:
        if "glossary" in card["tags"]:
            terms.append({"term": card["title"], "definition": card["spoken"], "source": card["id"]})
    return {"schema_version": 1, "terms": terms}


def classify_watch(watch: str) -> str:
    lower = watch.lower()
    if "metric" in lower or "denominator" in lower or "baseline" in lower or "attribution" in lower:
        return "weak metric framing"
    if "production" in lower or "prototype" in lower or "pilot" in lower or "maturity" in lower:
        return "confuses prototype/pilot/production"
    if "defensive" in lower or "blame" in lower:
        return "sounds defensive"
    if "mechanism" in lower or "generic" in lower or "framework" in lower:
        return "too generic"
    if "claim" in lower or "over" in lower:
        return "overclaims maturity"
    return "lacks mechanism"


def build_drills(cards: list[dict[str, Any]], metadata: dict[str, Any]) -> dict[str, Any]:
    drills: list[dict[str, Any]] = []
    confidence_options = ["Evidence-backed", "Framework-only", "Perspective", "Unclear"]

    for card in cards:
        if card["section_id"] == "Q":
            continue
        if card.get("confidence"):
            correct = card["confidence"].split("—", 1)[0].strip()
            if correct not in confidence_options:
                correct = next((option for option in confidence_options if option.lower() in card["confidence"].lower()), "Unclear")
            drills.append(
                {
                    "id": f"{card['id']}-confidence",
                    "type": "mcq",
                    "card_id": card["id"],
                    "prompt": f"What confidence framing belongs with: {card['title']}?",
                    "options": confidence_options,
                    "correct_index": confidence_options.index(correct),
                    "explanation": f"Card {card['id']} is tagged: {card['confidence']}",
                }
            )
        if card.get("watch"):
            category = classify_watch(card["watch"])
            options = [
                "overclaims maturity",
                "weak metric framing",
                "too generic",
                "sounds defensive",
                "lacks mechanism",
                "confuses prototype/pilot/production",
            ]
            drills.append(
                {
                    "id": f"{card['id']}-watch",
                    "type": "weak_answer",
                    "card_id": card["id"],
                    "prompt": f"What is the main risk in a weak answer to: {card['title']}?",
                    "options": options,
                    "correct_index": options.index(category),
                    "explanation": card["watch"],
                }
            )
        if card.get("must_master") and card.get("watch"):
            drills.append(
                {
                    "id": f"{card['id']}-hostile",
                    "type": "hostile_followup",
                    "card_id": card["id"],
                    "prompt": f"Interviewer pushes on '{card['title']}': {card['watch']}",
                    "options": [],
                    "correct_index": 0,
                    "explanation": f"Best response principle: {card['anchor'] or card['confidence']}",
                }
            )

    for index, row in enumerate(metadata.get("metric_defense", []), start=1):
        values = list(row.values())
        if len(values) >= 2:
            drills.append(
                {
                    "id": f"metric-{index:02d}",
                    "type": "metric_precision",
                    "card_id": "",
                    "prompt": f"Which phrasing is most defensible for {values[0]}?",
                    "options": [
                        values[1],
                        "Claim it as a clean randomized causal result.",
                        "Use the largest number without naming denominator.",
                        "Avoid baseline and cohort details.",
                    ],
                    "correct_index": 0,
                    "explanation": values[1],
                }
            )

    return {"schema_version": 1, "drills": drills}


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description="Convert PaperCoach Markdown prep sheet into generated JSON.")
    parser.add_argument("markdown", nargs="?", type=Path, help="Markdown prep sheet path. Defaults to sample-data/papercoach.")
    args = parser.parse_args()

    markdown_path = choose_markdown(args.markdown)
    markdown = markdown_path.read_text(encoding="utf-8")
    metadata = extract_metadata(markdown)
    cards = parse_cards(markdown)

    if len(cards) < 50:
        raise SystemExit(f"Extracted only {len(cards)} cards from {markdown_path}; expected at least 50.")

    section_counts = Counter(card["section_id"] for card in cards)
    must_master_count = sum(1 for card in cards if card["must_master"])
    payload = {
        "schema_version": 1,
        "source_path": str(markdown_path.relative_to(REPO_ROOT)),
        "card_count": len(cards),
        "must_master_count": must_master_count,
        "section_counts": dict(sorted(section_counts.items())),
        "metadata": metadata,
        "cards": cards,
    }

    interview_cards_path = GENERATED_DIR / "interview_cards.json"
    drills_path = GENERATED_DIR / "drills.json"
    glossary_path = GENERATED_DIR / "glossary.json"
    dist_deck_path = DIST_DECK_DIR / "interview_cards.json"

    write_json(interview_cards_path, payload)
    write_json(drills_path, build_drills(cards, metadata))
    write_json(glossary_path, build_glossary(metadata, cards))
    dist_deck_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(interview_cards_path, dist_deck_path)

    print(f"Source: {markdown_path}")
    print(f"Card count: {len(cards)}")
    print(f"Must-master count: {must_master_count}")
    print("Section counts:")
    for section, count in sorted(section_counts.items()):
        print(f"  {section}: {count}")
    print("Outputs:")
    print(f"  {interview_cards_path.relative_to(REPO_ROOT)}")
    print(f"  {drills_path.relative_to(REPO_ROOT)}")
    print(f"  {glossary_path.relative_to(REPO_ROOT)}")
    print(f"  {dist_deck_path.relative_to(REPO_ROOT)}")


if __name__ == "__main__":
    main()
