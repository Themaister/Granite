add_granite_executable(gltf-image-packer image_packer.cpp)
target_link_libraries(gltf-image-packer PRIVATE granite-stb)

add_granite_executable(ibl-brdf-lut-generate brdf_lut_generate.cpp)

add_granite_offline_tool(convert-equirect-to-environment convert_equirect_to_environment.cpp)

add_granite_offline_tool(convert-cube-to-environment convert_cube_to_environment.cpp)

add_granite_offline_tool(gtx-convert gtx_convert.cpp)
target_link_libraries(gtx-convert PRIVATE granite-scene-export)

add_granite_offline_tool(gtx-cat gtx_cat.cpp)

add_granite_offline_tool(gltf-repacker gltf_repacker.cpp)
target_link_libraries(gltf-repacker PRIVATE granite-scene-export granite-rapidjson)

add_granite_offline_tool(obj-to-gltf obj_to_gltf.cpp)
target_link_libraries(obj-to-gltf PRIVATE granite-scene-export)

add_granite_offline_tool(image-compare image_compare.cpp)
target_link_libraries(image-compare PRIVATE granite-stb granite-rapidjson)

add_granite_offline_tool(build-smaa-luts build_smaa_luts.cpp smaa/AreaTex.h smaa/SearchTex.h)

add_granite_offline_tool(bitmap-to-mesh bitmap_mesh.cpp bitmap_to_mesh.cpp bitmap_to_mesh.hpp)
target_link_libraries(bitmap-to-mesh PRIVATE meshoptimizer granite-scene-export)

add_granite_application(aa-bench aa_bench.cpp)

add_granite_headless_application(aa-bench-headless aa_bench.cpp)

add_granite_application(texture-viewer texture_viewer.cpp)
