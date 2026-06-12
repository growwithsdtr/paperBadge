# PaperBadge Project State — v5.8-dev10 Handoff

_Last updated: 2026-06-12_

---

## Git / Remote Status

| Item | Value |
|------|-------|
| Branch | `main` |
| Remote | `https://github.com/growwithsdtr/paperBadge.git` |

---

## Firmware

- **Version:** `v5.8-dev10` (`src/main.cpp:20`)
- **Build:** SUCCESS — RAM 49.5% · Flash 47.6%
- **Upload:** SUCCESS — `/dev/cu.usbmodem1101`
- **Smoke test:** PASS (7/7 boot log checks)

---

## What Changed in v5.8-dev10

### Settings UI Refinements (from dev9 local diff, kept)

Battery and USB power info on the Settings screen now renders as a proper compact row:

- **Battery row:** `"X% (YYYYmV)"` text left-aligned + bar right-aligned on the same line (y=80)
- **USB row:** `"USB: yes/no (YYYYmV)"` below battery (y=110)
- All button rows shifted down by 26px to clear the extra info area
- Reader size label: y=142 (was 116), buttons: y=168 (was 142)
- Refresh label: y=232 (was 206), buttons: y=258 (was 232)
- Power label: y=322 (was 296), buttons: y=348 (was 322)
- Orientation label: y=412 (was 386), buttons: y=438 (was 412)
- Advanced button: y=502 (was 476)

Power button labels now adapt to font size — `isMediumSize` flag enables longer labels at Reader M:

| Setting | S/L label | M label |
|---------|-----------|---------|
| Refresh Balanced | `Bal` | `Balanced` |
| Power Responsive | `Resp` | `Responsive` |
| Power Balanced | `Bal` | `Balanced` |
| Power Max Battery | `Max` | `Max Batt` |

---

### LightNap Safety Patch

**Problem:** LightNap uses a 2s timer-only sleep cycle. On wake, a tap that roused the device
from sleep could accidentally register as an answer selection in Exam or Drills.

**Three-part fix:**

#### 1. Answer-selection guard (`isAnswerSelectionActive()`)

New function blocks LightNap entry whenever the user's next tap could record an answer:

- **Exam:** `gScreen == Screen::Exam && gExamActive && !gExamSummary` — question is active
- **Drills:** `gScreen == Screen::Drills && gSelectedOption < 0 && isOptionDrillScreen(...)` — option drill awaiting first tap

Guard applied in both `lightNapBlockedReason()` (Power Lab display) and `maybeEnterBadgeSleep()` (nap entry path).

Read-only screens (Home, Badge, Practice, Glossary, Results, DrillsMenu) remain LightNap-eligible.

#### 2. Post-wake input debounce (400ms)

After `esp_light_sleep_start()` returns, the existing input-lock mechanism is engaged for 400ms:

```cpp
gInputLocked = true;
gInputLockedAtMs = wakeNow;
gInputUnlockAtMs = wakeNow + 400;
```

`handleTouch()` already checks `gInputLocked` and ignores all touch events during this window.
The existing 8s watchdog still clears stale locks.

#### 3. `sleepAuditStatusLine()` wording fix

Old: `"enabled: light experiment after Badge idle"` (incorrectly implied Badge-only)

New: `"enabled: light sleep on idle eligible screens"` — accurately reflects expanded eligible screen set.

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

## Physical QA Checklist (v5.8-dev10)

- [ ] Home screen shows 7 buttons (no Debug)
- [ ] Settings: battery % and mV visible on Settings screen
- [ ] Settings: USB power line visible below battery
- [ ] Settings: Refresh shows Fast / Bal(anced) / Clean
- [ ] Settings: Power shows Resp(onsive) / Bal(anced) / Max (Batt)
- [ ] Settings: Reader M shows full labels; Reader S/L shows compact
- [ ] Settings "Advanced" button navigates to Advanced screen
- [ ] Advanced: info text and button grid do not overlap
- [ ] Advanced: "Reset Type", "Trace Dump", "Layout Log" labels fit buttons
- [ ] Advanced: Sleep mode button visible only on Power Lab page 3
- [ ] Power Lab page 3: body text does not overlap Sleep mode button
- [ ] Power Lab page 4: shows GPIO48, RTC range, deep sleep blocked
- [ ] Power Lab page 1: shows `LightNap eligible: yes/no`
- [ ] Power Lab page 1: shows `answer selection active` when in active Exam question
- [ ] Practice: bottom tap on last page → next item
- [ ] Practice: top tap on first page → previous item
- [ ] Glossary: same tap behavior as Practice
- [ ] Drills: reader size (S/M/L) affects question and option text
- [ ] Exam: reader size affects question and option text
- [ ] Badge screen: LightNap activates in Balanced or Max Battery after idle
- [ ] Home screen: LightNap eligible (check Power Lab page 1)
- [ ] Settings screen: LightNap NOT eligible (control screen)
- [ ] Responsive profile: no LightNap even with Sleep mode on
- [ ] Exam active question: LightNap blocked (`answer selection active`)
- [ ] Drill option awaiting tap: LightNap blocked (`answer selection active`)
- [ ] Post-wake: first 400ms ignores taps (no accidental answer recorded)
- [ ] Balanced refresh: does not clean every 2 taps during rapid navigation
- [ ] Fast refresh: stays fast during rapid navigation, cleans every ~16 transitions

---

## Next Steps

1. **Japanese readiness implementation** — UTF-8 sanitizer, CJK word wrap, Japanese font path, generic deck schema
2. **Physical QA this checklist** — especially LightNap answer-selection guard and post-wake debounce
3. Long-term: GT911 touch INT wake research if alternative GPIO mapping found
