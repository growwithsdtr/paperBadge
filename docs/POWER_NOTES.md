# Power Notes

## Current Strategy

PaperBadge/PaperCoach uses e-ink as a static display whenever possible. Badge mode renders once and holds the image without periodic refresh. Coach modes redraw only on explicit navigation or mode changes.

## Current UX Decisions

- Use outlined controls instead of large black-filled buttons to reduce ghosting.
- Use Reader M, High Contrast, and Balanced refresh as the default QA baseline.
- Force clean refreshes for major transitions, feedback, badge, and QR/photo zoom.
- Do not force clean refreshes for every ordinary page turn.
- Keep Badge language on Manual toggle and Auto interval Off for static conference use.
- Do not enable deep sleep by default until PaperS3 wake behavior is physically verified.

## Implemented

- Wi-Fi is disconnected and set to `WIFI_OFF`.
- Bluetooth is stopped with `btStop()`.
- Speaker output is stopped.
- IMU polling is disabled in `M5.config()`.
- Badge language auto-rotation is disabled in Conference Badge power mode.
- Battery, charge, and VBUS reads are cached for 45 seconds.
- Render code logs repeated same-screen redraw warnings.
- Debug -> Power Audit shows voltage, percent, USB/VBUS, charge state, Wi-Fi, Bluetooth, IMU, speaker, CPU frequency, power mode, Badge sleep setting, last sleep attempt, wake reason, boot millis, refresh count, badge redraw count, last refresh reason, and last input time.
- Power Audit reports sleep as off, light experiment enabled, or deep blocked.
- Badge sleep setting supports Off, Light, and Deep experiment.

## Badge Sleep

- Off: no sleep behavior beyond the normal loop delay policy.
- Light: after static Badge mode sits idle for about 30 seconds, firmware enters short timer-based light sleep cycles. Touch wake is not used for Light mode.
- Deep experiment: selectable, but blocked in firmware until PaperS3 touch wake is physically verified. The audit screen and serial log report the block.

## Experimental / Not Yet Enabled

- Deep sleep with touch wake.
- Long-duration light sleep with touch wake.
- Persistent low-power state across all Coach modes.
- PMIC-specific power-off behavior.

## Must Be Physically Verified

- Whether PaperS3 touch interrupt can safely wake from deep sleep.
- Whether light sleep disrupts USB serial, SD, or touch responsiveness on the actual device.
- Whether conference badge mode remains wakeable after extended idle.
- Whether battery percentage from M5Unified is reliable enough or voltage-only fallback is preferable.

## Why E-Ink Helps

The e-paper panel holds its last image without continuous redraw. Once the badge is rendered, the display does not need a refresh loop to remain visible. Power savings therefore come from preventing unnecessary CPU wakeups, radio/peripheral activity, and repeated screen refreshes, not from redrawing the badge.
