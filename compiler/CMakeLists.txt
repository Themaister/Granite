add_library(compiler STATIC compiler.cpp compiler.hpp)
target_link_libraries(compiler shaderc util filesystem)
target_include_directories(compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(compiler PRIVATE ${GRANITE_CXX_FLAGS})
