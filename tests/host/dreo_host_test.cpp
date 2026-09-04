#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#define protected public
#include "components/dreo/dreo.h"
#include "components/dreo/binary_sensor/dreo_binary_sensor.h"
#include "components/dreo/select/dreo_select.h"
#undef protected

using esphome::dreo::Dreo;
using esphome::dreo::DreoBinarySensor;
using esphome::dreo::DreoDatapoint;
using esphome::dreo::DreoDatapointType;
using esphome::dreo::DreoSelect;

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

std::vector<uint8_t> from_hex(const std::string &hex) {
  require(hex.size() % 2 == 0, "fixture has an odd number of hex digits");
  std::vector<uint8_t> data;
  data.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2)
    data.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
  return data;
}

std::vector<uint8_t> load_fixture(const std::string &name) {
  std::ifstream input("tests/host/fixtures/" + name + ".hex");
  require(input.good(), "cannot open fixture " + name);
  std::ostringstream contents;
  contents << input.rdbuf();
  std::string hex;
  for (char c : contents.str()) {
    if (!std::isspace(static_cast<unsigned char>(c)))
      hex.push_back(c);
  }
  return from_hex(hex);
}

std::vector<uint8_t> integer_dp(uint8_t id, const std::vector<uint8_t> &value) {
  std::vector<uint8_t> result{id, 1, static_cast<uint8_t>(DreoDatapointType::INTEGER),
                              static_cast<uint8_t>(value.size() >> 8), static_cast<uint8_t>(value.size())};
  result.insert(result.end(), value.begin(), value.end());
  return result;
}

void append(std::vector<uint8_t> &target, const std::vector<uint8_t> &value) {
  target.insert(target.end(), value.begin(), value.end());
}

const DreoDatapoint *find_dp(const Dreo &dreo, uint8_t id) {
  for (const auto &datapoint : dreo.datapoints_) {
    if (datapoint.id == id)
      return &datapoint;
  }
  return nullptr;
}

void test_marker_default() {
  Dreo dreo;
  dreo.send_datapoint_command_(7, DreoDatapointType::INTEGER, {0});
  require(dreo.command_queue_.size() == 1, "marker test did not queue a command");
  require(dreo.command_queue_[0].payload.size() == 6, "marker test queued an unexpected payload");
  require(dreo.command_queue_[0].payload[1] == 0, "default datapoint marker is not zero");
}

void run_baseline() {
  {
    Dreo dreo;
    auto body = integer_dp(40, {0x7f, 0xff, 0xff, 0xff});
    append(body, {41, 1, static_cast<uint8_t>(DreoDatapointType::ENUM), 0, 1, 9});
    dreo.handle_datapoints_(body.data(), body.size());
    require(find_dp(dreo, 40) != nullptr, "four-byte integer baseline did not parse");
    require(find_dp(dreo, 41) != nullptr, "four-byte positive control did not continue");
  }
  for (const auto &value : {std::vector<uint8_t>{0x7f}, std::vector<uint8_t>{0x7f, 0xff}}) {
    Dreo dreo;
    auto body = integer_dp(40, value);
    append(body, {41, 1, static_cast<uint8_t>(DreoDatapointType::ENUM), 0, 1, 9});
    dreo.handle_datapoints_(body.data(), body.size());
    require(dreo.datapoints_.empty(), "narrow integer unexpectedly parsed in baseline");
  }
  {
    Dreo dreo;
    auto report = load_fixture("full_report");
    dreo.handle_datapoints_(report.data(), report.size());
    require(dreo.datapoints_.size() == 3, "real-report baseline did not stop at dp4");
    require(dreo.datapoints_[0].id == 1 && dreo.datapoints_[1].id == 2 && dreo.datapoints_[2].id == 3,
            "real-report baseline stored an unexpected datapoint set");
  }
  {
    Dreo dreo;
    auto report = load_fixture("string_first");
    dreo.handle_datapoints_(report.data(), report.size());
    require(dreo.datapoints_.empty(), "string-first baseline unexpectedly continued");
  }
  test_marker_default();
  std::cout << "PASS: baseline observes one- and two-byte rejection, four-byte success, dp4 truncation, "
               "string-first truncation, and marker 0\n";
}

#ifdef DREO_FIXED_TESTS
void require_integer(uint8_t width, const std::vector<uint8_t> &bytes, int32_t expected) {
  Dreo dreo;
  auto body = integer_dp(width, bytes);
  dreo.handle_datapoints_(body.data(), body.size());
  auto *datapoint = find_dp(dreo, width);
  require(datapoint != nullptr, "supported integer width was rejected");
  require(datapoint->len == bytes.size(), "integer width was not retained");
  require(datapoint->value_int == expected, "integer was not sign-extended correctly");
}

void run_fixed() {
  require_integer(1, {0x80}, std::numeric_limits<int8_t>::min());
  require_integer(2, {0x80, 0x00}, std::numeric_limits<int16_t>::min());
  require_integer(4, {0x80, 0x00, 0x00, 0x00}, std::numeric_limits<int32_t>::min());
  require_integer(11, {0x00}, 0);
  require_integer(12, {0x00, 0x00}, 0);
  require_integer(14, {0x00, 0x00, 0x00, 0x00}, 0);
  require_integer(21, {0x7f}, std::numeric_limits<int8_t>::max());
  require_integer(22, {0x7f, 0xff}, std::numeric_limits<int16_t>::max());
  require_integer(24, {0x7f, 0xff, 0xff, 0xff}, std::numeric_limits<int32_t>::max());

  for (size_t width : {size_t{0}, size_t{3}, size_t{5}}) {
    Dreo dreo;
    auto body = integer_dp(50, std::vector<uint8_t>(width, 0));
    append(body, {51, 1, static_cast<uint8_t>(DreoDatapointType::ENUM), 0, 1, 9});
    dreo.handle_datapoints_(body.data(), body.size());
    require(dreo.datapoints_.empty(), "invalid integer width did not reject the report");
  }

  {
    Dreo dreo;
    auto report = load_fixture("full_report");
    dreo.handle_datapoints_(report.data(), report.size());
    const std::vector<std::pair<uint8_t, int32_t>> expected{{4, 1},  {5, 7},  {6, 1},  {7, 1},  {8, 70},
                                                            {9, 0},  {10, 0}, {12, 3}, {13, 0}, {14, 87},
                                                            {15, 0}, {16, 0}, {17, 0}, {18, 0}, {19, 0},
                                                            {20, 61}, {22, 0}, {23, 0}, {25, 0}, {26, 0},
                                                            {27, 0}, {28, 0}};
    for (const auto &[id, value] : expected) {
      auto *datapoint = find_dp(dreo, id);
      require(datapoint != nullptr, "full report lost a supported datapoint after dp4");
      if (datapoint->type == DreoDatapointType::INTEGER)
        require(datapoint->value_int == value, "full report stored a wrong integer value");
      else if (datapoint->type == DreoDatapointType::BOOLEAN)
        require(datapoint->value_bool == static_cast<bool>(value), "full report stored a wrong boolean value");
    }
    require(find_dp(dreo, 11) == nullptr, "unknown string datapoint was published");
  }
  {
    Dreo dreo;
    auto report = load_fixture("string_first");
    dreo.handle_datapoints_(report.data(), report.size());
    require(find_dp(dreo, 12) != nullptr && find_dp(dreo, 12)->value_int == 3,
            "string-first report did not continue at dp12");
    require(find_dp(dreo, 28) != nullptr, "string-first report did not reach dp28");
    require(find_dp(dreo, 11) == nullptr, "string-first report published the string");
  }

  {
    Dreo dreo;
    dreo.set_integer_datapoint_value(60, 0x01020304);
    require(dreo.command_queue_.back().payload == std::vector<uint8_t>({60, 0, 2, 0, 4, 1, 2, 3, 4}),
            "integer write did not default to four bytes");
  }
  const std::vector<uint8_t> encoded_value{1, 2, 3, 4};
  for (uint8_t width : {uint8_t{1}, uint8_t{2}, uint8_t{4}}) {
    Dreo dreo;
    auto observed = integer_dp(61, std::vector<uint8_t>(width, 0));
    dreo.handle_datapoints_(observed.data(), observed.size());
    dreo.set_integer_datapoint_value(61, 0x01020304);
    const auto &normal = dreo.command_queue_.back().payload;
    require(normal.size() == static_cast<size_t>(5 + width), "normal setter ignored observed width");
    const std::vector<uint8_t> expected(encoded_value.end() - width, encoded_value.end());
    require(std::vector<uint8_t>(normal.end() - width, normal.end()) == expected,
            "normal setter did not emit the expected big-endian suffix");
    dreo.force_set_integer_datapoint_value(61, 0x01020304);
    const auto &forced = dreo.command_queue_.back().payload;
    require(forced.size() == static_cast<size_t>(5 + width), "forced setter ignored observed width");
    require(std::vector<uint8_t>(forced.end() - width, forced.end()) == expected,
            "forced setter did not emit the expected big-endian suffix");
  }

  {
    Dreo dreo;
    test_marker_default();
    dreo.set_command_datapoint_marker(1);
    dreo.send_datapoint_command_(7, DreoDatapointType::INTEGER, {0});
    require(dreo.command_queue_.back().payload[1] == 1, "configured datapoint marker was not emitted");
  }

  {
    Dreo dreo;
    DreoSelect select;
    select.set_dreo_parent(&dreo);
    select.set_select_id(70, true);
    select.set_select_mappings({4, 7});
    select.traits.options_ = {"four", "seven"};
    select.setup();
    auto body = integer_dp(70, {7});
    dreo.handle_datapoints_(body.data(), body.size());
    require(select.publish_count == 1 && select.last_state == 1, "integer select mapping was not type-safe");
  }
  {
    Dreo dreo;
    DreoBinarySensor sensor;
    sensor.set_dreo_parent(&dreo);
    sensor.set_sensor_id(71);
    sensor.setup();
    for (uint8_t value : {uint8_t{0}, uint8_t{1}, uint8_t{4}}) {
      auto body = integer_dp(71, {value});
      dreo.handle_datapoints_(body.data(), body.size());
      require(sensor.state == (value != 0), "integer binary-sensor mapping was not type-safe");
    }
  }

  std::cout << "PASS: fixed C++ product source handles widths, continuation, writes, marker, select, and binary sensor\n";
}
#endif

}  // namespace

int main(int argc, char **argv) {
  const std::string mode = argc > 1 ? argv[1] : "fixed";
  if (mode == "marker") {
    test_marker_default();
    std::cout << "PASS: product-source marker assertion\n";
    return 0;
  }
  if (mode == "baseline") {
    run_baseline();
    return 0;
  }
#ifdef DREO_FIXED_TESTS
  if (mode == "fixed") {
    run_fixed();
    return 0;
  }
#endif
  std::cerr << "unknown or unavailable mode: " << mode << '\n';
  return 2;
}
