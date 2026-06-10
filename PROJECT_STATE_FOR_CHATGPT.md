# PaperBadge Project State — v5.8-dev3-power Handoff

_Last updated: 2026-06-10_

---

## Git / Remote Status

| Item | Value |
|------|-------|
| HEAD | `5842b65` |
| Branch | `main` |
| Remote | `https://github.com/growwithsdtr/paperBadge.git` |

---

## Firmware

- **Version:** `v5.8-dev3-power` (`src/main.cpp:20`)
- **Build:** SUCCESS — RAM 49.4% · Flash 47.4% · 11.06 s
- **Upload:** SUCCESS — `/dev/tty.usbmodem1101`, 28.35 s

---

## Why CPU Scaling Was Not Visible Before (Root Cause)

`prepareFullRefresh()` (called at the top of every render function including `renderPowerAudit()`) calls `restoreActiveCpu()` before drawing. This reset `gIdleCpuScaled = false` and set CPU back to 240 MHz **before** the Power Audit UI ever read those values. Even if the CPU had been at 80 MHz for 5 minutes, the screen always showed "240 MHz [active]".

The fix: persist diagnostic state that survives the restore.

---

## What Changed in v5.8-dev3-power

### New Persistent Diagnostic Globals

| Variable | Type | Purpose |
|----------|------|---------|
| `gLastIdleScaledAtMs` | `uint32_t` | `millis()` when CPU last entered 80 MHz |
| `gLastRestoreAtMs` | `uint32_t` | `millis()` when CPU last restored to 240 MHz |
| `gLastRestoreReason` | `String` | Why restore happened ("input", "display refresh", etc.) |
| `gPreRenderCpuMhz` | `uint32_t` | CPU MHz captured before `prepareFullRefresh()` runs |
| `gPreRenderWasIdleScaled` | `bool` | Was CPU idle-scaled before this render began |

### Updated Functions

- **`restoreActiveCpu()`** — now records `gLastRestoreAtMs` and `gLastRestoreReason` whenever it actually fires
- **`maybeScaleIdleCpu()`** — now records `gLastIdleScaledAtMs` on successful scale
- **`renderPowerAudit()`** — captures `gPreRenderCpuMhz` / `gPreRenderWasIdleScaled` BEFORE calling `prepareFullRefresh()`

### New Helpers

- **`idleScaleBlockedReason()`** — returns human-readable string explaining why idle scaling can't fire now, or `""` if eligible
- **`msSinceIdleScaled()`** — returns "Xs ago" / "never" based on `gLastIdleScaledAtMs`

### Power Audit Page 2 (CPU Diagnostics) — Redesigned

Now shows:

```
CPU now: 240 MHz [active]
Pre-render: 80 MHz [was idle-scaled]    ← or "was active"
Profile: Balanced  threshold: 60s
Last scaled: 73s ago                    ← or "never"
Last restore: display refresh           ← or "input", etc.
Since input: 68012ms / 60000ms threshold
Eligible: yes  idle mode: active        ← or "Idle blocked: reason"
Loop delay: 250ms  refreshes: N
```

- "CPU now" always shows 240 MHz (render restores it — correct behavior)
- "Pre-render" is the honest signal: shows what state the CPU was in when this render was triggered
- "Last scaled / Last restore" survive across renders so history is never lost

### Debug Screen — Profile Cycling Restored

Added `Profile: <name>` button (bottom-right, alongside Touch Debug) to the Debug screen. Previously profile cycling was only in the now-cleaned Power Audit footer. Tapping cycles Balanced → Aggressive → Badge Max → Balanced and saves to prefs.

---

## How to Test Idle Scaling

### Balanced Profile (default, 60s threshold)

1. Navigate to Debug → Power Audit → page 2
2. Do not touch the screen
3. Wait **65 seconds**
4. Tap "Next >" to advance to page 3 and back to page 2 (any tap refreshes the display)
5. Check "Pre-render": should say **"80 MHz [was idle-scaled]"**
6. "Last scaled" shows how many seconds ago
7. "Last restore" shows "Next tap" or "display refresh"

### Aggressive Profile (25s threshold)

1. Debug screen → tap "Profile: Balanced" until it shows "Profile: Aggressive"
2. Navigate to Power Audit page 2
3. Wait **30 seconds**
4. Tap to refresh — "Pre-render" should show "80 MHz [was idle-scaled]"

### Badge Max Profile (20s threshold, Badge screen only)

1. Set profile to Badge Max via Debug screen
2. Navigate to Home → Badge screen
3. Wait **25 seconds**
4. Return to Debug → Power Audit page 2 to see history

---

## Profile Switching Access

| Location | How to access |
|----------|--------------|
| Debug screen | "Profile: Balanced/Aggressive/Badge Max" button (bottom-right area) |
| Power Audit (removed in dev2) | Footer toggle buttons removed — use Debug screen instead |

---

## Power Audit Pages Summary

| Page | Content |
|------|---------|
| 1 | Battery, USB, Charge, Wi-Fi/BT, Power mode, Profile |
| 2 | CPU diagnostics with full history (new in dev3) |
| 3 | Sleep mode, Last sleep, Wake reason, Since input |
| 4 | Answer keys, Touch coords, Deck info, Firmware version |

---

## Current UX Decisions (v5.8-dev3-power)

| Decision | Value |
|----------|-------|
| Practice header line 1 | id &#124; Must/Card &#124; compact section |
| Practice header line 2 | compact synthesized title |
| Practice header divider | yes, at y=84 |
| Page count in header | no |
| Option button font | regular (non-bold) |
| Settings "Advanced" hint | removed |
| Power Audit footer | 3-button: prev / home / next |
| Profile cycling | Debug screen bottom-right button |
| Ghosting on card change | clean refresh forced |
| Default power profile | Balanced (60s threshold) |
| Deep sleep | blocked (touch wake unverified) |
| CPU scaling history | persistent across renders (new) |

---

## Power Profiles

| Profile | Threshold | Eligible screens | Notes |
|---------|-----------|-----------------|-------|
| Balanced | 60 s | All static screens | Default, safe |
| Aggressive | 25 s | All static screens | Scales sooner |
| Badge Max | 20 s | Badge screen only | Badge-first |

All profiles: CPU 240→80 MHz on static screens after threshold. Restore to 240 before any refresh/input/SD. No deep sleep.

Static screens eligible: InterviewPractice, Glossary, Results, Settings, Debug, PowerAudit, VisualQA, HelpLegend, FontLab.

---

## Known Risks

1. **Badge screen** — PowerAudit is eligible for idle scaling but rendering it always restores. History now captures this.
2. **Loop delay at idle** — 250ms (Balanced) / 400ms (Aggressive) during idle. Screen does not live-update; user must tap to see current history.
3. **Light sleep** — Light sleep experiment active only in Aggressive/BadgeMax if enabled via Settings. Not default.
4. **USB/charging** — does not block idle scaling; user sees charging state on page 1.

---

## Next Physical QA Checklist

1. **Power Audit page 2 — never scaled:** open fresh, verify "Pre-render: 240 MHz [was active]" and "Last scaled: never"
2. **Power Audit page 2 — Balanced idle:** wait 65s on any eligible screen with no touch, then open Power Audit page 2. Verify "Pre-render: 80 MHz [was idle-scaled]" and "Last scaled: Xs ago"
3. **Power Audit page 2 — restore reason:** tap to refresh; "Last restore" should show "display refresh" or "Next tap" (not "active")
4. **Since input counter:** verify it counts up from last touch correctly
5. **Eligible / blocked:** verify "Eligible: yes" on PowerAudit page, and "Idle blocked: BadgeMax: not Badge screen" on BadgeMax profile + non-badge screen
6. **Debug screen Profile button:** verify it shows current profile name, tap cycles correctly, persists after reboot
7. **Build/upload:** no regression — practice, drill, exam, settings, badge all functional

---

## Remaining Known Issues

1. **Power cycling observable only after tap** — screen doesn't live-update; user must tap to see "Pre-render" state. This is acceptable (avoids redraw loop).
2. **Profile switching from Power Audit** — only via Debug screen now (Power Audit footer cleaned in dev2).
3. **SD deck-dump Best: letter** — unguarded `'A' + item.correctIndex`. Low priority.
4. **Japanese / UTF-8** — no CJK font. Out of scope.
5. **Deep sleep** — blocked (touch wake unverified on PaperS3).
6. **Light sleep** — experiment only in Aggressive/BadgeMax if enabled.
