#!/usr/bin/env python3

from gltf_tools.MeshBuilder import *
from gltf_tools.MaterialBuilder import *
from gltf_tools.ImageResolver import *
from gltf_tools.GLTFBuilder import *
import json
import sys
import struct

images = ImageResolver()
mesh = MeshBuilder()
materials = MaterialResolver()

material = materials.get_material('DEFAULT')
material.base_color_factor = (1.0, 0.0, 0.0, 1.0)
material.double_sided = True
material.base_color = 'pine'
material.alpha_mode = 'MASK'

mesh.material = 'DEFAULT'
mesh.add_quad(
        Vertex((0.5, 0.0, 1.0), (0.0,  1.0)),
        Vertex((0.5, 0.0, 0.0), (0.25, 1.0)),
        Vertex((0.5, 1.0, 1.0), (0.0,  0.0)),
        Vertex((0.5, 1.0, 0.0), (0.25, 0.0)))
mesh.add_quad(
        Vertex((0.0, 0.0, 0.5), (0.25, 1.0)),
        Vertex((1.0, 0.0, 0.5), (0.5, 1.0)),
        Vertex((0.0, 1.0, 0.5), (0.25, 0.0)),
        Vertex((1.0, 1.0, 0.5), (0.5, 0.0)))

images.register_texture('pine', '../textures/Pine_BaseColor.png', NEAREST_WRAP)

gltf = build_gltf([mesh], materials, images)
print(json.dumps(gltf, indent = 4))

