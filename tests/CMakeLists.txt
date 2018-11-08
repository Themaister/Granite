add_granite_application(ui-sandbox ui_sandbox.cpp)
target_compile_definitions(ui-sandbox PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

add_granite_application(triangle-test triangle.cpp)
if (NOT ANDROID)
	target_compile_definitions(triangle-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

add_granite_application(display-timing-test display_timing.cpp)
if (NOT ANDROID)
	target_compile_definitions(display-timing-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

add_granite_application(clustering-visualizer clustering_visualizer.cpp)
target_compile_definitions(clustering-visualizer PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

add_granite_application(render-graph-sandbox render_graph_sandbox.cpp)
target_compile_definitions(render-graph-sandbox PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

add_granite_application(bandlimited-pixel-test bandlimited_pixel_test.cpp)
target_compile_definitions(bandlimited-pixel-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

add_granite_offline_tool(thread-group-test thread_group_test.cpp)
add_granite_offline_tool(intrusive-test intrusive_ptr_test.cpp)
add_granite_offline_tool(ecs-test ecs_test.cpp)

if (GRANITE_AUDIO)
    add_granite_offline_tool(audio-test audio_test.cpp)
	target_compile_definitions(audio-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

    add_granite_application(audio-application audio_application.cpp)
    if (NOT ANDROID)
        target_compile_definitions(audio-application PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
    endif()
endif()
