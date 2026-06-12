# PaperBadge PRD — v0.6

_Product requirements for the PaperBadge firmware, current state and near-term roadmap._

---

## Product Overview

PaperBadge is firmware for the M5Stack PaperS3, an ESP32-S3 device with a 540×960 e-ink display and
capacitive touch screen. It serves two roles simultaneously:

1. **Conference badge** — shows a personal badge image (name, photo, QR code) that persists while
   the device is worn or displayed at a desk.
2. **Interview prep tool (PaperCoach)** — a full-featured flashcard and quiz system built into the
   same device, navigable with taps and long presses.

The device runs on battery and is carried to conferences. Battery life during extended wear is a
first-class concern.

---

## Current Behavior (v5.8)

### Badge Mode

The badge screen shows a full-screen PNG image loaded from SD or embedded in firmware. Two badge
images are supported (English and Japanese). Auto-rotate is configurable (Off, 15s, 30s, 60s).

Badge orientation: **Strap** (180° rotation for lanyard wear) or **Handheld** (normal portrait).
The setting persists across reboots.

### PaperCoach — Content

PaperCoach content is embedded in firmware and optionally loaded from SD card:

| Type | Count (embedded) | Description |
|------|-----------------|-------------|
| Practice cards | 71 | Interview Q&A with prompt, answer, rubric |
| Drill cards | 149 | MCQ drills with 4 options and feedback |
| Glossary terms | 44 | AI/ML/PM terminology with definitions |

Content is organized into categories. Practice cards include a Must Master subset of 17 cards.

### PaperCoach — Screens

| Screen | Purpose |
|--------|---------|
| Home | Hub: 7 navigation buttons |
| Practice / InterviewPractice | Multi-page practice card reader |
| Drills / DrillsMenu | MCQ drill selection and answer flow |
| Exam | 5- or 10-question timed exam with scoring |
| Glossary / GlossaryMenu | Paged term viewer by category |
| Results | Accuracy stats, weak areas, recent history |
| Settings | Reader size, refresh mode, power profile, orientation |
| Advanced | Typography lab, power diagnostics, sleep controls |
| Power Lab | 4-page CPU/battery/sleep diagnostics |

### Settings

Three user-facing controls on the Settings screen:

**Reader size** (S / M / L) — controls body font size in Practice, Drills, Exam, Glossary.
Also scales option button text size in MCQ drills and exams.

| Size | Body px | Description |
|------|---------|-------------|
| S | 24 | Compact; more text per page |
| M | 31 | Default; comfortable reading |
| L | 40 | Large print; fewer items per page |

**Refresh mode** (Fast / Bal / Clean) — controls e-ink refresh cadence.

| Mode | Cadence |
|------|---------|
| Fast | Clean refresh every ~16 transitions |
| Bal | Clean refresh every ~10 transitions |
| Clean | Clean refresh on every transition |

**Power profile** (Resp / Bal / Max) — controls idle and sleep aggressiveness.

| Profile | WarmIdle threshold | LightNap threshold | Nap duration |
|---------|---------------------|-------------------|--------------|
| Resp (Responsive) | 30s | Disabled | — |
| Bal (Balanced) | 15s | 10 min idle | 12s |
| Max (Max Battery) | 5s | 5 min idle | 15s |

All settings persist across reboots via ESP32 Preferences (NVS).

### Power Management

**CPU scaling (WarmIdle):** When the device is idle on a static screen, CPU frequency drops from 240
MHz to 80 MHz after the profile's WarmIdle threshold. Restores to 240 MHz before any display
refresh.

**LightNap:** After extended idle on eligible content screens (Badge, Home, Practice menus, Drills,
Exam, Glossary, Results), the device enters a 2-second timer-based light sleep cycle. Each cycle
saves ~2s of full CPU active time. Short taps during the nap window are missed; a sustained tap
(~2s hold) is detected.

LightNap is blocked (never entered) when:
- Sleep mode is Off (resets on every reboot)
- Power profile is Responsive
- Screen is a control/diagnostic screen (Settings, Advanced, Power Lab, etc.)
- An exam question or MCQ drill option is awaiting a tap (answer-selection guard)
- Input lock is active (including the 400ms post-wake debounce window)

**Deep sleep:** Permanently blocked — the GT911 touch controller INT is on GPIO48, which is not
in the ESP32-S3 RTC GPIO range (0–21). No verified deep sleep wake source exists.

### Display Refresh

E-ink screens require a "clean" (full) refresh periodically to prevent ghosting. Fast and Balanced
modes use a cadence counter to limit how often full refreshes happen during navigation. Clean mode
always uses a full refresh.

Badge and image screens always use a full refresh on entry.

---

## Near-Term Roadmap

### Japanese Deck Support (next milestone)

**Goal:** Allow PaperBadge to load and display Japanese-language content from an SD card deck,
without breaking existing English content.

**Requirements:**

1. **UTF-8 text sanitizer** — strip or replace characters not renderable by the current GFX bitmap
   font stack. Prevents garbage rendering of multi-byte sequences.

2. **CJK word wrap** — the existing `wrapReaderTextToLines` function splits on spaces, which does
   not work for Japanese (no spaces between words). Need character-boundary wrapping as fallback
   when no space-break candidates exist within the content width.

3. **Japanese font path** — define an SD path convention (e.g., `/paperbadge/fonts/jp_body.vlw`)
   for a VLW-format Japanese font. Fallback to embedded ASCII font when font is not present.

4. **Generic deck schema** — the current embedded deck schema is English-centric (fields: `prompt`,
   `answer`, `rubric`, `options`). The SD deck loader should accept an equivalent schema that
   doesn't require English field naming. Document the schema in `CONTENT_SCHEMA.md`.

5. **Language toggle** — the existing `badgeLanguage` setting (English/Japanese/Auto) already
   controls badge image selection. Extend it to also select which deck is loaded on boot.

**Out of scope for this milestone:**
- In-firmware Japanese font embedding (binary size concern)
- Furigana or ruby text support
- Auto-detection of content language

**Blocked until:**
- UTF-8 sanitizer is implemented
- CJK word wrap is implemented
- Japanese font file is available on SD

### Other Items (backlog)

- **GT911 touch INT wake research** — if an alternate GPIO mapping for the touch controller is
  found that falls in the RTC range, deep sleep becomes possible.
- **Deck update flow** — UI for reloading a deck from SD without rebooting.
- **Results export** — write session_results.json to SD for offline review.

---

## Non-Goals

- Bluetooth or WiFi connectivity during normal badge use (radios disabled at boot for power)
- Cloud sync or remote content updates
- Streaming audio or video
- Multi-user / shared device mode
