# PaperBadge / PaperCoach QA Guide

## Firmware Update Checklist

1. Build: `pio run`
2. Upload: `pio run -t upload --upload-port /dev/cu.usbmodem1101`
3. Smoke test: `UPLOAD=0 bash .claude/skills/run-paperbadge/smoke.sh`
4. Monitor briefly: `python3 .claude/skills/run-paperbadge/serial_capture.py --no-reset --tail 30`
5. Photograph the required screen batch below.

Port: `/dev/cu.usbmodem1101` (macOS). Use `cu.` prefix, not `tty.`.

## Physical QA After Each Firmware Update

- Use these settings unless the test says otherwise:
  - Reader M
  - Sans Bold-like or High Contrast
  - Balanced refresh
  - Battery Saver
  - Badge language Manual toggle
  - Auto interval Off

- Badge English render.
- Badge Japanese render.
- Home/Menu, confirming no subtitle under PaperBadge.
- Practice entry screen.
- Practice Must card first page.
- Practice long Answer page.
- Practice footer arrows wrapping between cards.
- Drills category menu.
- Weak Answer A01 question plus choices at Reader M.
- Metric Precision question plus choices at Reader M.
- Drill split page, if any, confirming options repeat a question reminder.
- Drill feedback page after answer selection.
- Exam entry screen.
- Exam final results screen.
- Glossary category grid.
- Glossary term page.
- Results empty state and populated state after at least one answer.
- Settings power controls.
- Settings → Advanced → Power Lab pages 1–4.
- Japanese menu (Home → Japanese).
- Japanese Daily Questions — confirm Japanese prompt/choices render correctly (no "?" or boxes).
- Japanese Daily Questions feedback page — correct/wrong, answer sentence, explanation, tags.
- Japanese Reference.
- Japanese Results empty state and populated state after at least one Japanese answer — confirm
  it never shows inside the regular Results screen.
- Japanese Mock Test placeholder.

## Power Verification (Home/Menu WarmIdle, v5.8-dev19)

- Power = Max, Refresh = Balanced.
- Go to Home, do not touch.
- After the WarmIdle threshold (5s on Max), confirm CPU MHz drops — check serial log for
  `Power CPU scale: idle ...` or revisit Power Lab page 1 and read "Last scaled"/"80MHz last"
  (visiting Power Lab itself restores CPU, so these history fields are read after the fact).
- Wait toward the LightNap threshold; Power Lab page 1's "LightNap (this screen)" row shows
  a countdown or a block reason.
- Repeat the Home check on: Badge, Home, Interview Results, Glossary, Settings, Advanced, Power Lab.
- Confirm Drills/Exam pre-answer screens still block WarmIdle/LightNap while an option is awaiting a tap.
- Confirm no deep sleep behavior is introduced (Sleep mode stays Off/Light only; Deep experiment
  stays blocked).

## Japanese Verification (v5.9-dev2, polish pass)

**Typography (Reader S/M/L):**
- Change Settings → Reader to S, then enter Japanese Daily Questions. Prompt and choices should
  render at 24px Gothic. Change to M → 28px. Change to L → 32px. Each step must be visibly
  larger than the previous.
- At Reader M, confirm the prompt area is comfortable and choices are readable without zooming.
- Settings, Home, and all other control screens must remain the same size regardless of Reader.

**Question layout:**
- Pre-answer footer: only a Home button (no "Next" label).
- Post-answer footer: Next button + Home button, split 50/50.
- Buttons should be outlined (no large black fill). Text should be vertically centered.

**Feedback layout:**
- "Correct" or "Wrong" title: should be English Sans Bold-like, not Gothic, and not huge.
- Correct answer line, answer sentence, Japanese explanation: Gothic at reader size.
- English meaning ("EN: …"): smaller than the Japanese body text; ASCII font path.
- Grammar tag (if present): compact label below the English meaning.

**Reference (structured):**
- Home → Japanese → Reference must show three sections: Kanji, Grammar, Vocabulary.
- Each section has an English section header and Japanese terms underneath.
- No raw rows like "もじ kanji: 郵,便,局". Kanji and Vocabulary items should be deduped.

**Results (compact):**
- Home → Japanese → Results must show a compact summary using normal-sized English text —
  not the large font used in Interview Results. Title ~32px, stat rows ~26px, category rows ~24px.
- Empty state: compact message in normal-sized text.
- At least one answered question required to see the populated state.

**Ghosting:**
- When entering Daily Questions from the Japanese menu, confirm the previous menu is not
  visible behind the new question screen. The transition must use a clean EPD refresh.
- Same check for Reference, Results, Mock Test placeholder.
- Feedback transition (tapping a choice) must also get a clean refresh.

**Japanese text integrity (unchanged from dev1):**
- Confirm: 郵便局, 引っ越した, 荷物, 違っていました, 子供のころ, ものだ, てばかりいる,
  とっちゃいけない, ちゃう, とく — all render as Japanese kanji/kana, never "?" or boxes.
- Confirm Japanese text wraps without splitting a multi-byte character mid-glyph.

**Power and isolation (unchanged from dev1):**
- Japanese Daily Questions pre-answer blocks WarmIdle/LightNap; feedback is LightNap-eligible.
- Japanese Results is RAM-only and resets on reboot; never appears in the regular Results screen.
- English/interview typography is unchanged outside Japanese screens.

## Japanese Verification (v5.9-dev1, baseline)

- Confirm these strings render as Japanese kanji/kana, never as "?" or boxes, on the Daily
  Questions prompt/choices/feedback screens: 郵便局, 引っ越した, 荷物, 違っていました, 子供のころ,
  ものだ, てばかりいる, とっちゃいけない, ちゃう, とく.
- Confirm Japanese text wraps without splitting a multi-byte character mid-glyph.
- Confirm Japanese Daily Questions pre-answer screen blocks WarmIdle/LightNap while waiting for a
  tap (same guard as Drills/Exam); feedback state is LightNap-eligible.
- Confirm English/interview typography (Sans Bold-like, not High Contrast) is unchanged elsewhere.
- Confirm Japanese Results is RAM-only and resets after a reboot, and never appears inside the
  regular Results screen.

## Photo Batches (v5.8-dev19)

10 photos per QA pass:

1. Static Badge.
2. Settings screen — confirm all buttons (Reader S/M/L, Fast/Balanced/Clean, Responsive/Balanced/Max,
   Normal/Strap, Advanced) use the same visual font size. Selected state shows triple border.
3. Practice large Answer page with paragraph spacing.
4. Weak Answer A01 question/options at Reader L — confirm option boxes same height, text centered.
5. Metric Precision question/options at Reader M — confirm option boxes same height.
6. A longer Exam question, plus options page if split.
7. Drill feedback page with Selected / Best / Why this is best blocks.
   Confirm known colon-label patterns (e.g. "Question: ... Answer: ...") break to separate lines.
   Confirm prose with spaced dashes ("risk - signal - stop") stays on one line (not split).
8. Hostile Follow-up drill — confirm "Suggested response" label visible, body readable.
9. Results page after 3+ answers — confirm Summary+Categories combined if ≤3 categories.
10. Settings → Advanced → Power Lab pages 1–4.

## Render Trace

Render traces append to:

```text
/papercoach/debug/render_trace.txt
```

Use Settings → Advanced → Dump render trace to write the latest trace explicitly. Each trace should include screen/mode, item/card id, stage, page index/total, visible excerpt, line count, wrapped line range, font, reader size, split-layout flag, clean-refresh flag, truncation status, and warning.

## Export Deck Dump

Use Settings → Advanced → Export deck. If SD is mounted, firmware writes:

```text
/papercoach/debug/embedded_deck_dump.md
```

The repo-side comparison file is:

```text
docs/embedded_deck_dump.md
```

## Known E-Ink Visual Risks

- Ghosting after large black option buttons, avoided by bordered buttons.
- Ghosting after feedback screens, mitigated with clean refresh on feedback entry and return.
- Header duplication, especially drill type/category labels.
- Option pages without question context.
- Text wrapping inside buttons at Reader L.
- Small icon strokes fading after partial refresh.
- Repeated redraws of static Badge mode wasting battery.

## Current UX Decisions To Verify

- Buttons should be outlined by default, not large black-filled blocks.
- Header text should be readable or absent; tiny metadata should not become visual noise.
- Drill and Exam questions should use the same readable question/option layout.
- Results should paginate rather than shrink everything onto one screen.
- Badge mode should not redraw or rotate language when Manual toggle and Auto interval Off are selected.
