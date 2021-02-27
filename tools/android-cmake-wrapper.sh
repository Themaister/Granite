#!/bin/bash

if [ -z $ANDROID_HOME ]; then
	echo "ANDROID_HOME is not set!"
	exit 1
fi

cmake $@ \
	-DCMAKE_TOOLCHAIN_FILE="$ANDROID_HOME/ndk-bundle/build/cmake/android.toolchain.cmake" \
	-DANDROID_TOOLCHAIN=clang \
	-DANDROID_ARM_MODE=arm \
	-DANDROID_CPP_FEATURES=exceptions \
	-DANDROID_PLATFORM=android-26 \
	-DANDROID_ARM_NEON=ON \
	-DANDROID_ABI=arm64-v8a
