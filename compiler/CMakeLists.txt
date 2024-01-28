add_granite_internal_lib(granite-compiler
        glsl_compiler.cpp glsl_compiler.hpp
        slang_compiler.cpp slang_compiler.hpp)
target_include_directories(granite-compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(granite-compiler
        PUBLIC granite-application-global
        PRIVATE SPIRV-Tools granite-path granite-util shaderc slang)

if (GRANITE_SHADER_COMPILER_OPTIMIZE)
    target_compile_definitions(granite-compiler PRIVATE GRANITE_COMPILER_OPTIMIZE=1)
else()
    target_compile_definitions(granite-compiler PRIVATE GRANITE_COMPILER_OPTIMIZE=0)
endif()

