add_granite_internal_lib(granite-compiler compiler.cpp compiler.hpp)
target_include_directories(granite-compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(granite-compiler
        PUBLIC granite-application-global
        PRIVATE SPIRV-Tools shaderc granite-path granite-util)

if (GRANITE_SHADER_COMPILER_OPTIMIZE)
    target_compile_definitions(granite-compiler PRIVATE GRANITE_COMPILER_OPTIMIZE=1)
else()
    target_compile_definitions(granite-compiler PRIVATE GRANITE_COMPILER_OPTIMIZE=0)
endif()

