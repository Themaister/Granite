add_granite_executable(gltf-image-packer image_packer.cpp)
target_link_libraries(gltf-image-packer stb util)

add_granite_executable(ibl-brdf-lut-generate brdf_lut_generate.cpp)
target_link_libraries(ibl-brdf-lut-generate math util scene-formats)

add_granite_offline_tool(convert-equirect-to-environment convert_equirect_to_environment.cpp)
target_link_libraries(convert-equirect-to-environment scene-formats)

add_granite_offline_tool(gtx-convert gtx_convert.cpp)
target_link_libraries(gtx-convert texture-compression)

add_granite_offline_tool(gltf-repacker gltf_repacker.cpp)
target_link_libraries(gltf-repacker scene-formats-export util rapidjson)

add_granite_offline_tool(obj-to-gltf obj_to_gltf.cpp)
target_link_libraries(obj-to-gltf scene-formats-export util)

add_granite_offline_tool(image-compare image_compare.cpp)
target_link_libraries(image-compare scene-formats util filesystem threading)

add_granite_offline_tool(build-smaa-luts build_smaa_luts.cpp smaa/AreaTex.h smaa/SearchTex.h)
target_link_libraries(build-smaa-luts util)

add_granite_application(aa-bench aa_bench.cpp)
add_granite_headless_application(aa-bench-headless aa_bench.cpp)
target_link_libraries(aa-bench-headless)
