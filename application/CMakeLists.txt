add_granite_library(application-managers global_managers.hpp global_managers.cpp)
target_include_directories(application-managers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(application-managers filesystem threading event ui-renderer)
if (GRANITE_AUDIO)
    target_link_libraries(application-managers audio-backend)
endif()

add_granite_library(application application.cpp application.hpp)
target_include_directories(application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(application vulkan-backend rapidjson renderer)

add_subdirectory(input EXCLUDE_FROM_ALL)
add_subdirectory(events EXCLUDE_FROM_ALL)
add_subdirectory(platforms EXCLUDE_FROM_ALL)

add_granite_library(application-wsi application_wsi.cpp application_wsi.hpp application_wsi_events.hpp)
target_include_directories(application-wsi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(application-wsi vulkan-backend event application-input)

if (NOT ANDROID)
    add_granite_library(application-entry application_entry.cpp)
    target_link_libraries(application-entry granite)
endif()

add_granite_library(application-entry-headless application_entry.cpp)
target_link_libraries(application-entry-headless granite)
target_compile_definitions(application-entry-headless PRIVATE APPLICATION_ENTRY_HEADLESS=1)
