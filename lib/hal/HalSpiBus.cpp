#include "HalSpiBus.h"

HalSpiBus::HalSpiBus() {
  mutex = xSemaphoreCreateRecursiveMutex();
  assert(mutex != nullptr);
}

HalSpiBus& HalSpiBus::getInstance() {
  static HalSpiBus spiBus;
  return spiBus;
}

HalSpiBus::Lock::Lock() { xSemaphoreTakeRecursive(HalSpiBus::getInstance().mutex, portMAX_DELAY); }

HalSpiBus::Lock::~Lock() { xSemaphoreGiveRecursive(HalSpiBus::getInstance().mutex); }
