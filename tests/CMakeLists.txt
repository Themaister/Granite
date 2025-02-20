add_granite_application(ui-sandbox ui_sandbox.cpp)
target_compile_definitions(ui-sandbox PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

add_granite_application(linear-image-test linear_image_test.cpp)
if (NOT ANDROID)
    target_compile_definitions(linear-image-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

add_granite_application(ycbcr-sampling-test ycbcr_sampling.cpp)
if (NOT ANDROID)
    target_compile_definitions(ycbcr-sampling-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

add_granite_application(mrt-color-mask mrt_color_mask.cpp)
if (NOT ANDROID)
    target_compile_definitions(mrt-color-mask PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

add_granite_application(multiview-test multiview_test.cpp)
if (NOT ANDROID)
    target_compile_definitions(multiview-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

add_granite_application(image-query-lod-test image_query_lod.cpp)
if (NOT ANDROID)
    target_compile_definitions(image-query-lod-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

add_granite_application(divergent-lod-test divergent_lod_test.cpp)
if (NOT ANDROID)
    target_compile_definitions(divergent-lod-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

add_granite_application(triangle-test triangle.cpp)
if (NOT ANDROID)
    target_compile_definitions(triangle-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

add_granite_application(controller-test controller_test.cpp)
if (NOT ANDROID)
    target_compile_definitions(controller-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

add_granite_application(triangle-mesh-test triangle_mesh.cpp)
if (NOT ANDROID)
    target_compile_definitions(triangle-mesh-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

add_granite_application(latency-test latency_test.cpp)
add_granite_application(hdr-test hdr_test.cpp)
if (NOT ANDROID)
    target_compile_definitions(hdr-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

add_granite_application(multi-draw-indirect multi_draw_indirect.cpp)
if (NOT ANDROID)
    target_compile_definitions(multi-draw-indirect PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

add_granite_application(bindless-test bindless_test.cpp)
if (NOT ANDROID)
    target_compile_definitions(bindless-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

add_granite_application(conservative-raster-test conservative_raster_test.cpp)
if (NOT ANDROID)
    target_compile_definitions(conservative-raster-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

add_granite_application(clustering-visualizer clustering_visualizer.cpp)
target_compile_definitions(clustering-visualizer PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

add_granite_application(subgroup-test subgroup.cpp)
target_compile_definitions(subgroup-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

add_granite_application(fragment-output-components fragment_output_components.cpp)
target_compile_definitions(fragment-output-components PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

add_granite_application(basic-compute basic_compute.cpp)
target_compile_definitions(basic-compute PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

add_granite_application(coherency coherency.cpp)
if (NOT ANDROID)
    target_compile_definitions(coherency PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

add_granite_application(debug-channel debug_channel.cpp)
target_compile_definitions(debug-channel PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

add_granite_application(pcf-test pcf_test.cpp)
target_compile_definitions(pcf-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

add_granite_application(cube-array-test cube_array_test.cpp)
target_compile_definitions(cube-array-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

add_granite_application(async-spec-constant async_spec_constant.cpp)
target_compile_definitions(async-spec-constant PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

add_granite_application(render-graph-sandbox render_graph_sandbox.cpp)
target_compile_definitions(render-graph-sandbox PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

add_granite_application(bandlimited-pixel-test bandlimited_pixel_test.cpp)
if (NOT ANDROID)
    target_compile_definitions(bandlimited-pixel-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

add_granite_application(async-compute-present-test async_compute_present.cpp)
if (NOT ANDROID)
    target_compile_definitions(async-compute-present-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

add_granite_offline_tool(sampler-precision sampler_precision.cpp)
target_compile_definitions(sampler-precision PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

add_granite_offline_tool(robustness2-test robustness2.cpp)
target_compile_definitions(robustness2-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

add_granite_offline_tool(thread-group-test thread_group_test.cpp)
add_granite_offline_tool(intrusive-test intrusive_ptr_test.cpp)
add_granite_offline_tool(lru-cache-test lru_cache_test.cpp)
add_granite_offline_tool(ecs-test ecs_test.cpp)
add_granite_offline_tool(simd-test simd_test.cpp)
add_granite_offline_tool(imported-host imported_host.cpp)
add_granite_offline_tool(imported-host-concurrent imported_host_concurrent.cpp)
add_granite_offline_tool(atomic-append-buffer-test atomic_append_buffer_test.cpp)
add_granite_offline_tool(unordered-array-test unordered_array_test.cpp)
add_granite_offline_tool(z-binning-test z_binning_test.cpp)
add_granite_offline_tool(animation-rail-test animation_rail_test.cpp)
if (NOT ANDROID)
    target_compile_definitions(z-binning-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

add_granite_offline_tool(hemisphere-integration-test hemisphere_integration.cpp)

add_granite_offline_tool(texture-decoder-test texture_decoder_test.cpp)

if (GRANITE_ASTC_ENCODER_COMPRESSION)
    target_link_libraries(texture-decoder-test PRIVATE astc-encoder)
    target_compile_definitions(texture-decoder-test PRIVATE HAVE_ASTC_DECODER)
endif()

if (GRANITE_AUDIO)
    add_granite_offline_tool(audio-test audio_test.cpp)
    target_link_libraries(audio-test PRIVATE granite-audio)
    add_granite_offline_tool(tone-filter-bench tone_filter_bench.cpp)
    target_link_libraries(tone-filter-bench PRIVATE granite-audio)
    target_compile_definitions(audio-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")

    add_granite_application(audio-application audio_application.cpp)
    if (NOT ANDROID)
        target_compile_definitions(audio-application PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
    endif()
    target_link_libraries(audio-application PRIVATE granite-audio)

    add_granite_offline_tool(resampler-test resampler_test.cpp)
    target_link_libraries(resampler-test PRIVATE granite-audio)
endif()

if (GRANITE_FFMPEG)
    add_granite_offline_tool(video-encode-test video_encode_test.cpp)
    target_link_libraries(video-encode-test PRIVATE granite-video)
    if (GRANITE_AUDIO)
        target_link_libraries(video-encode-test PRIVATE granite-audio)
    endif()
endif()

add_granite_offline_tool(linkage-test linkage_test.cpp)
add_granite_offline_tool(external-objects external_objects.cpp)
add_granite_offline_tool(performance-query performance_query.cpp)
add_granite_offline_tool(asset-manager-test asset_manager_test.cpp)

add_granite_offline_tool(meshopt-sandbox meshopt_sandbox.cpp)
if (NOT ANDROID)
    target_compile_definitions(meshopt-sandbox PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()
target_link_libraries(meshopt-sandbox PRIVATE granite-scene-export)

add_granite_application(meshlet-viewer meshlet_viewer.cpp)
if (NOT ANDROID)
    target_compile_definitions(meshlet-viewer PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()
target_link_libraries(meshlet-viewer PRIVATE granite-scene-export)

add_granite_application(dgc-test-graphics dgc_test_graphics.cpp)
add_granite_application(dgc-test-compute dgc_test_compute.cpp)

if (NOT ANDROID)
    target_compile_definitions(dgc-test-graphics PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
    target_compile_definitions(dgc-test-compute PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

option(GRANITE_TEST_INTEROP "Enable interop tests." OFF)
if (GRANITE_TEST_INTEROP)
    add_granite_offline_tool(granite-gl-interop gl_interop_test.cpp
            glad/src/glad.c
            glad/include/glad/glad.h
            glad/include/KHR/khrplatform.h)
    target_link_libraries(granite-gl-interop PRIVATE SDL3-static granite-vulkan)
    target_include_directories(granite-gl-interop PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/glad/include)

    if (WIN32)
        add_granite_offline_tool(granite-d3d11-interop d3d11_interop_test.cpp)
        target_link_libraries(granite-d3d11-interop PRIVATE SDL3-static granite-vulkan d3d11 dxgi)
        add_granite_offline_tool(granite-d3d12-interop d3d12_interop_test.cpp)
        target_link_libraries(granite-d3d12-interop PRIVATE SDL3-static granite-vulkan d3d12 dxgi)
    endif()
endif()

if (GRANITE_FFMPEG)
    add_granite_application(video-player video_player.cpp)
    if (NOT ANDROID)
        target_compile_definitions(video-player PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
    endif()
    target_link_libraries(video-player PRIVATE granite-video)
    if (GRANITE_AUDIO)
        target_link_libraries(video-player PRIVATE granite-audio)
    endif()
endif()

add_granite_application(gltf-viewer-simple gltf_viewer_simple.cpp)
add_granite_offline_tool(hiz-test hiz.cpp)
if (NOT ANDROID)
    target_compile_definitions(hiz-test PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()

