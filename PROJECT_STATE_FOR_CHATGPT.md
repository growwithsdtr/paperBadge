# PaperBadge Project State — v5.8-dev16 Handoff

_Last updated: 2026-06-13_

---

## Git / Remote Status

| Item | Value |
|------|-------|
| Branch | `main` |
| Remote | `https://github.com/growwithsdtr/paperBadge.git` |

---

## Firmware

- **Version:** `v5.8-dev16` (`src/main.cpp:20`)
- **Build:** SUCCESS — RAM 49.5% · Flash 47.7%
- **Upload:** SUCCESS — `/dev/cu.usbmodem1101`
- **Smoke test:** PASS (7/7 boot log checks)

---

## What Changed in v5.8-dev16

### Fix 1: Option box heights — tier-based + vertically centered text

**Problem:** Option boxes used a continuous height formula and fixed top padding of 12px,
making the top and bottom padding visually asymmetric.

**Fix:**
- `optionButtonHeightFor` now snaps to discrete tier heights (1-line / 2-line / 3-line).
  All options on a screen share the max tier. Reader S/M/L change the tier thresholds.
- `drawOptionButton` centers the text block vertically: `textY = rect.y + (rect.h - textBlockH) / 2`.
- `drawDrillResultView` (post-answer result view) applies the same centering.
- Measurement font (`applyTypographyFont`) and drawing font (`applyBodyFont`) are the same
  for each reader size.

Tier heights (vPad = 24px for M/L, 20px for S):
- Reader L: 1-line → 74px, 2-line → 124px, 3-line → 174px
- Reader M: 1-line → 70px (min), 2-line → 106px, 3-line → 147px
- Reader S: 1-line → 60px (min), 2-line → 84px, 3-line → 116px

### Fix 2: Drill answer flow state machine — comments + correct result-view tap behavior

The tap state machine is now documented with inline comments. Key change:

| Location | Top-half tap | Bottom-half tap |
|----------|-------------|-----------------|
| Question/options (pre-answer) | prev page within item | next options page |
| **Result view (was: prev item)** | **prev result page, or no-op** | feedback page 0 |
| Feedback page 1 | back to result view | next feedback page OR next item |
| Feedback page 2+ | previous feedback page | next feedback page OR next item |

Previously, tapping the top half of the result view navigated to the previous drill item.
Now it pages within the result view (or does nothing on the first page). Footer arrows (← →)
still navigate between drill items from any state.

### Fix 3: Result view pagination — fit-aware, same plan as pre-answer

`currentCoachReaderPageCount()` previously returned 1 for the result view unconditionally.
Now it calls `buildDrillPagePlan` and returns `plan.totalPages`, matching the pre-answer
behavior. `drawDrillResultView` uses `gCoachStage` to render the correct page:
- Combined layout: question + all options on one page (unchanged)
- Split layout: question pages, then options pages with compact reminder and selected/correct state

Serial log: `Drill result view: item=X page=P/N combined=yes|no`

### Fix 4: Feedback formatter — colon-label and hyphen-list splitting

`formatFeedbackBody()` extended with two new splitting rules:

**Colon-label splitting:** If 2+ `"Label: "` patterns exist (short word before `": "`),
insert a newline before each label word. Skips URLs (`://`). Handles patterns like:
- `A: ... B: ... C: ...`
- `Problem: ... Fix: ... Result: ...`
- `Question: ... Answer: ...`

**Hyphen-list splitting:** If 2+ `" - "` occurrences, insert newlines to split list items.

Existing rules preserved: numbered lists (`1. ` / `1)`) and semicolon clauses (2+ `; `).

Applied to: drill feedback, practice body sections, hostile follow-up answers, suggested
responses routed through `appendFeedbackSection` / `appendGlossarySectionFormatted`.

### Fix 5: Follow-up drill — stage renamed "Defense" → "Suggested response"

In `buildCoachReaderStages`, the `CoachItemType::HostileFollowup` answer stage is renamed
from "Defense" to "Suggested response" to make the interviewer-prompt / candidate-answer
hierarchy explicit. The body now passes through `formatFeedbackBody` before building pages.
This matches the label already used in `buildPracticeLines` for the Practice view.

### Fix 6: Results page — fit-based combining

`resultsCombinedFirstPage()` replaced the `statCount <= 3` count check with a measured fit
check. The function now computes:
- Condensed summary block height: 270px (fixed)
- Per-category max height: 2 × metadataLineHeight + 12 + 62 = 126px
- Available content area: 742px (960 - header 132 - footer 86)

For statCount ≤ 3: needed ≤ 648px < 742px — always fits and correctly combines.
For statCount > 4: excluded by early return, split as before.
The constants are documented so future content additions have an obvious update path.

### Fix 7: Settings — Advanced button uses consistent 24px font

`drawButton(gAdvancedButton, "Advanced")` replaced with
`drawSegmentedButton(gAdvancedButton, "Advanced", false)`. This uses `kSegmentedPx` (24px),
matching all segmented controls ("S/M/L", "Fast/Balanced/Clean", "Responsive/Balanced/Max",
"Normal/Strap"). Previously the Advanced button used `buttonPx` (28px), making it visually
larger than all surrounding buttons.

### Fix 8: Documentation updated

`PROJECT_STATE_FOR_CHATGPT.md`, `docs/PRD_PaperBadge_v0.6.md`, `docs/QA_GUIDE.md`,
and `README.md` updated to v5.8-dev16. Port reference updated to `/dev/cu.usbmodem1101`.

---

## Known Limitations

- **Japanese support:** Not yet implemented. Font assets, deck format, and SRS logic are
  planned for a future milestone after v5.8.
- **Deep sleep:** Remains blocked — PaperS3 touch wake is not physically verified.
- **Per-option explanations:** Not embedded; drill feedback shows shared explanation only.
- **Results fit check:** Uses a conservative worst-case estimate (2-line labels). If a
  future content update changes the summary block height, update `resultsCombinedFirstPage`.

---

## Settings — fixed medium UI

Settings renders at `FontSizeMode::Large` regardless of Reader S/M/L (saved and restored).
Reader size only affects study content (Drills, Exam, Practice, Glossary).

Key layout constants:
- Section label font: 28px (`kSettingsLabelPx`)
- Segmented button font: 24px (`kSegmentedPx`) — used for all buttons including Advanced
- Button height: 52px
- Battery bar: 38px height, vertically centered in 52px block
- Y positions: Reader (206/242), Refresh (310/346), Power (414/450), Orientation (518/554),
  Advanced (632), Home (height-82)

---

## Drill/Exam Option Boxes — current spec

- All options on a screen use the same shared height (`sharedOptionButtonHeight`).
- Height snaps to a tier based on the maximum line count across all options.
- Text is centered vertically inside the box.
- Reader S/M/L controls option font size (24/31/40px), which controls tier thresholds.
- `optionButtonHeightFor` and `drawOptionButton` must always apply the same px (via
  `optionTextPxFor`), ensuring measurement and drawing are consistent.

---

## Drill State Machine — summary

After an option is tapped:
1. `gSelectedOption = option`, `gDrillShowFeedback = true`, `gCoachStage = 0` → feedback shown.
2. Feedback top-half: prev feedback page OR back to result view (gCoachStage=0 reset).
3. Result view bottom-half: enter feedback at page 0.
4. Result view top-half: prev result page, or no-op on page 0.
5. Feedback bottom-half (last page): next drill item.
6. Footer ← →: always move between items.

---

## Upload / Monitor Commands

```bash
pio run
pio run -t upload --upload-port /dev/cu.usbmodem1101
python3 .claude/skills/run-paperbadge/serial_capture.py
UPLOAD=0 bash .claude/skills/run-paperbadge/smoke.sh
```

Port: `/dev/cu.usbmodem1101` (macOS cu. prefix).

---

## What Changed in v5.8-dev15

### Fix 1: Normalized option box heights across Drill/Exam screens

All answer option boxes on a single question screen now share the same height (the max height
required by any option on that screen). Previously, if option A needed 2 lines and B–D needed 1,
the boxes had inconsistent heights which looked broken.

**Implementation:** `sharedOptionButtonHeight(item, width)` computes the per-option max and
normalizes before drawing. Applied to the combined and options-only paths in `renderCoachScreen`,
`renderExamQuestion`, and `drawDrillResultView`. `buildDrillPagePlan` also normalizes heights
internally so the fit calculation is consistent with the draw result.

Serial log updated: `Drill combined shown: item=X options=N sharedH=H totalPages=P`

### Fix 2: Drill answer tap flow — verified correct from dev14

The immediate-feedback-on-tap flow (set `gDrillShowFeedback = true` on first option tap) was
implemented in v5.8-dev14 and remains stable. No code change in dev15; behavior documented.

### Fix 3: Feedback and body text formatting for readability

`formatFeedbackBody(text)` helper inserts hard line breaks at:
- Numbered list items (`1. ` / `1) ` preceded by whitespace)
- Semicolon-separated clauses (2+ occurrences of `; ` → one clause per line)

### Fix 4: Drill follow-up / hostile-followup label updated

`buildPracticeLines` for `HostileFollowup` items now labels the answer section
"Suggested response" (was "Defense"). Formatting applied via `appendGlossarySectionFormatted`.

### Fix 5: Results pages — combined summary + categories on page 1

When the session has ≤3 category stats, the Summary and Categories content is combined onto
a single first page. Condensed summary block followed by thin divider line and category bars.

### Fix 6: Settings page — larger labels, bigger buttons, improved layout

- `kSegmentedPx`: 20 → 24
- Section labels: 24px → 28px (`kSettingsLabelPx`)
- Button height `bh`: 48 → 52
