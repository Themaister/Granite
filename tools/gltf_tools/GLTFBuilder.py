from gltf_tools.BufferResolver import *

def build_gltf(meshes, materials, textures):
    buffers = BufferResolver()

    mesh_objects = [x.to_json_object(buffers, materials) for x in meshes]
    material_objects = [x.to_json_object(textures) for x in materials.materials]

    nodes = []
    scenes = []

    for i, mesh in enumerate(meshes):
        nodes.append({
            'children' : [],
            'mesh' : i,
            'name' : mesh.name if mesh.name else ''
            })

    scenes.append({
        'nodes' : [x for x in range(len(nodes))]
        })

    gltf = {
            'asset' : {
                'generator' : 'Granite glTF 2.0 exporter',
                'version' : '2.0'
                },
            'meshes' : mesh_objects,
            'materials' : material_objects,
            'images' : textures.to_images(),
            'samplers' : textures.to_samplers(),
            'textures' : textures.to_textures(),
            'nodes' : nodes,
            'scene' : 0,
            'scenes' : scenes,
            'buffers' : buffers.buffers,
            'bufferViews' : buffers.views,
            'accessors' : buffers.accessors
            }

    return gltf
