# Agent workflow — standing rules for this repo (M5PaperS3 PaperBadge firmware)

## Shipping — automatic at the end of EVERY task that changes code. Never ask me to push or flash; I never do it by hand.
1. Run `pio run` to verify the build. Keep output terse: exit status, errors, and the final RAM/flash line only.
2. If green, commit the changes.
3. Push to origin automatically.
4. Flash the device automatically: `pio run -t upload`. The device is on USB on this machine. If `pio device list` shows no port, report that this environment can't reach the device (a setup issue) — do NOT silently skip flashing.
5. Update COMMIT_LOG.md with the new commits.
"Done" = committed, pushed, AND flashed. Never just compiled.

## Guardrails — always
- Do not modify Interview render/touch paths or applyTypographyFont / applySansBoldFont.
- No external fonts; no new Japanese question content.
- firmware.bin must stay < 12MB.
- Locate edits by symbol, never by line number.
- Stop and report on any Interview regression.
