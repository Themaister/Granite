add_granite_library(vulkan-wsi wsi.cpp wsi.hpp)
target_link_libraries(vulkan-wsi vulkan-backend util event application)
target_include_directories(vulkan-wsi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})


