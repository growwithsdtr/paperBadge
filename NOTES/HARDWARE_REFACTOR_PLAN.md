# Hardware refactor plan

Branch: `hardware-layer-reader-refactor`

Reference clones were inspected under `/tmp/paperBadge-hardware-refs` so the
firmware tree stays clean.

## Reference inventory

- `m5stack/M5Unified` at `8108bfa`
  - `Power_Class` maps M5PaperS3 battery to ADC on GPIO3 with a 2.0 ratio,
    charge status to GPIO4, and touch INT to GPIO48.
  - M5Unified uses `gpio_wakeup_enable(GPIO48, GPIO_INTR_LOW_LEVEL)` plus
    `esp_sleep_enable_gpio_wakeup()` for PaperS3 light sleep. It does not treat
    GPIO48 as RTC-capable for deep sleep.
- `m5stack/M5GFX` at `27e1ef0`
  - `Touch_GT911` clears stale GT911 data and explicitly sleeps/wakes the touch
    controller. That supports a diagnostic wrapper, not direct register copies.
- `m5stack/M5PaperS3-UserDemo` at `5e275ad`
  - Factory demo uses `epd_quality` for grayscale test bars and explicitly draws
    16 gray steps. That validates a conservative 16-gray palette for app UI.
- `m5stack/M5EPD` at `ef1be61`
  - Historical reference only; do not copy old M5Paper power paths into PaperS3.
- `espressif/esp-idf` at `fb14a3e`
  - `examples/system/light_sleep` registers timer, GPIO, and UART wake sources
    then reports `esp_sleep_get_wakeup_causes()`.
  - `examples/system/deep_sleep` keeps tiny RTC state, reports wake cause, and
    starts deep sleep only after a valid wake source is configured.
  - `examples/lowpower/power_management` shows `esp_pm_configure()` plus PM
    locks. For this Arduino firmware DFS stays compile-time gated.
- `crosspoint-reader/crosspoint-reader` at `362dcb2`
  - Rich EPUB/TXT reader architecture with filesystem helpers, cache directories,
    recent books, and page progress. Too large to port wholesale.
- `juicecultus/crosspoint-reader-papers3` at `d9792a5`
  - PaperS3 fork reads battery via ADC averaging/hysteresis, turns Wi-Fi off on
    network activity exit, and uses cached TXT page offsets. These are good MVP
    patterns for PaperBadge.
- `Xinyuan-LilyGO/LilyGo-EPD47` at `3607e73`
  - Useful concepts: `epd_poweroff_all()`, 4-bit grayscale buffers, and touch
    sleep/wakeup. Do not copy pin mappings or PMIC assumptions.
- `Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO` at `9f89f2a`
  - Useful as a cautionary source for panel power sequencing and GT911 interrupt
    modes. Do not copy board definitions.

## PaperS3 constraints used for implementation

- Touch INT is GPIO48. It can be used for light-sleep GPIO wake, but it is not
  an RTC GPIO on ESP32-S3, so deep sleep touch wake remains blocked until a
  different verified wake source exists.
- The existing firmware already protects answer screens from light naps; keep
  that policy intact and extend it only where Reader is passive.
- Battery percent is approximate when derived from ADC voltage. Report raw mV,
  percent source, charge state, VBUS availability, and sample age.
- Wi-Fi and Bluetooth should be off unless explicitly requested by a future
  network workflow.
- Dynamic frequency scaling can affect display/touch/SD timing. Keep manual CPU
  policy as-is and gate ESP-IDF DFS behind `ENABLE_EXPERIMENTAL_DFS`.

## Commit plan

1. `reference-notes`
   - Add this plan and reference decisions.
2. `hardware-layer`
   - Add `src/hw` managers for battery, sleep/wake/RTC, Wi-Fi, display, touch,
     SD, power mode, and diagnostics.
   - Integrate low-risk wrappers into setup, SD mount, refresh, battery polling,
     Wi-Fi-off policy, and light nap entry.
3. `power-diagnostics`
   - Extend Power Lab/Audit rows with manager snapshots: wake/reset, battery
     source, Wi-Fi state, heap/PSRAM, display refresh mode, SD index state.
4. `reader-app`
   - Add `src/reader` TXT/MD library, pagination, state, cache stubs, and EPUB
     stub.
   - Add `src/apps/ReaderApp`, plus a fifth Home button between Japanese and
     Settings.
5. `docs`
   - Add final summary, test plan, SD book placement notes, EPUB/manga roadmap,
     and commit log entries.

## Deferred hardware tests

- Confirm light-sleep GPIO wake from GT911 INT does not immediately wake-loop on
  this physical PaperS3.
- Confirm battery voltage/percent behavior on USB, battery only, and charging.
- Confirm display ghosting cadence after repeated Reader page turns.
- Confirm SD index write path on the target card: `/paperBadge/library_index.json`.
