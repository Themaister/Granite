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

uvy0 = 256.0 / 512.0
uvy2 = (256.0 + 32.0) / 512.0

uvx1 = (128.0 + 64.0) / 512.0
uvx2 = (128.0 + 64.0 + 32.0) / 512.0

mesh = meshes.get_mesh('clover2')
mesh.material = 'DEFAULT'
mesh.add_quad(
        Vertex((0.0, 0.175, 0.5), (uvx1, uvy2)),
        Vertex((0.5, 0.175, 0.5), (uvx2, uvy2)),
        Vertex((0.0, 0.175, 0.0), (uvx1, uvy0)),
        Vertex((0.5, 0.175, 0.0), (uvx2, uvy0)))

images.register_texture('pine', '../textures/Pine_BaseColor.ktx', CHUNKY_WRAP)

node = Node()
node.mesh = 'clover2'

gltf = build_gltf([node], meshes, materials, images)
print(json.dumps(gltf, indent = 4))

