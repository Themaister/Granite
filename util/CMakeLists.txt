add_library(util util.hpp util.cpp)
target_compile_options(util PRIVATE ${GRANITE_CXX_FLAGS})
target_include_directories(util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})