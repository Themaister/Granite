option(GRANITE_ISPC_TEXTURE_COMPRESSION "Enable ISPC texture compression" ON)
option(GRANITE_ASTC_ENCODER_COMPRESSION "Enable astc-encoder texture compression" ON)

add_granite_library(texture-compression texture_compression.hpp texture_compression.cpp)

set(GRANITE_ISPC_LIBRARY_DIR "" CACHE TYPE PATH)
set(GRANITE_ISPC_INCLUDE_DIR "" CACHE TYPE PATH)

if (GRANITE_ISPC_TEXTURE_COMPRESSION)
    find_library(ISPC_LIBRARY NAMES ispc_texcomp HINTS ${GRANITE_ISPC_LIBRARY_DIR})
    find_path(ISPC_INCLUDE_DIR NAMES ispc_texcomp.h HINTS ${GRANITE_ISPC_INCLUDE_DIR})
    if (ISPC_LIBRARY AND ISPC_INCLUDE_DIR)
        target_link_libraries(texture-compression ${ISPC_LIBRARY})
        target_compile_definitions(texture-compression PRIVATE HAVE_ISPC)
        target_include_directories(texture-compression PUBLIC ${ISPC_INCLUDE_DIR})
    endif()
endif()

if (GRANITE_ASTC_ENCODER_COMPRESSION)
    target_link_libraries(texture-compression astc-encoder)
    target_compile_definitions(texture-compression PRIVATE HAVE_ASTC_ENCODER)
endif()
target_link_libraries(texture-compression gli util filesystem renderer)

add_granite_library(scene-formats
        gltf.cpp gltf.hpp
        scene_formats.hpp
        texture_files.cpp texture_files.hpp
        gltf_export.cpp gltf_export.hpp)
target_link_libraries(scene-formats math util filesystem renderer vulkan-backend rapidjson gli texture-compression)
target_include_directories(scene-formats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

