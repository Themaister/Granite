add_library(vulkan-symbols STATIC
   vulkan_symbol_wrapper.cpp
   )
target_include_directories(vulkan-symbols PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/khronos)

add_library(vulkan-backend
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
        shader_manager.cpp
        memory_allocator.cpp)

target_include_directories(vulkan-backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/khronos)
target_compile_definitions(vulkan-backend PUBLIC VULKAN_DEBUG)
target_compile_options(vulkan-backend PRIVATE ${GRANITE_CXX_FLAGS})

add_subdirectory(wsi)

add_subdirectory(spirv-cross EXCLUDE_FROM_ALL)
target_link_libraries(vulkan-backend spirv-cross-core)
target_link_libraries(vulkan-backend vulkan-symbols)
target_link_libraries(vulkan-backend util)
target_link_libraries(vulkan-backend vulkan-wsi)
target_link_libraries(vulkan-backend filesystem)
target_link_libraries(vulkan-backend compiler)

if (UNIX)
   target_link_libraries(vulkan-backend dl)
   target_compile_definitions(vulkan-backend PRIVATE HAVE_DYLIB)
endif()
