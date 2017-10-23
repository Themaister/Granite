find_library(ISPC NAMES ispc_texcomp)

add_granite_library(texture-compression texture_compression.hpp texture_compression.cpp)
if (ISPC)
    target_link_libraries(texture-compression ${ISPC})
    target_compile_definitions(texture-compression PRIVATE HAVE_ISPC)
endif()
target_link_libraries(texture-compression gli util filesystem renderer)

add_granite_library(scene-formats
        gltf.cpp gltf.hpp
        scene_formats.hpp
        texture_loading.cpp texture_loading.hpp
        gltf_export.cpp gltf_export.hpp)
target_link_libraries(scene-formats math util filesystem renderer vulkan-backend rapidjson gli texture-compression)
target_include_directories(scene-formats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

