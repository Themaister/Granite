add_granite_library(ecs ecs.cpp ecs.hpp)
target_include_directories(ecs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ecs util)