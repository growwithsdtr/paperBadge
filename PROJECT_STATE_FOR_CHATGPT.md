# PaperBadge Project State — v5.8-dev4-powerlab Handoff

_Last updated: 2026-06-10_

---

## Git / Remote Status

| Item | Value |
|------|-------|
| HEAD | `20ffa0f` |
| Branch | `main` |
| Remote | `https://github.com/growwithsdtr/paperBadge.git` |

---

## Firmware

- **Version:** `v5.8-dev4-powerlab` (`src/main.cpp:20`)
- **Build:** SUCCESS — RAM 49.5% · Flash 47.5% · 10.47 s
- **Upload:** SUCCESS — `/dev/tty.usbmodem1101`, 28.36 s

---

## Phase 1 — Discovered Low-Power Limits

### CPU Frequency (ESP32-S3 via Arduino HAL)

| Frequency | Status | Notes |
|-----------|--------|-------|
| 240 MHz | Active — already used | Default active freq |
| 160 MHz | Safe, unsupported in this build | Not used, no firmware benefit planned |
| 80 MHz | **Safe — already used for idle** | `setCpuFrequencyMhz(80)` confirmed working |
| 40 MHz | **Unsupported / unsafe** | ESP32-S3 Arduino HAL minimum for stable Arduino core + USB CDC is 80 MHz. 40 MHz is not a supported Arduino operating frequency on S3. |

Active CPU scaling is distinct from sleep: scaling keeps the CPU running but slower. Sleep (light/deep) suspends execution.

### Sleep Sources Available

| Source | Status |
|--------|--------|
| Timer wakeup (`esp_sleep_enable_timer_wakeup`) | **Used** — light sleep timer-wake in Badge mode |
| Touch controller interrupt | **Unknown** — not configured; PaperS3 touch wake from light/deep sleep not verified |
| GPIO / physical button | Not configured |
| RTC timer | Same as timer wakeup |
| IMU interrupt | Not configured; IMU not started |
| USB / UART | Wakes from light sleep automatically on CDC traffic |

Light sleep with timer wake: **confirmed working** (badge mode, BadgeSleepMode::Light).  
Deep sleep: **blocked** — touch controller wake source not verified on PaperS3; if deep sleep fires, all RAM is lost (reset-like wake).

### Display / E-ink

| Item | Status |
|------|--------|
| E-ink panel between refreshes | Already idle — no power draw while static |
| M5GFX display sleep/power-down API | Not exposed in current M5GFX version — `M5.Display.sleep()` is not available |
| Touch controller separate from display | Yes — GT911 touch IC is separate from the EPD panel |
| Touch controller stays awake while display is static | Yes — touch polling continues normally |
| Redraw loop | None — screen is only redrawn on explicit touch |

**Already optimized:** no redraw loop, e-ink is static between renders.

### Peripherals

| Peripheral | Status | Classification |
|-----------|--------|---------------|
| Wi-Fi | Off at boot (`WiFi.disconnect + WIFI_OFF`) | **Already optimized** |
| Bluetooth | Stopped at boot (`btStop()`) | **Already optimized** |
| Speaker | Stopped at boot (`M5.Speaker.stop()`) | **Already optimized** |
| Microphone | Not used / not started | **Already optimized** |
| IMU | Not started | **Already optimized** |
| SD card | Mounted but idle between file ops | **Already optimized** — no dequeue risk |
| Battery/PMIC poll | 45s active; 120s Aggressive idle; 180s BadgeMax idle | **New in dev4** |
| Serial logging | Active always (USB CDC) | Safe to suppress in idle; not implemented yet |

---

## What Changed in v5.8-dev4-powerlab

### Power Event Logger (new globals)

| Variable | Type | Purpose |
|----------|------|---------|
| `gCpuScaleCount` | `uint32_t` | Count of CPU scale-downs to 80 MHz |
| `gCpuRestoreCount` | `uint32_t` | Count of CPU restores to 240 MHz |
| `gLast80MhzDurationMs` | `uint32_t` | Duration of last 80 MHz interval (ms) |
| `gCumulative80MhzMs` | `uint32_t` | Total ms spent at 80 MHz since boot |
| `gLongest80MhzMs` | `uint32_t` | Longest single 80 MHz interval (ms) |
| `gPowerLabPage` | `uint8_t` | Current Power Lab page (0 or 1) |
| `gRedrawWhileIdleCount` | `uint32_t` | Number of redraws that fired while idle mode was active |
| `gLightSleepEnteredCount` | `uint32_t` | Number of light sleep entries |
| `gLightSleepWakeCount` | `uint32_t` | Number of light sleep wakes |

### Updated Functions

- **`restoreActiveCpu()`** — now computes `gLast80MhzDurationMs`, accumulates `gCumulative80MhzMs`, updates `gLongest80MhzMs`, increments `gCpuRestoreCount`
- **`maybeScaleIdleCpu()`** — now increments `gCpuScaleCount` on confirmed scale
- **`maybeEnterBadgeSleep()`** — now increments `gLightSleepEnteredCount` / `gLightSleepWakeCount`
- **`prepareFullRefresh()`** — now increments `gRedrawWhileIdleCount` when `gIdleModeActive`
- **`batteryLevelPercent()`** — now uses `profileBatteryPollMs()` instead of constant

### New Functions

- **`profileBatteryPollMs()`** — returns 45s (Normal), 120s (Aggressive idle), 180s (BadgeMax idle)
- **`fmtDurationMs()`** / **`fmtMsSince()`** — human-readable ms/age formatters for Power Lab display
- **`renderPowerLab()`** — new 2-page Power Lab screen (see below)

### Power Lab Screen (new)

Access: **Debug → Power Lab**

Two-page screen with footer: **Profile | Refresh | Home**

**Page 1 — CPU & Event History:**
- Profile name + threshold
- CPU now / Pre-render CPU (pre-restore)
- Scale events / Restore events
- Last scaled (age) / Last restore reason / Last restore (age)
- Last 80 MHz duration / Total 80 MHz time / Longest 80 MHz interval
- Last input age / idle threshold
- Idle blocked reason or "Eligible: yes"
- Input locked / Touch active
- Loop delay
- Refresh count / Redraws while idle
- Last refresh reason / Current screen

**Page 2 — Peripheral & Sleep State:**
- Wi-Fi / Bluetooth status
- Speaker / Mic / IMU status
- SD state
- Battery mV / % / poll age
- Battery poll interval (active vs idle rate)
- Charge state / USB VBUS
- Light sleep mode / entered / woke counts
- Last sleep attempt / wake reason
- Deep sleep: blocked (with reason)
- Power mode / idle entry count

**Footer interactions:**
- **Profile button** — cycles profile (Balanced → Aggressive → Badge Max → Balanced), saves prefs
- **Refresh** — advances page (1→2→1) and re-renders to capture fresh state
- **Home** — returns to home screen
- **Tap anywhere else on screen** — re-renders current page to capture fresh state

### Profile Improvements (dev4)

| Profile | Idle scale threshold | Idle loop delay | Battery poll (idle) |
|---------|---------------------|-----------------|---------------------|
| Balanced | 60s | 250ms | 45s |
| Aggressive | 25s | 400ms | 120s |
| Badge Max | 20s | 600ms (badge only) | 180s |

### Screen Enum Addition

`Screen::PowerLab` added between `Screen::PowerAudit` and `Screen::FontLab`.  
`isStaticIdleScreen()` includes `PowerLab` → eligible for idle CPU scaling.

### Debug Screen Change

"Power Audit" button in Debug → **"Power Lab"** (navigates to `renderPowerLab()`).  
`renderPowerAudit()` remains in the codebase but is no longer reachable from Debug.

---

## Power Lab Navigation

```
Home → Debug → Power Lab
```

From Power Lab: tap **Refresh** to cycle page (1↔2) and re-render fresh state.  
From Power Lab: tap **Profile** to cycle power profile.  
From Power Lab: tap anywhere else to re-render current page.

---

## Test Procedures

### Balanced Profile (default, 60s threshold)

1. Debug → Power Lab → Page 1
2. Do not touch the screen
3. Wait **65 seconds**
4. Tap anywhere on the screen (not a footer button)
5. Page re-renders. Check:
   - **Scale events:** > 0
   - **Pre-render:** "80 MHz [was idle-scaled]"
   - **Last scaled:** ~65s ago
   - **Last restore reason:** "display refresh" or "tap refresh"
   - **Last 80 MHz duration:** ~65s
6. Tap Refresh to see Page 2 (peripheral state)

### Aggressive Profile (25s threshold)

1. From Debug screen → tap "Profile: Balanced" until "Profile: Aggressive"
2. Debug → Power Lab → Page 1
3. Wait **30 seconds**
4. Tap anywhere — Pre-render should show "80 MHz [was idle-scaled]"
5. Loop delay shown: 400ms
6. Battery poll interval shown: 120s

### Badge Max Profile (20s threshold, badge-only)

1. Set profile to Badge Max via Debug screen
2. Navigate to Home → Badge screen
3. Wait **25 seconds**
4. Navigate to Debug → Power Lab → Page 1
5. Scale events > 0; Last 80 MHz duration ~25s
6. Loop delay shown: 600ms (only while on Badge screen)

---

## How to Interpret Event Counters

| Counter | Meaning |
|---------|---------|
| Scale events | CPU successfully reduced to 80 MHz this many times since boot |
| Restore events | CPU restored to 240 MHz this many times (≈ scale events, may lag by 1) |
| Last 80 MHz duration | How long the LAST idle interval lasted before restore |
| Total 80 MHz time | Cumulative idle time since boot — confirms real savings |
| Longest 80 MHz interval | Best single idle stretch — diagnostic for "can it stay idle?" |
| Redraws while idle | Any redraws that happened with idle mode active — should be low |
| Light sleep entered/woke | Only > 0 in Aggressive/BadgeMax with BadgeSleepMode::Light enabled |

---

## Power Lever Classification

| Lever | Classification |
|-------|---------------|
| CPU 240→80 MHz on static screens | **Already implemented** |
| Wi-Fi off | **Already implemented** |
| Bluetooth off | **Already implemented** |
| Speaker stopped | **Already implemented** |
| IMU not started | **Already implemented** |
| Mic not used | **Already implemented** |
| Battery poll rate (idle-aware) | **New in dev4** — Aggressive: 120s, BadgeMax: 180s |
| Loop delay increase while idle | **Already implemented** + BadgeMax 600ms new in dev4 |
| E-ink static between renders | **Already at zero power** — no action needed |
| Light sleep (badge mode, timer wake) | **Experimental** — behind BadgeSleepMode::Light setting |
| Display/EPD power-down API | **Unsupported** — M5GFX does not expose this |
| Deep sleep | **Blocked** — touch wake unverified on PaperS3 |
| SD deinitialization | **Not needed** — SD is idle between ops; dequeue risky |
| Serial log suppression (Aggressive idle) | **Not yet implemented** — identified as safe-to-optimize |
| 40 MHz CPU | **Unsupported** — ESP32-S3 Arduino minimum is 80 MHz |

---

## Known Risks

1. **`gPowerLabButton` vs `gPowerAuditButton`** — Debug screen now uses `gPowerLabButton`. `renderPowerAudit()` still exists but is unreachable from the UI. If Power Audit is needed directly, it can be re-wired to a footer button inside Power Lab.
2. **Badge screen** — PowerLab is eligible for idle scaling but navigating to it always restores CPU before render. History captures this.
3. **Input lock stuck** — `gInputLocked` with `gInputUnlockAtMs == 0` means an in-flight render; `updateInputLock()` clears it once `finishDisplayRefresh()` fires and the timer expires. No stuck-lock timeout needed in normal operation.
4. **Deep sleep** — blocked; if enabled experimentally it resets all RAM/state including Power Lab counters.
5. **Light sleep USB wake** — USB CDC traffic will wake from light sleep (ESP32-S3 behavior). This is safe; `gLastWakeReason` will show "uart" or similar.

---

## UX Decisions Unchanged from v5.8-dev3

| Decision | Value |
|----------|-------|
| Practice header layout | unchanged (Layout Batch A passed) |
| Coach margin | kCoachMargin = 20 (unchanged — layout frozen) |
| Default power profile | Balanced (60s threshold) |
| Deep sleep | blocked |
| Profile cycling | Debug screen + Power Lab footer |
