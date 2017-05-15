add_granite_library(vulkan-wsi wsi.cpp wsi.hpp input.hpp input.cpp)
target_link_libraries(vulkan-wsi vulkan-backend util event)
target_include_directories(vulkan-wsi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

option(WSI "WSI backend" "GLFW")
if (NOT WSI)
   set(WSI "GLFW")
endif()

if (ANDROID)
	add_granite_library(android-glue
		android.cpp
		${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
		${ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c)

	target_include_directories(android-glue
		PUBLIC
		${ANDROID_NDK}/sources/android/native_app_glue)

	target_compile_definitions(vulkan-wsi PUBLIC HAVE_ANDROID_SURFACE)
	target_link_libraries(vulkan-wsi vulkan-symbols android-glue)
	target_link_libraries(android-glue util)
elseif (${WSI} MATCHES "GLFW")
	set(GLFW_BUILD_TESTS OFF)
	add_subdirectory(../../third_party/glfw glfw EXCLUDE_FROM_ALL)
	target_compile_definitions(vulkan-wsi PUBLIC HAVE_GLFW)
	target_link_libraries(vulkan-wsi vulkan-symbols glfw)
elseif (${WSI} MATCHES "DISPLAY")
	target_compile_definitions(vulkan-wsi PUBLIC HAVE_KHR_DISPLAY)
	target_link_libraries(vulkan-wsi vulkan-symbols -pthread)
endif()
