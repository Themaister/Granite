add_granite_offline_tool(glfft-test glfft_cli.cpp glfft_cli.hpp glfft_main.cpp glfft_test.cpp)
target_link_libraries(glfft-test PRIVATE muFFT)

add_granite_offline_tool(fft-test fft_test.cpp)
target_link_libraries(fft-test PRIVATE muFFT)
add_granite_offline_tool(fft-bench fft_bench.cpp)
