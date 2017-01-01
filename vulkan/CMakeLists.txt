add_library(vulkan-symbols
   vulkan_symbol_wrapper.cpp
   )
target_link_libraries(vulkan-symbols)
target_include_directories(vulkan-symbols PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../khronos/include)

add_library(vulkan
        vulkan.cpp
        device.cpp
        image.cpp
        cookie.cpp
        sampler.cpp
        command_pool.cpp
        fence_manager.cpp
        descriptor_set.cpp
        semaphore_manager.cpp
        command_buffer.cpp
        shader.cpp
        render_pass.cpp
        buffer.cpp
        chain_allocator.cpp
        semaphore.cpp
        memory_allocator.cpp)

target_include_directories(vulkan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../khronos/include)
target_compile_definitions(vulkan PUBLIC VULKAN_DEBUG)

add_subdirectory(wsi)

add_subdirectory(SPIRV-Cross EXCLUDE_FROM_ALL)
target_link_libraries(vulkan spirv-cross-core)
target_link_libraries(vulkan vulkan-symbols)

if (UNIX)
   target_link_libraries(vulkan dl)
   target_compile_definitions(vulkan PRIVATE HAVE_DYLIB)
endif()
