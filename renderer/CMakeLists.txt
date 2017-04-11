add_library(renderer STATIC
        mainloop.cpp mainloop.hpp
        render_queue.hpp render_queue.cpp
        mesh.hpp mesh.cpp
        scene.hpp scene.cpp
        shader_suite.hpp shader_suite.cpp
        render_context.hpp render_context.cpp)
target_link_libraries(renderer vulkan-backend math)
target_include_directories(renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(renderer PRIVATE ${GRANITE_CXX_FLAGS})
