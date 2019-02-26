#!/bin/bash
../tools/create_android_build.py \
	--output-gradle android \
	--application-id net.themaister.gltf_viewer \
	--granite-dir .. \
	--native-target gltf-viewer \
	--app-name "Granite glTF Viewer" \
	--abis armeabi-v7a \
	--cmake-lists-toplevel ../CMakeLists.txt \
	--assets assets
