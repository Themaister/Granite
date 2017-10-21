add_granite_executable(gltf-image-packer image_packer.cpp)
target_link_libraries(gltf-image-packer stb util)

add_granite_executable(ibl-brdf-lut-generate brdf_lut_generate.cpp)
target_link_libraries(ibl-brdf-lut-generate math util gli)

add_granite_offline_tool(convert-equirect-to-environment convert_equirect_to_environment.cpp)

find_library(ISPC NAMES ispc_texcomp)

if (ISPC)
    add_granite_offline_tool(ktx-convert ktx_convert.cpp)
    target_link_libraries(ktx-convert util gli ${ISPC})
endif()

add_granite_offline_tool(png2ktx png2ktx.cpp)
target_link_libraries(png2ktx gli)

add_granite_offline_tool(gltf-repacker gltf_repacker.cpp)
target_link_libraries(gltf-repacker scene-formats util rapidjson)
