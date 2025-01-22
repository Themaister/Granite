set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_INSTALL ON CACHE BOOL "" FORCE)
set(SPIRV_HEADERS_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
set(SPIRV_HEADERS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SHADERC_THIRD_PARTY_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE STRING "Third party path." FORCE)
set(SPIRV_CHECK_CONTEXT OFF CACHE BOOL "Disable SPIR-V IR context checking." FORCE)
set(SPIRV-Headers_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/spirv-headers" CACHE STRING "SPIRV-Headers path")
set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(FOSSILIZE_RAPIDJSON_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/rapidjson/include" CACHE STRING "Fossilize rapidjson path." FORCE)
set(MUFFT_ENABLE_FFTW OFF CACHE BOOL "Disable FFTW tests." FORCE)
set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)

if (GRANITE_VULKAN_SPIRV_CROSS)
    add_subdirectory(spirv-cross EXCLUDE_FROM_ALL)
endif()

if (GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER)
    add_subdirectory(shaderc EXCLUDE_FROM_ALL)
endif()

add_subdirectory(renderdoc EXCLUDE_FROM_ALL)

if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/stb/stb/stb_vorbis.c)
    add_subdirectory(stb)
endif()

if ((NOT ANDROID) AND GRANITE_ASTC_ENCODER_COMPRESSION)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
        message("Enabling SSE 4.1 path for astcenc")
        set(ISA_SSE41 ON CACHE BOOL "" FORCE)
    else()
        set(ISA_NONE ON CACHE BOOL "" FORCE)
    endif()
    set(DECOMPRESSOR OFF)
    set(UNITTEST OFF)
    set(CLI_BUILD OFF CACHE BOOL "" FORCE)
    add_subdirectory(astc-encoder EXCLUDE_FROM_ALL)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
        add_library(astc-encoder ALIAS astcenc-sse4.1-static)
    else()
        add_library(astc-encoder ALIAS astcenc-none-static)
    endif()
endif()

if (GRANITE_RENDERER)
    add_subdirectory(meshoptimizer EXCLUDE_FROM_ALL)
    add_subdirectory(mikktspace)
    add_subdirectory(muFFT EXCLUDE_FROM_ALL)
endif()

set(FOSSILIZE_CLI OFF CACHE BOOL "Fossilize CLI." FORCE)
set(FOSSILIZE_TESTS OFF CACHE BOOL "Fossilize tests." FORCE)

if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/rapidjson/include/rapidjson/document.h)
    add_library(granite-rapidjson INTERFACE)
    target_include_directories(granite-rapidjson INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/rapidjson/include)
endif()

if (NOT TARGET Vulkan::Headers)
    add_subdirectory(khronos/vulkan-headers)
endif()

add_library(granite-volk-headers INTERFACE)
target_include_directories(granite-volk-headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/volk)
target_link_libraries(granite-volk-headers INTERFACE Vulkan::Headers)

if (GRANITE_VULKAN_FOSSILIZE)
    # Some issues with Android build with missing shm_. Compile exactly what we need for now.
    add_library(fossilize STATIC
            fossilize/fossilize.hpp fossilize/fossilize.cpp
            fossilize/fossilize_db.hpp fossilize/fossilize_db.cpp
            fossilize/varint.hpp fossilize/varint.cpp
            fossilize/fossilize_application_filter.hpp fossilize/fossilize_application_filter.cpp
            fossilize/path.hpp fossilize/path.cpp
            fossilize/miniz/miniz.h fossilize/miniz/miniz.c
            fossilize/cli/fossilize_feature_filter.hpp fossilize/cli/fossilize_feature_filter.cpp)
    granite_setup_default_link_libraries(fossilize)

    get_target_property(SPIRV_HEADERS_PATH SPIRV-Headers INTERFACE_INCLUDE_DIRECTORIES)

    target_include_directories(fossilize
            PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/fossilize
            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/fossilize/miniz
            PRIVATE ${SPIRV_HEADERS_PATH}/spirv/unified1)
    target_link_libraries(fossilize PUBLIC granite-volk-headers PRIVATE granite-rapidjson)
    if (CMAKE_COMPILER_IS_GNUCXX OR (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang"))
        target_compile_options(fossilize PRIVATE -fvisibility=hidden)
        if (NOT APPLE)
            target_compile_definitions(fossilize PRIVATE _LARGEFILE64_SOURCE)
        endif()
        if (WIN32)
            target_compile_definitions(fossilize PRIVATE __USE_MINGW_ANSI_STDIO=1)
        endif()
    endif()
    if (WIN32)
        target_include_directories(fossilize PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/dirent)
    endif()
endif()

# volk must be STATIC.
add_library(granite-volk STATIC volk/volk.c volk/volk.h)
if (WIN32)
    target_compile_definitions(granite-volk PRIVATE VK_USE_PLATFORM_WIN32_KHR)
else()
    target_link_libraries(granite-volk PRIVATE dl)
endif()
target_link_libraries(granite-volk PRIVATE granite-volk-headers)

if ((NOT ANDROID) AND (${GRANITE_PLATFORM} MATCHES "SDL") AND (NOT GRANITE_SYSTEM_SDL))
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TIMERS ON CACHE BOOL "" FORCE)
    set(SDL_LIBC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    # Disable everything we don't care about.
    set(SDL_ATOMIC OFF CACHE BOOL "" FORCE)
    set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
    set(SDL_RENDER OFF CACHE BOOL "" FORCE)
    set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
    set(SDL_POWER OFF CACHE BOOL "" FORCE)
    set(SDL_FILE OFF CACHE BOOL "" FORCE)
    set(SDL_CPUINFO OFF CACHE BOOL "" FORCE)
    set(SDL_FILESYSTEM OFF CACHE BOOL "" FORCE)
    set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
    set(SDL_LOCALE OFF CACHE BOOL "" FORCE)
    set(SDL_MISC OFF CACHE BOOL "" FORCE)
    add_subdirectory(sdl3 EXCLUDE_FROM_ALL)
endif()

if (ANDROID AND GRANITE_AUDIO)
    add_subdirectory(oboe EXCLUDE_FROM_ALL)
    target_compile_options(oboe PUBLIC -Wno-unused-parameter)
    target_compile_features(oboe PRIVATE cxx_std_17)
endif()

if (GRANITE_VULKAN_SYSTEM_HANDLES AND GRANITE_RENDERER)
    # Custom integration, bypass all the noise.
    add_subdirectory(fsr2/src/ffx-fsr2-api/granite)
endif()

if (GRANITE_FFMPEG)
    add_subdirectory(pyroenc EXCLUDE_FROM_ALL)
endif()
