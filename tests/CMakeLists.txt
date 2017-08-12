add_granite_application(gltf-viewer viewer.cpp)
target_compile_definitions(gltf-viewer PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
