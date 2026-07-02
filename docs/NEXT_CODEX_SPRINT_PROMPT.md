# Next Codex Sprint Prompt — Physical QA Follow-Up

Use this after `qa-font-manga-interview-fixes` is built and flashed manually.

## Prompt

You are continuing firmware work on the M5PaperS3 PaperBadge.

Repo:
`/Users/danieljimenez/AIDevelopment/paperBadge`

Branch:
`qa-font-manga-interview-fixes`

Mode:
Start with physical-QA triage. Patch only regressions found on-device.

Do not flash unless explicitly asked. Known manual command:

```sh
pio run -t upload --upload-port /dev/cu.usbmodem1101
```

Read first:

- `COMMIT_LOG.md`
- `docs/RECOVERY_QA.md`
- `docs/MANGA_READER_LIMITS.md`
- `docs/FONT_LAB.md`
- `docs/DEVICE_LEARNINGS.md`
- `docs/FONT_MANGA_REDTEAM_REVIEW.md`

Verify on-device:

- Badge render, EN/JA toggle, QR zoom, and English 180-degree final sleep/power frame.
- Font Lab spacing, one-candidate pages, Latin-only labels, and production-vs-QA wording.
- Small JPEG CBZ opens and remains stable.
- Manga portrait and landscape touch zones match `docs/MANGA_READER_LIMITS.md`.
- Manga overlay Fit, Orientation, Refresh, Clean now, Library, and Close controls work.
- Landscape slice navigation advances/reverses through all slices; going back from slice 1 lands on the previous page's last slice.
- PNG-in-CBZ follows fit/slice behavior.
- ZIP64, CBR/RAR, WebP-only, corrupt ZIP, empty/no-JPEG-PNG, and allocation-failure manga archives show distinct diagnostics.
- Serial log reports IMU probe reads at `0x68` and `0x69`; auto-rotation is not expected yet.
- Interview Practice answer pagination reaches all answer sections without cutting content.
- Interview Size visibly changes Practice, Drill, Feedback, and Glossary spacing.
- Reader EPUBs show distinct messages for oversized spine entries versus total extracted-text truncation.
- Settings persistence survives reboot.

Guardrails:

- Do not break Badge, Interview routes, Japanese Practice/Mock/Reference, small JPEG CBZ opening, Settings pages, or the app size limit.
- Do not add external fonts or new Japanese question content.
- Do not promote Font Lab candidates to runtime fonts in a QA patch.
- Do not add ZIP64, RAR/CBR, or WebP decoders unless a later sprint explicitly scopes that work.
- Keep IMU autorotation probe-driven: implement a real driver only after physical serial logs confirm the chip/address and touch responsiveness on shared I2C.

For any code change:

1. Build with `pio run`.
2. Commit the scoped fix.
3. Push at the end if requested by the user.
4. Update `COMMIT_LOG.md` and affected docs.
