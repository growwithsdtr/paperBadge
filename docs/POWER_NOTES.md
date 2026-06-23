# Power Notes

## Current Strategy

PaperBadge/PaperCoach uses e-ink as a static display whenever possible. Badge mode renders once and holds the image without periodic refresh. Coach modes redraw only on explicit navigation or mode changes.

## Current UX Decisions

- Use outlined controls instead of large black-filled buttons to reduce ghosting.
- Use Reader M, Sans Bold-like or High Contrast, and Balanced refresh as the default QA baseline.
- Force clean refreshes for major transitions, feedback, badge, and QR/photo zoom.
- Do not force clean refreshes for every ordinary page turn.
- Keep Badge language on Manual toggle and Auto interval Off for static conference use.
- Do not enable deep sleep by default until PaperS3 wake behavior is physically verified.
- PaperS3 GT911 touch INT is GPIO48. It is usable as a light-sleep GPIO wake
  source but is not RTC-wake capable for deep sleep.

## Implemented

- Wi-Fi is disconnected and set to `WIFI_OFF`.
- Bluetooth is stopped with `btStop()`.
- Speaker output is stopped.
- IMU polling is disabled in `M5.config()`.
- Badge language auto-rotation is disabled in Conference Badge power mode.
- Battery, charge, and VBUS reads are cached for 45 seconds.
- Render code logs repeated same-screen redraw warnings.
- Static screens (`isStaticIdleScreen()`: Home, Practice/Glossary/Drills menus, Practice reading, Glossary term, Results, Settings, Advanced, and Debug-family screens) can enter a logged WarmIdle state after a per-profile threshold of inactivity (WarmIdle@30s Responsive / 15s Balanced / 5s Max Battery — see `profileIdleScaleThresholdMs()`). This slows the loop and scales CPU down but keeps touch polling active.
- Guarded idle CPU scaling (WarmIdle) can lower static idle screens to 80 MHz and restores 240 MHz on user activity, display refresh, SD writes, and debug/export work. As of v5.8-dev19, WarmIdle applies on Max Battery profile to every eligible static screen (previously gated to the Badge screen only — `idleScaleBlockedReason()`/`maybeScaleIdleCpu()` no longer carry a Max-Battery-only-Badge restriction).
- Home was added to the static-idle/WarmIdle screen set in v5.8-dev19 (it was already LightNap-eligible but never reached WarmIdle, so it stayed at 240 MHz indefinitely on all profiles, including Max Battery). Practice/Glossary/Drills menus were added alongside it — they are read-only navigation grids with no answer-timing concerns. Badge was deliberately left out of WarmIdle for this patch (LightNap-eligible only) to avoid touching static badge/auto-rotate behavior.
- Power Lab (Settings → Advanced → Power Lab) shows voltage, percent, USB/VBUS, charge state, Wi-Fi, Bluetooth, IMU, speaker, SD status, refresh mode, idle status, CPU frequency, whether WarmIdle is active and time remaining until it engages (or "blocked"/reason), LightNap eligibility/block reason/time remaining for the current screen, power mode, sleep mode/status, last sleep attempt, wake reason, boot millis, current screen name, display refresh count and age of the last refresh, badge redraw count, last refresh reason, answer-key validation status, and idle age since last input. Power Audit (the older single-page screen) is unreachable dead code, per v5.8-dev18.
- Power Lab and Power Audit now also show manager-backed wake/reset
  classification, RTC boot/app/reader state, SD index/book count, display
  partial-clean counters, heap/PSRAM, firmware sketch size, and the last
  `SleepManager` result.
- Badge sleep setting supports Off, Light, and Deep experiment.

## Badge Sleep

- Off: no sleep behavior beyond the normal loop delay policy.
- Light: after an eligible passive screen sits idle, firmware enters short light
  sleep cycles. Timer wake is always configured; on PaperS3, GT911 GPIO wake is
  attempted through GPIO48 and must be verified on the physical device.
- Deep experiment: selectable, but blocked in firmware until PaperS3 touch wake is physically verified. The audit screen and serial log report the block.

## Experimental / Not Yet Enabled

- Deep sleep with touch wake.
- Long-duration light sleep with verified touch wake.
- Persistent low-power state across all Coach modes.
- PMIC-specific power-off behavior.

## Must Be Physically Verified

- Whether PaperS3 GT911 GPIO wake from light sleep avoids wake loops and missed taps.
- Whether light sleep disrupts USB serial, SD, or touch responsiveness on the actual device.
- Whether conference badge mode remains wakeable after extended idle.
- Whether battery percentage from M5Unified is reliable enough or voltage-only fallback is preferable.

## Why E-Ink Helps

The e-paper panel holds its last image without continuous redraw. Once the badge is rendered, the display does not need a refresh loop to remain visible. Power savings therefore come from preventing unnecessary CPU wakeups, radio/peripheral activity, and repeated screen refreshes, not from redrawing the badge.
