#pragma once

#include <cstdint>
#include <ctime>

namespace ps3::battery {

// Initialise the ADC1 channel for VBAT (GPIO3) and configure GPIO4
// as the charge-status input. Idempotent. Returns true on success.
bool init();

// Battery voltage in millivolts. Returns -1 on read failure.
// Internally: ADC1_CH2 raw -> calibrated mV -> x2 (voltage divider).
int voltage_mv();

// Estimated battery level in 0..100. Maps 3300 mV -> 0%,
// 4100 mV -> 100% linearly (matches M5Unified's PaperS3 mapping).
// Returns -1 on read failure.
int level_pct();

// True if the LGS4056H reports charging (CHG_STAT pin LOW). USB power
// presence on its own is not enough — the cell has to actually be
// taking current.
bool is_charging();

// Append one line to /sdcard/temp/battery.log so sleep / wake
// voltage drift can be reviewed offline by pulling the SD card.
// Plugging USB to capture serial would start charging mid-
// measurement and contaminate the reading.
//
// Line format (optional fields appear only when their argument is
// non-negative):
//   <tag>  mv=N  pct=N  chg=N  t=N  [prev_mv=N]  [dur=N]
//
// `tag` is a short identifier such as "boot" / "light" / "wake" /
// "deep". `dur_sec`, when >= 0, records the duration of the
// preceding sleep (light-sleep duration for `wake` rows, deep-sleep
// duration for `boot` rows that follow a deep-sleep wake — note
// that time(nullptr) may reset across hardware reset and we can't
// always measure that, see save_deep_entry below). `prev_mv`, when
// >= 0, captures the voltage recorded at the start of the preceding
// sleep — used on boot rows after deep sleep so the ΔV across deep
// sleep is visible on a single line.
//
// Silently no-ops when the SD isn't mounted yet or the open/append
// fails — diagnostics shouldn't affect the rest of the boot path.
void log_event(const char* tag, int64_t dur_sec = -1, int prev_mv = -1);

// One-shot NVS bootstrap. Initialises the default NVS partition so
// save_deep_entry / load_and_clear_deep_entry below have a place to
// stash deep-sleep crossing state. Idempotent; returns true on
// success. Safe to call repeatedly.
bool init_nvs();

// Persist the moment a deep sleep starts (entry time in
// `time(nullptr)` seconds and entry voltage in millivolts) to NVS.
// NVS survives every reset path on ESP32-S3 — including the
// EN-pin / RST-button reset that wakes the chip out of deep sleep
// — which the older RTC_DATA_ATTR approach didn't (RTC slow RAM is
// cleared by POWERON / EN resets on this chip family). The pair is
// read back by load_and_clear_deep_entry() on the next boot.
void save_deep_entry(std::time_t entry_t, int entry_mv);

// Read and erase the deep-sleep entry record written by
// save_deep_entry(). Returns true if a record was present (the
// previous run entered deep sleep and we just woke from it).
//
// `out_t` and `out_mv` may be null. When time(nullptr) survived
// the reset (best case: ESP_RST_SW or ESP_RST_DEEPSLEEP wake)
// `(now - *out_t)` gives the deep-sleep duration in seconds;
// otherwise the subtraction goes non-positive and the caller should
// flag the duration as unknown.
bool load_and_clear_deep_entry(std::time_t* out_t, int* out_mv);

}  // namespace ps3::battery
