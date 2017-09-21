add_granite_library(application application.cpp application.hpp input.cpp input.hpp)
target_include_directories(application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(application vulkan-backend vulkan-wsi ui-renderer)

option(GRANITE_PLATFORM "Granite Platform" "GLFW")
if (NOT GRANITE_PLATFORM)
   set(GRANITE_PLATFORM "GLFW")
endif()

option(GRANITE_KHR_DISPLAY_ACQUIRE_XLIB "Try to acquire Xlib display when using VK_KHR_display." OFF)

if (ANDROID)
	target_link_libraries(application vulkan-symbols)
	target_sources(application PRIVATE application_android.cpp)

	add_granite_library(android-glue
			${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
			${ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c)

	target_include_directories(android-glue
			PUBLIC
			${ANDROID_NDK}/sources/android/native_app_glue)

	target_link_libraries(android-glue util)
	target_link_libraries(application android-glue)
elseif (${GRANITE_PLATFORM} MATCHES "libretro")
	add_granite_library(retro-utils application_libretro_utils.cpp application_libretro_utils.hpp)
	target_link_libraries(retro-utils vulkan-backend vulkan-wsi)

	target_link_libraries(application vulkan-symbols)
	target_sources(application PRIVATE application_libretro.cpp)
	target_compile_definitions(application PUBLIC HAVE_LIBRETRO)
	target_link_libraries(application retro-utils)
elseif (${GRANITE_PLATFORM} MATCHES "GLFW")
	set(GLFW_BUILD_TESTS OFF)
	add_subdirectory(../third_party/glfw glfw EXCLUDE_FROM_ALL)
	target_compile_definitions(application PUBLIC HAVE_GLFW)
	target_link_libraries(application vulkan-symbols glfw)
	target_sources(application PRIVATE application_glfw.cpp)
elseif (${GRANITE_PLATFORM} MATCHES "KHR_DISPLAY")
	target_compile_definitions(application PUBLIC HAVE_KHR_DISPLAY)
	target_link_libraries(application vulkan-symbols)
	if (GRANITE_KHR_DISPLAY_ACQUIRE_XLIB)
		target_link_libraries(application X11)
		target_compile_definitions(application PRIVATE "KHR_DISPLAY_ACQUIRE_XLIB")
	endif()
	if (NOT WIN32)
		target_link_libraries(application -pthread)
	endif()
	target_sources(application PRIVATE application_khr_display.cpp)
elseif (${GRANITE_PLATFORM} MATCHES "NULL")
	target_link_libraries(application vulkan-symbols)
	target_sources(application PRIVATE application_null.cpp)
else()
    message(FATAL "GRANITE_PLATFORM is not set.")
endif()
