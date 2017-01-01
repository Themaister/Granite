cmake_minimum_required(VERSION 3.5)
set(CMAKE_CXX_STANDARD 11)
project(Granite LANGUAGES CXX C)

if (CMAKE_COMPILER_IS_GNUCXX)
   set(GRANITE_CXX_FLAGS -Wall -Wextra -pedantic -Wno-missing-field-initializers)
endif()

option(VULKAN_WSI "Enable Vulkan WSI." ON)

add_subdirectory(util)
add_subdirectory(vulkan)
add_subdirectory(filesystem)

set(SHADERC_SPIRV_TOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spirv-tools")
set(SHADERC_GLSLANG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glslang")
set(SPIRV-Headers_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spirv-headers")
set(SHADERC_SKIP_TESTS ON)
add_subdirectory(shaderc EXCLUDE_FROM_ALL)
add_subdirectory(compiler)

add_executable(main main.cpp)
target_link_libraries(main vulkan compiler filesystem)
target_compile_options(main PRIVATE ${GRANITE_CXX_FLAGS})

