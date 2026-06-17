# Overnight implementation log

Base: `f2315aa` (`v5.9-dev3`)

## Commits

- `42af620` - Add reversible power trace instrumentation.
- `23a1024` - Enable Japanese Gothic 40 font bucket.
- `2f9c6f1` - Add Japanese codepoint detector.
- `fa981aa` - Add mixed Japanese text rendering helpers.
- `8dbe06b` - Route Japanese UI labels through safe text path.
- `d9f1e96` - Warn when Japanese reaches ASCII sanitizer.
- `1c01c92` - Guard Japanese mock test answer sleep.
- `5e2c2c9` - Add built-in Japanese Font Lab pages.
- `3af3018` - Persist Japanese results to isolated NDJSON log.

Each commit was followed by `pio run`; all builds passed before moving on.

## Block A power audit findings

- Max Battery WarmIdle threshold is `5000ms` via `profileIdleScaleThresholdMs()`.
- WarmIdle entry path is `loop()` -> `maybeEnterPowerIdle()` -> `enterIdleMode()` -> `maybeScaleIdleCpu()`.
- WarmIdle is gated by: `gInputLocked == false`, `gTouchActive == false`, `gLastUserActivityMs != 0`, post-touch guard >= `2000ms`, `isStaticIdleScreen(gScreen)`, `kEnableIdleCpuScaling`, and not already idle-scaled.
- CPU scales from active `240MHz` to requested idle `80MHz` through `setCpuFrequencyMhz(kIdleCpuMhz)`.
- `recordUserActivity()` callers are `touch press`, `touch release`, and `post-wake touch held`. Spurious touch-controller press/release/click noise can reset the idle timer if it reaches `M5.Touch.getDetail()` as those events; locked input is ignored before this reset.
- WarmIdle eligible: Home, Interview menu, Japanese menu/source/week/day, Japanese Reference, Japanese Results, Settings, Advanced, Power Lab, Font Lab, Practice menu, Glossary menu, Drills menu, Interview Practice, Glossary, Results.
- WarmIdle ineligible: active Drills question screen, active Exam question screen, Japanese Daily question screen, Badge.
- LightNap eligible: Badge, Home, Interview menu, Japanese menu/source/week/day, Japanese Daily, Japanese Reference, Japanese Results, Practice/Glossary/Drills menus, Interview Practice, Glossary, Results, Drills, Exam.
- LightNap ineligible by screen: Settings, Advanced, Debug, Power Lab, Font Lab, Visual QA, Help, Japanese Mock Test placeholder.
- Pre-answer Drills/Exam/JapaneseDaily are blocked by `isAnswerSelectionActive()`. A future Japanese Mock Test pre-answer state now has `gJapaneseMockTestAwaitingAnswer` as an answer-selection guard, but Mock Test was not added to LightNap eligibility.
- Battery polling cadence is 45s active, 120s idle Balanced, 180s idle Max Battery. Added `PWR` instrumentation logs voltage/current every 5s while `PWR_TRACE` is enabled.
- Display refresh locks input for 250ms on fast refresh and 600ms on clean refresh; `prepareFullRefresh()` restores active CPU before drawing.
- Repeated redraw warning exists in `prepareFullRefresh()` after 3 repeated same-screen/same-reason renders; no render loop was added or found in `loop()`.

## Skipped or deferred

- Did not fetch, subset, embed, or convert external fonts.
- Did not author or import new Japanese question content.
- Did not implement real Mock Test logic, deeper Reference behavior, SRS, Review, or cross-source learning.
- Did not change Interview content/rendering intentionally.
- `wrapJapaneseTextToLines()` already breaks by measured `display.textWidth()` per UTF-8 codepoint, so no wrapper conversion was needed.
- Font Lab reports whether the source string contains `?`; physical e-ink glyph fallback still needs morning visual QA.
- Japanese results persistence is backend-only. Existing Japanese Results UI remains the simple current-session summary.

## Final build

- Final `firmware.bin`: `3757040` bytes.
- Final PlatformIO report: RAM `70.4%`, flash `57.3%`.
- Stop line `firmware.bin > 12MB` was not reached.

## Morning QA checklist

- Boot serial: confirm firmware starts, SD mounts, and Japanese session counter/log messages do not block boot.
- Settings -> Max Battery: leave Home idle for at least 10s; confirm `PWR idle enter` and CPU scale to 80MHz.
- Check Home, Interview menu, Japanese menu, Japanese source/week/day, Reference, Results, Settings, Advanced, Power Lab for WarmIdle expectations.
- Check Drills pre-answer, Exam pre-answer, and Japanese Daily pre-answer do not enter LightNap while awaiting an answer.
- Japanese -> Font Lab (JP): cycle all pages; verify `_20/_24/_28/_32/_36/_40`, mixed header/prompt/choice, and meta sizes render without `????`.
- Japanese source/week/day: verify `500問` and `ぶんぽう` render as Japanese, not boxes or `?`.
- Japanese Daily: answer at least one correct and one incorrect item; verify feedback grammar tag is readable at >=24px and no sanitizer WARN appears for normal Japanese UI.
- Reboot with SD inserted; confirm `/papercoach/japanese/progress/session_counter.txt` increments and `/papercoach/japanese/progress/results.log` is append-only NDJSON.
- Corrupt-tail test if desired: append a partial line to Japanese results log and confirm boot skips it without failing.
- Confirm Interview Practice, Drills, Exam, Glossary, and Results still render and behave as before.
