# PaperBadge Project State — v5.6 Handoff

_Last updated: 2026-06-10_

---

## Git / Remote Status

| Item | Value |
|------|-------|
| HEAD | `589cbb1` — v5.6 fix drill keys feedback readability |
| Branch | `main` |
| Remote sync | **In sync** — pushed 2 commits (v5.5, v5.6) to origin/main |
| Remote | `https://github.com/growwithsdtr/paperBadge.git` |

---

## Firmware

- **Version:** `v5.6` (confirmed at `src/main.cpp:20` — `constexpr const char* kFirmwareVersion = "v5.6"`)
- **Build:** SUCCESS — RAM 49.4% · Flash 47.0% · 3.55 s
- **Upload:** SUCCESS — uploaded to `/dev/tty.usbmodem1101` (ESP32-S3, esptool 4.11, 28.5 s)

---

## Invalid Best: E/F Fix — Verified

All three layers are correct in v5.6:

### 1. Build tool (`tools/build_embedded_papercoach.py`)
- `embedded_options_for()` picks the best 4 options from 6-option source items, remapping `correct_index` via `keep_indices.index(source_correct)`.
- After-remap validation loop (lines 162–163) flags any item where `correct_index >= option_count` and reports it.
- Dry-run confirms **63 remaps, 0 invalid** after remap — all embedded drill correct indices are 0–3.

### 2. Embedded header (`src/embedded_papercoach_deck.h`)
- All `correctIndex` values are now in range `[0, optionCount)` — confirmed by build tool's zero-invalid output.

### 3. Runtime (`src/main.cpp`)
- `hasValidAnswerKey()` (line 4227–4229): returns true only if `correctIndex < optionCount && optionCount <= kMaxOptions`.
- `optionLetterOrDash()` (line 4232): returns `"-"` if `option >= optionCount`.
- `optionLabelWithSafeLetter()` (line 4239): returns `"- "` if `option >= optionCount`.
- Feedback page (line 4830): gates on `hasValidAnswerKey(item)` — shows `"-"` for invalid key, never a letter past D.
- Results `bestOption` (line 4584): set to `255` when invalid; display path (line 6329) shows `"-"` when `bestOption >= kMaxOptions`.
- Drills/Exam pool filter (line 2363): skips items where `!hasValidAnswerKey(item)`.
- SD session log (line 4538): `bestOption < kMaxOptions ? letter : "-"`.

**Conclusion: `Best: E` / `Best: F` cannot appear in feedback, results, or session logs in v5.6.**

### SD dump path caveat
The SD deck-dump function (line 4048–4049) does `'A' + item.correctIndex` without a validity guard — this path only runs when the operator explicitly triggers a deck dump to SD card (debug). It does not affect any user-facing screen.

### `docs/embedded_deck_dump.md`
**Marked stale.** This file was generated on-device pre-v5.6 and contains `Best: E/F` entries reflecting the old pre-remap indices. A stale notice has been prepended to the file. There is no Python path to regenerate it — it requires a device SD dump session. Regenerate only after next hardware QA session.

---

## Recommended Settings (for QA and normal use)

| Setting | Value |
|---------|-------|
| Font size | Reader M |
| Font style | Sans Bold-like |
| Refresh mode | Balanced refresh |
| Power | Battery Saver |

---

## Font Summary

- **English rendering:** Adafruit GFX bitmap buckets — `FreeSans` and `FreeSansBold` built into the library.
- **Sans Bold-like** and **High Contrast** both resolve to `FreeSansBold` at the glyph level; the difference is layout density and content weight, not the actual typeface.
- **Future Japanese:** Use `M5GFX` IPA Japanese fonts (`lgfxJapanGothic_*`) or efont CJK — **not** `FreeSans` variants, which have no CJK coverage.
- **Japanese support also requires** a UTF-8-safe text sanitizer; the current sanitizer is ASCII-path only.

---

## Remaining Known Issues

1. **SD deck-dump Best: letter** — unguarded `'A' + item.correctIndex` on line 4048–4049 of the dump-to-SD path. Low priority (debug feature, not user-facing).
2. **`docs/embedded_deck_dump.md` stale** — shows pre-remap `Best: E/F`; stale notice added. Must be regenerated on-device.
3. **Japanese / UTF-8** — no CJK font, no UTF-8 sanitizer. Out of scope for current sprint.
4. **Dynamic deck categories** — category grid is firmware-hardcoded; adding new category grids requires a firmware change.
5. **Category cap** — fixed at current count; increase needs firmware and schema work.
6. **SRS / long-term history** — not implemented.
7. **Glossary search** — not implemented.
8. **Generic stages array** — stages are still hardcoded; schema refactor deferred.

---

## Next QA Photo Checklist (v5.6, max 10 photos)

Per `docs/QA_GUIDE.md`:

1. Static Badge screen
2. Settings power row — `Conf. Badge` / Badge sleep labels
3. Practice large Answer page — confirm paragraph spacing
4. Weak Answer A01 question + choices at Reader M — confirm no `Best: E/F`
5. Metric Precision metric-01 question + choices at Reader M
6. A longer Exam question (+ options page if split)
7. Drill feedback page — Selected / Best / Why this is best blocks — confirm Best shows A–D or `-`
8. Help / Legend — confirm wrapped long lines
9. Results Recent misses page after at least one miss
10. Debug Power Audit screen

---

## Out of Scope (do not implement without explicit sign-off)

- Japanese/N3 content schema
- UTF-8 sanitizer overhaul
- External font loading (efont, lgfxJapanGothic, etc.)
- SRS / spaced repetition
- New drill or card content
- Broad UI redesign
