# PaperBadge Project State — v5.8-dev Handoff

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

- **Version:** `v5.8-dev` (`src/main.cpp:20`)
- **Build:** SUCCESS — RAM 49.4% · Flash 47.4% · 10.76 s
- **Upload:** SUCCESS — `/dev/tty.usbmodem1101`, 28.2 s

---

## What Changed in v5.8-dev

### Phase 1+2+4 — Unified Structured Practice Reader

Replaced the four separate stages (Question / Answer / Anchor / Watch-out) with a single continuous structured card renderer using the `GlossaryRenderLine` infrastructure already used by Glossary and Feedback screens.

**Sections per card type:**
- QA card: Question, Confidence, Answer, Anchor, Watch-out, Follow-up (explanation if present)
- HostileFollowup: Follow-up, Defense, Anchor
- WeakAnswer: Question, Explanation

**Typography:**
- Section labels: `applyCoachMetadataFont()` — small bold, metadata color
- Body lines: `applyCoachBodyFont()` — regular/non-bold, large readable
- Empty sections are skipped automatically

**Header:**
- Line 1: `{id} | {Must/Card} | {category}` (no stage name)
- Line 2: card title/question (if present and fits)
- Page number shown in header when >1 page

**Navigation:**
- Content tap top half = previous page
- Content tap bottom half = next page
- Footer left/right = previous/next card
- Home = home (unchanged)

**Confidence wrapping:**
- Confidence is now a proper section in the structured card (label + wrapped body)
- No longer clipped: paginated as part of the continuous reader

### Phase 3 — Tighter Margins

| Item | Before | After |
|------|--------|-------|
| `kCoachMargin` | 28 | 20 |

All content uses `kCoachMargin = 20` now. Headers and body aligned at x=20.

### Phase 5 — Drill/Exam Prompt Formatter

Added `formatDrillPrompt()`: inserts a `\n` after `: ` when the text following the colon is ≥16 chars and contains a space. Prevents long prompts like "What is the risk in a weak answer to: Self-introduction / career & recent work?" from running inline.

Applied in `buildDrillPagePlan()`.

### Phase 6 — Settings Power Wording

Settings screen "Power" section now shows:
- `Battery Saver` button (toggles Normal ↔ BatterySaver, shows `*` when active)
- `Advanced: Debug > Power Audit` text hint below

Previously was a read-only status line. Now tappable.

### Phase 7 — Power Audit Pagination (4 pages)

Power Audit is now paginated into 4 pages, navigable with `< Page` / `Page >` buttons:

| Page | Contents |
|------|----------|
| 1 | Battery/USB/radios/peripherals, Power mode, Profile |
| 2 | CPU MHz / idle scale, profile threshold, Refresh, Redraw count, Loop delay, Static screen status |
| 3 | Sleep mode/status, last sleep/wake, deep sleep block notice |
| 4 | Answer key warnings, sanitizer, touch diagnostics, deck info, firmware version |

Footer buttons on page 2: `Power: <mode>` + `Profile: <profile>` + `< Page` + `Page >` + `Home`
Footer buttons on other pages: `Power: <mode>` + `Sleep: <mode>` + `< Page` + `Page >` + `Home`

### Phase 8 — Experimental Power Profiles

Added `PowerProfile` enum: **Balanced** (default), **Aggressive**, **BadgeMax**.

| Profile | Idle scale threshold | Loop delay on static | Notes |
|---------|---------------------|---------------------|-------|
| Balanced | 60 s | 50 ms | Default, safe |
| Aggressive | 25 s | 100 ms idle / 400 ms active-idle | Lower CPU sooner |
| Badge Max | 20 s | 200 ms on badge | Badge-only, strongest safe experiment |

- Profiles do **not** enable deep sleep by default
- Deep sleep remains blocked (touch wake unverified on PaperS3)
- Light sleep allowed only in Aggressive/BadgeMax (BadgeSleepMode.Light must be explicitly set)
- CPU restores to 240 MHz on any input event before display refresh
- Profile saved to Preferences key `"powerProfile"`
- Cycled via `Profile: <name>` button on Power Audit page 2

### Phase 9 — v6 Architecture Notes (docs only)

Intended v6 main menu structure:
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
- Sanitizer must be made UTF-8 safe before enabling
- Japanese wrapping without spaces
- Generic section blocks (Prompt / Meaning / Grammar / Example / Answer / Explanation)

Navigation model (long-press Home → main home) — not yet changed.

---

## Recommended Settings (for QA and normal use)

| Setting | Value |
|---------|-------|
| Font size | Reader M |
| Font style | High Contrast |
| Refresh mode | Balanced refresh |
| Power | Battery Saver (toggle in Settings > Power) |
| Power Profile | Balanced (change in Debug > Power Audit page 2) |

---

## Font Summary

- **English body text:** `FreeSans9/12/18/24pt7b` (regular, non-bold) — body roles
- **English labels/titles:** `FreeSansBold9/12/18/24pt7b` — metadata, labels, headers, question prompts, option buttons
- **Future Japanese:** `M5GFX lgfxJapanGothic_*` — **not** FreeSans (no CJK). Requires UTF-8-safe sanitizer.

---

## Remaining Known Issues

1. **SD deck-dump Best: letter** — unguarded `'A' + item.correctIndex` in debug SD dump path. Low priority.
2. **`docs/embedded_deck_dump.md` stale** — regenerate after next device QA session.
3. **Japanese / UTF-8** — no CJK font, no UTF-8 sanitizer. Out of scope.
4. **Dynamic deck categories** — firmware-hardcoded; new grids require firmware change.
5. **SRS / long-term history** — not implemented.
6. **Glossary search** — not implemented.
7. **Deep sleep** — remains blocked (touch wake unverified on PaperS3).
8. **Power Audit page 2 CPU idle shows post-scale value** — CPU only scales after idle threshold; during active use will show 240 MHz.

---

## QA Photo Checklist (v5.8-dev)

1. Practice A04 — confirm unified reader, Confidence is a section (not inline), no clip
2. Practice B16, C23, C24, E33, F41 — confirm confidence wrapping on long cards
3. Practice any card — confirm header shows `{id} | {Must/Card} | {category}` (no stage name)
4. Practice multi-page card — confirm `< page >` navigation works, footer left/right for cards
5. Drill Weak Answer — confirm prompt breaks after `:` when applicable
6. Drill feedback page — confirm label bold, body regular
7. Settings screen — confirm Battery Saver button, `Advanced: Debug > Power Audit` hint
8. Power Audit page 1 — battery/USB/radios/profile row
9. Power Audit page 2 — CPU MHz, `Profile: Balanced` button, idle threshold
10. Power Audit page 3 — sleep status, deep sleep blocked notice

---

## Out of Scope (do not implement without explicit sign-off)

- Japanese/N3 content schema
- UTF-8 sanitizer overhaul
- External font loading
- SRS / spaced repetition
- New drill or card content
- Broad UI redesign
- Deep sleep by default
