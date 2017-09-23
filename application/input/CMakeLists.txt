add_granite_library(application-input input.hpp input.cpp)
target_include_directories(application-input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(application-input vulkan-wsi math ui-renderer)