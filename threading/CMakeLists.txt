add_granite_internal_lib(granite-threading
        thread_group.cpp thread_group.hpp
        thread_latch.cpp thread_latch.hpp
        task_composer.cpp task_composer.hpp)

target_include_directories(granite-threading PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(granite-threading PUBLIC granite-util granite-application-global)