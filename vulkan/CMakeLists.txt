add_granite_library(vulkan-interface texture_format.cpp texture_format.hpp)
target_include_directories(vulkan-interface PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/khronos)
target_link_libraries(vulkan-interface volk util)

add_granite_library(vulkan-backend
        vulkan.cpp vulkan.hpp
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
        query_pool.cpp query_pool.hpp)

option(GRANITE_VULKAN_FILESYSTEM "Enable features which depend on a filesystem implementation." ON)
if (GRANITE_VULKAN_FILESYSTEM)
    target_sources(vulkan-backend PRIVATE
            managers/shader_manager.cpp
            managers/shader_manager.hpp
            managers/texture_manager.cpp
            managers/texture_manager.hpp)

    target_link_libraries(vulkan-backend
            filesystem
            compiler
            scene-formats
            stb)
    target_compile_definitions(vulkan-backend PUBLIC GRANITE_VULKAN_FILESYSTEM)
    target_include_directories(vulkan-backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/managers)
endif()

target_include_directories(vulkan-backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (${CMAKE_BUILD_TYPE} MATCHES "Debug")
    target_compile_definitions(vulkan-backend PUBLIC VULKAN_DEBUG)
endif()

if (CMAKE_COMPILER_IS_GNUCXX OR (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang"))
    set_source_files_properties(texture_manager.cpp PROPERTIES
            COMPILE_FLAGS "-Wno-type-limits -Wno-pedantic -Wno-ignored-qualifiers -Wno-unused-parameter")
endif()

option(GRANITE_SPIRV_DUMP "Dump compiled SPIR-V modules to cache." OFF)
if (GRANITE_SPIRV_DUMP)
    target_compile_definitions(vulkan-backend PRIVATE GRANITE_SPIRV_DUMP)
endif()

option(GRANITE_VULKAN_MT "Make Vulkan backend thread-safe." ON)
if (GRANITE_VULKAN_MT)
    target_compile_definitions(vulkan-backend PUBLIC GRANITE_VULKAN_MT)
    target_link_libraries(vulkan-backend threading)
endif()

option(GRANITE_VULKAN_FOSSILIZE "Enable support for Fossilize." ON)
if (GRANITE_VULKAN_FOSSILIZE)
    target_link_libraries(vulkan-backend fossilize)
    target_compile_definitions(vulkan-backend PUBLIC GRANITE_VULKAN_FOSSILIZE)
    target_sources(vulkan-backend PRIVATE device_fossilize.cpp)
endif()

target_link_libraries(vulkan-backend vulkan-interface spirv-cross-core)

if (UNIX)
   target_link_libraries(vulkan-backend dl)
   target_compile_definitions(vulkan-backend PRIVATE HAVE_DYLIB)
endif()
