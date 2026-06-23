#include "BatteryManager.h"

namespace paperbadge {
namespace hw {

void BatteryManager::begin() {
  sample_ = {};
  sample_.source = "M5Unified";
}

const BatterySample& BatteryManager::sample(uint32_t nowMs) {
  sample_.voltageMv = M5.Power.getBatteryVoltage();
  sample_.percent = M5.Power.getBatteryLevel();
  sample_.currentMa = M5.Power.getBatteryCurrent();
  sample_.vbusMv = M5.Power.getVBUSVoltage();
  sample_.chargeState = mapChargeState(M5.Power.isCharging());
  sample_.sampledAtMs = nowMs;

  const bool paperS3 = M5.getBoard() == m5::board_t::board_M5PaperS3;
  sample_.source = paperS3 ? "M5Unified ADC (approx)" : "M5Unified";
  sample_.percentApproximate = paperS3;

  if (sample_.percent < 0 || sample_.percent > 100) {
    sample_.percent = estimatePercentFromVoltage(sample_.voltageMv);
    sample_.percentApproximate = true;
  }

  return sample_;
}

const BatterySample& BatteryManager::sampleIfStale(uint32_t intervalMs, uint32_t nowMs) {
  if (sample_.sampledAtMs == 0 || nowMs - sample_.sampledAtMs >= intervalMs) {
    return sample(nowMs);
  }
  return sample_;
}

const char* BatteryManager::chargeStateName(ChargeState state) {
  switch (state) {
    case ChargeState::Charging:
      return "charging";
    case ChargeState::Discharging:
      return "discharging";
    case ChargeState::Unknown:
    default:
      return "unknown";
  }
}

const char* BatteryManager::usbStateName(const BatterySample& sample) {
  if (sample.vbusMv >= 4300) return "yes";
  if (sample.vbusMv >= 0) return "no";
  if (sample.chargeState == ChargeState::Charging) return "likely";
  return "unknown";
}

String BatteryManager::summaryLine() const {
  String line = "battery: ";
  if (sample_.voltageMv > 0) {
    line += sample_.voltageMv;
    line += "mV ";
  } else {
    line += "unknown ";
  }

  if (sample_.percent >= 0) {
    line += sample_.percent;
    line += sample_.percentApproximate ? "% approx" : "%";
  } else {
    line += "--%";
  }

  line += " ";
  line += chargeStateName(sample_.chargeState);
  return line;
}

ChargeState BatteryManager::mapChargeState(m5::Power_Class::is_charging_t state) {
  switch (state) {
    case m5::Power_Class::is_charging:
      return ChargeState::Charging;
    case m5::Power_Class::is_discharging:
      return ChargeState::Discharging;
    case m5::Power_Class::charge_unknown:
    default:
      return ChargeState::Unknown;
  }
}

int32_t BatteryManager::estimatePercentFromVoltage(int16_t voltageMv) {
  if (voltageMv <= 0) return -1;
  const long estimated = map(static_cast<long>(voltageMv), 3300L, 4200L, 0L, 100L);
  return static_cast<int32_t>(constrain(estimated, 0L, 100L));
}

}  // namespace hw
}  // namespace paperbadge
