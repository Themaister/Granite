add_granite_library(fast-noise FastNoise/FastNoise.cpp FastNoise/FastNoise.h)
target_include_directories(fast-noise PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/FastNoise)

if (CMAKE_COMPILER_IS_GNUCXX)
    target_compile_options(fast-noise PUBLIC -Wno-unused-function)
endif()