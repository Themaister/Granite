add_granite_internal_lib(granite-vulkan-post-mortem
    post_mortem.cpp post_mortem.hpp)

target_include_directories(granite-vulkan-post-mortem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(granite-vulkan-post-mortem PRIVATE granite-util)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
    set(AFTERMATH_ARCH x64)
else()
    set(AFTERMATH_ARCH x86)
endif()

find_library(GFSDK_LIBRARY GFSDK_Aftermath_Lib.x64 HINTS ${AFTERMATH_SDK_PATH}/lib/${AFTERMATH_ARCH})

if (GFSDK_LIBRARY)
    target_sources(granite-vulkan-post-mortem PRIVATE
        NsightAftermathGpuCrashTracker.cpp NsightAftermathGpuCrashTracker.h
        NsightAftermathHelpers.h)
    target_compile_definitions(granite-vulkan-post-mortem PRIVATE HAVE_AFTERMATH_SDK)
    target_link_libraries(granite-vulkan-post-mortem PRIVATE ${GFSDK_LIBRARY} granite-volk-headers)
    target_include_directories(granite-vulkan-post-mortem PRIVATE ${AFTERMATH_SDK_PATH}/include)
    message("Found Aftermath SDK.")
else()
    message("Did not find Aftermath SDK in AFTERMATH_SDK_PATH=${AFTERMATH_SDK_PATH}.")
endif()
