add_library(stb STATIC stb_image.c stb_truetype.c)
target_include_directories(stb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)
if (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
	target_compile_options(stb PRIVATE -Wno-backslash-newline-escape)
endif()

add_library(stb-vorbis STATIC stb_vorbis.h stb/stb_vorbis.c)
target_include_directories(stb-vorbis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
	target_compile_options(stb-vorbis PRIVATE -Wno-backslash-newline-escape)
endif()

