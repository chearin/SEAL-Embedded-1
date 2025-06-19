rm -rf build
cmake -S . -B build -DSE_BUILD_LOCAL=ON -DCMAKE_BUILD_TYPE=Debug #-DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-elf-toolchain.cmake 
sudo cmake --build build --verbose
sudo ./build/bin/seal_embedded_tests # run tests locally


#! Release, -DCMAKE_BUILD_TYPE=Release