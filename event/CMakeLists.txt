add_library(event STATIC event.cpp event.hpp)
target_compile_options(event PRIVATE ${GRANITE_CXX_FLAGS})
target_include_directories(event PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})