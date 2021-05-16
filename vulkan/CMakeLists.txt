add_granite_internal_lib(granite-vulkan
        context.cpp context.hpp
        vulkan_headers.hpp vulkan_prerotate.hpp
        device.cpp device.hpp
        wsi.cpp wsi.hpp
        wsi_timing.cpp wsi_timing.hpp
        buffer_pool.cpp buffer_pool.hpp
        image.cpp image.hpp
        cookie.cpp cookie.hpp
        sampler.cpp sampler.hpp
        command_pool.cpp command_pool.hpp
        fence_manager.cpp fence_manager.hpp
        descriptor_set.cpp descriptor_set.hpp
        semaphore_manager.cpp semaphore_manager.hpp
        command_buffer.cpp command_buffer.hpp
        shader.cpp shader.hpp
        render_pass.cpp render_pass.hpp
        buffer.cpp buffer.hpp
        semaphore.cpp semaphore.hpp
        memory_allocator.cpp memory_allocator.hpp
        fence.hpp fence.cpp
        format.hpp
        limits.hpp
        type_to_string.hpp
        quirks.hpp
        vulkan_common.hpp
        event_manager.cpp event_manager.hpp
        pipeline_event.cpp pipeline_event.hpp
        query_pool.cpp query_pool.hpp
        texture/texture_format.cpp texture/texture_format.hpp)

target_include_directories(granite-vulkan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (GRANITE_RENDERDOC_CAPTURE)
    target_link_libraries(granite-vulkan PRIVATE granite-renderdoc-app)
    target_sources(granite-vulkan PRIVATE renderdoc_capture.cpp)
    if (NOT WIN32)
        target_link_libraries(granite-vulkan PRIVATE dl)
    endif()
endif()

if (GRANITE_VULKAN_MT)
    target_compile_definitions(granite-vulkan PUBLIC GRANITE_VULKAN_MT)
    if (GRANITE_VULKAN_THREAD_GROUP)
        target_link_libraries(granite-vulkan PRIVATE granite-threading)
        target_compile_definitions(granite-vulkan PRIVATE GRANITE_VULKAN_THREAD_GROUP)
    endif()
endif()

if (GRANITE_VULKAN_BETA)
    target_compile_definitions(granite-vulkan PUBLIC GRANITE_VULKAN_BETA)
endif()

if (GRANITE_VULKAN_FOSSILIZE)
    target_compile_definitions(granite-vulkan PUBLIC GRANITE_VULKAN_FOSSILIZE)
    target_sources(granite-vulkan PRIVATE device_fossilize.cpp)
    target_link_libraries(granite-vulkan PUBLIC fossilize)
endif()

if (GRANITE_VULKAN_FILESYSTEM)
    target_sources(granite-vulkan PRIVATE
            managers/shader_manager.cpp
            managers/shader_manager.hpp
            managers/texture_manager.cpp
            managers/texture_manager.hpp)

    target_sources(granite-vulkan PRIVATE
            texture/memory_mapped_texture.cpp texture/memory_mapped_texture.hpp
            texture/texture_files.cpp texture/texture_files.hpp
            texture/texture_decoder.cpp texture/texture_decoder.hpp)

    target_link_libraries(granite-vulkan
            PUBLIC granite-filesystem
            PRIVATE granite-rapidjson granite-stb granite-math)

    target_compile_definitions(granite-vulkan PUBLIC GRANITE_VULKAN_FILESYSTEM)
    target_include_directories(granite-vulkan PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/managers
            ${CMAKE_CURRENT_SOURCE_DIR}/texture)

    if (GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER)
        target_compile_definitions(granite-vulkan PUBLIC GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER=1)
        target_link_libraries(granite-vulkan PRIVATE granite-compiler)
    endif()

    if (CMAKE_COMPILER_IS_GNUCXX OR (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang"))
        set_source_files_properties(managers/texture_manager.cpp PROPERTIES COMPILE_FLAGS "-Wno-type-limits -Wno-pedantic -Wno-ignored-qualifiers -Wno-unused-parameter")
    endif()
endif()

if (${CMAKE_BUILD_TYPE} MATCHES "Debug")
    target_compile_definitions(granite-vulkan PUBLIC VULKAN_DEBUG)
endif()

target_link_libraries(granite-vulkan
        PRIVATE granite-volk
        PUBLIC granite-util granite-volk-headers)

if (GRANITE_VULKAN_SPIRV_CROSS)
    target_link_libraries(granite-vulkan PRIVATE spirv-cross-core)
    target_compile_definitions(granite-vulkan PRIVATE GRANITE_VULKAN_SPIRV_CROSS=1)
endif()
