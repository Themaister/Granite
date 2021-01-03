cmake_minimum_required(VERSION 3.5)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_STANDARD 99)
project(Granite LANGUAGES CXX C)

if (CMAKE_COMPILER_IS_GNUCXX OR (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang"))
    set(GRANITE_CXX_FLAGS -Wshadow -Wall -Wextra -Wno-comment -Wno-missing-field-initializers -Wno-empty-body -ffast-math)
    if (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
        set(GRANITE_CXX_FLAGS ${GRANITE_CXX_FLAGS} -Wno-backslash-newline-escape)
    endif()
    if (NOT (${CMAKE_BUILD_TYPE} MATCHES "Release"))
        message("Disabling frame pointer for profiling/debug.")
        set(GRANITE_CXX_FLAGS ${GRANITE_CXX_FLAGS} -fno-omit-frame-pointer)
    endif()
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
        message("Enabling SSE3 support.")
        set(GRANITE_CXX_FLAGS ${GRANITE_CXX_FLAGS} -msse3)
    endif()
elseif (MSVC)
    set(GRANITE_CXX_FLAGS /D_CRT_SECURE_NO_WARNINGS /wd4267 /wd4244 /wd4309 /wd4005 /MP /DNOMINMAX)
endif()

# We are overriding settings in subdirectories.
# Avoid warnings.
if (${CMAKE_VERSION} VERSION_GREATER "3.13.0")
   message("Setting CMake policy CMP0077.")
   cmake_policy(SET CMP0077 NEW)
endif()

option(GRANITE_SHARED "Build Granite as a shared library." OFF)
option(GRANITE_ISPC_TEXTURE_COMPRESSION "Enable ISPC texture compression" ON)
option(GRANITE_ASTC_ENCODER_COMPRESSION "Enable astc-encoder texture compression" ON)
option(GRANITE_TOOLS "Build Granite tools." ON)
option(GRANITE_KHR_DISPLAY_ACQUIRE_XLIB "Try to acquire Xlib display when using VK_KHR_display." OFF)
option(GRANITE_ANDROID_APK_FILESYSTEM "Use APK file system for assets and builtin files." ON)
option(GRANITE_SHADER_COMPILER_OPTIMIZE "Optimize SPIR-V." ON)
option(GRANITE_VULKAN_ONLY "Only enable Vulkan backend in build." OFF)
option(GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER "Enable Vulkan GLSL runtime compiler." ON)
option(GRANITE_SPIRV_DUMP "Dump compiled SPIR-V modules to cache." OFF)
option(GRANITE_VULKAN_MT "Make Vulkan backend thread-safe." ON)
option(GRANITE_VULKAN_FOSSILIZE "Enable support for Fossilize." OFF)
option(GRANITE_AUDIO "Enable Audio support." OFF)
option(GRANITE_PLATFORM "Granite Platform" "GLFW")
option(GRANITE_HIDDEN "Declare symbols as hidden by default. Useful if you build Granite as a static library and you link to it in your shared library." OFF)
option(GRANITE_SANITIZE_ADDRESS "Sanitize address" OFF)
option(GRANITE_SANITIZE_THREADS "Sanitize threads" OFF)
option(GRANITE_SANITIZE_MEMORY "Sanitize memory" OFF)
option(GRANITE_ANDROID_AUDIO_OBOE "Use the Oboe library for audio." ON)
option(GRANITE_TARGET_NATIVE "Target native arch (-march=native)" OFF)
option(GRANITE_BULLET "Enable Bullet support." OFF)
option(GRANITE_RENDERDOC_CAPTURE "Enable support for RenderDoc capture from API." ON)
option(GRANITE_ENABLE_THIRD_PARTY_TARGETS "Always include third party components to build." OFF)

if (GRANITE_HIDDEN)
    if (CMAKE_COMPILER_IS_GNUCXX OR (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang"))
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
    endif()
elseif(${GRANITE_PLATFORM} MATCHES "libretro")
    if (CMAKE_COMPILER_IS_GNUCXX OR (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang"))
        set(CMAKE_CXX_FLAGS "${CMAKE_CX_FLAGS} -fvisibility=hidden")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
    endif()
endif()

if (GRANITE_TARGET_NATIVE)
    if (CMAKE_COMPILER_IS_GNUCXX OR (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang"))
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native")
    endif()
endif()

set(GRANITE_LINK_FLAGS)
if (GRANITE_SANITIZE_ADDRESS)
    set(GRANITE_CXX_FLAGS ${GRANITE_CXX_FLAGS} -fsanitize=address)
    set(GRANITE_LINK_FLAGS "${GRANITE_LINK_FLAGS} -fsanitize=address")
endif()

if (GRANITE_SANITIZE_THREADS)
    set(GRANITE_CXX_FLAGS ${GRANITE_CXX_FLAGS} -fsanitize=thread)
    set(GRANITE_LINK_FLAGS "${GRANITE_LINK_FLAGS} -fsanitize=thread")
endif()

if (GRANITE_SANITIZE_MEMORY)
    set(GRANITE_CXX_FLAGS ${GRANITE_CXX_FLAGS} -fsanitize=memory)
    set(GRANITE_LINK_FLAGS "${GRANITE_LINK_FLAGS} -fsanitize=memory")
endif()

if (ANDROID)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if (GRANITE_SHARED)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    if (MSVC)
        set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    endif()
endif()

if (${GRANITE_PLATFORM} MATCHES "libretro")
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_INSTALL ON CACHE BOOL "" FORCE)
set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SHADERC_THIRD_PARTY_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party" CACHE STRING "Third party path." FORCE)
set(SPIRV_CHECK_CONTEXT OFF CACHE BOOL "Disable SPIR-V IR context checking." FORCE)
set(SPIRV-Headers_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spirv-headers" CACHE STRING "SPIRV-Headers path")
set(SHADERC_SKIP_TESTS ON)
set(FOSSILIZE_RAPIDJSON_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rapidjson/include" CACHE STRING "Fossilize rapidjson path." FORCE)
set(FOSSILIZE_VULKAN_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/khronos/vulkan-headers/include/vulkan" CACHE STRING "Fossilize Vulkan path." FORCE)
set(MUFFT_ENABLE_FFTW OFF CACHE BOOL "Disable FFTW tests." FORCE)

add_subdirectory(third_party/spirv-cross EXCLUDE_FROM_ALL)
add_library(volk STATIC third_party/volk/volk.c third_party/volk/volk.h)
if (NOT WIN32)
    target_link_libraries(volk PRIVATE dl)
endif()
target_include_directories(volk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/volk)
target_include_directories(volk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/khronos/vulkan-headers/include)

if ((NOT GRANITE_VULKAN_ONLY) OR GRANITE_ENABLE_THIRD_PARTY_TARGETS)
    add_subdirectory(third_party/shaderc EXCLUDE_FROM_ALL)
    add_subdirectory(third_party/stb EXCLUDE_FROM_ALL)
    add_subdirectory(third_party/astc-encoder/Source EXCLUDE_FROM_ALL)
    add_subdirectory(third_party/meshoptimizer EXCLUDE_FROM_ALL)
    add_subdirectory(third_party/mikktspace EXCLUDE_FROM_ALL)
    add_subdirectory(third_party/muFFT EXCLUDE_FROM_ALL)
    add_subdirectory(third_party/libco EXCLUDE_FROM_ALL)
    add_subdirectory(third_party EXCLUDE_FROM_ALL)
endif()

set(FOSSILIZE_CLI OFF CACHE BOOL "Fossilize CLI." FORCE)
set(FOSSILIZE_TESTS OFF CACHE BOOL "Fossilize tests." FORCE)
if (GRANITE_VULKAN_FOSSILIZE)
    add_subdirectory(third_party/fossilize EXCLUDE_FROM_ALL)
endif()

if (GRANITE_SHARED)
    add_library(granite SHARED)
    if (WIN32)
        install(TARGETS granite RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
    else()
        set(CMAKE_INSTALL_RPATH "\$ORIGIN/../lib:${CMAKE_INSTALL_PREFIX}/lib")
        install(TARGETS granite DESTINATION lib)
    endif()
else()
    add_library(granite STATIC)
    if (${GRANITE_PLATFORM} MATCHES "libretro")
        set_target_properties(${NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    elseif (GRANITE_POSITION_INDEPENDENT)
        set_target_properties(${NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    elseif (GRANITE_SHARED)
        set_target_properties(${NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    elseif (ANDROID)
        set_target_properties(${NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    else()
        set_target_properties(${NAME} PROPERTIES POSITION_INDEPENDENT_CODE OFF)
    endif()
endif()
target_compile_options(granite PRIVATE ${GRANITE_CXX_FLAGS})

if (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    target_link_libraries(granite PUBLIC atomic)
endif()

function(add_granite_executable NAME)
    if (ANDROID)
        add_library(${NAME} SHARED ${ARGN})
        target_link_libraries(${NAME} PRIVATE log android)
    elseif(${GRANITE_PLATFORM} MATCHES "libretro")
        add_library(${NAME} SHARED ${ARGN})
        if (CMAKE_COMPILER_IS_GNUCXX OR (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang"))
            set_target_properties(${NAME} PROPERTIES LINK_FLAGS "-Wl,--no-undefined")
        endif()
    else()
        add_executable(${NAME} ${ARGN})
    endif()
    # Need this for some reason on OSX.
    if (APPLE)
        set_target_properties(${NAME} PROPERTIES LINK_FLAGS "-Wl,-all_load")
    endif()
    target_link_libraries(${NAME} PRIVATE granite)
    target_compile_options(${NAME} PRIVATE ${GRANITE_CXX_FLAGS})
    if (GRANITE_SANITIZE_ADDRESS OR GRANITE_SANITIZE_THREADS OR GRANITE_SANITIZE_MEMORY)
        set_target_properties(${NAME} PROPERTIES LINK_FLAGS "${GRANITE_LINK_FLAGS}")
    endif()
endfunction()

function(add_granite_application TARGET_NAME)
    add_granite_executable(${TARGET_NAME} ${ARGN})
    if (ANDROID)
        target_link_libraries(${TARGET_NAME} PRIVATE granite)
    elseif (${GRANITE_PLATFORM} MATCHES "libretro")
        target_link_libraries(${TARGET_NAME} PRIVATE granite)
    else()
        target_link_libraries(${TARGET_NAME} PRIVATE granite-application-entry)
    endif()
    if (NOT ${GRANITE_PLATFORM} MATCHES "libretro")
        if (WIN32)
            if (CMAKE_COMPILER_IS_GNUCXX OR (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang"))
                set_target_properties(${TARGET_NAME} PROPERTIES LINK_FLAGS -mwindows)
            else()
                set_target_properties(${TARGET_NAME} PROPERTIES LINK_FLAGS /SUBSYSTEM:Windows)
            endif()
        endif()
    endif()
endfunction()

function(add_granite_headless_application TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN})
    target_compile_options(${TARGET_NAME} PRIVATE ${GRANITE_CXX_FLAGS})
    if (GRANITE_SANITIZE_ADDRESS OR GRANITE_SANITIZE_THREADS OR GRANITE_SANITIZE_MEMORY)
        set_target_properties(${TARGET_NAME} PROPERTIES LINK_FLAGS "${GRANITE_LINK_FLAGS}")
    endif()
    if (ANDROID)
        target_link_libraries(${TARGET_NAME} log android)
    endif()
    target_link_libraries(${TARGET_NAME} granite-application-entry-headless)
    if (WIN32)
        if (CMAKE_COMPILER_IS_GNUCXX OR (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang"))
            set_target_properties(${TARGET_NAME} PROPERTIES LINK_FLAGS -mwindows)
        else()
            set_target_properties(${TARGET_NAME} PROPERTIES LINK_FLAGS /SUBSYSTEM:Windows)
        endif()
    endif()
endfunction()

function(add_granite_offline_tool NAME)
    add_executable(${NAME} ${ARGN})

    # Need this for some reason on OSX.
    if (APPLE)
        set_target_properties(${NAME} PROPERTIES LINK_FLAGS "-Wl,-all_load")
    endif()
    target_link_libraries(${NAME} PRIVATE granite)
    target_compile_options(${NAME} PRIVATE ${GRANITE_CXX_FLAGS})
    if (GRANITE_SANITIZE_ADDRESS OR GRANITE_SANITIZE_THREADS OR GRANITE_SANITIZE_MEMORY)
        set_target_properties(${NAME} PROPERTIES LINK_FLAGS "${GRANITE_LINK_FLAGS}")
    endif()

    if (NOT ${GRANITE_PLATFORM} MATCHES "libretro")
        if (WIN32)
            if (CMAKE_COMPILER_IS_GNUCXX OR (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang"))
                set_target_properties(${NAME} PROPERTIES LINK_FLAGS -mconsole)
            else()
                set_target_properties(${NAME} PROPERTIES LINK_FLAGS /SUBSYSTEM:Console)
            endif()
        endif()
    endif()
endfunction()

set(GRANITE_ISPC_LIBRARY_DIR "" CACHE STRING "ISPC library directory." FORCE)
set(GRANITE_ISPC_INCLUDE_DIR "" CACHE STRING "ISPC include directory." FORCE)

target_include_directories(granite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/util ${CMAKE_CURRENT_SOURCE_DIR}/vulkan)

target_sources(granite PRIVATE
        application/application.hpp
        application/application_wsi.hpp

        util/logging.hpp util/logging.cpp
        util/message_queue.hpp util/message_queue.cpp
        util/aligned_alloc.cpp util/aligned_alloc.hpp
        util/bitops.hpp
        util/array_view.hpp
        util/variant.hpp
        util/enum_cast.hpp
        util/hash.hpp
        util/intrusive.hpp
        util/intrusive_list.hpp
        util/object_pool.hpp
        util/stack_allocator.hpp
        util/temporary_hashmap.hpp
        util/volatile_source.hpp
        util/read_write_lock.hpp
        util/async_object_sink.hpp
        util/unstable_remove_if.hpp
        util/intrusive_hash_map.hpp
        util/timer.hpp util/timer.cpp
        util/small_vector.hpp
        util/timeline_trace_file.hpp util/timeline_trace_file.cpp
        util/thread_name.hpp util/thread_name.cpp

        vulkan/texture_format.cpp vulkan/texture_format.hpp
        vulkan/context.cpp vulkan/context.hpp
        vulkan/vulkan_headers.hpp vulkan/vulkan_prerotate.hpp
        vulkan/device.cpp vulkan/device.hpp
        vulkan/wsi.cpp vulkan/wsi.hpp
        vulkan/wsi_timing.cpp vulkan/wsi_timing.hpp
        vulkan/buffer_pool.cpp vulkan/buffer_pool.hpp
        vulkan/image.cpp vulkan/image.hpp
        vulkan/cookie.cpp vulkan/cookie.hpp
        vulkan/sampler.cpp vulkan/sampler.hpp
        vulkan/command_pool.cpp vulkan/command_pool.hpp
        vulkan/fence_manager.cpp vulkan/fence_manager.hpp
        vulkan/descriptor_set.cpp vulkan/descriptor_set.hpp
        vulkan/semaphore_manager.cpp vulkan/semaphore_manager.hpp
        vulkan/command_buffer.cpp vulkan/command_buffer.hpp
        vulkan/shader.cpp vulkan/shader.hpp
        vulkan/render_pass.cpp vulkan/render_pass.hpp
        vulkan/buffer.cpp vulkan/buffer.hpp
        vulkan/semaphore.cpp vulkan/semaphore.hpp
        vulkan/memory_allocator.cpp vulkan/memory_allocator.hpp
        vulkan/fence.hpp vulkan/fence.cpp
        vulkan/format.hpp
        vulkan/limits.hpp
        vulkan/type_to_string.hpp
        vulkan/quirks.hpp
        vulkan/vulkan_common.hpp
        vulkan/event_manager.cpp vulkan/event_manager.hpp
        vulkan/pipeline_event.cpp vulkan/pipeline_event.hpp
        vulkan/query_pool.cpp vulkan/query_pool.hpp)

if (GRANITE_RENDERDOC_CAPTURE)
    add_subdirectory(third_party/renderdoc)
    target_link_libraries(granite PRIVATE renderdoc-app)
    target_compile_definitions(granite PRIVATE GRANITE_RENDERDOC_CAPTURE)
    target_sources(granite PRIVATE vulkan/renderdoc_capture.cpp)
    if (NOT WIN32)
        target_link_libraries(granite PRIVATE dl)
    endif()
endif()

if (GRANITE_VULKAN_MT)
    target_compile_definitions(granite PUBLIC GRANITE_VULKAN_MT)
    target_sources(granite PRIVATE vulkan/thread_id.hpp vulkan/thread_id.cpp)
endif()

if (WIN32)
    target_compile_definitions(volk PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()

if (NOT GRANITE_VULKAN_ONLY)
    if (NOT ANDROID)
        add_library(granite-application-entry STATIC application/application_entry.cpp)
        target_link_libraries(granite-application-entry granite)
        target_compile_options(granite-application-entry PRIVATE ${GRANITE_CXX_FLAGS})
    endif()

    add_library(granite-application-entry-headless STATIC application/application_entry.cpp)
    target_link_libraries(granite-application-entry-headless granite)
    target_compile_definitions(granite-application-entry-headless PRIVATE APPLICATION_ENTRY_HEADLESS=1)

    target_compile_definitions(granite PRIVATE GRANITE_DEFAULT_BUILTIN_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
    target_compile_definitions(granite PRIVATE GRANITE_DEFAULT_CACHE_DIRECTORY=\"${CMAKE_BINARY_DIR}/cache\")
    target_compile_definitions(granite PUBLIC NOMINMAX GRANITE_LOGGING_QUEUE)

    target_include_directories(granite PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/application
            ${CMAKE_CURRENT_SOURCE_DIR}/application/input
            ${CMAKE_CURRENT_SOURCE_DIR}/application/events
            ${CMAKE_CURRENT_SOURCE_DIR}/application/platforms
            ${CMAKE_CURRENT_SOURCE_DIR}/compiler
            ${CMAKE_CURRENT_SOURCE_DIR}/event
            ${CMAKE_CURRENT_SOURCE_DIR}/ecs
            ${CMAKE_CURRENT_SOURCE_DIR}/math
            ${CMAKE_CURRENT_SOURCE_DIR}/scene_formats
            ${CMAKE_CURRENT_SOURCE_DIR}/vulkan/managers
            ${CMAKE_CURRENT_SOURCE_DIR}/util
            ${CMAKE_CURRENT_SOURCE_DIR}/renderer
            ${CMAKE_CURRENT_SOURCE_DIR}/renderer/fft
            ${CMAKE_CURRENT_SOURCE_DIR}/network
            ${CMAKE_CURRENT_SOURCE_DIR}/ui
            ${CMAKE_CURRENT_SOURCE_DIR}/filesystem
            ${CMAKE_CURRENT_SOURCE_DIR}/filesystem/netfs
            ${CMAKE_CURRENT_SOURCE_DIR}/threading
    )

    target_sources(granite PRIVATE
            application/global_managers.hpp application/global_managers.cpp
            application/application.cpp
            application/application_wsi.cpp application/application_wsi_events.hpp
            application/platforms/application_headless.cpp application/platforms/hw_counters/hw_counter_interface.h
            application/input/input.hpp application/input/input.cpp
            application/events/application_events.hpp
            application/scene_viewer_application.cpp application/scene_viewer_application.hpp
            application/application_cli_wrapper.cpp application/application_cli_wrapper.hpp

            event/event.cpp event/event.hpp
            ecs/ecs.cpp ecs/ecs.hpp

            filesystem/filesystem.cpp filesystem/filesystem.hpp
            filesystem/path.cpp filesystem/path.hpp
            filesystem/netfs/fs-netfs.cpp filesystem/netfs/fs-netfs.hpp

            network/looper.cpp network/netfs.hpp network/network.hpp
            network/socket.cpp network/tcp_listener.cpp

            math/math.hpp math/math.cpp
            math/frustum.hpp math/frustum.cpp
            math/aabb.cpp math/aabb.hpp
            math/render_parameters.hpp
            math/interpolation.cpp math/interpolation.hpp
            math/muglm/muglm.cpp math/muglm/muglm.hpp
            math/muglm/muglm_impl.hpp math/muglm/matrix_helper.hpp
            math/transforms.cpp math/transforms.hpp
            math/simd.hpp math/simd_headers.hpp

            renderer/render_queue.hpp renderer/render_queue.cpp
            renderer/simple_renderer.hpp renderer/simple_renderer.cpp
            renderer/mesh.hpp renderer/mesh.cpp
            renderer/scene.hpp renderer/scene.cpp
            renderer/scene_renderer.hpp renderer/scene_renderer.cpp
            renderer/shader_suite.hpp renderer/shader_suite.cpp
            renderer/render_context.hpp renderer/render_context.cpp
            renderer/camera.hpp renderer/camera.cpp
            renderer/material.hpp
            renderer/abstract_renderable.hpp
            renderer/render_components.hpp
            renderer/mesh_util.hpp renderer/mesh_util.cpp
            renderer/material_util.hpp renderer/material_util.cpp
            renderer/renderer.hpp renderer/renderer.cpp
            renderer/renderer_enums.hpp
            renderer/material_manager.hpp renderer/material_manager.cpp
            renderer/animation_system.hpp renderer/animation_system.cpp
            renderer/render_graph.cpp renderer/render_graph.hpp
            renderer/ground.hpp renderer/ground.cpp
            renderer/post/hdr.hpp renderer/post/hdr.cpp
            renderer/post/fxaa.hpp renderer/post/fxaa.cpp
            renderer/post/smaa.hpp renderer/post/smaa.cpp
            renderer/post/temporal.hpp renderer/post/temporal.cpp
            renderer/post/aa.hpp renderer/post/aa.cpp
            renderer/post/ssao.hpp renderer/post/ssao.cpp
            renderer/utils/image_utils.hpp renderer/utils/image_utils.cpp
            renderer/lights/lights.cpp renderer/lights/lights.hpp
            renderer/lights/clusterer.cpp renderer/lights/clusterer.hpp
            renderer/lights/volumetric_fog.cpp renderer/lights/volumetric_fog.hpp
            renderer/lights/light_info.hpp
            renderer/lights/deferred_lights.hpp renderer/lights/deferred_lights.cpp
            renderer/scene_loader.cpp renderer/scene_loader.hpp
            renderer/mesh_manager.cpp renderer/mesh_manager.hpp
            renderer/common_renderer_data.cpp renderer/common_renderer_data.hpp
            renderer/cpu_rasterizer.cpp renderer/cpu_rasterizer.hpp
            renderer/threaded_scene.cpp renderer/threaded_scene.hpp

            scene_formats/texture_compression.hpp scene_formats/texture_compression.cpp
            scene_formats/gltf.cpp scene_formats/gltf.hpp
            scene_formats/obj.cpp scene_formats/obj.hpp
            scene_formats/scene_formats.hpp scene_formats/scene_formats.cpp
            scene_formats/light_export.cpp scene_formats/light_export.hpp
            scene_formats/camera_export.cpp scene_formats/camera_export.hpp
            scene_formats/memory_mapped_texture.cpp scene_formats/memory_mapped_texture.hpp
            scene_formats/texture_utils.cpp scene_formats/texture_utils.hpp
            scene_formats/texture_files.cpp scene_formats/texture_files.hpp
            scene_formats/gltf_export.cpp scene_formats/gltf_export.hpp
            scene_formats/rgtc_compressor.cpp scene_formats/rgtc_compressor.hpp
            scene_formats/tmx_parser.cpp scene_formats/tmx_parser.hpp
            scene_formats/texture_decoder.cpp scene_formats/texture_decoder.hpp

            threading/thread_group.cpp threading/thread_group.hpp
            threading/task_composer.cpp threading/task_composer.hpp

            ui/font.hpp ui/font.cpp
            ui/flat_renderer.hpp ui/flat_renderer.cpp
            ui/sprite.cpp ui/sprite.hpp
            ui/widget.hpp ui/widget.cpp
            ui/window.hpp ui/window.cpp
            ui/vertical_packing.cpp ui/vertical_packing.hpp
            ui/horizontal_packing.cpp ui/horizontal_packing.hpp
            ui/image_widget.cpp ui/image_widget.hpp
            ui/label.cpp ui/label.hpp
            ui/slider.cpp ui/slider.hpp
            ui/click_button.cpp ui/click_button.hpp
            ui/toggle_button.cpp ui/toggle_button.hpp
            ui/ui_manager.hpp ui/ui_manager.cpp

            util/cli_parser.cpp util/cli_parser.hpp
            util/dynamic_library.cpp util/dynamic_library.hpp
            util/string_helpers.cpp util/string_helpers.hpp

            util/bitmap_to_mesh.cpp util/bitmap_to_mesh.hpp
            util/cooperative_task.hpp util/cooperative_task.cpp
            util/generational_handle.hpp
            util/lru_cache.hpp
            util/no_init_pod.hpp
    )

    if (${CMAKE_SYSTEM} MATCHES "Linux")
        include(FindPkgConfig)
        pkg_check_modules(HOTPLUG_UDEV libudev)
        if (HOTPLUG_UDEV_FOUND)
            target_sources(granite PRIVATE application/input/input_linux.cpp application/input/input_linux.hpp)
            target_include_directories(granite PUBLIC ${HOTPLUG_UDEV_INCLUDE_DIRS})
            target_link_libraries(granite PRIVATE ${HOTPLUG_UDEV_LIBRARIES})
            target_compile_options(granite PRIVATE ${HOTPLUG_UDEV_CFLAGS_OTHER})
        endif()
    elseif (WIN32)
        target_sources(granite PRIVATE application/input/xinput_windows.cpp application/input/xinput_windows.hpp)
    endif()

    target_sources(granite PRIVATE
            vulkan/managers/shader_manager.cpp
            vulkan/managers/shader_manager.hpp
            vulkan/managers/texture_manager.cpp
            vulkan/managers/texture_manager.hpp)

    target_compile_definitions(granite PUBLIC GRANITE_VULKAN_FILESYSTEM)
    if (GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER)
        target_compile_definitions(granite PUBLIC GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER=1)
        target_sources(granite PRIVATE
            compiler/compiler.cpp compiler/compiler.hpp

            renderer/ocean.hpp renderer/ocean.cpp
            renderer/fft/glfft.cpp
            renderer/fft/glfft.hpp
            renderer/fft/glfft_common.hpp
            renderer/fft/glfft_interface.hpp
            renderer/fft/glfft_granite_interface.hpp
            renderer/fft/glfft_granite_interface.cpp
            renderer/fft/glfft_wisdom.cpp
            renderer/fft/glfft_wisdom.hpp)
        target_link_libraries(granite PRIVATE shaderc SPIRV-Tools)
    endif()

    if (HOTPLUG_UDEV_FOUND)
		target_compile_definitions(granite PRIVATE HAVE_LINUX_INPUT)
	elseif (WIN32)
		target_compile_definitions(granite PRIVATE HAVE_XINPUT_WINDOWS)
	endif()

    if (WIN32)
        target_sources(granite PRIVATE filesystem/windows/os_filesystem.cpp filesystem/windows/os_filesystem.hpp)
        target_link_libraries(granite PRIVATE ws2_32)
        target_include_directories(granite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/filesystem/windows)
    elseif (ANDROID)
        target_sources(granite PRIVATE filesystem/linux/os_filesystem.cpp filesystem/linux/os_filesystem.hpp)
        target_sources(granite PRIVATE filesystem/android/android.cpp filesystem/android/android.hpp)
        target_include_directories(granite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/filesystem/linux)
        target_include_directories(granite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/filesystem/android)
    else()
        target_sources(granite PRIVATE filesystem/linux/os_filesystem.cpp filesystem/linux/os_filesystem.hpp)
        target_include_directories(granite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/filesystem/linux)
    endif()

    if (GRANITE_AUDIO)
        include(FindPkgConfig)

        target_sources(granite PRIVATE
                audio/audio_interface.cpp audio/audio_interface.hpp
                audio/audio_mixer.cpp audio/audio_mixer.hpp
                audio/audio_resampler.cpp audio/audio_resampler.hpp
                audio/dsp/sinc_resampler.cpp audio/dsp/sinc_resampler.hpp
                audio/dsp/dsp.hpp audio/dsp/dsp.cpp
                audio/dsp/tone_filter.hpp audio/dsp/tone_filter.cpp
                audio/dsp/tone_filter_stream.hpp audio/dsp/tone_filter_stream.cpp
                audio/audio_events.hpp
                audio/dsp/audio_fft_eq.cpp audio/dsp/audio_fft_eq.hpp
                audio/dsp/pole_zero_filter_design.cpp audio/dsp/pole_zero_filter_design.hpp
                audio/vorbis_stream.hpp audio/vorbis_stream.cpp)

        target_include_directories(granite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/audio)
        target_link_libraries(granite PRIVATE stb-vorbis muFFT)
        target_compile_definitions(granite PUBLIC HAVE_GRANITE_AUDIO=1)

        if (ANDROID)
            if (GRANITE_ANDROID_AUDIO_OBOE)
                add_subdirectory(third_party/oboe EXCLUDE_FROM_ALL)
                target_sources(granite PRIVATE audio/audio_oboe.cpp audio/audio_oboe.hpp)
                target_compile_definitions(granite PRIVATE AUDIO_HAVE_OBOE=1)
                target_link_libraries(granite PRIVATE oboe)
                target_compile_options(oboe PUBLIC -Wno-unused-parameter)
            else()
                target_link_libraries(granite PRIVATE OpenSLES aaudio)
                target_compile_definitions(granite PRIVATE AUDIO_HAVE_OPENSL=1)
                target_compile_definitions(granite PRIVATE AUDIO_HAVE_AAUDIO=1)
                target_sources(granite PRIVATE audio/audio_opensl.cpp audio/audio_opensl.hpp audio/audio_aaudio.cpp audio/audio_aaudio.hpp)
            endif()
        elseif (NOT WIN32)
            pkg_check_modules(PULSEAUDIO libpulse)
            if (PULSEAUDIO_FOUND)
                target_sources(granite PRIVATE audio/audio_pulse.cpp audio/audio_pulse.hpp)
                target_link_libraries(granite PRIVATE ${PULSEAUDIO_LIBRARIES})
                target_include_directories(granite PRIVATE ${PULSEAUDIO_INCLUDEDIR})
                target_compile_definitions(granite PRIVATE AUDIO_HAVE_PULSE=1)
            endif()
        elseif (WIN32)
            target_sources(granite PRIVATE audio/audio_wasapi.cpp audio/audio_wasapi.hpp)
            target_compile_definitions(granite PRIVATE AUDIO_HAVE_WASAPI=1)
        endif()
    endif()

    if (GRANITE_BULLET)
        set(USE_GLUT OFF CACHE BOOL "" FORCE)
        set(BUILD_EGL OFF CACHE BOOL "" FORCE)
        set(BUILD_BULLET3 OFF CACHE BOOL "" FORCE)
        set(BUILD_PYBULLET OFF CACHE BOOL "" FORCE)
        set(USE_GRAPHICAL_BENCHMARK OFF CACHE BOOL "" FORCE)
        set(USE_DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
        set(BUILD_CPU_DEMOS OFF CACHE BOOL "" FORCE)
        set(INSTALL_LIBS ON CACHE BOOL "" FORCE)
        option(GRANITE_BULLET_ROOT "" "Path to a Bullet library checkout.")
        if (NOT GRANITE_BULLET_ROOT)
            set(GRANITE_BULLET_ROOT $ENV{BULLET_ROOT})
            message("GRANITE_BULLET_ROOT not set, overriding with environment variable BULLET_ROOT.")
        endif()
        message("Including bullet3 from GRANITE_BULLET_ROOT: ${GRANITE_BULLET_ROOT}")
        add_subdirectory(${GRANITE_BULLET_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/third_party/bullet3 EXCLUDE_FROM_ALL)
        target_sources(granite PRIVATE physics/physics_system.cpp physics/physics_system.hpp)
        target_include_directories(granite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/physics PRIVATE ${GRANITE_BULLET_ROOT}/src)
        target_compile_definitions(granite PUBLIC HAVE_GRANITE_PHYSICS=1)
        target_link_libraries(granite PRIVATE BulletDynamics BulletCollision LinearMath)
    endif()

    if (GRANITE_ISPC_TEXTURE_COMPRESSION)
        find_library(ISPC_LIBRARY NAMES ispc_texcomp HINTS ${GRANITE_ISPC_LIBRARY_DIR})
        find_path(ISPC_INCLUDE_DIR NAMES ispc_texcomp.h HINTS ${GRANITE_ISPC_INCLUDE_DIR})
        if (ISPC_LIBRARY AND ISPC_INCLUDE_DIR)
            message("Enabling ISPC texture compression.")
            target_link_libraries(granite PRIVATE ${ISPC_LIBRARY})
            target_compile_definitions(granite PRIVATE HAVE_ISPC)
            target_include_directories(granite PUBLIC ${ISPC_INCLUDE_DIR})
        else()
            message("Could not find ISPC texture compression.")
        endif()
    endif()

    if (GRANITE_ASTC_ENCODER_COMPRESSION)
        target_link_libraries(granite PRIVATE astc-encoder)
        target_compile_definitions(granite PRIVATE HAVE_ASTC_ENCODER)
    endif()

    if (GRANITE_VULKAN_FOSSILIZE)
        target_compile_definitions(granite PUBLIC GRANITE_VULKAN_FOSSILIZE)
        target_sources(granite PRIVATE vulkan/device_fossilize.cpp)
        target_link_libraries(granite PUBLIC fossilize)
    endif()

    if (GRANITE_SHADER_COMPILER_OPTIMIZE)
        target_compile_definitions(granite PRIVATE GRANITE_COMPILER_OPTIMIZE=1)
    else()
        target_compile_definitions(granite PRIVATE GRANITE_COMPILER_OPTIMIZE=0)
    endif()

    if (NOT GRANITE_PLATFORM)
       set(GRANITE_PLATFORM "GLFW")
    endif()

    if (ANDROID)
        target_sources(granite PRIVATE
                application/platforms/application_android.cpp)
        add_library(android-glue STATIC
                ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
                ${ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c)
        target_include_directories(android-glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)
        target_link_libraries(granite PRIVATE android-glue)
        if (GRANITE_ANDROID_APK_FILESYSTEM)
            target_compile_definitions(granite PRIVATE ANDROID_APK_FILESYSTEM)
        endif()
        target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_ANDROID_KHR=1)
    elseif (${GRANITE_PLATFORM} MATCHES "libretro")
        target_sources(granite PRIVATE
                application/platforms/application_libretro.cpp
                application/platforms/application_libretro_utils.cpp
                application/platforms/application_libretro_utils.hpp)
        target_compile_definitions(granite PUBLIC HAVE_LIBRETRO)
        target_include_directories(granite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/application/platforms/libretro)
    elseif (${GRANITE_PLATFORM} MATCHES "GLFW")
        set(GLFW_BUILD_TESTS OFF)
        add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)
        target_sources(granite PRIVATE
                application/platforms/application_glfw.cpp)
        target_link_libraries(granite PRIVATE glfw)
    elseif (${GRANITE_PLATFORM} MATCHES "KHR_DISPLAY")
        target_sources(granite PRIVATE
                application/platforms/application_khr_display.cpp)
        if (GRANITE_KHR_DISPLAY_ACQUIRE_XLIB)
            target_link_libraries(granite PRIVATE X11)
            target_compile_definitions(granite PRIVATE "KHR_DISPLAY_ACQUIRE_XLIB")
            target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_XLIB_XRANDR_EXT=1)
        endif()
    else()
        message(FATAL "GRANITE_PLATFORM is not set.")
    endif()
endif()

if (${CMAKE_BUILD_TYPE} MATCHES "Debug")
    target_compile_definitions(granite PUBLIC VULKAN_DEBUG)
endif()

if (CMAKE_COMPILER_IS_GNUCXX OR (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang"))
    set_source_files_properties(vulkan/managers/texture_manager.cpp PROPERTIES COMPILE_FLAGS "-Wno-type-limits -Wno-pedantic -Wno-ignored-qualifiers -Wno-unused-parameter")
endif()

if (GRANITE_SPIRV_DUMP)
    target_compile_definitions(granite PRIVATE GRANITE_SPIRV_DUMP)
endif()

target_link_libraries(granite PUBLIC volk PRIVATE spirv-cross-core)

if (NOT GRANITE_VULKAN_ONLY)
    target_link_libraries(granite
            PUBLIC
                rapidjson
            PRIVATE
                stb
                mikktspace
                meshoptimizer
                libco)
    target_include_directories(granite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb/stb)
endif()

if (NOT WIN32)
    target_link_libraries(granite PRIVATE -pthread dl m)
else()
    target_link_libraries(granite PUBLIC ws2_32)
endif()

if (NOT GRANITE_VULKAN_ONLY)
    if ("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_SOURCE_DIR}")
        set(GRANITE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    else()
        set(GRANITE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" PARENT_SCOPE)
    endif()
    function(add_blob_archive_target TARGET_NAME BLOB_NAME)
        find_package(Python3 REQUIRED)
        set_source_files_properties(
                ${CMAKE_BINARY_DIR}/${BLOB_NAME}.h
                ${CMAKE_BINARY_DIR}/${BLOB_NAME}.c
                PROPERTIES GENERATED ON)
        add_library(${TARGET_NAME} STATIC
                ${CMAKE_BINARY_DIR}/${BLOB_NAME}.h
                ${CMAKE_BINARY_DIR}/${BLOB_NAME}.c)
        target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_BINARY_DIR})
        # ... CMake, y u gotta be like this.
        set(_arg_list)
        set(_arg_counter 0)
        foreach(_arg IN LISTS ARGN)
            math(EXPR _arg_counter_mod "${_arg_counter} % 2")
            if (NOT ${_arg_counter_mod})
                list(APPEND _arg_list --input)
            endif()
            list(APPEND _arg_list ${_arg})
            math(EXPR _arg_counter "${_arg_counter} + 1")
        endforeach()
        add_custom_command(OUTPUT
            ${CMAKE_BINARY_DIR}/${BLOB_NAME}.h
            ${CMAKE_BINARY_DIR}/${BLOB_NAME}.c
            COMMAND ${Python3_EXECUTABLE}
                ${GRANITE_SOURCE_DIR}/tools/blobify.py
                --output ${CMAKE_BINARY_DIR}/${BLOB_NAME}.blob
                ${_arg_list}
            COMMAND ${Python3_EXECUTABLE}
                ${GRANITE_SOURCE_DIR}/tools/bin_to_text.py
                --output ${CMAKE_BINARY_DIR}/${BLOB_NAME}
                --blob-name ${BLOB_NAME}
                ${CMAKE_BINARY_DIR}/${BLOB_NAME}.blob
            BYPRODUCTS
                ${CMAKE_BINARY_DIR}/${BLOB_NAME}.blob)
    endfunction()

    add_subdirectory(tests)
    add_subdirectory(viewer)

    if (GRANITE_TOOLS)
        add_subdirectory(tools)
        add_granite_executable(netfs-server network/netfs_server.cpp)
        add_subdirectory(renderer/fft/test)
    endif()
endif()
