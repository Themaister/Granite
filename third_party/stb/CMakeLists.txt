add_granite_third_party_lib(granite-stb stb_image.c stb_truetype.c)
target_include_directories(granite-stb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)
if (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
	target_compile_options(granite-stb PRIVATE -Wno-backslash-newline-escape)
endif()

add_granite_third_party_lib(granite-stb-vorbis stb_vorbis.h stb/stb_vorbis.c)
target_include_directories(granite-stb-vorbis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
	target_compile_options(granite-stb-vorbis PRIVATE -Wno-backslash-newline-escape)
endif()

