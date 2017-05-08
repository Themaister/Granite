add_granite_library(importers
        gltf.cpp gltf.hpp)
target_link_libraries(importers math util filesystem renderer vulkan-backend)
target_include_directories(importers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(importers PRIVATE RAPIDJSON_HAS_STDSTRING=1)
target_include_directories(importers PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/rapidjson/include)
