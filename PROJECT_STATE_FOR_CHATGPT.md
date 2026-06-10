# PaperBadge Project State — v5.7 Handoff

_Last updated: 2026-06-10_

---

## Git / Remote Status

| Item | Value |
|------|-------|
| HEAD | `6b8171b` — v5.7 refine typography margins and settings power UX |
| Branch | `main` |
| Remote sync | **In sync** — pushed to origin/main |
| Remote | `https://github.com/growwithsdtr/paperBadge.git` |

---

## Firmware

- **Version:** `v5.7` (`src/main.cpp:20`)
- **Build:** SUCCESS — RAM 49.4% · Flash 47.3% · 10.80 s
- **Upload:** SUCCESS — `/dev/tty.usbmodem1101`, 28.3 s

---

## What Changed in v5.7

### Phase 1–5 — Typography (bold labels, regular body)

Added `applySansFont()` → regular FreeSans (9/12/18/24pt7b, already in M5GFX).  
Added `applyBodyFont()` / `applyCoachBodyFont()` — same size slot as content font but uses regular instead of bold.

**Where regular body font now applies:**
- `appendGlossaryWrappedBody()` — Glossary Definition / Why it matters / Example body text
- `drawGlossaryTermPage()` — `GlossaryLineKind::Body` rendering
- `drawFeedbackPage()` — feedback body lines (Selected value, Best value, Why explanation)
- `renderInterviewPracticeScreen()` — Answer, Anchor, Watch-out stages
- `practicePageCountsFor()` — uses body font for spoken/anchor/watch page-count measurement

**Still bold:** Question prompt, option buttons, section labels (Selected / Best / Why this is best / Definition / etc.), headers, titles, footer buttons.

### Phase 2 — Confidence wrapping

The single-line `"Confidence: ..."` overflow is fixed.  
Now renders: **"Confidence"** label (metadata bold) + wrapped body lines (regular body font) below it.  
Each line is checked against `footerY - 4` before rendering — no clip.  
If no vertical space, the block is skipped rather than clipped.

### Phase 6 — Tighter margins

| Item | Before | After |
|------|--------|-------|
| `kCoachMargin` | 34 | 28 |
| `practiceLayoutFor contentX` | 38 | `kCoachMargin` (28) |
| `practiceLayoutFor contentW` | `width - 76` | `width - kCoachMargin*2` (width - 56) |

Headers and body are now aligned at x=28 (was 34/38). Body is 20px wider total.

### Phase 7 — Settings power simplified

**Settings screen** no longer shows the two cycling power buttons.  
Shows: `Power: Battery Saver  (change in Debug > Power Audit)`

**Power Audit screen** gains two new cycling buttons above Home:
- `Power: <mode>` — taps cycle through Normal / Battery Saver / Conference Badge
- `Sleep: <mode>` — taps cycle through Off / Light / DeepExperiment

Stored preferences are unchanged (same Preferences keys, same save/load logic).

### Phase 8 — Power audit sanity (verified, no changes needed)

Confirmed in firmware:
- Wi-Fi: `WiFi.mode(WIFI_OFF)` at boot
- Bluetooth: never started
- IMU: disabled (Debug screen: "imu off")
- Speaker: stopped (Debug screen: "spk stopped")
- Battery polling cached at `kPowerPollIntervalMs = 45000 ms`
- CPU idle scaling: tracked via `gIdleCpuScaled` flag (Power Audit shows status)
- No periodic redraw on static screens: redraw only triggered by input events / badge language timer

---

## Recommended Settings (for QA and normal use)

| Setting | Value |
|---------|-------|
| Font size | Reader M |
| Font style | Sans Bold-like |
| Refresh mode | Balanced refresh |
| Power | Battery Saver (set in Debug > Power Audit) |

---

## Font Summary

- **English body text:** `FreeSans9/12/18/24pt7b` (regular, non-bold) — added in v5.7 for body roles
- **English labels/titles:** `FreeSansBold9/12/18/24pt7b` — all metadata, labels, headers, question prompts, option buttons
- **Sans Bold-like and High Contrast** both resolve to FreeSansBold for labels and FreeSans (regular) for body
- **LargeReader** same: FreeSansBold labels, FreeSans body
- **DebugMono** uses FreeMonoBold throughout
- **Future Japanese:** `M5GFX lgfxJapanGothic_*` or efont CJK — **not** FreeSans (no CJK). Requires UTF-8-safe sanitizer before enabling.

---

## Remaining Known Issues

1. **SD deck-dump Best: letter** — unguarded `'A' + item.correctIndex` at `src/main.cpp:4049` in debug SD dump path. Low priority.
2. **`docs/embedded_deck_dump.md` stale** — stale notice added. Regenerate after next device QA session with SD dump.
3. **Japanese / UTF-8** — no CJK font, no UTF-8 sanitizer. Out of scope.
4. **Dynamic deck categories** — firmware-hardcoded; new grids require firmware change.
5. **SRS / long-term history** — not implemented.
6. **Glossary search** — not implemented.
7. **Generic stages array** — still hardcoded.
8. **Deep sleep** — remains experimental/debug-only; wake reliability not confirmed.

---

## Next QA Photo Checklist (v5.7, max 10 photos)

1. Practice A04 Question page — confirm Confidence shows as label + wrapped body (not clipped)
2. Practice Answer page (any long card) — confirm regular/non-bold body text, paragraph spacing
3. Drill Weak Answer A01 — question + options at Reader M
4. Drill feedback page — Selected label (bold) / body (regular) / Best label (bold) / body (regular) / Why (regular)
5. Glossary term page — Term bold, Definition/Why/Example labels bold, body text regular
6. Settings screen — confirm no power cycling buttons; shows `Power: Battery Saver` status text
7. Debug > Power Audit — confirm `Power: <mode>` and `Sleep: <mode>` cycling buttons above Home
8. Exam results screen — confirm layout unchanged
9. Badge English render
10. Debug Power Audit screen (full audit rows)

---

## Out of Scope (do not implement without explicit sign-off)

- Japanese/N3 content schema
- UTF-8 sanitizer overhaul
- External font loading
- SRS / spaced repetition
- New drill or card content
- Broad UI redesign
- Deep sleep by default
