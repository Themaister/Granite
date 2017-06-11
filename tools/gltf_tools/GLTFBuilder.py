from gltf_tools.BufferResolver import *

class SceneBuilder():
    def __init__(self):
        self.node_index = 0
        self.nodes = []
        self.top_level_nodes = []

    def add_node(self, node, meshes):
        children = [self.add_node(x, meshes) for x in node.children]

        json_node = {
                'children' : children,
                'translation' : [x for x in node.translation],
                'scale' : [x for x in node.scale],
                'rotation' : [x for x in node.rotation]
                }

        if node.mesh:
            json_node['mesh'] = meshes.get_mesh_index(node.mesh)

        index = self.node_index
        self.node_index += 1
        self.nodes.append(json_node)
        return index


def build_gltf(nodes, meshes, materials, textures):
    buffers = BufferResolver()

    mesh_objects = [x.to_json_object(buffers, materials) for x in meshes.meshes]
    material_objects = [x.to_json_object(textures) for x in materials.materials]

    scene = SceneBuilder()

    for node in nodes:
        scene.top_level_nodes.append(scene.add_node(node, meshes))

    scenes = [{
        'nodes' : [x for x in scene.top_level_nodes]
        }]

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
            'nodes' : scene.nodes,
            'scene' : 0,
            'scenes' : scenes,
            'buffers' : buffers.buffers,
            'bufferViews' : buffers.views,
            'accessors' : buffers.accessors
            }

    return gltf
