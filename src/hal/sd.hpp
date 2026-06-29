#pragma once

namespace ps3::sd {

// Mount the SD card via SPI on the M5Stack Paper S3 wiring
// (MISO=40, MOSI=38, CLK=39, CS=47). Mount point: "/sdcard".
//
// Idempotent — safe to call more than once.
bool mount();

// Tear down the FAT VFS mount and free SPI resources.
void unmount();

}  // namespace ps3::sd
