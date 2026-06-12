# PaperBadge Project State — v5.8-dev8 Handoff

_Last updated: 2026-06-12_

---

## Git / Remote Status

| Item | Value |
|------|-------|
| Branch | `main` |
| Remote | `https://github.com/growwithsdtr/paperBadge.git` |

---

## Firmware

- **Version:** `v5.8-dev8` (`src/main.cpp:20`)
- **Build:** SUCCESS — RAM 49.5% · Flash 47.6%
- **Upload:** SUCCESS — `/dev/tty.usbmodem1101`

---

## What Changed in v5.8-dev8

### Deep Sleep Feasibility Audit (Phase 1 — Research Only)

Searched M5GFX, M5Unified, board definitions, and ESP-IDF for M5PaperS3 deep sleep wake sources.

**Findings:**

| Question | Answer |
|----------|--------|
| Can ESP32-S3 deep sleep be entered from this firmware? | Yes — `esp_deep_sleep_start()` works, but no safe wake source exists |
| GT911 INT pin for M5PaperS3? | **GPIO_NUM_48** (M5GFX.cpp:2023, confirmed in board detection block) |
| Is GPIO48 RTC-wake capable on ESP32-S3? | **NO** — ESP32-S3 RTC GPIO range is 0–21 only. GPIO48 is digital-only |
| Is there a physical button wake source? | Power hold = GPIO_NUM_44 (also NOT RTC GPIO) |
| Is USB-only wake acceptable as standby? | No |
| Does GT911 stay powered during deep sleep? | Unknown — likely unpowered |
| Light sleep wake verified? | Timer wake: YES. Touch INT wake: NO |
| What app state needs persist/restore after deep sleep? | Settings, screen, session ID, coach index, results — significant |

**Conclusion: Deep sleep remains BLOCKED.** No RTC-capable wake source found. Light sleep (timer only) is the only safe option.

The Power Lab page 3 and Power Audit now display these specific findings.

---

### Power Profile Rename (Phase 2)

User-facing names now match intent:

| Enum value | Old display name | New display name |
|------------|-----------------|-----------------|
| `PowerProfile::Balanced` | "Balanced" | **"Responsive"** |
| `PowerProfile::Aggressive` | "Low Power" | **"Balanced"** |
| `PowerProfile::BadgeMax` | "Max Battery" | **"Max Battery"** (unchanged) |

**Updated thresholds:**

| Profile | WarmIdle threshold | Light sleep idle | Nap duration | Light sleep |
|---------|-------------------|-----------------|--------------|-------------|
| Responsive (Balanced enum) | 30s | N/A | N/A | **Disabled** |
| Balanced (Aggressive enum) | 15s | 10 min | 12s | Allowed |
| Max Battery (BadgeMax enum) | 5s | 5 min | 15s | Allowed |

**Responsive profile disables light sleep entirely** (`profileAllowsLightSleep()` = false) — prioritizes tap responsiveness.

Listen windows updated: Balanced=12s, Max Battery=15s.

---

### Home / Settings / Advanced Structure (Phase 3)

**New Home screen** (Debug removed):
- Badge
- Practice
- Drills
- Exam
- Glossary
- Results
- Settings

**New Settings screen** (compact segmented rows, no Font style / Badge language / Sleep):
- Reader size: segmented row **S / M / L** (stars active choice)
- Refresh: segmented row **Fast / Balanced / Clean**
- Power: segmented row **Resp / Bal / Max** (short labels to fit 3 columns)
- Badge orientation: 2-way **Normal / Strap**
- **Advanced** button (full-width)
- Home icon button (bottom)

**New Advanced screen** (`Screen::Advanced`, accessible from Settings → Advanced):
- Replaces Debug on Home — all diagnostic content now lives here
- Shows: firmware, SD, power profile/thresholds, battery, font, refresh, touch debug, trace, deck export status
- Action buttons: Help, Power Lab, Visual QA, Font Lab, Reset typography, Layout log, Dump render trace, Touch dbg, Export deck, **Sleep mode** toggle
- Home icon button at bottom
- Navigate: Settings → Advanced → Power Lab / Font Lab / etc.

Old Debug screen (`Screen::Debug`) still exists as a reachable screen for backward compatibility (e.g., from Power Lab Home button) but Debug is no longer on Home.

---

### LightNap Eligibility (Phase 5)

**Light sleep (LightNap) now only activates on the Badge screen.**

Previously it could activate on: Practice, Glossary, Settings, Debug, Power Lab, Results.

New rule in `maybeEnterBadgeSleep()`:
```cpp
if (gScreen != Screen::Badge) return;           // LightNap: Badge only
if (!profileAllowsLightSleep()) return;          // Responsive profile: disabled
```

WarmIdle CPU scaling still applies to all `isStaticIdleScreen()` screens as before.

---

### Power Lab Fixes (Phase 6)

- Footer profile button now uses short label: **Resp / Bal / Max** (avoids "Max Battery" overflow)
- Page 3 wake source notes updated with specific audit findings:
  - GT911 INT = GPIO48, not RTC-wake capable
  - ESP32-S3 RTC GPIO range documented
  - Profile note: if Responsive, shows "light sleep disabled"
- Power Audit page 2 updated similarly

---

### Practice / Glossary Tap Navigation (Phase 7)

**Practice reader** (`handleInterviewPracticeTouch`):
- Bottom half: if not last page → next page; **if on last page → next item**
- Top half: if not first page → previous page; **if on first page → previous item**

**Glossary reader** (generic coach screen handler):
- Same behavior as Practice
- `allowItemNav = (gScreen == Screen::Glossary)` — Drills/Exam are excluded to prevent accidental option selection

Does NOT apply to Drill/Exam answer selection screens.

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
Advanced → Power Lab (pages 1-3)
Advanced → Font Lab
Advanced → Visual QA
Advanced → Help/Legend
Advanced → Home
Power Lab → Home
```

---

## Power Modes — Current Behavior

### Profile Thresholds

| Profile | WarmIdle | LightNap eligible | LightNap threshold | Nap duration | Listen window |
|---------|----------|------------------|--------------------|--------------|---------------|
| Responsive | 30s | Badge only, but **BLOCKED** by profileAllowsLightSleep()=false | — | — | — |
| Balanced | 15s | Badge only | 10 min | 12s | 12s |
| Max Battery | 5s | Badge only (only Badge for WarmIdle too) | 5 min | 15s | 15s |

### Light Sleep Behavior
- Enters only on Badge screen
- Timer wake only (touch INT not configured)
- Short taps during sleep are missed
- After each wake: listen window where any touch prevents re-sleep

---

## Refresh Modes

| Mode | Behavior |
|------|----------|
| Fast | Fastest updates, most ghosting |
| Balanced | Default — clean on screen entry, fast on re-renders |
| Clean | Always full clean refresh, slowest |

---

## Reader Size

Controls font size in:
- Practice body text
- Glossary body text

Does NOT currently apply to Drill/Exam question stems (Phases 8+ work remaining).

---

## Known Limitations

1. **Touch INT not configured** — Timer wake only for LightNap. Short taps during sleep missed.
2. **Deep sleep blocked** — GPIO48 (GT911 INT) is not RTC-wake capable on ESP32-S3.
3. **Reader size inconsistency** — Drill/Exam question stems and explanations don't yet follow Reader size (Phase 8).
4. **Japanese** — Blocked until: UTF-8 sanitizer, CJK word wrap, Japanese font path, generic deck schema.

---

## Physical QA Checklist (v5.8-dev8)

- [ ] Home screen shows 7 buttons (no Debug)
- [ ] Settings shows segmented rows for Reader / Refresh / Power
- [ ] Settings "Advanced" button navigates to Advanced screen
- [ ] Advanced screen shows diagnostic info + all tool buttons
- [ ] Advanced → Power Lab navigates correctly
- [ ] Power profile labels in Settings show Resp/Bal/Max
- [ ] Changing power profile in Settings cycles correctly
- [ ] Changing reader size in Settings cycles correctly
- [ ] Changing refresh in Settings cycles correctly
- [ ] Badge orientation toggle works
- [ ] Practice: bottom tap on last page → advances to next item
- [ ] Practice: top tap on first page → goes to previous item
- [ ] Glossary: same tap behavior as Practice
- [ ] Badge screen: LightNap only activates in Balanced or Max Battery profile
- [ ] Responsive profile: no LightNap even with Sleep mode on
- [ ] Power Lab page 3: footer shows Resp/Bal/Max (no overflow)
- [ ] Power Lab page 3 body text: shows audit findings (GPIO48, RTC range)
- [ ] WarmIdle thresholds: Responsive@30s, Balanced@15s, Max@5s

---

## Next Steps (Not Implemented)

- Phase 8: Reader size consistency for Drill/Exam question/explanation rendering
- Phase 9: Document Refresh mode (done in Settings layout, needs QA)
- Phase 10: Japanese readiness docs only
- Long-term: GT911 touch INT wake research if alternative GPIO mapping found
