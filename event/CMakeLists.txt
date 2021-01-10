add_granite_internal_lib(granite-event event.hpp event.cpp)
target_include_directories(granite-event PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(granite-event PUBLIC granite-util granite-application-global)