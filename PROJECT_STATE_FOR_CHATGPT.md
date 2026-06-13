# PaperBadge Project State — v5.8-dev15 Handoff

_Last updated: 2026-06-13_

---

## Git / Remote Status

| Item | Value |
|------|-------|
| Branch | `main` |
| Remote | `https://github.com/growwithsdtr/paperBadge.git` |

---

## Firmware

- **Version:** `v5.8-dev15` (`src/main.cpp:20`)
- **Build:** SUCCESS — RAM 49.5% · Flash 47.7%
- **Upload:** SUCCESS — `/dev/cu.usbmodem1101`
- **Smoke test:** PASS (7/7 boot log checks)

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
implemented in v5.8-dev14 and remains stable. No code change in dev15; behavior documented:

| Location | Top-half tap | Bottom-half tap |
|----------|-------------|-----------------|
| Question/options screen (pre-answer) | prev page / item | options page / next drill item |
| Result view (post-answer, options shown) | previous item (if any) | feedback page |
| Feedback page 1 | back to result view | next feedback page OR next item |
| Feedback page 2+ | previous feedback page | next feedback page OR next item |

### Fix 3: Feedback and body text formatting for readability

New `formatFeedbackBody(text)` helper inserts hard line breaks at:
- Numbered list items (`1. ` / `1) ` preceded by whitespace)
- Semicolon-separated clauses (2+ occurrences of `; ` → one clause per line)

Applied in:
- `appendFeedbackSection` → "Selected", "Best", "Why this is best" on the drill feedback page
- `appendGlossarySectionFormatted` → body sections in Practice cards (Answer, Defense, Explanation, Anchor, Follow-up)

Does not split prose, short phrases, decimal numbers, or URLs.

### Fix 4: Drill follow-up / hostile-followup label updated

`buildPracticeLines` for `HostileFollowup` items now labels the answer section
**"Suggested response"** (was "Defense") to make the question/answer hierarchy explicit.
Formatting is applied via `appendGlossarySectionFormatted`.

### Fix 5: Results pages — combined summary + categories on page 1

When the session has ≤3 category stats (fits on one category page), the Summary and Categories
content is combined onto a single first page. The condensed summary block is followed by a
thin divider line and then the category bars.

Page count logic:
- 0 answers: 1 page (empty state)
- 1–3 stats: 3 pages (combined summary+cats · weakest · recent)
- 4–6 stats: 4 pages (combined + cats2 · weakest · recent)
- 7–8 stats: 5 pages (combined + cats2 + cats3 · weakest · recent)

New function: `renderResultsSummaryAndCategoriesPage()`. `resultsCombinedFirstPage()` controls
the branching. `resultsPageCountFor()` adjusted accordingly.

### Fix 6: Settings page — larger labels, bigger buttons, improved layout

- `kSegmentedPx`: 20 → **24** (segmented button font, Gothic_24 / SansBold12pt)
- Section labels: `applyCoachMetadataFont()` (24px) → **`applyTypographyFont(28)`** (28px)
- mV / USB detail lines: also bumped to 28px for consistency
- Button height `bh`: 48 → **52** for better touch targets
- Y-positions adjusted throughout to accommodate larger fonts and uniform spacing:
  - Reader size: label y=206, buttons y=242
  - Refresh: label y=310, buttons y=346
  - Power: label y=414, buttons y=450
  - Orientation: label y=518, buttons y=554
  - Advanced: y=632
  - Home: y=height-82
- Battery bar height: 44 → 38px (proportional to taller block now using 52px height)
- Selected state: unchanged (triple border + fake-bold; no `*`)

---

## What Changed in v5.8-dev14

### Fix 1: Drill/Exam option text now truly scales — 32px fallback eliminated

**Root cause of v5.8-dev13 failure:** `optionTextPxFor` capped Reader L at 36px as its "large"
size. When a label overflowed 2 lines at 36px it returned 32px. For Gothic: gothic_32 vs M's
gothic_28 is visually indistinguishable. For Sans (High Contrast): 36px → FreeSansBold18pt7b,
the same font as M at 31px. Users saw no difference between M and L option text.

**Fix:** Removed the 36px cap. `preferredPx = type.bodyPx` (40 for L, 31 for M, 24 for S).
Overflow logic for Reader L:
1. Try 40px (→ FreeSansBold24pt7b / gothic_36). If ≤2 lines → return 40px.
2. If >2 lines at 40px, try 36px. If ≤2 lines → return 36px.
3. If >2 lines at 36px → return 36px anyway (allow 3 lines, button grows). Never 32px.

Updated option font table:

| Reader | preferredPx | Overflow | Font (fits 1-2 lines) | Font (3 lines) |
|--------|-------------|----------|----------------------|----------------|
| S | 24 | — | Gothic_24 / SansBold12pt | n/a |
| M | 31 | 24px | Gothic_28 / SansBold18pt | Gothic_24 |
| L | 40 | 36px | Gothic_36 / SansBold24pt | Gothic_36 |

`optionButtonHeightFor` grows automatically for 3-line labels (no separate min change needed).

Serial log updated: `Drill fonts: screen=X reader=Y qPx=Z opt0Px=W opt0H=V feedbackPx=U bodyPx=T`

### Fix 2: Drill answer tap immediately shows feedback

**Was:** tapping an answer set `gDrillShowFeedback = false` → result view first → required second
tap to reach feedback.

**Now:** tapping an answer sets `gDrillShowFeedback = true` → feedback page shown immediately.

### Fix 3: Drill post-answer content navigation complete

Full tap map now:

| Location | Top-half tap | Bottom-half tap |
|----------|-------------|-----------------|
| Result view (question+options) | previous item (if any) | feedback page |
| Feedback page 1 | back to result view | next feedback page OR next item |
| Feedback page 2+ | previous feedback page | next feedback page OR next item |

- Footer arrows (← →): still move to prev/next item regardless of sub-page.
- Home: unchanged.
- `gDrillShowFeedback` resets to false on every item navigation (`nextCoachItem`, `previousCoachItem`).

### Fix 4: Settings "Reader size" section label — explicit font call

Added `applyCoachMetadataFont()` immediately before `drawString("Reader size", ...)` to match
the explicit calls already present for "Refresh", "Power", and "Orientation". No visual change
(the font was already 24px via carryover), but now all four labels are equally explicit.

---

## What Changed in v5.8-dev13

### Fix 1: Settings section labels now fixed medium size

"Refresh", "Power", and "Orientation" labels were silently drawn at 20px (`kSegmentedPx`
leftover from preceding button row). Now all four labels explicitly call `applyCoachMetadataFont()`
(24px) before drawing.

### Fix 2: Battery % text vertically co-aligned with battery bar

Uses `middle_left` datum at vertical centerline of battery block.

### Fix 3: Drill/Exam Reader L option fallback raised to 32px

Changed fallback from 31 (gothic_28, same as M) to 32 (gothic_32). **Note:** physical QA showed
this was insufficient — 32px is still too close to M. Fixed properly in v5.8-dev14.

### Fix 4: Drill post-answer tap behavior — result/feedback sub-pages

Introduced `gDrillShowFeedback` bool. After answer tap: result view first, then feedback on
second tap. **Note:** flow updated in v5.8-dev14 to show feedback immediately on answer tap.

---

## What Changed in v5.8-dev12

### Fix 1: UI chrome fonts fixed — Settings/Advanced/Power Lab stable regardless of Reader size

`renderSettings`, `renderAdvanced`, and `renderPowerLab` now pin `gSettings.fontSizeMode = FontSizeMode::Large`
at entry and restore it on exit.

### Fix 2: Settings segmented controls — thick-border selected state, no stars

`drawSegmentedButton(rect, label, selected)` helper:
- Selected: 3-rect thick border + fake-bold text
- Unselected: 2-rect normal border, regular text
- No filled background, no `*` marker
- Labels at `kSegmentedPx = 20`

### Fix 3: Drill/Exam Reader L visible (coachReaderSizeFor auto-downgrade removed)

Removed auto-downgrade from `coachReaderSizeFor()`. `optionTextPxFor()` caps at 36px.

---

## Screen Navigation Map

```
Badge ←→ Home
Home → Badge
Home → Practice → PracticeMenu → InterviewPractice
Home → Drills → DrillsMenu → Drills
Home → Exam → Exam
Home → Glossary → GlossaryMenu → Glossary
Home → Results
Home → Settings
Settings → Advanced
Advanced → Power Lab (pages 1-4)
Advanced → Font Lab
Advanced → Visual QA
Advanced → Help/Legend
Advanced → Home
Power Lab → Home
```

---

## Power Modes — Current Behavior

### Profile Thresholds

| Profile | WarmIdle | LightNap idle threshold | Nap duration | Listen window |
|---------|----------|------------------------|--------------|---------------|
| Responsive (enum Balanced) | 30s | Disabled | — | 10s |
| Balanced (enum Aggressive) | 15s | 10 min | 12s | 12s |
| Max Battery (enum BadgeMax) | 5s | 5 min | 15s | 15s |

### LightNap Policy
- Enters on any `isLightNapEligibleScreen()` — Badge, Home, Practice menus, Drills, Exam, Results, etc.
- **Blocked** on control/diagnostic screens (Settings, Advanced, Debug, PowerLab, etc.)
- **Blocked** while `isAnswerSelectionActive()` — active Exam question OR Drills option awaiting tap
- **Blocked** while input locked, touch active, or in wake listen window
- Responsive profile: always disabled
- Post-wake: 400ms input lock prevents accidental answer registration

### CPU Scaling (WarmIdle)
- Applies to all `isStaticIdleScreen()` screens (includes Settings, Advanced, Power Lab, etc.)
- Max Battery profile: Badge screen only for CPU scaling (separate from LightNap eligibility)

---

## Refresh Modes

| Mode | Cadence | When clean |
|------|---------|-----------|
| Fast | ~16 transitions between cleans | Badge/image entry or hard limit |
| Balanced | ~10 transitions between cleans | Badge/image entry or balanced limit |
| Clean | Every transition | Always |

---

## Reader Size

Controls font size in **content/study screens only**:
- Practice body text
- Glossary body text
- Drill question stems and option buttons
- Exam question stems and option buttons
- Drill/Exam feedback and explanation pages

**Does NOT affect** Settings, Advanced, Debug, Power Lab, or other control/diagnostic screens.
Those screens pin `fontSizeMode = Large` internally so their layout is stable.

Font sizes at each Reader size:

| Reader | Body | Question stem | Option text (1-2 lines) | Option text (3 lines) | Feedback |
|--------|------|--------------|-------------------------|----------------------|---------|
| S | 24px | 28px | 24px | 24px | 24px |
| M | 31px | 35px | 31px (Gothic_28 / Sans18pt) | 24px | 31px |
| L | 40px | 40px | 40px (Gothic_36 / **Sans24pt**) | 36px (Gothic_36) | 40px |

---

## Deep Sleep

**BLOCKED — will not be pursued further in this version.**

| Question | Answer |
|----------|--------|
| GT911 touch INT | GPIO48 |
| ESP32-S3 RTC GPIO range | 0–21 only |
| GPIO48 RTC-wake capable? | **NO** |
| Power button GPIO44 RTC-wake capable? | **NO** |
| Deep sleep status | **BLOCKED** |
| Light sleep wake | Timer only |

Sleep Off resets on reboot (experimental flag, not sticky).

---

## Known Limitations

1. **Touch INT not configured** — Timer wake only for LightNap. Short taps during sleep missed.
2. **Deep sleep blocked** — GPIO48 (GT911 INT) is not RTC-wake capable on ESP32-S3.
3. **Japanese** — Blocked until: UTF-8 sanitizer, CJK word wrap, Japanese font path, generic deck schema.

---

## Physical QA Checklist (v5.8-dev15)

### Settings screen (v5.8-dev15)
- [ ] Settings layout looks identical with Reader S, Reader M, and Reader L
- [ ] Section labels ("Reader size", "Refresh", "Power", "Orientation") are visibly **larger** than before (28px, not 24px)
- [ ] Button text ("Fast", "Responsive", "Normal", etc.) is visibly larger (24px, not 20px)
- [ ] "Responsive" and "Balanced" fit cleanly in their button without wrapping or clipping
- [ ] "Reader size" row: selected option shows thick border + bold label (no `*`)
- [ ] Battery %: left-aligned, bar right-aligned, both vertically co-level on same centerline
- [ ] mV and USB+charging lines visible below battery block at 28px
- [ ] Advanced button visible, navigates to Advanced screen

### Results screen (v5.8-dev15)
- [ ] With ≤3 categories: page 1 shows summary block + divider + category bars on same page
- [ ] With ≤3 categories: page count is 3 (summary+cats · weakest · recent)
- [ ] No important results data is missing from any page
- [ ] Navigation arrows work correctly on all pages

### Drill/Exam option height (v5.8-dev15)
- [ ] All four option boxes on a screen have the same height
- [ ] When one option needs 2 lines, all boxes grow to 2-line height (not just that one)
- [ ] Single-line options still use compact height when all four fit on one line
- [ ] Serial log shows `Drill combined shown: item=X options=4 sharedH=N totalPages=1`

### Practice feedback formatting (v5.8-dev15)
- [ ] Numbered list items in feedback (1. / 2. / 3.) start on their own line
- [ ] Semicolon-separated clauses (3+) each appear on their own line
- [ ] Prose and short phrases are NOT split

### Advanced screen
- [ ] Advanced looks the same with Reader S / M / L
- [ ] Info text and button grid do not overlap

### Power Lab
- [ ] Power Lab looks the same with Reader S / M / L
- [ ] Power Lab page 1: shows `LightNap (this screen): no — control screen`
- [ ] Power Lab page 4: reachable via Page button tap (cycles 1→2→3→4→1)
- [ ] Power Lab page 4: shows current firmware version in audit header

### Drill / Exam Reader size
- [ ] Drill Reader S → M: question text and option text both visibly larger
- [ ] Drill Reader M → L: question text visibly larger
- [ ] Drill Reader M → L: **option button text visibly larger** (Sans24pt vs Sans18pt for High Contrast; Gothic_36 vs Gothic_28 for default)
- [ ] Exam same as Drill for all three reader sizes
- [ ] No option text clipping outside button borders at any size
- [ ] For long labels: option button grows taller rather than shrinking text below M level
- [ ] Serial log shows `Drill fonts: screen=Drills reader=L qPx=40 opt0Px=40 opt0H=NNN feedbackPx=40`
- [ ] Serial log shows `optionText: L 40->36px label=... midLines=... fits=yes/allow3` for overflow labels

### Drill answer tap flow
- [ ] **Tap an answer option → feedback/explanation page shown immediately** (no second tap needed)
- [ ] On feedback page: tap top half → result view (question + options with selected/correct highlighted)
- [ ] On result view: tap bottom half → feedback page
- [ ] On result view: tap top half → previous drill item (same as ← arrow)
- [ ] On feedback last page: tap bottom half → next drill item (same as → arrow)
- [ ] Footer arrow buttons (← →) advance to prev/next item from both result and feedback views
- [ ] Home button returns to Home from both views
- [ ] Tapping content area before selecting any answer does NOT skip item or show feedback

### Drill / Exam — no regression
- [ ] Practice, Glossary, Badge screens unaffected
- [ ] Exam option tap still records answer; Exam result screen unchanged

### Power / sleep
- [ ] Changing power profile in Settings persists after reboot
- [ ] Changing power profile in Power Lab persists after reboot
- [ ] Badge screen: LightNap activates in Balanced or Max Battery after idle
- [ ] Settings screen: LightNap NOT eligible (control screen)
- [ ] Responsive profile: no LightNap even with Sleep mode on
- [ ] Exam active question: LightNap blocked (answer selection active)
- [ ] Post-wake: first 400ms ignores taps

### Navigation / regression
- [ ] Home screen shows 7 buttons (no Debug)
- [ ] Practice: bottom tap on last page → next item
- [ ] Practice: top tap on first page → previous item

---

## Next Steps

1. **Physical QA v5.8-dev15** — verify: option boxes all same height on each question screen; drill first answer tap goes directly to feedback; results combined page fits cleanly; Settings labels and buttons larger; no regressions
2. **Japanese readiness implementation** — UTF-8 sanitizer, CJK word wrap, Japanese font path, generic deck schema (see `docs/PRD_PaperBadge_v0.6.md`). **Not implemented yet.**
3. Long-term: GT911 touch INT wake research if alternative GPIO mapping found
