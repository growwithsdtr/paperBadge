# PaperBadge Project State — v5.8-dev2 Handoff

_Last updated: 2026-06-10_

---

## Git / Remote Status

| Item | Value |
|------|-------|
| HEAD | (see latest commit) |
| Branch | `main` |
| Remote | `https://github.com/growwithsdtr/paperBadge.git` |

---

## Firmware

- **Version:** `v5.8-dev2` (`src/main.cpp:20`)
- **Build:** SUCCESS — RAM 49.4% · Flash 47.4% · 10.70 s
- **Upload:** SUCCESS — `/dev/tty.usbmodem1101`, 28.2 s

---

## What Changed in v5.8-dev2

### Phase 1 — Compact Practice Header

Practice header now follows the curated UI-label spec:

**Line 1:** `{id} | {Must/Card} | {compact section}`
**Line 2:** compact synthesized title (NOT raw question, NOT stage name, NOT page count)
**Divider:** thin horizontal line at y=84 between header and body

New functions:
- `compactHeaderCategory(raw)` — extended with explicit mappings for all known sections:
  - "Background, Motivation & Fit" → "Background / Fit"
  - "Product Strategy & Discovery" → "Product Strategy"
  - "Stakeholder Management & Influence" → "Stakeholder Mgmt"
  - "Execution / Delivery / Tradeoffs" → "Execution / Tradeoffs"
  - "Data, Metrics & Analytics" → "Data & Metrics"
  - "Cross-functional Leadership" → "Cross-func Leadership"
  - "Technical Depth & AI/ML" → "Technical / AI/ML"
- `compactPracticeTitle(rawTitle)` — synthesizes compact display title:
  1. Checks known mapping table first (C23, A01, A04, etc.)
  2. Strips common question starters ("How do you", "Tell me about a time", etc.)
  3. Removes trailing "?"
  4. Falls back to sanitized raw title (truncated by `fitHeaderText` if too long)

**Page count removed from header line 1.** Pagination is still available via page taps.

### Phase 3 — Drill/Exam Option Typography

Option buttons now use `applyBodyFont` (regular FreeSans) instead of `applyTypographyFont` (bold).
- Outlined buttons preserved
- Text is lighter/less visually heavy
- Applies to Drills, BlitzQuiz, WeakAnswerDetector, MetricPrecision, and Exam screens

### Phase 5 — Settings Footer Cleanup

Removed the "Advanced: Debug > Power Audit" text line from Settings.
It was positioned at y=916 which overlapped with the Home button at y=900 (display height 960).
Settings now ends cleanly with the Battery Saver button and Home button.
Power Audit remains accessible via Debug menu.

### Phase 6 — Power Audit Footer Simplified

Power Audit footer replaced with standard 3-button layout:

```
[ < Prev ]   [ Home ]   [ Next > ]
```

Removed from footer: "Power: mode", "Sleep: mode", "Profile: mode" toggle buttons.
These were visually crowded and created unnecessary power-cycling risk in the footer.
Power profile and sleep mode remain accessible via Settings and future Debug > Power Lab.

### Phase 7 — Power Audit Observability (page 1)

Power Audit page 1 CPU row now shows:
- `CPU: 240 MHz [active]` — at full speed
- `CPU: 80 MHz [idle-scaled]` — after idle threshold
- `Profile: Balanced  idle after: 60s` row added below CPU row
- Reordered for readability: CPU → Profile → Idle → Static → Loop delay → Refresh → Redraw → Last refresh → Poll age

Power profiles and CPU scaling implementation unchanged (already complete in v5.8-dev).

### Phase 8 — Ghosting Fix: Clean Refresh on Card Change

`nextCoachItem()` and `previousCoachItem()` now always trigger `gCoachNeedsCleanEntryRefresh = true` when on `Screen::InterviewPractice`.

Previously, card navigation only triggered clean refresh when leaving feedback (after answering a drill). Now any card change in Practice triggers a full clean entry refresh to clear ghosting.

---

## Practice Header Compact Title Examples

| Card | Raw title | Compact title |
|------|-----------|---------------|
| A01 | Self-introduction / career & recent work | Self-introduction, career & recent work |
| A04 | Success / 90-day impact & successful traits | 90-day impact & successful traits |
| C23 | How do you QA/test a non-deterministic chatbot or AI output | QA/testing non-deterministic AI output |
| C24 | Guardrails against hallucinations / PII leaks | Hallucination & PII guardrails |
| A02 | Why leave consulting for an in-house role | Leave consulting for in-house role |
| A03 | Why this company and industry | This company and industry |

---

## Current UX Decisions (v5.8-dev2)

| Decision | Value |
|----------|-------|
| Practice header line 1 | id &#124; Must/Card &#124; compact section |
| Practice header line 2 | compact synthesized title |
| Practice header divider | yes, at y=84 |
| Page count in header | no (removed in dev2) |
| Stage name in header | no (removed in dev) |
| Option button font | regular (non-bold) |
| Settings "Advanced" hint | removed (no space above Home) |
| Power Audit footer | 3-button: prev / home / next |
| Ghosting on card change | clean refresh forced |
| Default power profile | Balanced |
| Deep sleep | blocked (touch wake unverified) |

---

## Power Profiles (v5.8-dev / still current)

| Profile | Idle scale threshold | Loop delay idle | Notes |
|---------|---------------------|-----------------|-------|
| Balanced | 60 s | 250 ms | Default, safe |
| Aggressive | 25 s | 400 ms | Lower CPU sooner |
| Badge Max | 20 s | 200 ms (badge) | Badge-first, strongest safe |

All profiles: CPU 240→80 MHz on static screens after threshold, restore to 240 before any refresh/SD/input.
No deep sleep in any profile by default.

---

## Recommended Settings

| Setting | Value |
|---------|-------|
| Font size | Reader M |
| Font style | High Contrast |
| Refresh mode | Balanced refresh |
| Power | Battery Saver (toggle in Settings > Power) |
| Power Profile | Balanced (change in Debug > Power Audit) |

---

## Font Summary

- **English body text:** `FreeSans9/12/18/24pt7b` (regular, non-bold) — body roles, option buttons
- **English labels/titles:** `FreeSansBold9/12/18/24pt7b` — metadata, labels, headers, question prompts
- **Future Japanese:** `M5GFX lgfxJapanGothic_*` — **not** FreeSans (no CJK). Requires UTF-8-safe sanitizer.

---

## Remaining Known Issues

1. **Power Audit toggle buttons removed from footer** — Power mode/sleep/profile toggles are no longer in Power Audit footer. Currently accessible via Settings (power mode) and Debug (profile via Power Audit). A future "Debug > Power Lab" screen could consolidate these.
2. **SD deck-dump Best: letter** — unguarded `'A' + item.correctIndex` in debug SD dump path. Low priority.
3. **`docs/embedded_deck_dump.md` stale** — regenerate after next device QA session.
4. **Japanese / UTF-8** — no CJK font, no UTF-8 sanitizer. Out of scope.
5. **Dynamic deck categories** — firmware-hardcoded; new grids require firmware change.
6. **SRS / long-term history** — not implemented.
7. **Glossary search** — not implemented.
8. **Deep sleep** — remains blocked (touch wake unverified on PaperS3).

---

## QA Photo Checklist (v5.8-dev2)

1. Practice A01 — confirm header: `A01 | Must | Background / Fit` on line 1, "Self-introduction, career & recent work" on line 2, divider visible
2. Practice C23 — confirm header: `C23 | Must | AI/ML Evaluation & QA` on line 1, "QA/testing non-deterministic AI output" on line 2
3. Practice A04 — confirm Confidence is a section (not inline), no clip; compact title "90-day impact & successful traits"
4. Practice B16, C24, E33, F41 — confirm confidence wrapping on long cards, compact title on each
5. Practice multi-page — confirm page navigation works via content taps; footer left/right for cards; NO page count in header
6. Practice card change — confirm no ghosting (clean refresh fires on every card change)
7. Drill screen — confirm option buttons use regular (lighter) font, not bold
8. Settings screen — confirm no "Advanced: Debug > Power Audit" text; power section shows Battery Saver button then Home
9. Power Audit page 1 — confirm battery/USB/radios rows
10. Power Audit page 1 — confirm `CPU: 240 MHz [active]` row and `Profile: Balanced  idle after: 60s` row
11. Power Audit navigation — confirm 3-button footer: < Prev, Home icon, Next >
12. Power Audit page 2 → idle on page for 60s → re-enter → confirm `CPU: 80 MHz [idle-scaled]` shows

---

## Intended v6 Structure (not yet implemented)

```
Main Menu:
  Badge
  Interview Prep: Practice / Drills / Exam / Glossary / Results
  Japanese (v6): Practice / Drills / Exam / Glossary / Results
  Settings
  Debug
```

Japanese readiness requirements (not yet implemented):
- Font: M5GFX `lgfxJapanGothic_*` or efontJA_*
- UTF-8-safe sanitizer before enabling
- Japanese text wrapping without spaces
- Generic section/category deck schema
- Future decks should provide explicit `compact_title` field instead of relying on raw question text synthesis

---

## Out of Scope (do not implement without explicit sign-off)

- Japanese/N3 content schema
- UTF-8 sanitizer overhaul
- External font loading
- SRS / spaced repetition
- New drill or card content
- Broad UI redesign
- Deep sleep by default
- Full v6 app split
