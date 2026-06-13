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
- Settings power and Badge sleep controls.
- Debug Power Audit screen.

## Photo Batches (v5.8-dev16)

10 photos per QA pass:

1. Static Badge.
2. Settings screen — confirm all buttons (Reader S/M/L, Fast/Balanced/Clean, Responsive/Balanced/Max,
   Normal/Strap, Advanced) use the same visual font size. Selected state shows triple border.
3. Practice large Answer page with paragraph spacing.
4. Weak Answer A01 question/options at Reader L — confirm option boxes same height, text centered.
5. Metric Precision question/options at Reader M — confirm option boxes same height.
6. A longer Exam question, plus options page if split.
7. Drill feedback page with Selected / Best / Why this is best blocks.
   Confirm colon-label patterns (e.g. "Question: ... Answer: ...") break to separate lines.
8. Hostile Follow-up drill — confirm "Suggested response" label visible, body readable.
9. Results page after 3+ answers — confirm Summary+Categories combined if ≤3 categories.
10. Debug Power Audit.

## Render Trace

Render traces append to:

```text
/papercoach/debug/render_trace.txt
```

Use Debug -> Dump render trace to write the latest trace explicitly. Each trace should include screen/mode, item/card id, stage, page index/total, visible excerpt, line count, wrapped line range, font, reader size, split-layout flag, clean-refresh flag, truncation status, and warning.

## Export Deck Dump

Use Debug -> Export deck text. If SD is mounted, firmware writes:

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
