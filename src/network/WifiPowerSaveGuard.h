#pragma once

#ifndef SIMULATOR
#include "esp_wifi.h"
#endif

class WifiPowerSaveGuard final {
 public:
#ifndef SIMULATOR
  WifiPowerSaveGuard() {
    if (esp_wifi_get_ps(&previousMode_) == ESP_OK) {
      restorePreviousMode_ = true;
    }
    esp_wifi_set_ps(WIFI_PS_NONE);
  }

  ~WifiPowerSaveGuard() {
    if (restorePreviousMode_) {
      esp_wifi_set_ps(previousMode_);
    }
  }
#else
  WifiPowerSaveGuard() = default;
  ~WifiPowerSaveGuard() = default;
#endif

  WifiPowerSaveGuard(const WifiPowerSaveGuard&) = delete;
  WifiPowerSaveGuard& operator=(const WifiPowerSaveGuard&) = delete;

 private:
#ifndef SIMULATOR
  wifi_ps_type_t previousMode_ = WIFI_PS_MIN_MODEM;
  bool restorePreviousMode_ = false;
#endif
};
