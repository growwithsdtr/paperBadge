# PaperBadge Project State — v5.8-dev11 Handoff

_Last updated: 2026-06-12_

---

## Git / Remote Status

| Item | Value |
|------|-------|
| Branch | `main` |
| Remote | `https://github.com/growwithsdtr/paperBadge.git` |

---

## Firmware

- **Version:** `v5.8-dev11` (`src/main.cpp:20`)
- **Build:** SUCCESS — RAM 49.5% · Flash 47.6%
- **Upload:** SUCCESS — `/dev/cu.usbmodem1101`
- **Smoke test:** PASS (7/7 boot log checks)

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

Controls font size in:
- Practice body text
- Glossary body text
- Drill question stems and option buttons
- Exam question stems and option buttons
- Drill/Exam feedback and explanation pages

Option buttons scale correctly at S/M/L: S→24px, M→31px, L→40px (falls back to next size down if
label doesn't fit in 2 lines).

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

## Physical QA Checklist (v5.8-dev11)

- [ ] Home screen shows 7 buttons (no Debug)
- [ ] Settings: large battery % visible, thick bar to the right
- [ ] Settings: mV line and USB+charging line below battery
- [ ] Settings: Refresh shows Fast / Bal / Clean (no wrapping)
- [ ] Settings: Power shows Resp / Bal / Max (no wrapping)
- [ ] Settings "Advanced" button navigates to Advanced screen
- [ ] Advanced: info text and button grid do not overlap
- [ ] Power Lab page 1: shows `LightNap (this screen): no — control screen`
- [ ] Power Lab page 4: reachable via Page button tap (cycles 1→2→3→4→1)
- [ ] Power Lab page 4: shows current firmware version in audit header
- [ ] Changing power profile in Settings persists after reboot
- [ ] Changing power profile in Power Lab persists after reboot
- [ ] Drills Reader L: option buttons visibly larger than Reader M
- [ ] Drills Reader S: option buttons visibly smaller than Reader M
- [ ] Exam Reader L: option buttons visibly larger than Reader M
- [ ] Practice: bottom tap on last page → next item
- [ ] Practice: top tap on first page → previous item
- [ ] Badge screen: LightNap activates in Balanced or Max Battery after idle
- [ ] Settings screen: LightNap NOT eligible (control screen)
- [ ] Responsive profile: no LightNap even with Sleep mode on
- [ ] Exam active question: LightNap blocked (answer selection active)
- [ ] Post-wake: first 400ms ignores taps

---

## Next Steps

1. **Japanese readiness implementation** — UTF-8 sanitizer, CJK word wrap, Japanese font path, generic deck schema (see `docs/PRD_PaperBadge_v0.6.md`)
2. **Physical QA this checklist** — especially Reader L option sizing and Power Lab page 4
3. Long-term: GT911 touch INT wake research if alternative GPIO mapping found
