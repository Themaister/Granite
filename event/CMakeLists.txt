add_granite_library(event event.cpp event.hpp ecs.hpp ecs.cpp)
target_include_directories(event PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(event util application-managers)
