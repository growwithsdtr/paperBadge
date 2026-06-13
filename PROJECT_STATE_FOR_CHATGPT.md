# PaperBadge Project State — v5.8-dev12 Handoff

_Last updated: 2026-06-13_

---

## Git / Remote Status

| Item | Value |
|------|-------|
| Branch | `main` |
| Remote | `https://github.com/growwithsdtr/paperBadge.git` |

---

## Firmware

- **Version:** `v5.8-dev12` (`src/main.cpp:20`)
- **Build:** SUCCESS — RAM 49.5% · Flash 47.6%
- **Upload:** SUCCESS — `/dev/cu.usbmodem1101`
- **Smoke test:** PASS (7/7 boot log checks)

---

## What Changed in v5.8-dev12

### Fix 1: UI chrome fonts fixed — Settings/Advanced/Power Lab stable regardless of Reader size

`renderSettings`, `renderAdvanced`, and `renderPowerLab` now pin `gSettings.fontSizeMode = FontSizeMode::Large`
at entry and restore it on exit. This means `coachTypography()` inside `drawButton()` always uses:

- `buttonPx = 24` (consistent button text)
- `titlePx = 40` (consistent header)
- `metadataPx = 24` (consistent labels)

Previously, Reader L (XL) made `buttonPx = 31`, causing Settings/Advanced/Power Lab button text to
grow with Reader size, crowding the layout.

### Fix 2: Settings segmented controls — thick-border selected state, no stars

Replaced `drawButton(..., "S *")` pattern with a new `drawSegmentedButton(rect, label, selected)` helper:
- **Selected**: 3-rect thick rounded border + fake-bold text (draw string twice, offset 1px)
- **Unselected**: 2-rect normal border, regular text
- No filled background, no `*` marker

Labels now use `kSegmentedPx = 20` (Gothic 20px bitmap) so full labels fit in 150px segmented buttons:

| Row | Labels |
|-----|--------|
| Reader | S / M / L |
| Refresh | Fast / Balanced / Clean |
| Power | Responsive / Balanced / Max |
| Orientation | Normal / Strap |

### Fix 3: Settings battery block vertical alignment

Battery block redesigned for co-alignment of % text and bar:
- `battBlockH = 54` containing both % text (40px font) and bar (44px tall)
- Bar top computed as `battBlockY + (battBlockH - barH) / 2` — vertically centered within block
- % text at `battBlockY + 4` — visually centers with bar
- mV and USB details immediately below the block

### Fix 4: Drill/Exam Reader L now visibly different from Reader M

Root cause: `coachReaderSizeFor()` auto-downgraded Reader L (XL) → Reader M (Large) for choice-question
screens. Both sizes rendered at 31px body — indistinguishable.

Fix:
1. Removed auto-downgrade from `coachReaderSizeFor()` — it now returns the canonical reader size directly.
2. `applyCoachQuestionFont()`: removed the `> 36 ? 36 :` cap — Reader L now renders question stems at 40px.
3. `optionTextPxFor()`: caps `largePx` at 36 (safe maximum for option buttons) instead of uncapping.

Result:

| Reader | Question stem | Option text |
|--------|--------------|-------------|
| S | 28px | 24px |
| M | 35px | 31px |
| L | 40px | 36px (capped) |

Feedback/explanation pages use `applyCoachBodyFont()` → bodyPx (24/31/40px) — unchanged, visibly different.

---

## What Changed in v5.8-dev11

### Fix 1: Power Lab page 4 unreachable

Touch handler used `kPlPageCount = 3` while `renderPowerLab()` defines `kPowerLabPageCount = 4`.
Fixed: handler now uses `4`. Page button cycles through all four pages.

### Fix 2: Settings power profile persistence

Settings power profile buttons (Resp / Bal / Max) were missing `saveSettings()` calls.
Fixed: all three handlers now call `saveSettings()` matching Power Lab behavior.

### Fix 3: Power Lab LightNap label clarified

"LightNap eligible:" renamed to **"LightNap (this screen):"** — makes clear that when viewing
Power Lab (a control screen), the result of `no — control screen` reflects the current Power Lab
screen, not a prior content screen the user came from.

### Fix 4: Settings labels — always compact

Removed the `isMediumSize` adaptive label expansion introduced in dev10. Labels are now always
compact to prevent wrapping inside 150px segmented buttons:

| Row | Labels |
|-----|--------|
| Reader | S / M / L |
| Refresh | Fast / **Bal** / Clean |
| Power | **Resp** / **Bal** / **Max** |
| Orientation | Normal / Strap |

### Fix 5: Drill/Exam option text scales with Reader size

`optionTextPxFor()` was capping `largePx = min(bodyPx, 31)`, making Reader L (bodyPx=40)
look identical to Reader M (bodyPx=31) for option buttons.

Fixed: removed the `>= 31 ? 31` cap. Now uses `bodyPx` directly:

| Reader | bodyPx | optionTextPx (if fits 2 lines) |
|--------|--------|-------------------------------|
| S | 24 | 24 (= buttonPx; no change) |
| M | 31 | 31 |
| L | 40 | 40 (or falls back to 31 if text is too long) |

The existing 2-line overflow guard still applies: if a label doesn't fit in 2 lines at `largePx`,
the function falls back to `compactPx` (buttonPx). No button height overflow possible.

### Fix 6: Settings battery layout redesign

Old layout: single metadata-font line with thin 22px bar inline.

New layout for glanceability:

```
[  100%  ]                    [████████████████████]   ← thick bar (66×220px)
4156mV
USB: no  discharging
```

- Battery % uses `applyCoachTitleFont()` (31–40px depending on reader size), left at x=36
- Thick bar: h=66px, w=220px, right-aligned at x=width-36-220, same y as % text
- mV line below (metadata font), then USB+charging status below that
- All settings rows shifted down ~68px to accommodate taller battery section:
  - Reader size: label y=210, buttons y=236
  - Refresh: label y=300, buttons y=326
  - Power: label y=390, buttons y=416
  - Orientation: label y=480, buttons y=506
  - Advanced: y=570

### Fix 7: Power Lab page 4 audit version string

Changed hardcoded `"v5.8-dev9"` to `kFirmwareVersion` so it stays current automatically.

### Docs

- Created `docs/PRD_PaperBadge_v0.6.md` — product behavior reference and Japanese deck roadmap

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

| Reader | Body | Question stem | Option text | Feedback |
|--------|------|--------------|-------------|---------|
| S | 24px | 28px | 24px | 24px |
| M | 31px | 35px | 31px | 31px |
| L | 40px | 40px | 36px (capped) | 40px |

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

## Physical QA Checklist (v5.8-dev12)

### Settings screen
- [ ] Settings layout looks identical with Reader S, Reader M, and Reader L
- [ ] "Reader size" row: selected option shows thick border + bold label (no `*`)
- [ ] "Refresh" row: shows Fast / Balanced / Clean — no wrapping
- [ ] "Power" row: shows Responsive / Balanced / Max — no wrapping
- [ ] "Orientation" row: shows Normal / Strap — no wrapping
- [ ] Battery %: left-aligned, bar right-aligned, both vertically co-level
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
- [ ] Drill Reader M → L: option button text visibly larger
- [ ] Drill Reader S → M → L: each step visibly different
- [ ] Exam Reader M → L: question text visibly larger
- [ ] Exam Reader M → L: option button text visibly larger
- [ ] No option text clipping outside button borders

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
2. **Physical QA this checklist** — confirm Reader L is now visibly larger in Drill/Exam
3. Long-term: GT911 touch INT wake research if alternative GPIO mapping found
