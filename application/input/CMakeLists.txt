add_granite_internal_lib(granite-input input.hpp input.cpp)
target_include_directories(granite-input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(granite-input PUBLIC granite-util granite-event granite-math)