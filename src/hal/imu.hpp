#pragma once

#include <cstdint>

namespace ps3::imu {

struct ProbeResult {
    bool found = false;
    uint8_t address = 0;
    uint8_t chip_id = 0;
};

ProbeResult probe();
ProbeResult last_probe();
bool available();

}  // namespace ps3::imu
