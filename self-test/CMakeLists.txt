cmake_minimum_required(VERSION 3.5)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_STANDARD 99)
project(Granite-Self-Test LANGUAGES CXX C)

add_subdirectory(.. granite)
add_granite_offline_tool(link-test link_test.cpp)
granite_install_executable(link-test)
