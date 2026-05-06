#include "HalSpiBus.h"

#include <Logging.h>

HalSpiBus::HalSpiBus() {
  mutex = xSemaphoreCreateRecursiveMutex();
  if (mutex == nullptr) {
    LOG_ERR("SPI", "Failed to create SPI bus mutex - bus is unusable");
  }
}

HalSpiBus& HalSpiBus::getInstance() {
  static HalSpiBus spiBus;
  return spiBus;
}

HalSpiBus::Lock::Lock() {
  auto& bus = HalSpiBus::getInstance();
  if (bus.mutex == nullptr) {
    LOG_ERR("SPI", "SPI bus mutex not initialized, skipping lock");
    return;
  }
  xSemaphoreTakeRecursive(bus.mutex, portMAX_DELAY);
  acquired = true;
}

HalSpiBus::Lock::~Lock() {
  if (!acquired) return;
  xSemaphoreGiveRecursive(HalSpiBus::getInstance().mutex);
}
