add_granite_library(renderer
        render_queue.hpp render_queue.cpp
        mesh.hpp mesh.cpp
        scene.hpp scene.cpp
        shader_suite.hpp shader_suite.cpp
        render_context.hpp render_context.cpp
        camera.hpp camera.cpp
        material.hpp
        abstract_renderable.hpp
        render_components.hpp
        mesh_util.hpp mesh_util.cpp
        material_util.hpp material_util.cpp
        renderer.hpp renderer.cpp renderer_enums.hpp
        material_manager.hpp material_manager.cpp
        animation_system.hpp animation_system.cpp
        render_graph.cpp render_graph.hpp
        ground.hpp ground.cpp
        post/hdr.hpp post/hdr.cpp
        utils/image_utils.hpp utils/image_utils.cpp
        lights/lights.cpp lights/lights.hpp
        lights/clusterer.cpp lights/clusterer.hpp
        scene_loader.cpp scene_loader.hpp)
target_link_libraries(renderer vulkan-backend math importers application-events gli)
target_include_directories(renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(renderer PRIVATE RAPIDJSON_HAS_STDSTRING=1)
target_include_directories(renderer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/rapidjson/include)
