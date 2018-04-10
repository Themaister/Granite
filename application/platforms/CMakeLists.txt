option(GRANITE_PLATFORM "Granite Platform" "GLFW")
if (NOT GRANITE_PLATFORM)
   set(GRANITE_PLATFORM "GLFW")
endif()

option(GRANITE_KHR_DISPLAY_ACQUIRE_XLIB "Try to acquire Xlib display when using VK_KHR_display." OFF)

add_granite_library(application-headless application_headless.cpp)
target_link_libraries(application-headless
		vulkan-symbols application vulkan-backend
		vulkan-wsi ui-renderer
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
elseif (${GRANITE_PLATFORM} MATCHES "libretro")
	add_granite_library(retro-utils application_libretro_utils.cpp application_libretro_utils.hpp)
	target_link_libraries(retro-utils vulkan-backend vulkan-wsi)
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
	target_link_libraries(application-main vulkan-symbols glfw)
elseif (${GRANITE_PLATFORM} MATCHES "CUSTOM_SURFACE")
	add_granite_library(application-main application_custom_surface.cpp)
	target_compile_definitions(application-main PUBLIC HAVE_CUSTOM_SURFACE)
	target_link_libraries(application-main util)
elseif (${GRANITE_PLATFORM} MATCHES "KHR_DISPLAY")
	add_granite_library(application-main application_khr_display.cpp)
	target_compile_definitions(application-main PUBLIC HAVE_KHR_DISPLAY)
	if (GRANITE_KHR_DISPLAY_ACQUIRE_XLIB)
		target_link_libraries(application-main X11)
		target_compile_definitions(application-main PRIVATE "KHR_DISPLAY_ACQUIRE_XLIB")
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
		vulkan-symbols vulkan-backend vulkan-wsi
		ui-renderer
		application-input application-events)


