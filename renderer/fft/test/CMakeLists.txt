add_granite_offline_tool(glfft-test glfft_cli.cpp glfft_cli.hpp glfft_main.cpp glfft_test.cpp)
target_link_libraries(glfft-test PRIVATE muFFT)
