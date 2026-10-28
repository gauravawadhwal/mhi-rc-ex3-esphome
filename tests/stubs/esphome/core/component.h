#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>
#include <set>
namespace esphome {
inline uint32_t test_millis = 10000;
inline uint32_t millis() { return test_millis; }
namespace setup_priority { constexpr float DATA = 0; }
class PollingComponent { public: virtual void setup() {} virtual void update() {} virtual void loop() {} virtual float get_setup_priority() const { return 0; } };
}
