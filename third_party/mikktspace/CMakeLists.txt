add_library(mikktspace STATIC mikktspace.c mikktspace.h)
target_include_directories(mikktspace PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})