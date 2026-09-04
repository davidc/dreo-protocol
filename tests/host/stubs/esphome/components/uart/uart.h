#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace esphome::uart {

class UARTDevice {
 public:
  size_t available() const { return 0; }
  bool read_array(uint8_t *, size_t) { return false; }
  void write_array(std::initializer_list<uint8_t> data) { tx_bytes.insert(tx_bytes.end(), data); }
  void write_array(const uint8_t *data, size_t size) { tx_bytes.insert(tx_bytes.end(), data, data + size); }
  void write_byte(uint8_t value) { tx_bytes.push_back(value); }

  std::vector<uint8_t> tx_bytes;
};

}  // namespace esphome::uart
