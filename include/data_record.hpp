#pragma once
#include <cstdint>

struct DataRecord {
  uint32_t provider_peer;
  uint32_t customer_peer;
  int indicator;
};
