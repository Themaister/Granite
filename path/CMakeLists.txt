add_granite_internal_lib(granite-path path.hpp path.cpp)
target_include_directories(granite-path PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(granite-path PRIVATE granite-util)