add_library(stb STATIC stb_image.c stb_truetype.c)
target_include_directories(stb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)

add_library(stb-vorbis STATIC stb_vorbis.h stb/stb_vorbis.c)
target_include_directories(stb-vorbis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

