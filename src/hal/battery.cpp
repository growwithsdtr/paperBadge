#include "battery.hpp"

#include <cstdio>
#include <ctime>

#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace ps3::battery {

namespace {
constexpr const char* TAG = "battery";

// Pin map per M5Unified Power_Class.cpp (board_M5PaperS3 case).
constexpr adc_unit_t    BAT_ADC_UNIT     = ADC_UNIT_1;
constexpr adc_channel_t BAT_ADC_CHANNEL  = ADC_CHANNEL_2;       // = GPIO3 on ADC1
constexpr adc_atten_t   BAT_ADC_ATTEN    = ADC_ATTEN_DB_12;
constexpr adc_bitwidth_t BAT_ADC_BITS    = ADC_BITWIDTH_12;
constexpr float         VBAT_DIVIDER     = 2.0f;

constexpr gpio_num_t    CHG_STAT_PIN     = GPIO_NUM_4;

// Voltage → percentage curve. The original code linear-interpolated
// 3300 mV = 0% to 4100 mV = 100% (matched M5Unified) but that maps
// the natural ~80 mV voltage sag between "charging at 4.20 V" and
// "just unplugged loaded" to a 10-15 percentage-point drop, which
// the user sees as a glitchy "100% → 85%" jump on unplug.
//
// Replaced with a piecewise-linear approximation of an actual LiPo
// discharge curve at moderate load, then calibrated to this Paper
// S3's specific ADC offset. Field measurement: charger holds the
// cell at the 4.20 V CV target (chg=1 readings clip to ~4178 mV in
// /sdcard/temp/battery.log), then settles to ~4120 mV under normal
// reading load when charging completes. That means the divider +
// ADC chain undershoots by ~30-80 mV across the upper range — the
// chip never sees > ~4180 mV no matter how full the cell is. We
// pin the top of the curve to that empirical "full" plateau so a
// freshly charged device actually reads 100% on the toolbar.
//
// Lower end of the curve is unchanged: bending hard around
// 3.7-3.8 V keeps the displayed percentage tracking rough
// remaining capacity rather than just normalised voltage. Same
// table is used regardless of charging state.
struct VoltagePercent { int mv; int pct; };
constexpr VoltagePercent kCurve[] = {
    { 4120, 100 },   // empirical full-charge reading on this device
    { 4000,  80 },
    { 3900,  65 },
    { 3800,  45 },
    { 3700,  25 },
    { 3600,  10 },
    { 3500,   5 },
    { 3300,   0 },
};
constexpr int kCurvePoints = sizeof(kCurve) / sizeof(kCurve[0]);

// ADC sampling: take SAMPLES raw reads, drop the TRIM highest and
// TRIM lowest, average the rest. ADC1 with DB_12 attenuation has
// ±20-30 mV of single-shot noise; 16 trimmed reads collapses that
// to single-mV consistency without spending more than a few hundred
// microseconds total.
constexpr int kAdcSamples = 16;
constexpr int kAdcTrim    = 2;

adc_oneshot_unit_handle_t s_adc       = nullptr;
adc_cali_handle_t         s_cali      = nullptr;
bool                      s_inited    = false;

}  // namespace

bool init() {
    if (s_inited) return true;

    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = BAT_ADC_UNIT;
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit: %s", esp_err_to_name(err));
        return false;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten    = BAT_ADC_ATTEN;
    chan_cfg.bitwidth = BAT_ADC_BITS;
    err = adc_oneshot_config_channel(s_adc, BAT_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel: %s", esp_err_to_name(err));
        return false;
    }

    // Curve-fitting calibration — ESP32-S3 supports it natively, the
    // returned voltages are factory-trimmed within ±20 mV typically.
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id  = BAT_ADC_UNIT;
    cali_cfg.chan     = BAT_ADC_CHANNEL;
    cali_cfg.atten    = BAT_ADC_ATTEN;
    cali_cfg.bitwidth = BAT_ADC_BITS;
    err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "adc_cali_create_scheme_curve_fitting: %s — "
                      "continuing without calibration",
                 esp_err_to_name(err));
        s_cali = nullptr;
    }
#endif

    // Charge status pin: LGS4056H drives it LOW while charging.
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << CHG_STAT_PIN;
    io.mode         = GPIO_MODE_INPUT;
    io.pull_up_en   = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_DISABLE;
    err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config CHG_STAT: %s", esp_err_to_name(err));
        return false;
    }

    s_inited = true;
    ESP_LOGI(TAG, "battery ready (ADC1 GPIO3, CHG_STAT GPIO%d)",
             (int)CHG_STAT_PIN);
    return true;
}

int voltage_mv() {
    if (!s_inited) return -1;

    int raws[kAdcSamples];
    for (int i = 0; i < kAdcSamples; ++i) {
        if (adc_oneshot_read(s_adc, BAT_ADC_CHANNEL, &raws[i]) != ESP_OK) {
            return -1;
        }
    }

    // Insertion sort (cheap for N=16) so we can drop the high/low
    // tails before averaging.
    for (int i = 1; i < kAdcSamples; ++i) {
        const int v = raws[i];
        int j = i - 1;
        while (j >= 0 && raws[j] > v) {
            raws[j + 1] = raws[j];
            --j;
        }
        raws[j + 1] = v;
    }

    int64_t sum = 0;
    for (int i = kAdcTrim; i < kAdcSamples - kAdcTrim; ++i) {
        sum += raws[i];
    }
    const int avg_raw = static_cast<int>(sum / (kAdcSamples - 2 * kAdcTrim));

    int cal_mv = 0;
    if (s_cali) {
        if (adc_cali_raw_to_voltage(s_cali, avg_raw, &cal_mv) != ESP_OK) {
            return -1;
        }
    } else {
        // Fallback: linear approximation with no calibration. ATTEN_DB_12
        // gives a usable range up to ~3.3 V at the pin; full-scale = 4095.
        cal_mv = (avg_raw * 3100) / 4095;
    }
    return static_cast<int>(cal_mv * VBAT_DIVIDER);
}

int level_pct() {
    const int mv = voltage_mv();
    if (mv < 0) return -1;

    // Off-table clamps (kCurve is sorted high-to-low).
    if (mv >= kCurve[0].mv)              return kCurve[0].pct;
    if (mv <= kCurve[kCurvePoints - 1].mv) return kCurve[kCurvePoints - 1].pct;

    // Piecewise linear interpolation between adjacent table entries.
    // Walk top-down; once we find the segment that brackets `mv`, lerp.
    for (int i = 1; i < kCurvePoints; ++i) {
        if (mv >= kCurve[i].mv) {
            const int hi_mv  = kCurve[i - 1].mv;
            const int lo_mv  = kCurve[i].mv;
            const int hi_pct = kCurve[i - 1].pct;
            const int lo_pct = kCurve[i].pct;
            // (mv - lo_mv) / (hi_mv - lo_mv) is in [0, 1]; rescale to
            // the [lo_pct, hi_pct] band, with mid-point rounding.
            const int span_mv  = hi_mv - lo_mv;
            const int span_pct = hi_pct - lo_pct;
            const int extra    = (mv - lo_mv) * span_pct + span_mv / 2;
            return lo_pct + extra / span_mv;
        }
    }
    return 0;  // unreachable — clamp above caught mv <= last entry
}

bool is_charging() {
    if (!s_inited) return false;
    return gpio_get_level(CHG_STAT_PIN) == 0;
}

void log_event(const char* tag, int64_t dur_sec, int prev_mv) {
    if (!s_inited) return;
    if (!tag)      tag = "?";

    // Sample everything *before* opening the file — we want the
    // measurement to reflect the moment the event happened, not
    // whatever load fopen/fprintf added.
    const int  mv   = voltage_mv();
    const int  pct  = level_pct();
    const bool chg  = is_charging();
    const long long t = static_cast<long long>(std::time(nullptr));

    constexpr const char* kLogPath = "/sdcard/paperBadge/logs/battery.log";
    FILE* fp = std::fopen(kLogPath, "a");
    if (!fp) {
        // Most common reason is that /sdcard/temp/ doesn't exist yet
        // — main.cpp creates it before SD-touching activity, but
        // log_event() may be called from a path that runs earlier on
        // a fresh card. Fall back to silently dropping the line:
        // diagnostic logs shouldn't break boot.
        ESP_LOGW(TAG, "log_event: fopen %s failed", kLogPath);
        return;
    }
    // Build the row in a buffer so optional fields (prev_mv, dur)
    // get appended cleanly without re-issuing fprintf format strings.
    char line[160];
    int n = std::snprintf(line, sizeof(line),
                          "%-5s mv=%-5d pct=%-3d chg=%d t=%-7lld",
                          tag, mv, pct, chg ? 1 : 0, t);
    if (prev_mv >= 0 && n > 0 && n < static_cast<int>(sizeof(line))) {
        n += std::snprintf(line + n, sizeof(line) - n,
                           " prev_mv=%-5d", prev_mv);
    }
    if (dur_sec >= 0 && n > 0 && n < static_cast<int>(sizeof(line))) {
        n += std::snprintf(line + n, sizeof(line) - n,
                           " dur=%lld",
                           static_cast<long long>(dur_sec));
    }
    if (n > 0 && n < static_cast<int>(sizeof(line))) {
        n += std::snprintf(line + n, sizeof(line) - n, "\n");
    }
    if (n > 0) {
        std::fwrite(line, 1, n, fp);
    }
    std::fclose(fp);
}

// ---- NVS-backed deep-sleep crossing state ----

namespace {
constexpr const char* kNvsNamespace = "ps3_battery";
constexpr const char* kNvsKeyDeepT  = "deep_t";    // int64, seconds
constexpr const char* kNvsKeyDeepMv = "deep_mv";   // int32, millivolts
}  // namespace

bool init_nvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES
        || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs_flash_init: %s — erasing and retrying",
                 esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void save_deep_entry(std::time_t entry_t, int entry_mv) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save_deep_entry: nvs_open %s failed: %s",
                 kNvsNamespace, esp_err_to_name(err));
        return;
    }
    nvs_set_i64(h, kNvsKeyDeepT,  static_cast<int64_t>(entry_t));
    nvs_set_i32(h, kNvsKeyDeepMv, static_cast<int32_t>(entry_mv));
    err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save_deep_entry: nvs_commit failed: %s",
                 esp_err_to_name(err));
    }
}

bool load_and_clear_deep_entry(std::time_t* out_t, int* out_mv) {
    nvs_handle_t h;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    int64_t t  = 0;
    int32_t mv = 0;
    const bool has_t  = nvs_get_i64(h, kNvsKeyDeepT,  &t)  == ESP_OK;
    const bool has_mv = nvs_get_i32(h, kNvsKeyDeepMv, &mv) == ESP_OK;
    if (has_t || has_mv) {
        // Erase whichever key existed so the next plain boot doesn't
        // mistakenly inherit a stale entry.
        nvs_erase_key(h, kNvsKeyDeepT);
        nvs_erase_key(h, kNvsKeyDeepMv);
        nvs_commit(h);
    }
    nvs_close(h);
    if (!(has_t && has_mv)) return false;
    if (out_t)  *out_t  = static_cast<std::time_t>(t);
    if (out_mv) *out_mv = static_cast<int>(mv);
    return true;
}

}  // namespace ps3::battery
