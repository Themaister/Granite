add_granite_library(rapidjson rapidjson_wrapper.hpp .dummy.cpp)
target_include_directories(rapidjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/rapidjson/include)

add_library(volk STATIC volk/volk.c volk/volk.h)
target_include_directories(volk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/volk ${CMAKE_CURRENT_SOURCE_DIR}/khronos)

add_library(muFFT STATIC
        muFFT/kernel.c
        muFFT/fft.h
        muFFT/fft.c
        muFFT/fft_internal.h
        muFFT/cpu.c)
target_include_directories(muFFT PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/muFFT)

