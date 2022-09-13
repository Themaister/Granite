#!/bin/bash

# This is run from inside the container.
# Useful as an initial template.

mkdir -p build-steamrt
cd build-steamrt
cmake .. \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX=output \
	-DGRANITE_VIEWER_INSTALL=ON \
	-DGRANITE_AUDIO=ON \
	-DGRANITE_ASTC_ENCODER_COMPRESSION=OFF \
	-DPYTHON_EXECUTABLE=$(which python3) \
	-G Ninja

ninja install/strip -v

