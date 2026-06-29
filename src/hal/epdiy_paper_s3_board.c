// M5Stack Paper S3 board definition for epdiy.
//
// Pin mapping derived from M5GFX's Bus_EPD configuration. epdiy itself does
// not ship a Paper S3 board, so we supply our own EpdBoardDefinition and
// register it from the application via epd_set_board().

#include "epd_board.h"
#include "epd_display.h"
#include <epdiy.h>

#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdbool.h>
#include <stddef.h>

// Local copy of the LCD bus config types used by epdiy's lcd_driver. We do
// not pull lcd_driver.h here so that this file stays a small, freestanding
// board definition.
typedef struct {
    gpio_num_t data[16];
    gpio_num_t clock;
    gpio_num_t ckv;
    gpio_num_t start_pulse;
    gpio_num_t leh;
    gpio_num_t stv;
} lcd_bus_config_t;

typedef struct {
    size_t pixel_clock;
    int ckv_high_time;
    int line_front_porch;
    int le_high_time;
    int bus_width;
    lcd_bus_config_t bus;
} LcdEpdConfig_t;

void epd_lcd_init(const LcdEpdConfig_t* config, int display_width, int display_height);
void epd_lcd_deinit(void);

static const char* TAG = "ps3_board";

// Data bus (8-bit)
#define PS3_D0 GPIO_NUM_6
#define PS3_D1 GPIO_NUM_14
#define PS3_D2 GPIO_NUM_7
#define PS3_D3 GPIO_NUM_12
#define PS3_D4 GPIO_NUM_9
#define PS3_D5 GPIO_NUM_11
#define PS3_D6 GPIO_NUM_8
#define PS3_D7 GPIO_NUM_10

// Control lines
#define PS3_PWR GPIO_NUM_46
#define PS3_SPH GPIO_NUM_13
#define PS3_SPV GPIO_NUM_17
#define PS3_OE  GPIO_NUM_45
#define PS3_LE  GPIO_NUM_15
#define PS3_CL  GPIO_NUM_16
#define PS3_CKV GPIO_NUM_18

static bool s_powered = false;

static void ps3_config_pins(void) {
    gpio_config_t io = {0};
    io.intr_type = GPIO_INTR_DISABLE;
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pin_bit_mask = (1ULL << PS3_PWR) | (1ULL << PS3_SPH) | (1ULL << PS3_SPV)
                    | (1ULL << PS3_OE)  | (1ULL << PS3_LE)  | (1ULL << PS3_CL)
                    | (1ULL << PS3_CKV);
    gpio_config(&io);

    gpio_set_level(PS3_PWR, 0);
    gpio_set_level(PS3_OE, 0);
    gpio_set_level(PS3_SPV, 0);
    gpio_set_level(PS3_SPH, 0);
    gpio_set_level(PS3_LE, 0);
    gpio_set_level(PS3_CL, 0);
    gpio_set_level(PS3_CKV, 0);
}

static void ps3_bus_init(void) {
    const EpdDisplay_t* display = epd_get_display();

    lcd_bus_config_t bus = {
        .data = {PS3_D0, PS3_D1, PS3_D2, PS3_D3, PS3_D4, PS3_D5, PS3_D6, PS3_D7,
                 -1, -1, -1, -1, -1, -1, -1, -1},
        .clock = PS3_CL,
        .ckv = PS3_CKV,
        .start_pulse = PS3_SPH,
        .leh = PS3_LE,
        .stv = PS3_SPV,
    };

    LcdEpdConfig_t cfg = {
        .pixel_clock = display->bus_speed * 1000 * 1000,
        .ckv_high_time = 60,
        .line_front_porch = 4,
        .le_high_time = 4,
        .bus_width = display->bus_width,
        .bus = bus,
    };

    ESP_LOGI(TAG, "Init Paper S3 EPD bus %dx%d, bus_width=%d",
             display->width, display->height, display->bus_width);
    epd_lcd_init(&cfg, display->width, display->height);
}

static void ps3_power_control(bool on) {
    if (on == s_powered) return;
    s_powered = on;

    if (on) {
        gpio_set_level(PS3_OE, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_set_level(PS3_PWR, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_set_level(PS3_SPV, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
    } else {
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_set_level(PS3_PWR, 0);
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_set_level(PS3_OE, 0);
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_set_level(PS3_SPV, 0);
    }
}

static void ps3_init(uint32_t epd_row_width) {
    (void)epd_row_width;
    ps3_config_pins();
    ps3_bus_init();
}

static void ps3_deinit(void) {
    epd_lcd_deinit();
    ps3_power_control(false);
}

static void ps3_set_ctrl(epd_ctrl_state_t* state, const epd_ctrl_state_t* const mask) {
    (void)mask;
    ps3_power_control(state->ep_output_enable);
}

static void ps3_poweron(epd_ctrl_state_t* state) {
    state->ep_output_enable = true;
    epd_ctrl_state_t mask = { .ep_output_enable = true };
    ps3_set_ctrl(state, &mask);
}

static void ps3_poweroff(epd_ctrl_state_t* state) {
    state->ep_output_enable = false;
    epd_ctrl_state_t mask = { .ep_output_enable = true };
    ps3_set_ctrl(state, &mask);
}

static float ps3_temperature(void) {
    // TODO: Phase 1 — read from on-board temperature sensor if present.
    return 20.0f;
}

static void ps3_set_vcom(int value) {
    (void)value;
}

const EpdBoardDefinition paper_s3_board = {
    .init = ps3_init,
    .deinit = ps3_deinit,
    .set_ctrl = ps3_set_ctrl,
    .poweron = ps3_poweron,
    .measure_vcom = NULL,
    .poweroff = ps3_poweroff,
    .set_vcom = ps3_set_vcom,
    .get_temperature = ps3_temperature,
    .gpio_set_direction = NULL,
    .gpio_read = NULL,
    .gpio_write = NULL,
};
