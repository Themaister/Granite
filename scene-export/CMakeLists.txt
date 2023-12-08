add_granite_internal_lib(granite-scene-export
        texture_compression.hpp texture_compression.cpp
        obj.cpp obj.hpp
        light_export.cpp light_export.hpp
        camera_export.cpp camera_export.hpp
        gltf_export.cpp gltf_export.hpp
        rgtc_compressor.cpp rgtc_compressor.hpp
        tmx_parser.cpp tmx_parser.hpp
        meshlet_export.cpp meshlet_export.hpp
        texture_utils.cpp texture_utils.hpp)

target_include_directories(granite-scene-export PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(granite-scene-export
        PUBLIC granite-vulkan granite-threading granite-math granite-renderer
        PRIVATE granite-rapidjson granite-mikktspace meshoptimizer granite-stb)

if (GRANITE_ISPC_TEXTURE_COMPRESSION)
    set(GRANITE_ISPC_LIBRARY_DIR "" CACHE STRING "ISPC library directory.")
    set(GRANITE_ISPC_INCLUDE_DIR "" CACHE STRING "ISPC include directory.")
    find_library(ISPC_LIBRARY NAMES ispc_texcomp HINTS ${GRANITE_ISPC_LIBRARY_DIR})
    find_path(ISPC_INCLUDE_DIR NAMES ispc_texcomp.h HINTS ${GRANITE_ISPC_INCLUDE_DIR})
    if (ISPC_LIBRARY AND ISPC_INCLUDE_DIR)
        message("Enabling ISPC texture compression.")
        target_link_libraries(granite-scene-export PRIVATE ${ISPC_LIBRARY})
        target_compile_definitions(granite-scene-export PRIVATE HAVE_ISPC)
        target_include_directories(granite-scene-export PUBLIC ${ISPC_INCLUDE_DIR})
    else()
        message("Could not find ISPC texture compression.")
    endif()
endif()

if ((NOT ANDROID) AND GRANITE_ASTC_ENCODER_COMPRESSION)
    target_link_libraries(granite-scene-export PRIVATE astc-encoder)
    target_compile_definitions(granite-scene-export PRIVATE HAVE_ASTC_ENCODER)
endif()
