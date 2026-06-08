# PaperBadge / PaperCoach QA Guide

## Firmware Update Checklist

1. Build: `pio run`
2. Upload: `pio run -t upload`
3. Monitor briefly: `pio device monitor -p /dev/tty.usbmodem1101 -b 115200`
4. Stop monitor with `Ctrl+C` so the port is free.
5. Photograph the required screen batch below.

## Physical QA After Each Firmware Update

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

## Photo Batches

- Batch 1: Badge, Home, Settings, Debug Power Audit.
- Batch 2: Practice entry, Question, Answer, Anchor/Watch-out.
- Batch 3: Drills question/options, split options if visible, feedback.
- Batch 4: Exam entry, one exam question, exam summary.
- Batch 5: Glossary grid, one term page, Results page.

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
