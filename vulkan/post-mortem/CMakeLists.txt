add_granite_internal_lib(granite-vulkan-post-mortem
        post_mortem.cpp post_mortem.hpp)

target_include_directories(granite-vulkan-post-mortem PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/aftermath-include)
target_include_directories(granite-vulkan-post-mortem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(granite-vulkan-post-mortem PRIVATE granite-util)

find_library(GFSDK_LIBRARY GFSDK_Aftermath_Lib.x64)
if (GFSDK_LIBRARY)
    target_sources(granite-vulkan-post-mortem PRIVATE
            NsightAftermathGpuCrashTracker.cpp NsightAftermathGpuCrashTracker.h
            NsightAftermathHelpers.h)
    target_compile_definitions(granite-vulkan-post-mortem PRIVATE HAVE_AFTERMATH_SDK)
    target_link_libraries(granite-vulkan-post-mortem PRIVATE ${GFSDK_LIBRARY})
    message("Found Aftermath SDK.")
endif()