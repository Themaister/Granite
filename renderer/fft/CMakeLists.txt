add_granite_library(glfft
        glfft.cpp
        glfft.hpp
        glfft_common.hpp
        glfft_interface.hpp
        glfft_granite_interface.hpp
        glfft_granite_interface.cpp
        glfft_wisdom.cpp
        glfft_wisdom.hpp)

target_link_libraries(glfft rapidjson filesystem vulkan-backend)
target_include_directories(glfft PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(test)