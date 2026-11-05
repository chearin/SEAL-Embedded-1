#!/bin/bash
# usage: build_one.sh <tree_dir> <variant>
d="$1"; v="$2"
rm -rf "$d/build"
if [ "$v" = meas ] || [ "$v" = csv ]; then
  cmake -S "$d" -B "$d/build" -DSE_BUILD_LOCAL=ON -DCMAKE_BUILD_TYPE=Release > "$d/cmake.log" 2>&1 || { echo "CMAKE_FAIL $d"; exit 1; }
else
  cmake -S "$d" -B "$d/build" -DSE_BUILD_LOCAL=ON -DCMAKE_BUILD_TYPE=Release "-DCMAKE_C_FLAGS_RELEASE=-O3 -USE_UNDEF_ASSERT_FORCE" > "$d/cmake.log" 2>&1 || { echo "CMAKE_FAIL $d"; exit 1; }
fi
cmake --build "$d/build" --verbose > "$d/build.log" 2>&1 || { echo "BUILD_FAIL $d"; exit 1; }
test -x "$d/build/bin/seal_embedded_tests" && echo "OK $d" || echo "NOBIN $d"
