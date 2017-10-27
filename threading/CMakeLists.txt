add_granite_library(threading thread_group.cpp thread_group.hpp)
target_include_directories(threading PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(threading util)