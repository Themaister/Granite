add_library(rapidjson STATIC rapidjson_wrapper.hpp .dummy.cpp)
target_include_directories(rapidjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/rapidjson/include)

add_library(renderdoc-app INTERFACE)
target_include_directories(renderdoc-app INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/renderdoc)
