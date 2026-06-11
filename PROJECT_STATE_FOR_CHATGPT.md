# PaperBadge Project State — v5.8-dev5-kindlepower Handoff

_Last updated: 2026-06-11_

---

## Git / Remote Status

| Item | Value |
|------|-------|
| Branch | `main` |
| Remote | `https://github.com/growwithsdtr/paperBadge.git` |

---

## Firmware

- **Version:** `v5.8-dev5-kindlepower` (`src/main.cpp:20`)
- **Build:** SUCCESS — RAM 49.5% · Flash 47.5%
- **Upload:** SUCCESS — `/dev/tty.usbmodem1101`

---

## Phase 1 — v5.8-dev4-powerlab Confirmed Baseline (Frozen)

### Physical Long Test Results (v5.8-dev4)

| Metric | Result |
|--------|--------|
| Total 80 MHz time | ~16h52m (≈1012m12s) |
| Longest single 80 MHz interval | ~13h25m (≈805m5s) |
| Redraws while idle | 0 |
| Battery depleted | Yes — proving CPU-only 80 MHz standby is insufficient for week-like runtime |
| E-ink image after battery depletion | Visible (expected e-ink behavior — not a touch bug) |

**Conclusion:** 80 MHz active idle alone does not achieve Kindle-like standby. Light sleep / deeper power stages are required.

---

## What Changed in v5.8-dev5-kindlepower

### Power Stage Ladder (new)

| Stage | CPU | When |
|-------|-----|------|
| **Active** | 240 MHz | During touch, render, SD, parse, heavy work |
| **WarmIdle** | 80 MHz | After short idle threshold (profile-dependent) |
| **LightNap** | 80 MHz + sleep | During 2s timer nap cycles (opt-in via Sleep Lab) |
| **Hibernate** | deep sleep | Blocked — touch wake unverified on PaperS3 |

### New Globals

| Variable | Type | Purpose |
|----------|------|---------|
| `gPowerStage` | `PowerStage` | Current explicit power stage |
| `gLastPowerStage` | `PowerStage` | Previous stage before last transition |
| `gStageTransitionCount` | `uint32_t` | Total stage transitions since boot |
| `gStageEnteredAtMs` | `uint32_t` | Millis when current stage was entered |
| `gLightSleepTotalMs` | `uint32_t` | Cumulative light sleep time |
| `gLightSleepAttemptCount` | `uint32_t` | Light sleep attempt count |
| `gLightSleepFailedCount` | `uint32_t` | Failed entries (reserved) |
| `gLastLightSleepDurationMs` | `uint32_t` | Duration of last nap |
| `gLongestLightSleepMs` | `uint32_t` | Longest single nap |
| `gHibernateArmed` | `bool` | Deep sleep arm flag (always false — blocked) |
| `gInputDetectedAfterWake` | `bool` | Touch detected in first poll after nap wake |

### Shortened Idle Thresholds

| Profile | WarmIdle (80 MHz) | LightNap (timer nap) |
|---------|-------------------|----------------------|
| Balanced | **12s** (was 60s) | **7 min** (opt-in) |
| Aggressive | **7s** (was 25s) | **90s** (opt-in) |
| Badge Max | **4s** (was 20s) | **40s** (opt-in) |

`kPostTouchIdleGuardMs` lowered from 5s to 2s so the 4s BadgeMax threshold can actually fire.

### Light Nap Behavior (extended from Badge-only)

- Was: Badge screen only, 30s idle, 2s nap
- Now: **Any static idle screen**, profile-based threshold, 2s timer nap cycle
- Opt-in: must enable via Power Lab → Sleep Lab → Sleep mode button
- Nap cycle: sleep 2s → wake by timer → poll touch → re-enter if still idle
- Touch during nap is missed; tap-and-hold ~2s will be detected post-wake
- `gInputDetectedAfterWake` is set if M5.Touch detects a touch on first post-wake poll

### Wake Sources (discovered / documented)

| Source | Status |
|--------|--------|
| Timer (`esp_sleep_enable_timer_wakeup`) | **Supported — used for nap cycles** |
| USB/UART (ESP32-S3 CDC) | **Supported — automatic from light sleep** |
| Touch (GT911 INT) | **Unknown — GPIO INT pin not configured** |
| Button/GPIO | **Not configured** |
| Deep sleep wake source | **Blocked — touch wake unverified** |
| RTC memory | Available but not used |
| Preferences/NVS | Available — used for settings persistence |

### Loop Delays (updated)

| State | Balanced | Aggressive | Badge Max |
|-------|----------|------------|-----------|
| WarmIdle static screen | 300ms (was 250ms) | 500ms (was 400ms) | 800ms (was 600ms) |
| ConferenceBadge idle | 600ms (was 500ms) | — | — |
| Badge Max badge-only | — | — | 250ms (was 200ms) |

### Serial Log Suppression

`isVerboseLogOk()` returns false when `gIdleModeActive && profile != Balanced`.
Battery poll log (`Power poll:`) is suppressed in Aggressive/BadgeMax idle.
Sleep wake/entry logs are never suppressed.

### Power Lab — 3 Pages (updated)

**Page 1 — CPU / Stage / Idle Counters:**
- Power stage (current + last + transitions)
- Profile + WarmIdle threshold + LightNap threshold
- CPU now / pre-render (MHz)
- Scale / restore event counts
- Last scaled / restored timestamps
- 80 MHz last/total/longest
- Light nap total / longest
- Last input age
- Idle blocked reason / idle mode status
- Loop delay / redraws while idle
- Screen / refresh count

**Page 2 — Battery / Peripherals:**
- Battery mV / % 
- Charge state + current mA + USB/VBUS
- Battery poll age + interval (active vs idle rate)
- Wi-Fi / BT / Speaker / Mic / IMU / SD status
- Redraws while idle

**Page 3 — Sleep Lab Controls:**
- Sleep mode: Off / Light test / Deep test
- Current power stage
- Light attempts / entered / woke
- Light total / last / longest
- Last wake reason
- Last sleep attempt
- Input detected after wake
- Wake source status table (touch-INT, timer, USB, GPIO, deep, hibernate)
- **Sleep mode cycle button** (body button, cycles Off → Light → Deep → Off)

**Footer:** `Profile | Page | Home`
- **Profile**: cycles power profile (Balanced → Aggressive → Badge Max)
- **Page**: advances page (1→2→3→1)
- **Home**: returns to home screen
- **Body tap**: re-renders current page (fresh state capture)

---

## CPU Frequency Discovery (unchanged from dev4)

| Frequency | Status |
|-----------|--------|
| 240 MHz | Active — default for rendering/input |
| 80 MHz | Safe — idle scaling confirmed |
| 40 MHz | Unsupported — ESP32-S3 Arduino minimum is 80 MHz |

---

## Peripherals (unchanged from dev4)

| Peripheral | Status |
|-----------|--------|
| Wi-Fi | Off at boot (WiFi.disconnect + WIFI_OFF) |
| Bluetooth | Stopped at boot (btStop()) |
| Speaker | Stopped at boot (M5.Speaker.stop()) |
| Microphone | Not used / not started |
| IMU | Not started |
| SD card | Mounted but idle between file ops |
| Battery/PMIC poll | Profile-aware: 45s active / 120s Aggressive idle / 180s BadgeMax idle |

---

## Safety and Recovery

- Default boot: no sleep, Active stage, normal rendering
- Light sleep: opt-in only (Power Lab → Sleep Lab → enable)
- Deep sleep / Hibernate: blocked — touch wake source not verified on PaperS3
- If deep sleep is accidentally entered (impossible via current UI), device will reset; prefs/NVS survive reset
- No stuck-idle risk: `recordUserActivity()` always restores CPU and resets idle state

---

## Physical QA Steps for v5.8-dev5-kindlepower

### 1. Confirm WarmIdle triggers faster

1. Home → Debug → Power Lab → Page 1
2. Do not touch screen
3. **Balanced profile**: wait 13–15 seconds, then tap body
4. Check: Stage shows `WarmIdle`, Scale events > 0, Pre-render was idle-scaled
5. Switch to **Aggressive** (tap Profile footer button): wait 8–10s → same result
6. Switch to **Badge Max**: wait 5–6s → same result

### 2. Confirm Power Lab Page 3 / Sleep Lab

1. Go to Power Lab → tap Page twice → reach Page 3
2. Verify: wake source table, sleep mode row, attempt/enter/wake counters
3. Tap the Sleep mode cycle button: cycles Off → Light test → Deep test → Off
4. Confirm "Sleep mode: Light test" appears

### 3. Light nap test (Badge Max + Light test)

1. Set profile to Badge Max
2. Go to Power Lab page 3, enable Light test
3. Navigate to Badge screen
4. Wait 45+ seconds without touching
5. After 1–2 minutes, return to Power Lab → Page 3
6. Check: Light attempts > 0, entered > 0, woke > 0, total > 0s
7. Wake reason: "timer"

### 4. Light nap on non-Badge static screen

1. Set Aggressive profile + Light test enabled
2. Go to Practice / Settings / Debug screen
3. Wait 90+ seconds
4. Return to Power Lab → Page 3: Light entered should be > 0

### 5. Confirm deep sleep blocked

1. Power Lab → Page 3 → cycle sleep to "Deep test"
2. Wait for idle threshold
3. Verify serial log: "Hibernate blocked: safe PaperS3 touch wake source is not verified."
4. Device does NOT reset

---

## Known Risks

1. **BadgeMax 4s threshold**: very aggressive. If a render takes >2s and the 2s post-touch guard is in play, idle may trigger very quickly. Monitor for unexpected idle during render operations.
2. **Light sleep on non-Badge screens**: new behavior in dev5. If a static screen has a bug that re-renders during sleep wake, `gRedrawWhileIdleCount` will catch it.
3. **Touch missed during 2s nap**: users on sleep-enabled profiles may notice short taps are missed. Workaround: tap-and-hold 2–3 seconds. This is labeled clearly in Power Lab Page 3.
4. **Deep sleep still blocked**: `gHibernateArmed` is a placeholder. The UI cycles to "Deep test" but only logs a blocked message; it does not sleep.

---

## UX Decisions Unchanged

| Decision | Value |
|----------|-------|
| Practice header layout | unchanged (Layout Batch A passed) |
| kCoachMargin | 20 (frozen) |
| Default power profile | Balanced (12s WarmIdle threshold) |
| Deep sleep | blocked |
| Profile cycling | Debug screen + Power Lab footer |
| Language/font/refresh settings | unchanged |
