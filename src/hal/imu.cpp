#include "imu.hpp"

#include <driver/i2c.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

namespace ps3::imu {

namespace {
constexpr const char* TAG = "imu";
constexpr i2c_port_t I2C_PORT = I2C_NUM_0;
constexpr uint8_t BMI_CHIP_ID_REG = 0x00;
constexpr uint8_t BMI270_EXPECTED_CHIP_ID = 0x24;

ProbeResult s_last_probe;

esp_err_t read_reg(uint8_t addr, uint8_t reg, uint8_t* value) {
    if (!value) return ESP_ERR_INVALID_ARG;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) return ESP_FAIL;

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, value, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    const esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return ret;
}

}  // namespace

ProbeResult probe() {
    s_last_probe = {};
    ESP_LOGI(TAG, "BMI270 probe on I2C0 SDA=41 SCL=42 addresses=0x68,0x69 chip_id_reg=0x00");
    const uint8_t addresses[] = {0x68, 0x69};
    for (const uint8_t addr : addresses) {
        uint8_t chip_id = 0;
        const esp_err_t err = read_reg(addr, BMI_CHIP_ID_REG, &chip_id);
        if (err == ESP_OK) {
            s_last_probe.found = true;
            s_last_probe.address = addr;
            s_last_probe.chip_id = chip_id;
            ESP_LOGI(TAG, "IMU responded at 0x%02X chip_id=0x%02X%s",
                     addr, chip_id,
                     chip_id == BMI270_EXPECTED_CHIP_ID ? " (BMI270 expected id)" : "");
            return s_last_probe;
        }
        ESP_LOGW(TAG, "no IMU response at 0x%02X: %s", addr, esp_err_to_name(err));
    }
    ESP_LOGW(TAG, "no BMI270-compatible IMU responded; manga orientation remains manual");
    return s_last_probe;
}

ProbeResult last_probe() {
    return s_last_probe;
}

bool available() {
    return s_last_probe.found;
}

}  // namespace ps3::imu
