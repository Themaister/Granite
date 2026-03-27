#!/bin/bash

# This is run from inside the container.
# Useful as an initial template.

mkdir -p build-steamrt-aarch64
cd build-steamrt-aarch64
cmake .. \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX=output \
	-DGRANITE_VIEWER_INSTALL=ON \
	-DGRANITE_AUDIO=ON \
	-DGRANITE_SHIPPING=ON \
	-DGRANITE_SYSTEM_SDL=ON \
	-DGRANITE_ASTC_ENCODER_COMPRESSION=OFF \
	--toolchain=/usr/share/steamrt/cmake/aarch64-linux-gnu-gcc.cmake \
	-G Ninja

ninja install/strip -v

