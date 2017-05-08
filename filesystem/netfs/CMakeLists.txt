add_granite_library(filesystem-netfs fs-netfs.cpp fs-netfs.hpp)
target_include_directories(filesystem-netfs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(filesystem-netfs util event networking -pthread)
