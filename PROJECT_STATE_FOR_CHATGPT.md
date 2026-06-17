# PaperBadge Project State — v5.9-dev2 Handoff

_Last updated: 2026-06-17_

---

## Git / Remote Status

| Item | Value |
|------|-------|
| Branch | `main` |
| Remote | `https://github.com/growwithsdtr/paperBadge.git` |

---

## Firmware

- **Version:** `v5.9-dev2` (`src/main.cpp:20`)
- **Build:** SUCCESS — RAM 51.3% · Flash 48.0%
- **Upload:** SUCCESS — `/dev/cu.usbmodem1101`
- **Smoke test:** PASS (7/7 boot log checks)

---

## What Changed in v5.9-dev2

### Japanese typography, layout, and ghosting polish

**Goal:** Make the v5.9-dev1 Japanese prototype visually readable on the physical device. Fix
text being too small, Results using huge English text, Reference being raw/repetitive, and
ghosting when entering Japanese sub-screens. No new features.

**Typography — Reader S/M/L now affects Japanese screens:**

Added `japaneseBodyPxForReader()` — maps the current Reader setting to a Japanese Gothic size:

| Reader setting | Japanese body px | Japanese title px | Button height |
|---------------|-----------------|-------------------|--------------|
| Reader S (Medium) | 24 | 28 | 76 |
| Reader M (Large, default) | 28 | 32 | 86 |
| Reader L (XL) | 32 | 36 | 96 |

`applyJapaneseTitleFont()` now scales one step above body rather than being fixed at 28px.
`applyJapaneseBodyFont()` now defaults to `japaneseBodyPxForReader()` instead of hardcoded 24.
`drawJapaneseOptionButton()` now scales button text and line height with reader size.
Explicit px overrides (e.g. fixed 20px for the compact header line) still work as before.

**Mixed English/Japanese font routing:**

Added `applyJapaneseEnglishLabelFont(px)` — calls `applySansBoldFont(px)` directly,
independent of `gSettings.fontStyleMode`. Used for English content inside Japanese screens
(feedback title "Correct"/"Wrong", English meaning line "EN: ...", tag labels, Results title/
stats, Reference section headers). This avoids HighContrast huge text while keeping the Sans
Bold-like visual that matches the English interview typography.

English-only text inside Japanese screens now goes through `wrapTextToLines()` (ASCII-safe),
not `wrapJapaneseTextToLines()`. Japanese text (prompt, choices, answer sentence, explanation)
still uses `wrapJapaneseTextToLines()` + Gothic fonts.

**Daily Questions layout (Patch 3):**

- Prompt uses `japaneseBodyPxForReader()` (was hardcoded 26).
- Prompt max lines: 6/5/4 for S/M/L to accommodate larger fonts without overflow.
- Four answer buttons scale height: 76/86/96 for S/M/L.
- Button gap: 14/14/12 for S/M/L.
- Pre-answer footer: Home only (no faded Next label).
- Post-answer footer: Next + Home (unchanged).

**Daily Questions feedback layout (Patch 4):**

- Title "Correct"/"Wrong": English Sans Bold at 28/32/36 for S/M/L (was Gothic 28px fixed).
- Correct answer line: Gothic at reader size.
- Answer sentence: Gothic at reader size.
- Japanese explanation: Gothic at reader size.
- English meaning ("EN: …"): Sans Bold 22px (smaller than Japanese body, ASCII-safe wrap).
- Grammar tag: Sans Bold 20px compact label; vocabulary/kanji tags removed from feedback
  (they are visible in Reference instead).

**Japanese Results (Patch 5):**

Rewrote to use `applyJapaneseEnglishLabelFont()` at fixed compact sizes (32/26/22/24px)
instead of `applyCoachTitleFont()`/`applyCoachContentFont()` which routed through HighContrast
and produced huge text. Layout: title (32), subtitle (20), Answered/Correct rows (26), By area
label (22), category rows (24). Empty state also uses fixed 24px.

**Japanese Reference (Patch 6):**

Rewrote from raw per-item rows to structured, deduped sections:

```
Japanese Reference
N3 sample · Week 1 Day 1

Kanji        [deduped single characters, space-separated]
Grammar      [deduped patterns, one per line]
Vocabulary   [deduped words, space-separated]
```

Section headers use `applyJapaneseEnglishLabelFont(22)`. Japanese content uses Gothic 24px.
Deduplication is O(n²) with the 11-item dataset — safe. `collectDeduped()` splits on commas
and trims whitespace.

**Clean refresh / ghosting (Patch 7):**

Added `"japanese entry"` as a recognized reason string in `isImageOrZoomRefresh()`. This
causes the e-ink refresh policy to use a full clean refresh (same path as image/zoom/badge
transitions) when entering Japanese sub-screens.

Default refresh reason for `renderJapaneseDaily`, `renderJapaneseResults`,
`renderJapaneseReference` is now `"japanese entry"`. The touch handler for JapaneseMenu uses
`"japanese entry"` explicitly when navigating to sub-screens. The answer-selected transition
(question → feedback) also uses `"japanese entry"`, replacing the former `"answer selected"`.

Result: entering any Japanese sub-screen triggers a clean EPD refresh in Fast/Balanced modes,
eliminating ghosting from previous menu screens behind Daily Questions / feedback / Reference /
Results / Mock Test placeholder.

**Not changed:** Interview Practice/Drills/Exam/Glossary/Results behavior, Home structure,
deep sleep, Badge behavior, existing English/interview typography, sanitizeCoachText(),
wrapTextToLines(), English font functions globally, Settings/Advanced control screens.

**Known limitations (unchanged from dev1):** Only Week 1 Day 1 is embedded (no full book
import); Mock Test is a placeholder; Japanese Results has no SD persistence; no SRS; no
volunteer notes; no multi-source concept UI.

---

## What Changed in v5.9-dev1

### Japanese support — Daily Questions prototype, added safely after the v5.8-dev19 power patch

**Goal:** Start Japanese support without touching Interview Practice/Drills/Exam/Glossary/Results
behavior, Home structure (beyond one new entry), English/interview typography, Badge behavior, or
deep sleep. No SRS, no volunteer notes, no multi-source concept UI, no full 新にほんご500問 import
in this pass.

**Added:**
- New Home entry **Japanese** → Japanese menu (Daily Questions, Mock Test, Reference, Results, Home).
- **Daily Questions**: one embedded, originally written N3-style sample set (Week 1, Day 1 —
  11 items covering もじ/kanji, ごい/vocabulary, ぶんぽう/grammar; `book_id = "n3_sample_w1d1"`, not
  an extraction of any copyrighted book). One question per screen, Japanese prompt, 4 outlined
  choices, immediate feedback (correct/wrong, correct choice, Japanese answer sentence, Japanese
  explanation, English meaning, grammar/vocab/kanji tags), Next/Home navigation. E-ink safe: white
  background, outlined buttons, no fills/gradients.
- **Reference**: single-page list of the grammar/vocabulary/kanji tags from the same Week 1 Day 1 set.
- **Results**: simple RAM-only tally — answered count, correct count/percent, breakdown by
  kanji/vocabulary/grammar. Backed by a new `JapaneseSessionResult`/`gJapaneseResults[64]` struct
  that is fully separate from `SessionResult`/`gSessionResults`; Japanese answers never appear in
  the existing Results screen, and the data resets on reboot (no SD persistence yet).
- **Mock Test**: placeholder only (`renderPlaceholderScreen`), no full test flow yet.
- **Japanese-safe text path** (parallel to, not replacing, the existing English path):
  `sanitizeJapaneseText()` preserves UTF-8 verbatim (does not route through the ASCII-only,
  destructive `sanitizeCoachText()`); `wrapJapaneseTextToLines()` wraps by UTF-8 code point via
  `utf8SequenceLength()` instead of splitting on spaces; `applyJapaneseTitleFont()`/
  `applyJapaneseBodyFont()` select `lgfxJapanGothic_*` sizes via the existing `applyGothicFont()`.
- **Power eligibility**: Japanese menu/Reference/Results/Mock Test are static-idle (WarmIdle)
  eligible like other menu/read-only screens. Daily Questions is excluded from WarmIdle (same
  precedent as Drills/Exam question screens) but is LightNap-eligible once in feedback state —
  `isAnswerSelectionActive()` blocks LightNap during the pre-answer state, mirroring Drills/Exam
  exactly. Deep sleep, Badge behavior, and all other power logic are unchanged.
- Required Japanese render-test strings confirmed present verbatim in the embedded dataset:
  郵便局, 引っ越した, 荷物, 違っていました, 子供のころ, ものだ, てばかりいる, とっちゃいけない,
  ちゃう, とく.

**Not changed:** Interview Practice/Drills/Exam/Glossary/Results behavior (aside from the new
Home entry routing to Japanese), Home's 4-domain structure (not introduced), deep sleep, Badge
behavior, existing English/interview typography (Sans Bold-like/High Contrast unchanged), the
existing `sanitizeCoachText()`/`wrapTextToLines()`/English font functions.

**Known limitations:** Only Week 1 Day 1 is embedded (no full book import); Mock Test is a
placeholder; Reference is a flat list, not a curated study view; Japanese Results has no SD
persistence; no SRS; no volunteer notes; no multi-source concept UI.

---

## What Changed in v5.8-dev19

### Home/Menu power verification patch (WarmIdle eligibility + Power Lab diagnostics)

**Problem (independent review):** Home was LightNap-eligible but never reached WarmIdle, so it
stayed at 240 MHz indefinitely regardless of profile. Separately, `idleScaleBlockedReason()` and
`maybeScaleIdleCpu()` carried a leftover restriction from v5.8-dev8 that blocked WarmIdle CPU
scaling on the Max Battery profile for every screen except Badge — so even screens that *were*
WarmIdle-eligible (Settings, Advanced, Glossary, Results, Power Lab, etc.) never scaled down under
Max Battery. This explained the reported battery drain on Home with Power = Max. Power Lab also
lacked explicit WarmIdle/LightNap countdown and current-screen/refresh-age fields needed to verify
the fix on real hardware.

**Fix:**
- `isStaticIdleScreen()`: added `Screen::Home`, `Screen::PracticeMenu`, `Screen::GlossaryMenu`,
  `Screen::DrillsMenu` — all read-only navigation/display screens with no answer-timing concerns.
  Badge was deliberately left out of WarmIdle (stays LightNap-eligible only) to avoid touching
  static badge/auto-rotate behavior in this patch.
- Removed the `gPowerProfile == PowerProfile::BadgeMax && gScreen != Screen::Badge` restriction
  from `idleScaleBlockedReason()` and `maybeScaleIdleCpu()`. Max Battery now scales CPU down on any
  static-idle-eligible screen as soon as its 5s threshold elapses, matching Responsive/Balanced
  semantics (just with a shorter threshold).
- LightNap eligibility, the answer-selection guard (`isAnswerSelectionActive()`), and Settings/
  Advanced/Power Lab's LightNap-ineligibility are all unchanged.
- Power Lab page 1 gained: explicit "WarmIdle: ACTIVE/inactive  in: <countdown|due now|blocked>"
  row; "LightNap (this screen): eligible/no — reason  in: <countdown|due now|blocked>"; "Screen:"
  row now reports the live current screen plus age of the last display refresh. Threshold values,
  CPU MHz, scale/restore counters, 80MHz duration stats, and battery poll age were already present
  and are unchanged.
- Touch-down (`recordUserActivity("touch press")`) already restores CPU to 240 MHz before any tap
  is processed, so adding WarmIdle to Home/menus does not introduce any tap-latency regression.
- No changes to Interview UI, drill/exam behavior, badge rendering, deck content, typography, or
  Japanese code. Deep sleep remains untouched and blocked (`BadgeSleepMode::DeepExperiment`, GT911
  wake, RTC wake logic all untouched). Japanese implementation has not been started.

---

## What Changed in v5.8-dev18

### Docs and serial config hygiene before Japanese support

**Problem:** `platformio.ini` still pointed at the legacy `/dev/tty.usbmodem1101` port. Separately,
`docs/PAPERCOACH_PRD.md` and `docs/QA_GUIDE.md` had drifted from actual firmware behavior: Debug
was listed as a top-level Home button (it was moved to Settings → Advanced back in dev17, but the
PRD's Mode Definitions/Navigation Model sections still showed it at the top level), Settings was
described as having badge sleep controls (sleep lives under Settings → Advanced), the Practice
stage list still said "Defense" instead of the actual "Suggested response" stage name, and the QA
guide described "Power Lab / Power Audit" as if Power Audit were a reachable screen — it is dead
code with no button ever wired to it; only Power Lab's 4 pages are reachable.

**Fix:**
- `platformio.ini`: `upload_port`/`monitor_port` changed to `/dev/cu.usbmodem1101`.
- `docs/PAPERCOACH_PRD.md`: Home menu listed as 7 buttons with Debug explicitly called out as not
  top-level; Settings bullet corrected to Reader size/Refresh/Power/Orientation/Advanced; "Defense"
  replaced with "Suggested response".
- `docs/QA_GUIDE.md`: "Power Lab / Power Audit" replaced with "Power Lab pages 1–4" (both
  occurrences); "Export deck text" corrected to "Export deck" to match the actual Advanced-screen
  button label; photo batch heading bumped to v5.8-dev18.
- `README.md`: firmware version bumped to v5.8-dev18 (Home/Settings/diagnostics/port sections were
  already accurate from the dev16/dev17 passes, so left unchanged).
- No UI behavior, drill/exam logic, power logic, LightNap, deep sleep, badge rendering, or deck
  content changed.

---

## What Changed in v5.8-dev17

### Fix 1: Split drill result pages are now all reachable

**Problem:** In the post-answer result view, the bottom-half tap always jumped straight to
feedback regardless of whether more result pages existed. On a 2-page split drill, result page 1
was unreachable.

**Fix:**
- Result view bottom-half: if `gCoachStage + 1 < resultPages`, advance to next result page.
  Only enters feedback on the *last* result page.
- Result view top-half: unchanged — goes to prev result page or no-op on page 0.
- Feedback top-half returning to result view: restores `gDrillLastResultPage` (the page the user
  was on when they entered feedback) instead of always resetting to page 0.
- Added `gDrillLastResultPage` global; reset on item change and option selection.
- State machine comment block updated to document all tap behaviors.
- Result page count in tap handler now uses `buildDrillPagePlan` directly (same source as draw).

### Fix 2: Conservative feedback formatting — colon-label and hyphen-list

**Problem:** `formatFeedbackBody` split on any `Word: value` pattern, including prose like
`outcome: low output: high`. It also split `A - B - C` prose on spaced dashes.

**Fix:**
- Colon-label splitting now requires the label to match a known allowlist:
  `Q`, `A`, `Question`, `Answer`, `Problem`, `Fix`, `Result`, `Selected`, `Best`, `Why`,
  `Risk`, `Action`, `Example`, `Note`.
- Random lowercase prose labels (`outcome:`, `output:`, `signal:`, etc.) are no longer split.
- Hyphen-list splitting removed: the old ` - ` prose splitting is gone. Text like
  `"risk - signal - stop"` stays on one line. Newline-prefixed hyphens (`\n- item`) are already
  on separate lines and need no action.
- Numbered list and semicolon splitting unchanged.

### Fix 3: Docs updated to v5.8-dev17

`PROJECT_STATE_FOR_CHATGPT.md`, `docs/PRD_PaperBadge_v0.6.md`, `docs/QA_GUIDE.md`,
and `README.md` updated:
- Version bump to v5.8-dev17.
- Debug removed as a top-level Home menu item; diagnostics now live under Settings → Advanced.
- Port reference confirmed `/dev/cu.usbmodem1101` throughout.
- Sleep controls path updated to Settings → Advanced → Power Lab.
- QA checklist references updated (Debug → Settings → Advanced).

---

## Known Limitations

- **Japanese support:** Not yet implemented. Next milestone is Japanese deck support.
- **Deep sleep:** Remains blocked — PaperS3 touch wake is not physically verified. Not being
  pursued further for now.
- **Per-option explanations:** Not embedded; drill feedback shows shared explanation only.
- **Interview mode:** Frozen after physical QA.
- No P0/P1 runtime blockers remain.

---

## Drill State Machine — summary (v5.8-dev17)

After an option is tapped:
1. `gSelectedOption = option`, `gDrillShowFeedback = true`, `gCoachStage = 0`, `gDrillLastResultPage = 0` → feedback shown.
2. Feedback top-half on page 0: return to result view at `gDrillLastResultPage`.
3. Feedback top-half on page N>0: go to prev feedback page.
4. Feedback bottom-half (not last page): advance feedback page.
5. Feedback bottom-half (last page): next drill item.
6. Result view bottom-half (not last page): advance to next result page.
7. Result view bottom-half (last page): save `gDrillLastResultPage`, enter feedback at page 0.
8. Result view top-half: prev result page, or no-op on page 0.
9. Footer ← →: always move between items from any state.

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

## Drill State Machine — summary (superseded by v5.8-dev17 section above)

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
