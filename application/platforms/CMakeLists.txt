# Must be static due to Granite::application_create() feedback.

if (ANDROID)
    add_granite_internal_static_lib(granite-platform application_android.cpp)
    find_package(game-activity REQUIRED CONFIG)
    find_package(games-controller REQUIRED CONFIG)
    find_package(games-frame-pacing REQUIRED CONFIG)
    get_target_property(game-activity-include game-activity::game-activity INTERFACE_INCLUDE_DIRECTORIES)
    add_library(android-game-activity STATIC
        ${game-activity-include}/game-activity/native_app_glue/android_native_app_glue.c
        ${game-activity-include}/game-activity/GameActivity.cpp
        ${game-activity-include}/game-activity/GameActivityEvents.cpp
        ${game-activity-include}/game-text-input/gametextinput.cpp)
    target_include_directories(android-game-activity PUBLIC ${game-activity-include})
    target_link_libraries(granite-platform PRIVATE granite-rapidjson android-game-activity
            games-controller::paddleboat games-frame-pacing::swappy)

    target_compile_definitions(granite-platform PRIVATE VK_USE_PLATFORM_ANDROID_KHR)
    if (GRANITE_ANDROID_SWAPPY)
        target_compile_definitions(granite-platform PRIVATE HAVE_SWAPPY)
    endif()
    if (GRANITE_ANDROID_APK_FILESYSTEM)
        target_compile_definitions(granite-platform PRIVATE ANDROID_APK_FILESYSTEM)
    endif()
elseif (${GRANITE_PLATFORM} MATCHES "libretro")
    add_granite_internal_static_lib(granite-platform
            application_libretro.cpp
            application_libretro_utils.cpp
            application_libretro_utils.hpp)
    target_compile_definitions(granite-platform PUBLIC HAVE_LIBRETRO)
    target_include_directories(granite-platform PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/libretro)
elseif (${GRANITE_PLATFORM} MATCHES "SDL")
    add_granite_internal_static_lib(granite-platform application_sdl3.cpp)

    if (GRANITE_SYSTEM_SDL)
        find_package(SDL3 REQUIRED)
        target_link_libraries(granite-platform PRIVATE SDL3::SDL3-shared)
    else()
        target_link_libraries(granite-platform PRIVATE SDL3-static)
        if (NOT WIN32)
            target_link_libraries(granite-platform PRIVATE dl)
        endif()
    endif()
    target_link_libraries(granite-platform PRIVATE granite-input-sdl)
elseif (${GRANITE_PLATFORM} MATCHES "headless")
    add_granite_internal_static_lib(granite-platform application_headless_wrapper.cpp)
elseif (${GRANITE_PLATFORM} MATCHES "null")
    add_granite_internal_static_lib(granite-platform application_null.cpp)
else()
    message(FATAL "GRANITE_PLATFORM is not set.")
endif()

if (NOT (${GRANITE_PLATFORM} MATCHES "null"))
    target_link_libraries(granite-platform PRIVATE granite-application-interface-query granite-application granite-input granite-application-global-init)
endif()

if (GRANITE_AUDIO)
    target_link_libraries(granite-platform PRIVATE granite-audio)
endif()
