#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cassert>

class HalSpiBus {
 public:
  class Lock {
   public:
    Lock();
    ~Lock();
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
  };

  static HalSpiBus& getInstance();

 private:
  HalSpiBus();

  SemaphoreHandle_t mutex = nullptr;

  friend class Lock;
};
