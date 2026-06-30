#include "touch.hpp"

#include <driver/gpio.h>
#include <driver/i2c.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

extern "C" {
#include <epdiy.h>     // epd_rotated_display_width/height for 180° flip
}

namespace ps3::touch {

namespace {
constexpr const char* TAG = "touch";

constexpr gpio_num_t  PIN_SDA = GPIO_NUM_41;
constexpr gpio_num_t  PIN_SCL = GPIO_NUM_42;
constexpr gpio_num_t  PIN_GT911_INT = GPIO_NUM_48;  // shared with BMI270 INT
constexpr i2c_port_t  I2C_PORT = I2C_NUM_0;
constexpr int         I2C_FREQ_HZ = 400000;

// Touch task runs at 100 Hz. Sub-20 ms taps were occasionally landing
// entirely between two polls — bumping to 10 ms keeps us at or below
// the GT911's internal sample rate so we don't miss the down-edge.
constexpr int TOUCH_POLL_INTERVAL_MS = 10;

// Pin the touch task to Core 1 so it keeps polling while Core 0 is
// busy with epdiy redraws. The queue holds at most one pending tap
// — additional taps while the queue is full are dropped, which gives
// the requested "1 ahead" UX (one buffered tap, then noise rejected).
constexpr UBaseType_t TOUCH_TASK_CORE     = 1;
constexpr uint32_t    TOUCH_TASK_STACK    = 4096;
constexpr UBaseType_t TOUCH_TASK_PRIORITY = 5;
constexpr UBaseType_t TOUCH_QUEUE_DEPTH   = 1;

uint8_t s_addr = 0;       // 0 = no chip
bool    s_finger_down = false;

// Live finger state, refreshed every poll cycle. Read by
// current_finger() for drag-style gestures (page-jump slider).
// volatile + 32-bit aligned ints make naive concurrent reads safe
// enough for our purposes — we don't need cross-coord atomicity, a
// 1-frame stale x or y is harmless.
volatile bool s_live_down = false;
volatile int  s_live_x    = 0;
volatile int  s_live_y    = 0;

// GT911 raw coordinates are calibrated to the firmware's normal
// portrait logical space (540x960). These flags rotate that portrait
// sample into the active epdiy logical display orientation.
volatile bool s_landscape = false;
volatile bool s_invert_xy = false;

QueueHandle_t s_event_queue = nullptr;
TaskHandle_t  s_task        = nullptr;

struct Event {
    int x;
    int y;
};

esp_err_t gt911_write(uint8_t addr, uint16_t reg,
                      const uint8_t* data, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) return ESP_FAIL;

    const uint8_t reg_hi = reg >> 8;
    const uint8_t reg_lo = reg & 0xFF;

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_hi, true);
    i2c_master_write_byte(cmd, reg_lo, true);
    if (data && len) {
        i2c_master_write(cmd, const_cast<uint8_t*>(data), len, true);
    }
    i2c_master_stop(cmd);

    const esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t gt911_read(uint8_t addr, uint16_t reg,
                     uint8_t* data, size_t len) {
    if (!data || !len) return ESP_ERR_INVALID_ARG;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) return ESP_FAIL;

    const uint8_t reg_hi = reg >> 8;
    const uint8_t reg_lo = reg & 0xFF;

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_hi, true);
    i2c_master_write_byte(cmd, reg_lo, true);

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);

    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    const esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return ret;
}

// Poll the GT911 once. Updates s_live_down / s_live_x / s_live_y on
// every successful read so current_finger() can serve drag-style
// callers, and emits a "tap edge" event (returns true with *x_out /
// *y_out filled) only on the down-edge so poll_tap() consumers still
// get one event per finger press.
bool read_one_tap(int* x_out, int* y_out) {
    if (!s_addr) return false;

    uint8_t status = 0;
    if (gt911_read(s_addr, 0x814E, &status, 1) != ESP_OK) {
        return false;
    }
    if ((status & 0x80) == 0) return false;

    const uint8_t points = status & 0x0F;
    const uint8_t zero = 0;
    gt911_write(s_addr, 0x814E, &zero, 1);

    if (points == 0) {
        s_finger_down = false;
        s_live_down   = false;
        return false;
    }

    // Always read coordinates so we can keep s_live_x / s_live_y up
    // to date for current_finger(). The earlier "skip while held"
    // optimisation (don't read coords) gets in the way of drag
    // tracking — slider users need a fresh sample every poll.
    uint8_t data[4] = {};
    if (gt911_read(s_addr, 0x8150, data, sizeof(data)) != ESP_OK) {
        return false;
    }
    const int gt_x = static_cast<int>((data[1] << 8) | data[0]);
    const int gt_y = static_cast<int>((data[3] << 8) | data[2]);

    // GT911 on Paper S3 is calibrated to the normal portrait logical
    // coordinates. Rotate that sample into the active display mode.
    int final_x = gt_x;
    int final_y = gt_y;
    if (s_landscape && s_invert_xy) {
        final_x = epd_rotated_display_width() - 1 - gt_y;
        final_y = gt_x;
    } else if (s_landscape) {
        final_x = gt_y;
        final_y = epd_rotated_display_height() - 1 - gt_x;
    } else if (s_invert_xy) {
        final_x = epd_rotated_display_width()  - 1 - gt_x;
        final_y = epd_rotated_display_height() - 1 - gt_y;
    }

    s_live_x    = final_x;
    s_live_y    = final_y;
    s_live_down = true;

    if (s_finger_down) return false;

    // Latch the down-edge after coords are committed so a transient
    // I2C error doesn't pin us into "finger held" — otherwise the
    // very tap that glitched (and every subsequent tap until the
    // user lifts their finger) would be silently dropped.
    s_finger_down = true;

    if (x_out) *x_out = final_x;
    if (y_out) *y_out = final_y;
    return true;
}

void touch_task(void*) {
    Event ev{};
    for (;;) {
        if (read_one_tap(&ev.x, &ev.y)) {
            // Skip-if-full semantics: drop new taps when the consumer
            // hasn't had time to read the queue yet. Means at most
            // one tap is buffered while the main task is rendering.
            xQueueSend(s_event_queue, &ev, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_INTERVAL_MS));
    }
}

}  // namespace

bool init() {
    if (s_addr) return true;

    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = PIN_SDA;
    conf.scl_io_num = PIN_SCL;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_FREQ_HZ;
    conf.clk_flags = 0;

    esp_err_t err = i2c_param_config(I2C_PORT, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config: %s", esp_err_to_name(err));
        return false;
    }

    err = i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "i2c_driver_install: %s", esp_err_to_name(err));
        return false;
    }

    uint8_t buf = 0;
    if (gt911_read(0x14, 0x814E, &buf, 1) == ESP_OK) {
        s_addr = 0x14;
    } else if (gt911_read(0x5D, 0x814E, &buf, 1) == ESP_OK) {
        s_addr = 0x5D;
    } else {
        ESP_LOGE(TAG, "GT911 not found on I2C bus");
        return false;
    }
    ESP_LOGI(TAG, "GT911 detected at 0x%02X", s_addr);

    s_event_queue = xQueueCreate(TOUCH_QUEUE_DEPTH, sizeof(Event));
    if (!s_event_queue) {
        ESP_LOGE(TAG, "queue create failed");
        return false;
    }

    const BaseType_t rc = xTaskCreatePinnedToCore(
        touch_task, "touch_poll",
        TOUCH_TASK_STACK, nullptr,
        TOUCH_TASK_PRIORITY, &s_task,
        TOUCH_TASK_CORE);
    if (rc != pdPASS) {
        ESP_LOGE(TAG, "touch_task create failed");
        vQueueDelete(s_event_queue);
        s_event_queue = nullptr;
        return false;
    }

    return true;
}

bool poll_tap(int* x_out, int* y_out) {
    if (!s_event_queue) return false;
    Event ev;
    if (xQueueReceive(s_event_queue, &ev, 0) != pdTRUE) {
        return false;
    }
    if (x_out) *x_out = ev.x;
    if (y_out) *y_out = ev.y;
    return true;
}

void drain() {
    if (!s_event_queue) return;
    Event ev;
    while (xQueueReceive(s_event_queue, &ev, 0) == pdTRUE) {}
}

bool current_finger(int* x, int* y) {
    if (!s_live_down) return false;
    if (x) *x = s_live_x;
    if (y) *y = s_live_y;
    return true;
}

void set_inverted(bool inverted) {
    set_rotation(false, inverted);
}

void set_rotation(bool landscape, bool inverted) {
    s_landscape = landscape;
    s_invert_xy = inverted;
}

void light_sleep_until_touch() {
    // GT911 keeps INT LOW while a finger is on the panel (or pulses it
    // on each new sample, depending on configuration). We wake on
    // LOW level — that catches both "user is already touching" and
    // "user starts touching after we slept".
    gpio_wakeup_enable(PIN_GT911_INT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    // Make sure no stale timer wakeup is configured.
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

    esp_light_sleep_start();

    gpio_wakeup_disable(PIN_GT911_INT);
}

WakeReason light_sleep_until_touch_or_timeout(uint64_t timeout_us) {
    gpio_wakeup_enable(PIN_GT911_INT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    if (timeout_us > 0) {
        esp_sleep_enable_timer_wakeup(timeout_us);
    } else {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    }

    esp_light_sleep_start();

    // Sample the cause before tearing down wakeup sources, since the
    // ESP-IDF docs are explicit that disabling a source clears its
    // pending status.
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    gpio_wakeup_disable(PIN_GT911_INT);
    if (timeout_us > 0) {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    }

    return (cause == ESP_SLEEP_WAKEUP_TIMER) ? WakeReason::Timer
                                             : WakeReason::Touch;
}

}  // namespace ps3::touch
