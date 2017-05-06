add_library(filesystem-netfs STATIC fs-netfs.cpp fs-netfs.hpp)
target_include_directories(filesystem-netfs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(filesystem-netfs PRIVATE ${GRANITE_CXX_FLAGS})
target_link_libraries(filesystem-netfs util event networking -pthread)