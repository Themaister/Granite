add_granite_library(application application.cpp application.hpp)
target_include_directories(application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(application vulkan-wsi)

target_compile_definitions(application PRIVATE RAPIDJSON_HAS_STDSTRING=1)
target_include_directories(application PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/rapidjson/include)

add_subdirectory(input)
add_subdirectory(events)
add_subdirectory(platforms)

