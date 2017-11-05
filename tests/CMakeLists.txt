add_granite_application(ui-sandbox ui_sandbox.cpp)
target_compile_definitions(ui-sandbox PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

add_granite_application(triangle-test triangle.cpp)
target_compile_definitions(triangle-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

add_granite_offline_tool(thread-group-test thread_group_test.cpp)
target_link_libraries(thread-group-test threading)
