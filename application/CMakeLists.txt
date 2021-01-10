add_granite_internal_lib(granite-application
        application.hpp
        application.cpp
        platforms/application_headless.cpp platforms/hw_counters/hw_counter_interface.h
        application_cli_wrapper.cpp application_cli_wrapper.hpp)

target_link_libraries(granite-application PUBLIC
        granite-vulkan
        granite-event
        granite-input
        granite-application-global-init
        granite-application-events
        granite-renderer
        granite-threading
        PRIVATE granite-stb granite-rapidjson granite-ui)

target_include_directories(granite-application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_granite_internal_lib(granite-scene-viewer scene_viewer_application.cpp scene_viewer_application.hpp)
target_include_directories(granite-scene-viewer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(granite-scene-viewer
        PUBLIC granite-scene-export granite-ui granite-application
        PRIVATE granite-rapidjson)

if (NOT ANDROID)
    add_granite_internal_lib(granite-application-entry STATIC application_entry.cpp)
    target_compile_options(granite-application-entry PRIVATE ${GRANITE_CXX_FLAGS})
    target_link_libraries(granite-application-entry
            PRIVATE granite-application granite-platform granite-filesystem)
endif()

add_library(granite-application-entry-headless STATIC application_entry.cpp)
target_compile_definitions(granite-application-entry-headless PRIVATE APPLICATION_ENTRY_HEADLESS=1)
target_link_libraries(granite-application-entry-headless
        PRIVATE granite-application granite-platform granite-filesystem)
target_compile_options(granite-application-entry-headless PRIVATE ${GRANITE_CXX_FLAGS})

add_subdirectory(events)
add_subdirectory(input)
add_subdirectory(platforms)
add_subdirectory(global)

if (GRANITE_AUDIO)
    target_link_libraries(granite-application PRIVATE granite-audio)
endif()
