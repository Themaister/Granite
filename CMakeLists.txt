cmake_minimum_required(VERSION 3.5)
set(CMAKE_CXX_STANDARD 11)
project(Granite LANGUAGES CXX C)

if (CMAKE_COMPILER_IS_GNUCXX)
   set(GRANITE_CXX_FLAGS -Wall -Wextra -pedantic -Wno-missing-field-initializers)
endif()

option(VULKAN_WSI "Enable Vulkan WSI." ON)

set(SHADERC_THIRD_PARTY_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party" CACHE STRING "Third party path." FORCE)
set(SPIRV-Headers_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spirv-headers" CACHE STRING "SPIRV-Headers path")
set(SHADERC_SKIP_TESTS ON)
add_subdirectory(third_party/shaderc EXCLUDE_FROM_ALL)
add_subdirectory(compiler)

add_subdirectory(util)
add_subdirectory(math)
add_subdirectory(vulkan)
add_subdirectory(filesystem)
add_subdirectory(renderer)

add_executable(main main.cpp)
target_link_libraries(main renderer vulkan compiler filesystem)
target_compile_options(main PRIVATE ${GRANITE_CXX_FLAGS})

