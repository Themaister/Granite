add_granite_library(scene-formats
        gltf.cpp gltf.hpp
        scene_formats.hpp
        texture_loading.cpp texture_loading.hpp
        gltf_export.cpp gltf_export.hpp)
target_link_libraries(scene-formats math util filesystem renderer vulkan-backend rapidjson gli)
target_include_directories(scene-formats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
