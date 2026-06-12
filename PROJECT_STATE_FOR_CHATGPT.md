# PaperBadge Project State — v5.8-dev6-settings Handoff

_Last updated: 2026-06-12_

---

## Git / Remote Status

| Item | Value |
|------|-------|
| Branch | `main` |
| Remote | `https://github.com/growwithsdtr/paperBadge.git` |

---

## Firmware

- **Version:** `v5.8-dev6-settings` (`src/main.cpp:20`)
- **Build:** SUCCESS — RAM 49.5% · Flash 47.5%
- **Upload:** SUCCESS — `/dev/tty.usbmodem1101`

---

## What Changed in v5.8-dev6-settings

### 1. Power Profile — User-Facing Labels

Internal enum names are **unchanged** (Balanced / Aggressive / BadgeMax).
User-facing labels shown in Settings, Power Lab, and Debug are now:

| Internal Enum | Old Label | New Label |
|---------------|-----------|-----------|
| `Balanced` | Balanced | **Balanced** |
| `Aggressive` | Aggressive | **Low Power** |
| `BadgeMax` | Badge Max | **Max Battery** |

Function changed: `powerProfileName()` (`src/main.cpp`).

### 2. Profile-Based LightNap Duration

Each light sleep nap cycle now uses a duration based on the active power profile.
Previously hardcoded at 2s for all profiles.

| Profile | Nap Cycle Duration |
|---------|-------------------|
| Balanced | **10s** |
| Low Power | **30s** |
| Max Battery | **60s** |

Function added: `profileLightSleepDurationUs()` (forward-declared with other profile functions).
`gLastSleepAttempt` now logs the actual duration (e.g. `"light nap 30s timer"`).

**Wake UX tradeoff:** Longer naps mean a short tap is more likely to be missed.
Guidance on Power Lab page 3: "Light sleep uses timer wake. Hold screen 2–3s to wake."
For Max Battery (60s naps), a longer hold may be needed if the touch lands mid-nap.

### 3. Settings Screen — Redesigned Layout

Settings now consolidates all everyday power controls. Old layout had text/button overlap
near the bottom; new layout has consistent spacing throughout.

**What's in Settings now:**
- Badge orientation (toggle: Strap 180 / Handheld 0)
- Badge language mode (cycle) + auto interval (cycle)
- **Reader size: compact 3-button row** — S / M / L (each 1/3 width, same row)
- Font style (cycle)
- Refresh mode (cycle: Fast / Balanced / Clean)
- **Power profile (cycle: Balanced / Low Power / Max Battery)** — NEW in Settings
- **Sleep mode (cycle: Off / Light / Deep test)** — NEW in Settings
- Dev hint: "(dev) Light sleep: hold 2–3s to wake" (shown only when sleep ≠ Off)
- Home button — full-width at bottom

**Removed from Settings:**
- Battery Saver toggle (redundant with Power Profile for typical users; still functional,
  accessible via Power Lab / Debug)

**Removed from Settings / Power Mode button (`gPowerModeButton`):** zeroed out in Settings layout.

### 4. Debug Screen — Fixed Text/Button Overlap

Previous layout had text rows at y≈620 with buttons starting at y≈598 — overlap.

**Removed verbose text lines:**
- "refresh end" (ms) — removed
- "debounce" (ms) — removed
- "touch down" + "touch up" + "last hit" — merged into one line
- "ignored" (last ignored touch reason) — removed

**Button changes:**
- Removed "Profile: …" cycle button from Debug (`gPowerProfileButton = {}` zeroed in Debug).
  Profile control is now in Settings and Power Lab footer.
- Button grid start moved from `display.height() - 362` to `display.height() - 420`
  to give clear separation from text area.

**Remaining Debug buttons (4 rows, 2 columns):**
Help · Power Lab · Visual QA · Font Lab · Reset typography · Layout log ·
Dump render trace · Export deck text · Touch debug · Home

### 5. Power Lab — Page 3 Wake Guidance

Added two new rows at the top of the Sleep Lab section:
- `"Light sleep uses timer wake. Hold screen 2-3s to wake."`
- `"Nap duration: Xs (ProfileName)"` — shows current profile's nap cycle length

Profile footer label now reads "Low Power" / "Max Battery" / "Balanced" (from `powerProfileName()`).

---

## Power Stage Ladder (unchanged from dev5)

| Stage | CPU | When |
|-------|-----|------|
| **Active** | 240 MHz | During touch, render, SD, parse, heavy work |
| **WarmIdle** | 80 MHz | After short idle threshold (profile-dependent) |
| **LightNap** | 80 MHz + sleep | During profile-duration timer nap (opt-in via Sleep mode) |
| **Hibernate** | deep sleep | Blocked — touch wake unverified on PaperS3 |

---

## WarmIdle Thresholds (unchanged from dev5)

| Profile | WarmIdle Threshold | LightNap Idle Threshold | LightNap Nap Duration |
|---------|--------------------|------------------------|-----------------------|
| Balanced | 12s | 7 min | **10s** |
| Low Power | 7s | 90s | **30s** |
| Max Battery | 4s | 40s | **60s** |

---

## Peripherals (unchanged)

| Peripheral | Status |
|-----------|--------|
| Wi-Fi | Off at boot (WiFi.disconnect + WIFI_OFF) |
| Bluetooth | Stopped at boot (btStop()) |
| Speaker | Stopped at boot (M5.Speaker.stop()) |
| Microphone | Not used / not started |
| IMU | Not started |
| SD card | Mounted but idle between file ops |
| Battery/PMIC poll | 45s active / 120s Low Power idle / 180s Max Battery idle |

---

## Safety and Recovery (unchanged)

- Default boot: no sleep, Active stage, normal rendering
- Light sleep: opt-in only (Settings → Sleep mode → Light, or Power Lab → Sleep Lab)
- Deep sleep / Hibernate: blocked — touch wake source not verified on PaperS3
- `recordUserActivity()` always restores CPU and resets idle state — no stuck-idle risk

---

## Japanese Readiness Audit

**Status: NOT READY — sanitizer destroys Japanese UTF-8**

The `sanitizeCoachText()` function (line ~3816) handles only a specific whitelist of UTF-8
sequences (smart quotes, dashes, bullets, nbsp, arrows, etc.). Any other multi-byte character
that is not in the whitelist hits the fallback at line ~3888:
```cpp
sanitized += "?";
index += utf8SequenceLength(ch);
```

Japanese characters (U+3040–U+9FFF, 3-byte UTF-8) would be replaced with "?" entirely.
The function counts these as `replacements` and logs them — so they are detectable via
`gSanitizerReplacementTotal` on the Debug screen.

**Required before enabling Japanese content:**
1. UTF-8-safe sanitizer — pass-through for valid multi-byte sequences that are not in
   the known-bad list; only replace characters that are actually problematic for the font.
2. Japanese word-wrap — M5GFX `drawString` does not break on spaces between kanji;
   need character-level line-break logic for CJK.
3. Japanese font — use `lgfxJapanGothic_*` or `efontJA_*` bitmap fonts from M5GFX,
   OR load a VLW/TTF that includes CJK glyphs.
4. New `LanguageMode::Japanese` path in `renderBadge()` — already partially scaffolded
   (see `gBadgeLanguage == BadgeLanguage::Japanese` branch at line ~2942).

**Proposed future main menu (architecture note only, no UI change yet):**
```
Badge
Interview Prep
Japanese        ← future, add only when all 4 readiness items above are complete
Settings
Debug
```

Do NOT add the Japanese menu item until the sanitizer is UTF-8-safe and Japanese content
files exist on SD card. An empty/broken screen in the main menu is worse than no option.

---

## Physical QA Checklist — v5.8-dev6-settings

### 1. Settings screen layout

1. Home → Settings
2. Verify layout top-to-bottom:
   - Title "Settings" at top
   - Power status line + battery bar
   - Badge orientation button
   - Badge language mode + auto interval buttons
   - Reader size: **S / M / L in one compact row** (all 3 side-by-side)
   - Font style button
   - Refresh mode button
   - **Power profile button** (should show "Balanced *", "Low Power *", or "Max Battery *")
   - **Sleep mode button** (shows "Off", "Light", or "Deep test")
   - "(dev) Light sleep: hold 2–3s to wake" hint (shown only when sleep ≠ Off)
   - **Home button — full width at bottom** (no overlap with anything above)

3. Tap Power profile: cycles Balanced → Low Power → Max Battery → Balanced
4. Tap Sleep mode: cycles Off → Light → Deep test → Off
5. Confirm settings persist after Home → Settings (saved to NVS)

### 2. Debug screen buttons clear

1. Home → Debug
2. Verify: no text overlaps button grid
3. Verify: **no "Profile: …" button** in Debug (moved to Settings)
4. Touch row shows as single compact line: "touch: dn X,Y  up X,Y  hit: label"
5. All 9 action buttons visible and tappable: Help, Power Lab, Visual QA, Font Lab,
   Reset typography, Layout log, Dump render trace, Export deck text, Touch debug
6. Home button full-width at bottom, not overlapping buttons above

### 3. Power Lab profile labels

1. Home → Debug → Power Lab
2. Verify footer "Profile" button shows: "Balanced", "Low Power", or "Max Battery"
3. Tap Profile footer: cycles through all three with new labels
4. Page 3 (Sleep Lab): verify text "Light sleep uses timer wake. Hold screen 2-3s to wake."
5. Page 3: verify "Nap duration: Xs (ProfileName)" shows correct value for current profile
   - Balanced → 10s
   - Low Power → 30s
   - Max Battery → 60s

### 4. LightNap duration is profile-based

1. Set Profile → Low Power (Settings or Power Lab footer)
2. Enable Sleep mode → Light (Settings or Power Lab page 3)
3. Navigate to Badge screen; wait ~90s without touching
4. Return to Power Lab → Page 3
5. Check "Last sleep attempt" — should say "light nap 30s timer" (not "light nap 2s timer")
6. Check counters: attempts > 0, entered > 0, woke > 0

### 5. Confirm deep sleep blocked

1. Power Lab → Page 3 → Sleep mode → Deep test
2. Wait for idle threshold
3. Serial log: "Hibernate blocked: safe PaperS3 touch wake source is not verified."
4. Device does NOT reset or freeze

---

## Known Risks (updated)

1. **Max Battery 60s nap UX**: If the user taps during a 60s nap, the touch is missed.
   Hold-and-wait is the only reliable method. The hint text says "2-3s" which is accurate
   for Balanced (10s naps) but understates the wait for Max Battery. An extended hold will
   eventually coincide with a wake poll.
2. **BadgeMax 4s WarmIdle threshold**: Very aggressive on active-render screens. Monitor
   `gRedrawWhileIdleCount` via Power Lab if rendering glitches appear.
3. **Battery Saver not in Settings**: Users who had Battery Saver on and reset settings
   will not see it in the new Settings UI. It remains in NVS and can be toggled via the
   Power Audit screen (Debug → Power Lab → Power Audit).
4. **Japanese content**: sanitizer replaces all non-whitelisted UTF-8 with "?". Do not
   load Japanese deck files until sanitizer is made UTF-8-safe.

---

## UX Decisions

| Decision | Value |
|----------|-------|
| Default power profile | Balanced (12s WarmIdle, 10s LightNap duration) |
| Deep sleep | Blocked |
| Profile cycling | Settings (primary) + Power Lab footer |
| Sleep mode cycling | Settings (primary) + Power Lab page 3 |
| Battery Saver toggle | Power Audit screen only (not in main Settings) |
| Language/font/refresh settings | Settings screen |
| Reader size | Compact 3-button row (S/M/L) in Settings |
