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

w = 1.0
l = 1.0
h = 0.5

x = w / 2.0
z = l / 2.0

uvy0 = (256.0 + 96.0) / 512.0
uvy = (256.0 + 96.0 + 32.0) / 512.0
uvx = 64.0 / 512.0

mesh = meshes.get_mesh('berry')
mesh.material = 'DEFAULT'
mesh.add_quad(
        Vertex((x, 0.0, l),   (0.0,  uvy)),
        Vertex((x, 0.0, 0.0), (uvx, uvy)),
        Vertex((x, h,   l),   (0.0,  uvy0)),
        Vertex((x, h,   0.0), (uvx, uvy0)))
mesh.add_quad(
        Vertex((0.0, 0.0, z), (0.0, uvy)),
        Vertex((w,   0.0, z), (uvx, uvy)),
        Vertex((0.0, h,   z), (0.0, uvy0)),
        Vertex((w,   h,   z), (uvx, uvy0)))

images.register_texture('pine', '../textures/Pine_BaseColor.ktx', CHUNKY_WRAP)

node = Node()
node.mesh = 'berry'

gltf = build_gltf([node], meshes, materials, images)
print(json.dumps(gltf, indent = 4))

