add_granite_library(compiler compiler.cpp compiler.hpp)
target_link_libraries(compiler shaderc util filesystem)
target_include_directories(compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
