add_library(renderer STATIC
        mainloop.cpp mainloop.hpp
        event.cpp event.hpp
        render_queue.hpp render_queue.cpp
        shader_manager.hpp shader_manager.cpp)
target_link_libraries(renderer vulkan math compiler)
target_include_directories(renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(renderer PRIVATE ${GRANITE_CXX_FLAGS})