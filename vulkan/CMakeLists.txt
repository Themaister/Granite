add_granite_internal_lib(granite-vulkan
        context.cpp context.hpp
        vulkan_headers.hpp vulkan_prerotate.hpp
        device.cpp device.hpp
        wsi.cpp wsi.hpp
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
        indirect_layout.cpp indirect_layout.hpp
        pipeline_cache.cpp pipeline_cache.hpp
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

if (WIN32 AND GRANITE_VULKAN_DXGI_INTEROP)
    target_sources(granite-vulkan PRIVATE wsi_dxgi.cpp wsi_dxgi.hpp)
    target_compile_definitions(granite-vulkan PUBLIC HAVE_WSI_DXGI_INTEROP)
endif()

target_include_directories(granite-vulkan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (GRANITE_RENDERDOC_CAPTURE)
    target_link_libraries(granite-vulkan PRIVATE granite-renderdoc-app)
    target_sources(granite-vulkan PRIVATE renderdoc_capture.cpp)
    if (NOT WIN32)
        target_link_libraries(granite-vulkan PRIVATE dl)
    endif()
endif()

if (GRANITE_VULKAN_SYSTEM_HANDLES)
    if (GRANITE_VULKAN_FOSSILIZE)
        target_compile_definitions(granite-vulkan PUBLIC GRANITE_VULKAN_FOSSILIZE)
        target_sources(granite-vulkan PRIVATE device_fossilize.cpp device_fossilize.hpp)
        target_link_libraries(granite-vulkan PUBLIC fossilize)
    endif()

    target_compile_definitions(granite-vulkan PUBLIC GRANITE_VULKAN_SYSTEM_HANDLES)
    target_sources(granite-vulkan PRIVATE
            managers/shader_manager.cpp
            managers/shader_manager.hpp
            managers/resource_manager.cpp
            managers/resource_manager.hpp)

    target_sources(granite-vulkan PRIVATE
            texture/memory_mapped_texture.cpp texture/memory_mapped_texture.hpp
            mesh/meshlet.hpp mesh/meshlet.cpp
            texture/texture_files.cpp texture/texture_files.hpp
            texture/texture_decoder.cpp texture/texture_decoder.hpp)

    target_link_libraries(granite-vulkan
            PUBLIC granite-filesystem
            PRIVATE granite-threading granite-rapidjson granite-stb granite-math)

    target_include_directories(granite-vulkan PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/managers
            ${CMAKE_CURRENT_SOURCE_DIR}/texture
            ${CMAKE_CURRENT_SOURCE_DIR}/mesh)

    if (GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER)
        target_compile_definitions(granite-vulkan PUBLIC GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER=1)
        target_link_libraries(granite-vulkan PRIVATE granite-compiler)
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

if (ANDROID AND GRANITE_ANDROID_SWAPPY)
    find_package(games-frame-pacing REQUIRED CONFIG)
	target_link_libraries(granite-vulkan PRIVATE games-frame-pacing::swappy)
	target_compile_definitions(granite-vulkan PRIVATE HAVE_SWAPPY)
endif()

if (GRANITE_SHIPPING)
    target_compile_definitions(granite-vulkan PUBLIC GRANITE_SHIPPING)
endif()

if (GRANITE_FFMPEG_VULKAN)
    target_compile_definitions(granite-vulkan PRIVATE HAVE_FFMPEG_VULKAN)
endif()

if (GRANITE_VULKAN_PROFILES)
    # Must be defined by caller as an INTERFACE library before including Granite.
    if (NOT TARGET granite-vulkan-profiles)
        message(FATAL_ERROR "granite-vulkan-profiles is not a target. This must be defined by caller before add_subdirectory(Granite).")
    endif()
    target_link_libraries(granite-vulkan PRIVATE granite-vulkan-profiles)
    target_compile_definitions(granite-vulkan PRIVATE GRANITE_VULKAN_PROFILES)
endif()

if (GRANITE_VULKAN_POST_MORTEM)
    add_subdirectory(post-mortem)
    target_link_libraries(granite-vulkan PRIVATE granite-vulkan-post-mortem)
    target_compile_definitions(granite-vulkan PUBLIC HAVE_GRANITE_VULKAN_POST_MORTEM)
endif()
