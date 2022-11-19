# Must be static due to Granite::application_create() feedback.

if (ANDROID)
    add_granite_internal_static_lib(granite-platform
            application_android.cpp)
    find_package(game-activity REQUIRED CONFIG)

    target_link_libraries(granite-platform PRIVATE granite-rapidjson)
    get_target_property(game-activity-include game-activity::game-activity INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(granite-platform PRIVATE ${game-activity-include})
    target_sources(granite-platform PRIVATE
            ${game-activity-include}/game-activity/native_app_glue/android_native_app_glue.c
            ${game-activity-include}/game-activity/GameActivity.cpp
            ${game-activity-include}/game-text-input/gametextinput.cpp)

    target_compile_definitions(granite-platform PRIVATE "VK_USE_PLATFORM_ANDROID_KHR")
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
elseif (${GRANITE_PLATFORM} MATCHES "GLFW")
    add_granite_internal_static_lib(granite-platform
            application_glfw.cpp)
    target_link_libraries(granite-platform PRIVATE glfw)
elseif (${GRANITE_PLATFORM} MATCHES "KHR_DISPLAY")
    add_granite_internal_static_lib(granite-platform
            application_khr_display.cpp)
    if (GRANITE_KHR_DISPLAY_ACQUIRE_XLIB)
        target_link_libraries(granite-platform PRIVATE X11)
        target_compile_definitions(granite-platform PRIVATE "KHR_DISPLAY_ACQUIRE_XLIB")
		target_compile_definitions(granite-platform PRIVATE "VK_USE_PLATFORM_XLIB_XRANDR_EXT")
    endif()
else()
    message(FATAL "GRANITE_PLATFORM is not set.")
endif()

target_link_libraries(granite-platform PRIVATE granite-application granite-input granite-application-global-init)

if (GRANITE_AUDIO)
    target_link_libraries(granite-platform PRIVATE granite-audio)
endif()
