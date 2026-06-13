# PaperBadge Project State — v5.8-dev13 Handoff

_Last updated: 2026-06-13_

---

## Git / Remote Status

| Item | Value |
|------|-------|
| Branch | `main` |
| Remote | `https://github.com/growwithsdtr/paperBadge.git` |

---

## Firmware

- **Version:** `v5.8-dev13` (`src/main.cpp:20`)
- **Build:** SUCCESS — RAM 49.5% · Flash 47.7%
- **Upload:** SUCCESS — `/dev/cu.usbmodem1101`
- **Smoke test:** PASS (7/7 boot log checks)

---

## What Changed in v5.8-dev13

### Fix 1: Settings section labels now fixed medium size

All four section labels ("Reader size", "Refresh", "Power", "Orientation") now call
`applyCoachMetadataFont()` immediately before `drawString`, ensuring a consistent fixed
24px font regardless of what the preceding segmented-button draw left behind.

Previously, only "Reader size" was reliably at 24px (it followed `applyCoachMetadataFont()` from
the battery block). "Refresh", "Power", and "Orientation" were silently drawn at 20px (the
`kSegmentedPx` font leftover from the preceding button row).

Settings layout is unchanged — same Y positions, same buttons.

### Fix 2: Battery % text vertically co-aligned with battery bar

Battery percentage text now uses `middle_left` datum and draws at the bar's vertical centerline
(`battBlockY + battBlockH / 2 = 99`). Previously it drew with `top_left` at `battBlockY + 4 = 76`,
leaving the text visually higher than the bar center.

### Fix 3: Drill/Exam Reader L option buttons now visibly larger than Reader M

Root cause: when a label was too long to fit in 2 lines at 36px (XL/L's largePx cap),
`optionTextPxFor` fell back to `compactPx = 31` (buttonPx for XL mode). `applyGothicFont(31)`
picks `lgfxJapanGothic_28` — the same font as M's maximum of 31px. So long labels in L silently
looked identical to M.

Fix: for XL/L mode (`bodyPx >= 40`), use 32px fallback instead of 31. `applyGothicFont(32)` picks
`lgfxJapanGothic_32`, which is visibly larger than `lgfxJapanGothic_28`.

Updated option font table:

| Reader | largePx | Fallback | Font (fits) | Font (fallback) |
|--------|---------|----------|-------------|-----------------|
| S | 24 | 24 | lgfxJapanGothic_24 | lgfxJapanGothic_24 |
| M | 31 | 24 | lgfxJapanGothic_28 | lgfxJapanGothic_24 |
| L | 36 | **32** | lgfxJapanGothic_36 | **lgfxJapanGothic_32** |

Added `Serial.printf("Drill fonts: reader=%s qPx=%u opt0Px=%u bodyPx=%u\n", ...)` before
each drill render for serial verification.

### Fix 4: Drill post-answer tap behavior — result/feedback sub-pages

Introduced `gDrillShowFeedback` (bool, default false) to split the post-answer state into
two internal pages within a single drill item:

1. **Result view** (`gDrillShowFeedback = false`): shows question + all option buttons with
   selected/correct options highlighted (3-rect thick border + bold text). No item skip.
2. **Feedback view** (`gDrillShowFeedback = true`): shows existing `drawFeedbackPage` content
   (explanation, best answer, etc.). Paginated by `gCoachStage`.

Touch behavior after answer selected:
- Content bottom half on result view → switch to feedback (`gDrillShowFeedback = true`)
- Content top half on feedback first page → switch back to result (`gDrillShowFeedback = false`)
- Content top half on feedback page 2+ → previous feedback page (`--gCoachStage`)
- Content bottom half on feedback → next feedback page (or no-op at last page)
- Footer arrows (← →): still move to prev/next drill item (unchanged)
- Home button: unchanged

`gDrillShowFeedback` is reset to `false` whenever `gSelectedOption` is reset:
`clearCoachData`, `startCoachMode`, `startPracticeMode`, `nextCoachItem`, `previousCoachItem`,
and the answer-selection touch handler.

`currentCoachReaderPageCount()` returns 1 for the result view and `feedbackPageCountFor()`
for the feedback view.

Exam answer recording is not changed.

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

| Reader | Body | Question stem | Option text (fits) | Option text (fallback) | Feedback |
|--------|------|--------------|---------------------|------------------------|---------|
| S | 24px | 28px | 24px | 24px | 24px |
| M | 31px | 35px | 31px (Gothic_28) | 24px (Gothic_24) | 31px |
| L | 40px | 40px | 36px (Gothic_36) | **32px (Gothic_32)** | 40px |

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

## Physical QA Checklist (v5.8-dev13)

### Settings screen
- [ ] Settings layout looks identical with Reader S, Reader M, and Reader L
- [ ] All four section labels ("Reader size", "Refresh", "Power", "Orientation") appear at the same size and weight
- [ ] "Reader size" row: selected option shows thick border + bold label (no `*`)
- [ ] "Refresh" row: shows Fast / Balanced / Clean — no wrapping
- [ ] "Power" row: shows Responsive / Balanced / Max — no wrapping
- [ ] "Orientation" row: shows Normal / Strap — no wrapping
- [ ] Battery %: left-aligned, bar right-aligned, both vertically co-level on same centerline
- [ ] mV and USB+charging lines visible below battery block
- [ ] Advanced button visible, navigates to Advanced screen

### Advanced screen
- [ ] Advanced looks the same with Reader S / M / L
- [ ] Info text and button grid do not overlap

### Power Lab
- [ ] Power Lab looks the same with Reader S / M / L
- [ ] Power Lab page 1: shows `LightNap (this screen): no — control screen`
- [ ] Power Lab page 4: reachable via Page button tap (cycles 1→2→3→4→1)
- [ ] Power Lab page 4: shows current firmware version in audit header

### Drill / Exam Reader size
- [ ] Drill Reader M → L: question text visibly larger
- [ ] Drill Reader M → L: option button text visibly larger (Gothic_32 vs Gothic_28 in fallback case)
- [ ] Drill Reader S → M → L: each step visibly different
- [ ] Exam Reader M → L: question text visibly larger
- [ ] Exam Reader M → L: option button text visibly larger
- [ ] No option text clipping outside button borders
- [ ] Serial log shows `Drill fonts: reader=L qPx=40 opt0Px=36` (or 32 for long options)

### Drill post-answer navigation
- [ ] After selecting answer: result view shown (question + options with thick border on selected/correct)
- [ ] Tapping bottom half of result view → feedback/explanation page appears
- [ ] Tapping top half of feedback page → result view returns
- [ ] No accidental skip to next/previous item when tapping content area post-answer
- [ ] Footer arrow buttons (← →) still advance to next/previous drill item
- [ ] Home button returns to Home from both result and feedback views

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

1. **Japanese readiness implementation** — UTF-8 sanitizer, CJK word wrap, Japanese font path, generic deck schema (see `docs/PRD_PaperBadge_v0.6.md`)
2. **Physical QA this checklist** — confirm Reader L visibly larger, settings labels consistent, drill post-answer nav correct
3. Long-term: GT911 touch INT wake research if alternative GPIO mapping found
