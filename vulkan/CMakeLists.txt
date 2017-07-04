add_granite_library(vulkan-symbols
   vulkan_symbol_wrapper.cpp
   )
target_include_directories(vulkan-symbols PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/khronos)

add_granite_library(vulkan-backend
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
        memory_allocator.cpp
        texture_manager.cpp
        fence.hpp
        format.hpp
        limits.hpp
        event_manager.cpp
        pipeline_event.cpp
        vulkan_events.hpp)

target_include_directories(vulkan-backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/khronos)
target_include_directories(vulkan-backend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/gli ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/glm)
target_compile_definitions(vulkan-backend PUBLIC VULKAN_DEBUG)

if (CMAKE_COMPILER_IS_GNUCXX)
    set_source_files_properties(texture_manager.cpp PROPERTIES COMPILE_FLAGS "-Wno-type-limits -Wno-pedantic -Wno-ignored-qualifiers -Wno-unused-parameter")
endif()

add_subdirectory(wsi)

add_subdirectory(spirv-cross EXCLUDE_FROM_ALL)
target_link_libraries(vulkan-backend spirv-cross-core)
target_link_libraries(vulkan-backend vulkan-symbols)
target_link_libraries(vulkan-backend util)
target_link_libraries(vulkan-backend vulkan-wsi)
target_link_libraries(vulkan-backend filesystem)
target_link_libraries(vulkan-backend compiler)
target_link_libraries(vulkan-backend stb)
target_link_libraries(vulkan-backend event)

if (UNIX)
   target_link_libraries(vulkan-backend dl)
   target_compile_definitions(vulkan-backend PRIVATE HAVE_DYLIB)
endif()
