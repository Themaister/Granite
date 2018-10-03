add_granite_library(compiler compiler.cpp compiler.hpp)
target_link_libraries(compiler shaderc util filesystem)
target_include_directories(compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

option(GRANITE_SHADER_COMPILER_OPTIMIZE "Optimize SPIR-V." ON)
if (GRANITE_SHADER_COMPILER_OPTIMIZE)
    target_compile_definitions(compiler PRIVATE GRANITE_COMPILER_OPTIMIZE=1)
else()
    target_compile_definitions(compiler PRIVATE GRANITE_COMPILER_OPTIMIZE=0)
endif()
target_link_libraries(compiler SPIRV-Tools)

