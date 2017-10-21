add_granite_library(application application.cpp application.hpp)
target_include_directories(application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(application vulkan-wsi rapidjson)

add_subdirectory(input)
add_subdirectory(events)
add_subdirectory(platforms)

