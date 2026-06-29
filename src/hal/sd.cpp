#include "sd.hpp"

#include <cstdio>

#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

namespace ps3::sd {

namespace {
constexpr const char* TAG = "sd";
constexpr const char* MOUNT_POINT = "/sdcard";

// M5Stack Paper S3 microSD wiring.
constexpr int PIN_MISO = 40;
constexpr int PIN_MOSI = 38;
constexpr int PIN_CLK  = 39;
constexpr int PIN_CS   = 47;

sdmmc_card_t* s_card = nullptr;
bool s_inited = false;
}  // namespace

bool mount() {
    if (s_inited) return true;

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = PIN_MOSI;
    bus_cfg.miso_io_num = PIN_MISO;
    bus_cfg.sclk_io_num = PIN_CLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4096;

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return false;
    }

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = static_cast<gpio_num_t>(PIN_CS);
    slot_cfg.host_id = SPI2_HOST;

    sdmmc_host_t host_cfg = SDSPI_HOST_DEFAULT();
    host_cfg.slot = SPI2_HOST;
    // 20 MHz, the SD-SPI spec ceiling. 40 MHz caused CRC errors at
    // mount on this hardware/card combo and got the card stuck in
    // a degraded mode that needed a power cycle to clear.
    host_cfg.max_freq_khz = 20000;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {};
    mount_cfg.format_if_mount_failed = false;
    mount_cfg.max_files = 5;
    mount_cfg.allocation_unit_size = 16 * 1024;

    err = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host_cfg, &slot_cfg,
                                  &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_sdspi_mount: %s", esp_err_to_name(err));
        spi_bus_free(SPI2_HOST);
        return false;
    }

    sdmmc_card_print_info(stdout, s_card);
    s_inited = true;
    return true;
}

void unmount() {
    if (!s_inited) return;
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
    spi_bus_free(SPI2_HOST);
    s_card = nullptr;
    s_inited = false;
}

}  // namespace ps3::sd
