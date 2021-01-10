add_granite_internal_lib(granite-ecs ecs.hpp ecs.cpp)
target_include_directories(granite-ecs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(granite-ecs PUBLIC granite-util)