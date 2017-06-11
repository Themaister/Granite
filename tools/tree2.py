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
material.base_color = 'pine'
material.alpha_mode = 'MASK'

w = 2.0
l = 2.0
h = 3.5

x = w / 2.0
z = l / 2.0

uv = 224.0 / 512.0

mesh = meshes.get_mesh('pine')
mesh.material = 'DEFAULT'
mesh.add_quad(
        Vertex((x, 0.0, l),   (0.5,  uv)),
        Vertex((x, 0.0, 0.0), (0.75, uv)),
        Vertex((x, h,   l),   (0.5,  0.0)),
        Vertex((x, h,   0.0), (0.75, 0.0)))
mesh.add_quad(
        Vertex((0.0, 0.0, z), (0.75, uv)),
        Vertex((w,   0.0, z), (1.0, uv)),
        Vertex((0.0, h,   z), (0.75, 0.0)),
        Vertex((w,   h,   z), (1.0, 0.0)))

images.register_texture('pine', '../textures/Pine_BaseColor.ktx', CHUNKY_WRAP)

node = Node()
node.mesh = 'pine'

gltf = build_gltf([node], meshes, materials, images)
print(json.dumps(gltf, indent = 4))

