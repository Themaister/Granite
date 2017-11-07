add_granite_application(gltf-viewer viewer.cpp)
add_granite_headless_application(gltf-viewer-headless viewer.cpp)

if (NOT ANDROID)
    target_compile_definitions(gltf-viewer PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
    target_compile_definitions(gltf-viewer-headless PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

#install_assets(gltf-viewer ${CMAKE_CURRENT_SOURCE_DIR}/assets ${CMAKE_CURRENT_SOURCE_DIR}/../assets/app)
#install_assets(gltf-viewer ${CMAKE_CURRENT_SOURCE_DIR}/../app/assets/builtin ${CMAKE_CURRENT_SOURCE_DIR}/../assets)
