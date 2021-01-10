# Must be static due to Granite::application_create() feedback.

if (ANDROID)
    add_granite_internal_static_lib(granite-platform
            application_android.cpp)
    add_library(android-glue STATIC
            ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
            ${ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c)
    target_include_directories(android-glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)
    target_link_libraries(granite-platform PRIVATE android-glue granite-rapidjson)
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
