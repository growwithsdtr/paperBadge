# PaperBadge / PaperCoach QA Guide

## Firmware Update Checklist

1. Build: `pio run`
2. Upload: `pio run -t upload`
3. Monitor briefly: `pio device monitor -p /dev/tty.usbmodem1101 -b 115200`
4. Stop monitor with `Ctrl+C` so the port is free.
5. Photograph the required screen batch below.

## Physical QA After Each Firmware Update

- Use these settings unless the test says otherwise:
  - Reader M
  - High Contrast
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

## Photo Batches

Maximum 10 photos after v5.5:

1. Static Badge.
2. Home menu.
3. Settings with QA settings visible.
4. Practice entry screen.
5. Practice large Answer page.
6. Drill question/options at Reader M.
7. Drill feedback page.
8. Glossary term page with Definition / Why it matters / Example.
9. Results Summary plus one post-summary Results page after at least one answer.
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
