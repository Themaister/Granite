option(GRANITE_PLATFORM "Granite Platform" "GLFW")
if (NOT GRANITE_PLATFORM)
   set(GRANITE_PLATFORM "GLFW")
endif()

option(GRANITE_KHR_DISPLAY_ACQUIRE_XLIB "Try to acquire Xlib display when using VK_KHR_display." OFF)

add_granite_library(application-headless application_headless.cpp hw_counters/hw_counter_interface.h)
target_link_libraries(application-headless
		volk application vulkan-backend
		vulkan-backend ui-renderer
		application-input application-events rapidjson)
target_include_directories(application-headless PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (ANDROID)
	option(GRANITE_ANDROID_APK_FILESYSTEM "Use APK file system for assets and builtin files." ON)
	add_granite_library(application-main application_android.cpp)

	add_granite_library(android-glue
			${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
			${ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c)

	target_include_directories(android-glue
			PUBLIC
			${ANDROID_NDK}/sources/android/native_app_glue)

	target_link_libraries(android-glue util)
	target_link_libraries(application-main android-glue rapidjson)
	if (GRANITE_ANDROID_APK_FILESYSTEM)
		target_compile_definitions(application-main PRIVATE ANDROID_APK_FILESYSTEM)
	endif()
	target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_ANDROID_KHR=1)
elseif (${GRANITE_PLATFORM} MATCHES "libretro")
	add_granite_library(retro-utils application_libretro_utils.cpp application_libretro_utils.hpp)
	target_link_libraries(retro-utils vulkan-backend vulkan-backend)
	target_include_directories(retro-utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
	target_link_libraries(retro-utils application)

	add_granite_library(application-main application_libretro.cpp)
	target_compile_definitions(application-main PUBLIC HAVE_LIBRETRO)
	target_link_libraries(application-main retro-utils)
elseif (${GRANITE_PLATFORM} MATCHES "headless")
	add_granite_library(application-main .dummy.cpp)
	target_link_libraries(application-main application-headless)
elseif (${GRANITE_PLATFORM} MATCHES "GLFW")
	set(GLFW_BUILD_TESTS OFF)
	add_subdirectory(../../third_party/glfw glfw EXCLUDE_FROM_ALL)
	add_granite_library(application-main application_glfw.cpp)
	target_compile_definitions(application-main PUBLIC HAVE_GLFW)
	target_link_libraries(application-main glfw)
	if (HOTPLUG_UDEV_FOUND)
		target_link_libraries(application-main input-linux)
		target_compile_definitions(application-main PRIVATE HAVE_LINUX_INPUT)
	elseif (WIN32)
		target_link_libraries(application-main xinput-windows)
		target_compile_definitions(application-main PRIVATE HAVE_XINPUT_WINDOWS)
	endif()
elseif (${GRANITE_PLATFORM} MATCHES "CUSTOM_SURFACE")
	add_granite_library(application-main application_custom_surface.cpp)
	target_compile_definitions(application-main PUBLIC HAVE_CUSTOM_SURFACE)
	target_link_libraries(application-main util)
elseif (${GRANITE_PLATFORM} MATCHES "KHR_DISPLAY")
	add_granite_library(application-main application_khr_display.cpp)
	target_compile_definitions(application-main PUBLIC HAVE_KHR_DISPLAY)
	if (HOTPLUG_UDEV_FOUND)
		target_link_libraries(application-main input-linux)
		target_compile_definitions(application-main PRIVATE HAVE_LINUX_INPUT)
	endif()
	if (GRANITE_KHR_DISPLAY_ACQUIRE_XLIB)
		target_link_libraries(application-main X11)
		target_compile_definitions(application-main PRIVATE "KHR_DISPLAY_ACQUIRE_XLIB")
		target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_XLIB_XRANDR_EXT=1)
	endif()
	if (NOT WIN32)
		target_link_libraries(application-main -pthread)
	endif()
else()
    message(FATAL "GRANITE_PLATFORM is not set.")
endif()

target_include_directories(application-main PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(application-main
		application
		volk vulkan-backend vulkan-backend
		ui-renderer
		application-input application-events)


