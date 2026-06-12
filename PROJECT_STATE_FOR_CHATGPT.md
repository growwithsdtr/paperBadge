# PaperBadge Project State — v5.8-dev9 Handoff

_Last updated: 2026-06-12_

---

## Git / Remote Status

| Item | Value |
|------|-------|
| Branch | `main` |
| Remote | `https://github.com/growwithsdtr/paperBadge.git` |

---

## Firmware

- **Version:** `v5.8-dev9` (`src/main.cpp:20`)
- **Build:** SUCCESS — RAM 49.5% · Flash 47.6%
- **Upload:** SUCCESS — `/dev/cu.usbmodem1101`
- **Smoke test:** PASS (7/7 boot log checks)

---

## What Changed in v5.8-dev9

### Settings Label Consistency (Phase 1)

Refresh segmented control now uses compact labels matching Power:

| Row | Labels |
|-----|--------|
| Reader | S / M / L |
| Refresh | Fast / **Bal** / Clean |
| Power | Resp / Bal / Max |
| Orientation | Normal / Strap |

"Balanced" was wrapping inside the 3-way segmented button. Changed to "Bal" to match the compact style of "Resp" and "Max".

---

### Advanced Screen Layout Fix (Phase 2)

**Before:** Button labels "Reset typography" and "Dump render trace" overflowed their buttons. Button grid start position could overlap diagnostic text.

**After:**
- Shortened labels: **Reset Type**, **Trace Dump**, **Layout Log**, **Touch Dbg On/Off**
- Reduced info text to 8 concise lines (removed redundant "profile thresholds" line — Power Lab has that)
- Button grid now starts at `max(y + 20, display.height() - 490)` — dynamically clears the last info line
- Sleep hint shortened: "Sleep: tap after wake  long press = disable"

---

### Power Lab Page 4 (Phase 3)

Power Lab split from 3 pages to **4 pages**:

| Page | Content |
|------|---------|
| 1 | CPU stage / idle counters / LightNap eligibility status |
| 2 | Battery / peripherals |
| 3 | Sleep diagnostics (nap, wake, listen window, touch timing, input lock) + Sleep mode button |
| 4 | Wake source / deep sleep audit (GPIO48, RTC range, blocked status, eligible screens) |

**Page 3 fix:** The sleep mode button was previously placed at `maxY - 10`, causing it to overlap with the last text row on page 3 when content was dense. It is now placed at `maxY + 10`, below the text zone and above the footer.

**Page 4** contains all wake source audit findings previously crowded at the bottom of page 3.

**Page 1** now shows: `LightNap eligible: yes/no (reason)` — real-time eligibility status for the current screen.

---

### Refresh Mode Cadence (Phase 4)

Balanced refresh was cleaning on nearly every `highQuality` screen transition, making navigation feel slow and distracting.

**New behavior:**

| Mode | When clean refresh happens |
|------|---------------------------|
| Fast | Every ~16 non-clean transitions, or Badge/image/zoom entry |
| Balanced | Every ~10 non-clean transitions, or Badge/image/zoom entry |
| Clean | Always |

**Key change:** `highQuality` parameter no longer triggers a clean refresh in Fast or Balanced mode. Only cadence counter and image/zoom transitions do. This means rapid navigation stays fast.

Constants:
- `kHardCleanTransitionLimit = 16` (was 14; used for Fast mode)
- `kBalancedCleanTransitionLimit = 10` (new; used for Balanced mode)

---

### LightNap Eligibility Policy (Phase 5)

**Before:** LightNap only activated on Badge screen.

**After:** LightNap activates on any normal content/display screen when idle.

**Eligible screens (isLightNapEligibleScreen):**
- Badge, Home
- PracticeMenu, GlossaryMenu, DrillsMenu
- InterviewPractice (Practice), Glossary
- Drills, Exam, Results

**NOT eligible (control/diagnostic screens):**
- Settings, Advanced, Debug
- PowerLab, PowerAudit, FontLab, VisualQa, HelpLegend
- QrZoom, PhotoZoom, TouchDebug

**Guards still apply** (LightNap never enters when):
- Sleep mode = Off
- Profile = Responsive (profileAllowsLightSleep() = false)
- Input is locked
- Touch is active
- Inside post-wake listen window

Power Lab page 1 shows live LightNap eligibility status and reason.

---

### Reader Size — Confirmed Working for Drills/Exam (Phase 6)

Reader size (S/M/L) already applied correctly to Drills and Exam in v5.8-dev8:
- `renderCoachScreen()` calls `coachReaderSizeFor(item)` for Drills
- `renderExamQuestion()` calls `coachReaderSizeFor(item)` for Exam
- Both set `gSettings.fontSizeMode = renderSize` before building layouts
- Option button font (`optionTextPxFor`) also scales with reader size (capped at 31px to prevent overflow)
- Feedback/explanation pages use `applyCoachBodyFont()` which reads from `coachTypography()` → reader size

No code change needed. PROJECT_STATE_FOR_CHATGPT.md previously stated this was incomplete — that was incorrect.

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
- Never enters on control/diagnostic screens
- Never enters while input locked, touch active, or in wake listen window
- Responsive profile: always disabled

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

Option buttons scale within limits: capped at 31px to prevent overflow on XL size.

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

## Physical QA Checklist (v5.8-dev9)

- [ ] Home screen shows 7 buttons (no Debug)
- [ ] Settings: Refresh shows Fast / Bal / Clean (no "Balanced" wrapping)
- [ ] Settings: Power shows Resp / Bal / Max
- [ ] Settings "Advanced" button navigates to Advanced screen
- [ ] Advanced: info text and button grid do not overlap
- [ ] Advanced: "Reset Type", "Trace Dump", "Layout Log" labels fit buttons
- [ ] Advanced: Sleep mode button visible only on Power Lab page 3
- [ ] Power Lab page 3: body text does not overlap Sleep mode button
- [ ] Power Lab page 4: shows GPIO48, RTC range, deep sleep blocked
- [ ] Power Lab page 1: shows LightNap eligible: yes/no
- [ ] Changing refresh in Settings shows Bal for Balanced mode
- [ ] Practice: bottom tap on last page → next item
- [ ] Practice: top tap on first page → previous item
- [ ] Glossary: same tap behavior as Practice
- [ ] Drills: reader size (S/M/L) affects question and option text
- [ ] Exam: reader size affects question and option text
- [ ] Badge screen: LightNap activates in Balanced or Max Battery after idle
- [ ] Home screen: LightNap eligible (check Power Lab page 1)
- [ ] Settings screen: LightNap NOT eligible (control screen)
- [ ] Responsive profile: no LightNap even with Sleep mode on
- [ ] Balanced refresh: does not clean every 2 taps during rapid navigation
- [ ] Fast refresh: stays fast during rapid navigation, cleans every ~16 transitions

---

## Next Steps

1. **Japanese readiness implementation** — UTF-8 sanitizer, CJK word wrap, Japanese font path, generic deck schema
2. **Physical QA this checklist** — especially Power Lab page 3 button overlap fix and LightNap on non-Badge screens
3. Long-term: GT911 touch INT wake research if alternative GPIO mapping found
