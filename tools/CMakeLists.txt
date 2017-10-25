add_granite_executable(gltf-image-packer image_packer.cpp)
target_link_libraries(gltf-image-packer stb util)

add_granite_executable(ibl-brdf-lut-generate brdf_lut_generate.cpp)
target_link_libraries(ibl-brdf-lut-generate math util gli)

add_granite_offline_tool(convert-equirect-to-environment convert_equirect_to_environment.cpp)

add_granite_offline_tool(ktx-convert ktx_convert.cpp)
target_link_libraries(ktx-convert texture-compression)

add_granite_offline_tool(gltf-repacker gltf_repacker.cpp)
target_link_libraries(gltf-repacker scene-formats-export util rapidjson)
