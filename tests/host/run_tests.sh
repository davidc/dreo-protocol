#!/bin/sh
set -eu

mode="${1:-fixed}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build="$root/tests/host/.build"
mkdir -p "$build"

cxxflags="-std=c++17 -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer"
if [ "$mode" = fixed ]; then
  cxxflags="$cxxflags -DDREO_FIXED_TESTS"
fi

# shellcheck disable=SC2086
c++ $cxxflags \
  -I "$root/tests/host/stubs" \
  -I "$root" \
  "$root/components/dreo/dreo.cpp" \
  "$root/components/dreo/select/dreo_select.cpp" \
  "$root/components/dreo/binary_sensor/dreo_binary_sensor.cpp" \
  "$root/tests/host/dreo_host_test.cpp" \
  -o "$build/dreo_host_test"

cd "$root"
"$build/dreo_host_test" "$mode"

if [ "$mode" = fixed ]; then
  PYTHONDONTWRITEBYTECODE=1 python3 tests/host/test_decode.py --fixed
else
  PYTHONDONTWRITEBYTECODE=1 python3 tests/host/test_decode.py
fi
