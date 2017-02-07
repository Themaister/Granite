add_library(vulkan-symbols STATIC
   vulkan_symbol_wrapper.cpp
   )
target_include_directories(vulkan-symbols PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/khronos)

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

target_include_directories(vulkan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/khronos)
target_compile_definitions(vulkan PUBLIC VULKAN_DEBUG)
target_compile_options(vulkan PRIVATE ${GRANITE_CXX_FLAGS})

add_subdirectory(wsi)

add_subdirectory(spirv-cross EXCLUDE_FROM_ALL)
target_link_libraries(vulkan spirv-cross-core)
target_link_libraries(vulkan vulkan-symbols)
target_link_libraries(vulkan util)

if (UNIX)
   target_link_libraries(vulkan dl)
   target_compile_definitions(vulkan PRIVATE HAVE_DYLIB)
endif()
