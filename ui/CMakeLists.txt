add_granite_library(ui-renderer font.hpp font.cpp)
target_link_libraries(ui-renderer renderer stb filesystem)
target_include_directories(ui-renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
