add_granite_library(importers
        gltf.cpp gltf.hpp importers.hpp)
target_link_libraries(importers math util filesystem renderer vulkan-backend rapidjson)
target_include_directories(importers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
