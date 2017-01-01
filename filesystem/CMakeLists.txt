add_library(filesystem filesystem.cpp filesystem.hpp)
target_include_directories(filesystem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(filesystem PRIVATE ${GRANITE_CXX_FLAGS})