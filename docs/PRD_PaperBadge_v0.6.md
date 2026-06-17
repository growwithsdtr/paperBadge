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

## Current Behavior (v5.9-dev2)

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
| Home | Hub: 8 navigation buttons (Badge, Practice, Drills, Exam, Glossary, Results, Settings, Japanese) |
| Practice / InterviewPractice | Multi-page practice card reader |
| Drills / DrillsMenu | MCQ drill selection and answer flow |
| Exam | 5- or 10-question timed exam with scoring |
| Glossary / GlossaryMenu | Paged term viewer by category |
| Results | Accuracy stats, weak areas, recent history (Interview Practice/Drills/Exam only) |
| Settings | Reader size, refresh mode, power profile, orientation |
| Advanced | Typography lab, power diagnostics, sleep controls |
| Power Lab | 4-page CPU/battery/sleep diagnostics |
| Japanese / JapaneseMenu | Self-contained N3-sample mode: Daily Questions, Mock Test, Reference, Results |
| JapaneseDaily | One embedded Week 1 Day 1 question per screen, 4 choices, immediate feedback |
| JapaneseReference | Grammar/vocab/kanji tags from the Week 1 Day 1 set |
| JapaneseResults | RAM-only answered/correct tally, by kanji/vocabulary/grammar |
| JapaneseMockTest | Placeholder only; no full test flow yet |

### Settings

Three user-facing controls on the Settings screen:

**Reader size** (S / M / L) — controls body font size in **study content screens**: Practice,
Drills, Exam, Glossary, and (from v5.9-dev2) Japanese Daily Questions and feedback.

App chrome (Settings, Advanced, Power Lab, Japanese menu/Reference/Results, and all other control
screens) uses fixed medium fonts independent of Reader size.

**English interview content (Practice/Drills/Exam/Glossary):**

| Size | Body px | Option text | 1-line box | 2-line box |
|------|---------|-------------|-----------|-----------|
| S | 24 | 24px | 60px | 84px |
| M | 31 | 31px | 70px | 106px |
| L | 40 | 36–40px | 74px | 124px |

**Japanese Daily Questions content (v5.9-dev2):**

| Reader | Japanese body | Japanese title | Button height |
|--------|--------------|---------------|--------------|
| S | 24px Gothic | 28px Gothic | 76px |
| M | 28px Gothic | 32px Gothic | 86px |
| L | 32px Gothic | 36px Gothic | 96px |

English labels inside Japanese screens (feedback title, English meaning, Results stats, Reference
headers) use Sans Bold directly at fixed compact sizes, independent of FontStyleMode. Japanese
text (prompt, choices, answer sentence, explanation) uses `lgfxJapanGothic_*` fonts and the
Japanese-safe wrap path.

Option boxes (English drills) snap to discrete tier heights (1-line / 2-line / 3-line). All options
on a single screen share the max tier. Text is centered vertically inside each box. Reader S/M/L
affects study screens only — Settings uses fixed medium font.

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

### Option Box Height — Tier-Based, Shared, Centered (v5.8-dev16, unchanged in dev17)

All answer option boxes on a single question screen share the same height tier (1-line / 2-line /
3-line). The tier is determined by the most-wrapped option. Text is vertically centered in each box.
`sharedOptionButtonHeight` applies in all paths: combined, options-only, result view, and Exam.

### Drill Post-Answer Navigation (v5.8-dev17)

After a drill answer is selected, feedback is shown immediately. The state machine:

1. **Feedback page** — shown immediately after answer tap. Shows "Selected", "Best", "Why this
   is best". Paginates if explanation is long.
2. **Result view** — shows question + all options with selected/correct highlighting. Paginates
   using `gCoachStage` with the same fit-aware plan as pre-answer.

Transitions:
- Option box tap (before answer): selects answer → **immediately goes to feedback (page 0)**
- Top-half tap on feedback page 1 → back to result view at `gDrillLastResultPage`
- Top-half tap on feedback page 2+ → previous feedback page
- Top-half tap on result view (page 0) → no-op (does NOT navigate to previous item)
- Top-half tap on result view (page 1+) → previous result page
- Bottom-half tap on result view (not last page) → advance to next result page
- Bottom-half tap on result view (last page) → save `gDrillLastResultPage`, enter feedback at page 0
- Bottom-half tap on feedback (last page) → next drill item
- Footer arrow buttons (← →): advance to next/previous drill item from any state

### Feedback and Body Text Formatting (v5.8-dev17)

`formatFeedbackBody(text)` breaks feedback and practice body text at structural boundaries only:
- Numbered list items (`1. ` / `1) `) start on their own line
- Semicolon-separated clauses (2+ `; ` → one clause per line)
- Colon-label patterns: only fires for known labels (`Q`, `A`, `Question`, `Answer`, `Problem`,
  `Fix`, `Result`, `Selected`, `Best`, `Why`, `Risk`, `Action`, `Example`, `Note`) with 2+ present.
  Random prose labels (`outcome:`, `output:`) are not split.
- Hyphen-list splitting removed: `A - B - C` prose stays on one line.
- Prose, short phrases, decimal numbers, and URLs are unaffected.

Applies to: drill feedback sections, practice card body sections (Answer, Suggested response,
Explanation, Anchor, Follow-up), and hostile follow-up answer stages in `buildCoachReaderStages`.

### Hostile Follow-up — "Suggested response" stage (v5.8-dev16)

In `buildCoachReaderStages`, `CoachItemType::HostileFollowup` answer stage renamed from "Defense"
to "Suggested response". Answer body passes through `formatFeedbackBody` before page building.
Matches the label already used in `buildPracticeLines` for the Practice view.

### Results Pages — Fit-Based Combined Summary+Categories (v5.8-dev16)

`resultsCombinedFirstPage()` uses a measured fit check instead of a raw count comparison:
- Condensed summary block: 270px
- Per-category worst case (2-line label): 126px
- Available height: 742px
- For ≤3 categories: 648px needed < 742px → combine

Page count: 3 pages for ≤3 categories; 4 pages for 4–6; 5 pages for 7–8.

### Settings Page — Fixed Medium UI (v5.8-dev15/dev16)

Settings uses a consistent fixed-medium font style independent of Reader S/M/L:

| Element | Font size | Notes |
|---------|-----------|-------|
| Screen title | 40px (title) | unchanged |
| Battery % | 40px (title) | large, vertically centered with bar |
| mV / USB detail | 28px | readable secondary info |
| Section labels | 28px | `kSettingsLabelPx` — above metadata |
| Segmented buttons | 24px | `kSegmentedPx` — all buttons including Advanced |
| Selected state | triple border + bold text | no `*`, no fill |
| Button height | 52px | better touch targets |

Full labels: Reader: S / M / L — Refresh: Fast / Balanced / Clean — Power: Responsive / Balanced / Max — Orientation: Normal / Strap

**Deep sleep:** Permanently blocked — the GT911 touch controller INT is on GPIO48, which is not
in the ESP32-S3 RTC GPIO range (0–21). No verified deep sleep wake source exists.

### Display Refresh

E-ink screens require a "clean" (full) refresh periodically to prevent ghosting. Fast and Balanced
modes use a cadence counter to limit how often full refreshes happen during navigation. Clean mode
always uses a full refresh.

Badge and image screens always use a full refresh on entry.

---

## Near-Term Roadmap

### Japanese Support — Daily Questions prototype (v5.9-dev1, done)

**Goal:** Add a safe, isolated Japanese-language study mode without touching existing English
PaperCoach behavior, Badge behavior, or deep sleep.

**Delivered in v5.9-dev1:**

1. **UTF-8 text sanitizer** — `sanitizeJapaneseText()` preserves UTF-8 verbatim (only strips ASCII
   control characters other than `\n`/`\t`). It is intentionally separate from the existing
   `sanitizeCoachText()`, which is ASCII-only and would replace Japanese glyphs with `?`.

2. **CJK word wrap** — `wrapJapaneseTextToLines()` wraps by UTF-8 code point (via
   `utf8SequenceLength()`) rather than splitting on spaces, and respects explicit `\n`. It does not
   modify the existing space-based `wrapTextToLines()` used by English content.

3. **Japanese font path** — `applyJapaneseTitleFont()`/`applyJapaneseBodyFont()` select
   `lgfxJapanGothic_*` sizes via the existing `applyGothicFont()` helper. No SD font file is
   required; this is firmware-embedded, in keeping with the existing embedded-first asset policy.

4. **Embedded-only content** — one originally written N3-style sample set (Week 1, Day 1; 11
   items covering もじ/ごい/ぶんぽう) is embedded directly in firmware (`kJapaneseDayItems`), not
   loaded from SD. This is intentionally narrower than a generic SD deck schema.

5. **Separate Results** — `JapaneseSessionResult`/`gJapaneseResults` is RAM-only and fully separate
   from `SessionResult`/`gSessionResults`; Japanese answers never appear in the existing Results
   screen.

**Out of scope for this milestone (unchanged):**
- Importing the full 新にほんご500問 book
- SRS / spaced repetition
- Volunteer notes
- Multi-source concept UI
- Furigana or ruby text support
- A full Mock Test flow (placeholder only)
- SD-loadable Japanese deck schema and the `badgeLanguage` deck-selection extension (still future
  work — see below)

### Japanese Deck Support — SD-loadable decks (future milestone, not started)

**Goal:** Allow PaperBadge to load and display Japanese-language content from an SD card deck, on
top of the v5.9-dev1 embedded-only foundation, without breaking existing English content.

**Requirements:**

1. **Generic deck schema** — the current embedded deck schema is English-centric (fields: `prompt`,
   `answer`, `rubric`, `options`). The SD deck loader should accept an equivalent schema that
   doesn't require English field naming. Document the schema in `CONTENT_SCHEMA.md`.

2. **Language toggle** — the existing `badgeLanguage` setting (English/Japanese/Auto) already
   controls badge image selection. Extend it to also select which deck is loaded on boot.

**Out of scope for this milestone:**
- In-firmware Japanese font embedding (binary size concern) — already satisfied by the embedded
  `lgfxJapanGothic_*` path from v5.9-dev1
- Furigana or ruby text support
- Auto-detection of content language

**Blocked until:**
- Generic deck schema is documented and implemented
- SD deck loader for Japanese content is implemented

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
