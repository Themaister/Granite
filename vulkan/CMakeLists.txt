add_granite_library(vulkan-symbols vulkan_symbol_wrapper.cpp vulkan_symbol_wrapper.h)
target_include_directories(vulkan-symbols PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/khronos)

add_granite_library(vulkan-backend
        vulkan.cpp vulkan.hpp
        device.cpp device.hpp
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
        shader_manager.cpp shader_manager.hpp
        memory_allocator.cpp memory_allocator.hpp
        texture_manager.cpp texture_manager.hpp
        fence.hpp fence.cpp
        format.hpp
        limits.hpp
        type_to_string.hpp
        quirks.hpp
        event_manager.cpp event_manager.hpp
        pipeline_event.cpp pipeline_event.hpp
        query_pool.cpp query_pool.hpp
        vulkan_events.hpp)

target_include_directories(vulkan-backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/khronos)

if (${CMAKE_BUILD_TYPE} MATCHES "Debug")
    target_compile_definitions(vulkan-backend PUBLIC VULKAN_DEBUG)
endif()

if (CMAKE_COMPILER_IS_GNUCXX OR (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang"))
    set_source_files_properties(texture_manager.cpp PROPERTIES COMPILE_FLAGS "-Wno-type-limits -Wno-pedantic -Wno-ignored-qualifiers -Wno-unused-parameter")
endif()

option(GRANITE_SPIRV_DUMP "Dump compiled SPIR-V modules to cache." OFF)
if (GRANITE_SPIRV_DUMP)
    target_compile_definitions(vulkan-backend PRIVATE GRANITE_SPIRV_DUMP)
endif()

option(GRANITE_VULKAN_MT "Make Vulkan backend thread-safe." ON)
if (GRANITE_VULKAN_MT)
    target_compile_definitions(vulkan-backend PRIVATE VULKAN_MT)
    target_link_libraries(vulkan-backend threading)
endif()

add_subdirectory(wsi)

target_link_libraries(vulkan-backend spirv-cross-core)
target_link_libraries(vulkan-backend vulkan-symbols)
target_link_libraries(vulkan-backend util)
target_link_libraries(vulkan-backend vulkan-wsi)
target_link_libraries(vulkan-backend filesystem)
target_link_libraries(vulkan-backend compiler)
target_link_libraries(vulkan-backend stb)
target_link_libraries(vulkan-backend event)
target_link_libraries(vulkan-backend math)
target_link_libraries(vulkan-backend scene-formats)
target_link_libraries(vulkan-backend vulkan-pipeline-cache)

if (UNIX)
   target_link_libraries(vulkan-backend dl)
   target_compile_definitions(vulkan-backend PRIVATE HAVE_DYLIB)
endif()
