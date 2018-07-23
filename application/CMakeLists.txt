add_granite_library(application application.cpp application.hpp)
target_include_directories(application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(application vulkan-backend rapidjson)

add_subdirectory(input)
add_subdirectory(events)
add_subdirectory(platforms)

add_granite_library(application-wsi application_wsi.cpp application_wsi.hpp application_wsi_events.hpp)
target_include_directories(application-wsi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(application-wsi vulkan-backend event application-input)
