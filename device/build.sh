cmake -S . -B build -DSE_BUILD_LOCAL=ON
cmake --build build -j
./build/bin/seal_embedded_tests # run tests locally