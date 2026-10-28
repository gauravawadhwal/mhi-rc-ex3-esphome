#pragma once
#include "esphome/core/component.h"
namespace esphome::uart {
class UARTDevice { public:
 std::vector<uint8_t> tx;
 bool available() { return false; }
 bool read_byte(uint8_t*) { return false; }
 void write_byte(uint8_t c) { tx.push_back(c); }
}; }
