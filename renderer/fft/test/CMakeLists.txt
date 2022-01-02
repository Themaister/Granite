add_granite_offline_tool(fft-test fft_test.cpp)
target_link_libraries(fft-test PRIVATE muFFT)
add_granite_offline_tool(fft-bench fft_bench.cpp)
