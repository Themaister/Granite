add_granite_internal_lib(granite-renderer
        render_queue.hpp render_queue.cpp
        simple_renderer.hpp simple_renderer.cpp
        mesh.hpp mesh.cpp
        scene.hpp scene.cpp
        node.hpp node.cpp
        scene_renderer.hpp scene_renderer.cpp
        shader_suite.hpp shader_suite.cpp
        render_context.hpp render_context.cpp
        camera.hpp camera.cpp
        material.hpp
        abstract_renderable.hpp
        render_components.hpp render_components.cpp
        mesh_util.hpp mesh_util.cpp
        material_util.hpp material_util.cpp
        material_manager.hpp material_manager.cpp
        renderer.hpp renderer.cpp
        flat_renderer.hpp flat_renderer.cpp
        renderer_enums.hpp
        animation_system.hpp animation_system.cpp
        render_graph.cpp render_graph.hpp
        ground.hpp ground.cpp
        post/hdr.hpp post/hdr.cpp
        post/fxaa.hpp post/fxaa.cpp
        post/smaa.hpp post/smaa.cpp
        post/temporal.hpp post/temporal.cpp
        post/aa.hpp post/aa.cpp
        post/ssao.hpp post/ssao.cpp
        post/ffx-cacao/src/ffx_cacao.cpp post/ffx-cacao/src/ffx_cacao_impl.cpp
        post/ffx-cacao/inc/ffx_cacao.h post/ffx-cacao/inc/ffx_cacao_impl.h
        post/spd.hpp post/spd.cpp
        post/ssr.hpp post/ssr.cpp
        utils/image_utils.hpp utils/image_utils.cpp
        lights/lights.cpp lights/lights.hpp
        lights/clusterer.cpp lights/clusterer.hpp
        lights/volumetric_fog.cpp lights/volumetric_fog.hpp lights/volumetric_fog_region.hpp
        lights/light_info.hpp
        lights/deferred_lights.hpp lights/deferred_lights.cpp
        lights/volumetric_diffuse.hpp lights/volumetric_diffuse.cpp
        lights/decal_volume.hpp lights/decal_volume.cpp
        formats/scene_formats.hpp formats/scene_formats.cpp
        formats/gltf.hpp formats/gltf.cpp
        scene_loader.cpp scene_loader.hpp
        ocean.hpp ocean.cpp
        fft/fft.cpp fft/fft.hpp
        sprite.cpp sprite.hpp
        common_renderer_data.cpp common_renderer_data.hpp
        font.cpp font.hpp
        threaded_scene.cpp threaded_scene.hpp)
target_include_directories(granite-renderer
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/fft
        ${CMAKE_CURRENT_SOURCE_DIR}/formats
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/post/ffx-cacao/inc)
target_link_libraries(granite-renderer PUBLIC
        granite-ecs
        granite-vulkan
        granite-math
        granite-event
        granite-input
        granite-application-events
        granite-application-global
        granite-threading
        PRIVATE granite-mikktspace meshoptimizer granite-rapidjson granite-stb granite-fsr2)

if (NOT ANDROID)
    set(GRANITE_FSR2_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/fsr2/src/ffx-fsr2-api/shaders)
    target_compile_definitions(granite-renderer PRIVATE GRANITE_FSR2_SHADER_DIR=\"${GRANITE_FSR2_SHADER_DIR}\")
endif()
