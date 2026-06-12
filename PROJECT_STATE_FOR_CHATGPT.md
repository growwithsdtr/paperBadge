# PaperBadge Project State — v5.8-dev7 Handoff

_Last updated: 2026-06-12_

---

## Git / Remote Status

| Item | Value |
|------|-------|
| Branch | `main` |
| Remote | `https://github.com/growwithsdtr/paperBadge.git` |

---

## Firmware

- **Version:** `v5.8-dev7` (`src/main.cpp:20`)
- **Build:** SUCCESS — RAM 49.5% · Flash 47.6%
- **Upload:** PENDING (device not connected at time of build)

---

## What Changed in v5.8-dev7 (wake hardening)

### Summary of Ambiguity in Prior Testing

User confirmed 80 MHz WarmIdle works and LightNap entered and woke by timer. Wake recovery
was ambiguous: in Badge mode, waking from LightNap worked but required a few tries / holding.
In at least one case the device appeared unresponsive and had to be rebooted. Root causes
were not definitively identified — could have been gesture timing, input-lock timing, or
the device immediately re-entering sleep after the 1-second minimum gap.

### 1. Post-Wake Listen Window (PHASE 2 — core fix)

After every timer wake from LightNap, the device now stays in a **listen window** during
which it will NOT immediately re-enter LightNap, regardless of idle time.

| Profile | Listen Window Duration |
|---------|----------------------|
| Balanced | 8s |
| Low Power (Aggressive) | 10s |
| Max Battery (BadgeMax) | 12s |

**Behavior:**
- Any touch press during the listen window calls `recordUserActivity()`, which resets the
  idle timer, exits the listen window early, and restores CPU to 240 MHz.
- If no touch occurs by window end, the idle timer has NOT been reset, so the device
  will re-enter LightNap on the next cycle after idle threshold.
- This replaces the old behavior: 1-second minimum gap → immediate re-sleep.

### 2. Shorter Dev-Phase Nap Durations (PHASE 3)

Shorter naps make it easier to test recovery without getting trapped.
Extend once listen window + tap-anywhere wake is confirmed reliable.

| Profile | Old Duration | New Duration |
|---------|-------------|--------------|
| Balanced | 10s | **5s** |
| Low Power | 30s | **10s** |
| Max Battery | 60s | **15s** |

### 3. Sleep Mode Resets Off on Reboot (PHASE 2 — safety)

`badgeSleepMode` is **never restored from NVS** on boot. The device always starts with
`BadgeSleepMode::Off` regardless of what was saved. Sleep must be manually re-enabled
from Settings or Power Lab each session.

- Power profile (Balanced / Low Power / Max Battery) IS still saved and restored.
- A serial log line confirms: `"Sleep mode: reset to Off on boot (not sticky)"`

### 4. Universal Emergency Long Press (PHASE 2 — dev escape)

While Sleep mode is active (Light or DeepExperiment), a long press **anywhere on screen**
will:
- Set Sleep mode to Off
- Save settings
- Navigate to Power Lab page 3 (Sleep Lab)
- Log: `"Emergency long press: sleep disabled, to Power Lab"`

This does NOT fire if Sleep mode is already Off (avoids interfering with normal use).
Counter: `gLongPressEscapeCount` (visible on Power Lab page 3).

### 5. Input Lock Watchdog (PHASE 2 — stuck-lock guard)

If `gInputLocked` remains true for more than **8 seconds**, it is force-cleared.
Debounce periods are at most 600 ms — an 8-second lock means something went wrong.
Counter: `gInputLockWatchdogCount` (visible on Power Lab page 3).

### 6. Touch Timestamps

`gLastTouchDownMs` and `gLastTouchUpMs` — millisecond timestamps of last press and release.
Both visible on Power Lab page 3.

### 7. Power Lab Page 3 — Full Wake Diagnostics

Page 3 now shows:

- Sleep mode + current/last power stage
- Nap duration, attempts, entered, woke
- Light nap total/last/longest
- Wake reason + time-since-wake
- Last sleep attempt string
- Listen window state: ACTIVE / idle, duration, entered count
- If active: remaining window time
- Input detected after wake (held during sleep) + listen window touch detected
- Last touch down/up timestamps (age)
- Last hit target + last ignored reason
- Input locked y/n + lock age + watchdog clear count
- Emergency long press escape count
- Wake source notes (timer supported; touch INT not configured; short taps missed;
  tap anywhere in listen window; long press disables; deep sleep blocked; sleep not sticky)

### 8. Settings Screen — Updated Dev Hint

When Sleep mode is not Off:
```
Light test: tap after timer wake. Long press=disable. Resets Off on reboot.
```

---

## Power Stage Ladder (unchanged)

| Stage | CPU | When |
|-------|-----|------|
| **Active** | 240 MHz | During touch, render, SD, parse, heavy work |
| **WarmIdle** | 80 MHz | After short idle threshold (profile-dependent) |
| **LightNap** | 80 MHz + sleep | During profile-duration timer nap (opt-in via Sleep mode) |
| **Hibernate** | deep sleep | Blocked — touch wake unverified on PaperS3 |

---

## Thresholds (updated nap durations)

| Profile | WarmIdle Threshold | LightNap Idle Threshold | Nap Duration | Listen Window |
|---------|--------------------|------------------------|-------------|---------------|
| Balanced | 12s | 7 min | **5s** | **8s** |
| Low Power | 7s | 90s | **10s** | **10s** |
| Max Battery | 4s | 40s | **15s** | **12s** |

---

## Wake Behavior Model

1. Device is at rest in WarmIdle (80 MHz, screen static, sleep mode = Light).
2. After LightNap idle threshold, `maybeEnterBadgeSleep()` enters `esp_light_sleep_start(napDuration)`.
3. After timer fires, CPU resumes in `maybeEnterBadgeSleep()`.
4. Wake reason is recorded. One `M5.update()` is polled — if touch count > 0, `gInputDetectedAfterWake = true`.
5. Listen window starts (`gInWakeListenWindow = true`, `gWakeListenWindowEndMs = now + listenMs`).
6. If touch was held during sleep: `recordUserActivity("post-wake touch held")` immediately exits window + resets idle.
7. Function returns. Main loop continues normally (M5.update → handleTouch → maybeEnterPowerIdle → maybeEnterBadgeSleep).
8. `maybeEnterBadgeSleep()` returns immediately while in listen window.
9. Any touch in the loop calls `recordUserActivity()`, which clears `gInWakeListenWindow`.
10. If no touch by window end: window expires, sleep cycle resumes if still idle.

**Key: touch interrupt wake is NOT configured.** Only timer wake is used.
Short taps during the sleep phase are missed. After each wake, there is an 8–12 s window.

---

## Safety and Recovery

- Default boot: no sleep, Active stage, normal rendering
- Sleep mode always resets Off on boot — not sticky
- Light sleep: opt-in only (Settings → Sleep mode → Light, or Power Lab page 3)
- Deep sleep / Hibernate: blocked — touch wake source not verified on PaperS3
- `recordUserActivity()` always restores CPU and resets idle state — no stuck-idle risk
- Input lock watchdog: 8s maximum lock time before force-clear
- Emergency long press: disables sleep from any screen (when sleep is active)

---

## Peripherals (unchanged)

| Peripheral | Status |
|-----------|--------|
| Wi-Fi | Off at boot |
| Bluetooth | Stopped at boot |
| Speaker | Stopped at boot |
| Microphone | Not used |
| IMU | Not started |
| SD card | Mounted but idle between file ops |
| Battery/PMIC poll | 45s active / 120s Low Power idle / 180s Max Battery idle |

---

## Physical QA — v5.8-dev7

### 1. Sleep resets Off on reboot

1. Settings → Sleep mode → Light → Home (confirm saved)
2. Power the device off and on (or hard reboot)
3. Settings → Sleep mode: **should show "Off"**
4. Serial log should contain: `"Sleep mode: reset to Off on boot (not sticky)"`

### 2. Post-wake listen window

1. Settings → Sleep mode → Light; Settings → Power profile → Balanced (5s nap, 8s window)
2. Navigate to Badge screen; wait 7+ minutes without touching
3. When nap begins, wait for it to complete (5s), then watch serial for `"Wake listen window: 8000ms started"`
4. Tap anywhere within 8s of wake
5. Power Lab page 3: verify `Listen window: touch: yes`, `last wake: Xs ago`
6. Device should stay awake and respond normally after tap

### 3. Emergency long press

1. Settings → Sleep mode → Light
2. Long press anywhere for ~1s (M5Unified hold threshold)
3. Should navigate to Power Lab page 3 with Sleep mode showing "Off"
4. Serial: `"Emergency long press: sleep disabled, to Power Lab"`
5. After reboot, Sleep mode should again be "Off"

### 4. Input lock watchdog (theoretical — hard to trigger manually)

1. Power Lab page 3 shows `watchdog clears: 0` normally
2. If a render crashes after `lockInputForRefresh` but before `finishDisplayRefresh`,
   the watchdog will clear after 8s and log

### 5. Power Lab page 3 diagnostics

1. Enable Sleep mode → Light → navigate to Badge → wait for a nap cycle
2. Power Lab → Page 3
3. Verify all new fields present and coherent:
   - Wake reason: `timer`
   - Listen window: `idle` (after window expired), `entered: 1+`
   - Listen touch: `yes` (if you tapped) / `no` (if you didn't)
   - Last touch down/up: show age in seconds

### 6. Nap durations

1. Set profile to each level; confirm Power Lab page 3 "Nap: Xs" matches:
   - Balanced → 5s
   - Low Power → 10s
   - Max Battery → 15s

---

## Next Physical Tests (priority order)

1. Verify Sleep mode Off on reboot (must pass before any sleep testing)
2. Verify 5s nap + 8s listen window in Balanced → tap within window → stays awake
3. Verify emergency long press disables sleep
4. Verify multiple nap cycles → each has a listen window → tap any one → stays awake
5. If above pass: test Low Power (10s nap) and Max Battery (15s nap)
6. If nap cycles are reliable: extend nap durations toward production values

---

## Known Risks (updated)

1. **Touch interrupt wake not configured**: Only timer wake is used. The GT911 touch
   controller interrupt GPIO is unknown. A short tap during a sleep cycle is still missed.
   Recovery requires tapping after the timer fires, within the listen window.
2. **Listen window interaction with other screens**: If the user is on a dynamic screen
   (not static idle), `maybeEnterBadgeSleep` already returns early (screen guard), so
   the listen window guard is effectively never needed there. No risk.
3. **BadgeMax 4s WarmIdle threshold**: Still very aggressive. Monitor `gRedrawWhileIdleCount`.
4. **Japanese content**: sanitizer replaces all non-whitelisted UTF-8 with "?". Do not load
   Japanese deck files until sanitizer is made UTF-8-safe.

---

## UX Decisions (updated)

| Decision | Value |
|----------|-------|
| Default power profile | Balanced (12s WarmIdle, 5s LightNap nap, 8s listen window) |
| Sleep mode on boot | **Always Off (not sticky)** |
| Deep sleep | Blocked |
| Listen window | 8–12s post-wake, any touch closes it |
| Emergency escape | Long press (sleep active) → disable sleep + go to Power Lab |
| Input lock max | 8s watchdog |
| Profile cycling | Settings (primary) + Power Lab footer |
| Sleep mode cycling | Settings (primary) + Power Lab page 3 |
