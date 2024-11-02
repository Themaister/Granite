add_granite_internal_lib(granite-application
        application.hpp
        application_glue.hpp
        application.cpp)

if (GRANITE_FFMPEG)
    target_link_libraries(granite-application PRIVATE granite-video)
endif()

target_link_libraries(granite-application PUBLIC
        granite-vulkan
        granite-event
        granite-input
        granite-application-global-init
        granite-application-events
        granite-threading
        granite-filesystem)

if (NOT (${GRANITE_PLATFORM} MATCHES "null"))
    target_sources(granite-application PRIVATE platforms/application_headless.cpp)
    target_link_libraries(granite-application PRIVATE granite-stb granite-rapidjson)
endif()

target_include_directories(granite-application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (TARGET granite-renderer)
    target_link_libraries(granite-application PUBLIC granite-renderer PRIVATE granite-ui)
    target_compile_definitions(granite-application PRIVATE HAVE_GRANITE_RENDERER)

    add_granite_internal_lib(granite-scene-viewer scene_viewer_application.cpp scene_viewer_application.hpp)
    target_include_directories(granite-scene-viewer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(granite-scene-viewer
            PUBLIC granite-scene-export granite-ui granite-application
            PRIVATE granite-rapidjson)
endif()

# Can be defined by application to get a custom entry point.
# Otherwise, get a default one.
if (NOT TARGET granite-application-interface-query)
    add_granite_internal_lib(granite-application-interface-query STATIC application_interface_query.cpp)
    target_compile_options(granite-application-interface-query PRIVATE ${GRANITE_CXX_FLAGS})
endif()

if (NOT ANDROID)
    add_granite_internal_lib(granite-application-entry STATIC application_entry.cpp)
    target_compile_options(granite-application-entry PRIVATE ${GRANITE_CXX_FLAGS})
    target_link_libraries(granite-application-entry
            PRIVATE granite-application-interface-query granite-application granite-platform granite-filesystem)
endif()

add_library(granite-application-entry-headless STATIC application_entry.cpp)
target_compile_definitions(granite-application-entry-headless PRIVATE APPLICATION_ENTRY_HEADLESS=1)
target_link_libraries(granite-application-entry-headless
        PRIVATE granite-application-interface-query granite-application granite-platform granite-filesystem)
target_compile_options(granite-application-entry-headless PRIVATE ${GRANITE_CXX_FLAGS})

add_subdirectory(events)
add_subdirectory(input)
add_subdirectory(platforms)
add_subdirectory(global)

if (GRANITE_AUDIO)
    target_link_libraries(granite-application PRIVATE granite-audio)
endif()
