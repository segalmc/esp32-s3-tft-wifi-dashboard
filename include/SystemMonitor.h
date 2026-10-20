#pragma once

#include <stdint.h>

class SystemMonitor {
 public:
  void begin();
  void update(uint32_t now_ms);

 private:
  uint32_t started_ms_ = 0;
  uint32_t last_report_ms_ = 0;
  uint32_t seconds_ = 0;
  bool boot_replayed_ = false;
};

