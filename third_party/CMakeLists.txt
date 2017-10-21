add_granite_library(fast-noise FastNoise/FastNoise.cpp FastNoise/FastNoise.h)
target_include_directories(fast-noise PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/FastNoise)

if (CMAKE_COMPILER_IS_GNUCXX)
	target_compile_options(fast-noise PUBLIC -Wno-unused-function -Wno-maybe-uninitialized PRIVATE -fno-strict-aliasing)
endif()

if (NOT MSVC)
	add_granite_library(mufft muFFT/fft_internal.h muFFT/fft.c muFFT/fft.h muFFT/kernel.c muFFT/cpu.c)
	target_include_directories(mufft PUBLIC muFFT)
endif()

add_granite_library(gli .dummy.cpp)
target_include_directories(gli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/gli)
if (CMAKE_COMPILER_IS_GNUCXX)
	target_compile_options(gli PUBLIC -Wno-type-limits -Wno-ignored-qualifiers -Wno-unused-parameter -Wno-pedantic)
endif()

add_granite_library(rapidjson .dummy.cpp)
target_include_directories(rapidjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/rapidjson/include)
target_compile_definitions(rapidjson PUBLIC RAPIDJSON_HAS_STDSTRING=1)
