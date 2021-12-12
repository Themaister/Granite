add_granite_internal_lib(granite-path path_utils.hpp path_utils.cpp)
target_include_directories(granite-path PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(granite-path PRIVATE granite-util)