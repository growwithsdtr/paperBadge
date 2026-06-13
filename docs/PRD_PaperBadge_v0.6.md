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

## Current Behavior (v5.8-dev15)

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

**Reader size** (S / M / L) — controls body font size in **study content screens only**: Practice,
Drills, Exam, and Glossary. Also scales option button text in MCQ drills and exams.

App chrome (Settings, Advanced, Power Lab, and all other control screens) uses a fixed medium font
independent of Reader size. Section labels and button text in Settings do not change with Reader size.

| Size | Body px | Option text | Description |
|------|---------|-------------|-------------|
| S | 24 | 24px (Gothic_24) | Compact; more text per page |
| M | 31 | 31px (Gothic_28) | Default; comfortable reading |
| L | 40 | 36px (Gothic_36), 32px fallback | Large print; fewer items per page |

The fallback applies when a specific option label is too long to fit in 2 lines at the preferred size.
Only that individual label downgrades; other options on the same screen are not affected.

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

### Option Box Height Normalization (v5.8-dev15)

All answer option boxes on a single question screen share the same height — the maximum required
by any individual option on that screen. If option A needs 2 lines and B–D need 1, all four boxes
grow to the 2-line height so the layout looks visually consistent.

`sharedOptionButtonHeight(item, width)` computes the max, applied in all option drawing paths:
combined, options-only, result view (post-answer), and Exam.

### Drill Post-Answer Navigation

After a drill answer is selected, the selected option is immediately registered and the feedback
page is shown. The drill item then allows toggling between two internal sub-pages:

1. **Feedback page** — shown immediately after answer tap. Shows "Selected", "Best", "Why this
   is best" sections. Text with numbered lists or multiple semicolons is formatted to break at
   clause boundaries for readability. May paginate if the explanation is long.
2. **Result view** — accessible from feedback by tapping the top-half (page 1 only). Shows the
   question stem + all four option boxes with the selected and correct options highlighted (triple
   border + bold text).

Transitions:
- Option box tap (before answer): selects answer → **immediately goes to feedback**
- Top-half tap on feedback page 1 → return to result view
- Top-half tap on feedback page 2+ → previous feedback page
- Bottom-half tap on result view → feedback page
- Bottom-half tap on feedback page → next feedback page OR next drill item
- Footer arrow buttons (← →) still advance to the next or previous drill item at any point
- Home button returns to Home from either sub-page

### Feedback and Body Text Formatting (v5.8-dev15)

`formatFeedbackBody(text)` is applied to feedback and practice body text before wrapping:
- Numbered list items (`1. ` / `1) `) start on their own line
- Semicolon-separated clauses (2+ `;` → one clause per line)
- Prose, short phrases, decimal numbers, and URLs are unaffected

Applies to: drill feedback sections (Selected, Best, Why this is best), practice card body
sections (Answer, Defense/Suggested response, Explanation, Anchor, Follow-up).

### Results Pages — Combined Summary+Categories (v5.8-dev15)

When the session has ≤3 category stats, the Summary and Categories content is merged into a
single first page (condensed summary block + divider line + category bars). Separate pages are
only created when category stats exceed 3 (requiring a second category page).

Page count: 3 pages for ≤3 categories; 4 pages for 4–6; 5 pages for 7–8.

### Settings Page — Fixed Medium UI (v5.8-dev15)

Settings uses a consistent fixed-medium font style independent of Reader S/M/L:

| Element | Font size | Notes |
|---------|-----------|-------|
| Screen title | 40px (title) | unchanged |
| Battery % | 40px (title) | large, vertically centered with bar |
| mV / USB detail | 28px | was 24px (metadata) |
| Section labels | 28px | was 24px — now noticeably larger |
| Segmented buttons | 24px | was 20px — "Responsive" / "Balanced" fit |
| Selected state | triple border + bold text | no `*`, no fill |
| Button height | 52px | was 48px — better touch targets |

Full labels used for all controls: Fast / Balanced / Clean; Responsive / Balanced / Max; Normal / Strap.

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
