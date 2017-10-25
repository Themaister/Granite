add_granite_application(gltf-viewer viewer.cpp)
target_compile_definitions(gltf-viewer PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

if (ANDROID)
	install_assets(gltf-viewer ${CMAKE_CURRENT_SOURCE_DIR}/../app/assets/builtin ${CMAKE_CURRENT_SOURCE_DIR}/../assets)
endif()

add_granite_application(ui-sandbox ui_sandbox.cpp)
target_compile_definitions(ui-sandbox PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

add_granite_application(triangle-test triangle.cpp)
target_compile_definitions(triangle-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
