cmake_minimum_required(VERSION 3.5)
set(CMAKE_CXX_STANDARD 11)
project(Granite LANGUAGES CXX C)

if (CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Wno-missing-field-initializers")
endif()

option(VULKAN_WSI "Enable Vulkan WSI." ON)

add_subdirectory(vulkan)

