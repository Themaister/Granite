#!/usr/bin/env python3

from gltf_tools.MeshBuilder import *
from gltf_tools.MaterialBuilder import *
from gltf_tools.ImageResolver import *
from gltf_tools.GLTFBuilder import *
from gltf_tools.Node import *
import json
import sys
import struct

images = ImageResolver()
meshes = MeshResolver()
materials = MaterialResolver()

material = materials.get_material('DEFAULT')
material.base_color_factor = (1.0, 0.0, 0.0, 1.0)
material.double_sided = True
material.base_color = 'maister'

mesh = meshes.get_mesh('DEFAULT')
mesh.material = 'DEFAULT'
mesh.add_quad(
        Vertex((0.0, 0.0, 0.0), (0.0, 0.0)),
        Vertex((1.0, 0.0, 0.0), (1.0, 0.0)),
        Vertex((0.0, 1.0, 0.0), (0.0, 1.0)),
        Vertex((1.0, 1.0, 0.0), (1.0, 1.0)))

node1 = Node()
node1.mesh = 'DEFAULT'
node2 = Node()
node2.mesh = 'DEFAULT'
node2.translation = (0.0, 4.0, 0.0)
node1.children.append(node2)

images.register_texture('maister', '../textures/maister.png', TRILINEAR_WRAP)

gltf = build_gltf([node1], meshes, materials, images)
print(json.dumps(gltf, indent = 4))

