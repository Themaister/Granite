#!/bin/bash
../tools/create_android_build.py \
	--output-gradle android \
	--application-id net.themaister.granite.test \
	--granite-dir .. \
	--native-target "$1" \
	--app-name "Granite Test" \
	--abis arm64-v8a \
	--cmake-lists-toplevel ../CMakeLists.txt \
	--assets assets
