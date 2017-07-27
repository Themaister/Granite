add_granite_library(filesystem-os os.cpp os.hpp)
target_include_directories(filesystem-os PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(filesystem-os util event)
