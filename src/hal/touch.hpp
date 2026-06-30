#pragma once

#include <cstdint>

namespace ps3::touch {

// Initialise the I2C bus and probe the GT911 capacitive touch
// controller used on M5Stack Paper S3 (SDA=41, SCL=42, addr 0x14
// or 0x5D depending on hardware revision). Returns true if the
// chip responded.
bool init();

// One-shot poll. Returns true exactly once per finger-down event,
// with `*x` and `*y` filled in *logical* coordinates that match
// ps3::display::width()/height() (i.e. the rotation applied via
// epd_set_rotation has already been undone).
//
// Designed to be called from a tight loop with a short delay; reads
// from the GT911 sample register and translates the finger-down
// transition into a single tap event.
bool poll_tap(int* x, int* y);

// Block in ESP_LIGHT_SLEEP until the GT911 INT pin (GPIO48) goes LOW,
// i.e. the next finger touch. CPU clocks down, the polling task and
// every other FreeRTOS task pause; the panel is unaffected (the last
// e-paper image is retained). Returns once the chip resumes.
//
// On Paper S3 GPIO48 is not an RTC IO, so we use the GPIO wake source
// (gpio_wakeup_enable + esp_sleep_enable_gpio_wakeup) rather than
// EXT0/EXT1, mirroring M5Unified's PaperS3 lightSleep path.
void light_sleep_until_touch();

// Why a light_sleep_until_touch_or_timeout() call returned. Used by
// the auto-sleep / auto-power-off chain: GPIO wake means the user
// touched the panel and we should resume normally; Timer wake means
// the auto-power-off deadline elapsed and the caller should escalate
// straight to deep sleep without showing the screen again.
enum class WakeReason {
    Touch,
    Timer,
};

// Same as light_sleep_until_touch() but also arms a timer wakeup
// for `timeout_us` microseconds. Whichever event fires first wakes
// the chip; the return value reports which one. Pass timeout_us=0
// to disable the timer (in which case this behaves identically to
// light_sleep_until_touch()).
WakeReason light_sleep_until_touch_or_timeout(uint64_t timeout_us);

// Drop any pending tap events from the queue. Useful right after
// light_sleep_until_touch() returns: the wake-triggering touch will
// land in the queue ~50 ms later and we usually want to discard it
// rather than treat it as a page-nav tap.
void drain();

// Live finger state, updated by the touch task on every poll. Returns
// true if a finger is currently on the panel, with `*x` and `*y`
// filled in logical coords. Use this for drag-style gestures (e.g.
// the page-jump slider) where you need to follow the finger rather
// than wait for a discrete tap edge.
bool current_finger(int* x, int* y);

// Set the logical touch transform matching display::Rotation.
void set_rotation(bool landscape, bool inverted);

// Mirror the panel rotation toggle (display::set_inverted). When
// inverted=true, GT911 raw coordinates are flipped 180° so logical
// (x, y) keeps matching the user's perceived screen position.
// inverted=false uses raw pass-through, matching the default
// EPD_ROT_INVERTED_PORTRAIT calibration.
void set_inverted(bool inverted);

}  // namespace ps3::touch
