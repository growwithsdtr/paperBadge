#pragma once

#include <Arduino.h>
#include <M5Unified.h>

namespace paperbadge {
namespace hw {

enum class ChargeState : uint8_t {
  Discharging,
  Charging,
  Unknown,
};

struct BatterySample {
  int16_t voltageMv = -1;
  int32_t percent = -1;
  bool percentApproximate = true;
  int32_t currentMa = 0;
  int16_t vbusMv = -1;
  ChargeState chargeState = ChargeState::Unknown;
  uint32_t sampledAtMs = 0;
  const char* source = "not sampled";

  bool usbPresent() const {
    return vbusMv >= 4300 || chargeState == ChargeState::Charging;
  }
};

class BatteryManager {
 public:
  void begin();
  const BatterySample& sample(uint32_t nowMs = millis());
  const BatterySample& sampleIfStale(uint32_t intervalMs, uint32_t nowMs = millis());
  const BatterySample& latest() const { return sample_; }

  static const char* chargeStateName(ChargeState state);
  static const char* usbStateName(const BatterySample& sample);
  String summaryLine() const;

 private:
  static ChargeState mapChargeState(m5::Power_Class::is_charging_t state);
  static int32_t estimatePercentFromVoltage(int16_t voltageMv);

  BatterySample sample_;
};

}  // namespace hw
}  // namespace paperbadge
