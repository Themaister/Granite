add_granite_library(ui-renderer font.hpp font.cpp flat_renderer.hpp flat_renderer.cpp sprite.cpp sprite.hpp)
target_link_libraries(ui-renderer renderer stb filesystem)
target_include_directories(ui-renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
