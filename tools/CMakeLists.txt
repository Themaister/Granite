add_granite_executable(gltf-image-packer image_packer.cpp)
target_link_libraries(gltf-image-packer stb util)

add_granite_executable(ibl-brdf-lut-generate brdf_lut_generate.cpp)
target_link_libraries(ibl-brdf-lut-generate math util)
target_include_directories(ibl-brdf-lut-generate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/gli)
