add_library(event STATIC event.cpp event.hpp ecs.hpp ecs.cpp)
target_compile_options(event PRIVATE ${GRANITE_CXX_FLAGS})
target_include_directories(event PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(event util)