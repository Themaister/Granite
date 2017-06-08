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

mesh.material = 'DEFAULT'
mesh.add_triangle(
        Vertex((0.0, 0.0, 0.0), (0.0, 0.0)),
        Vertex((1.0, 0.0, 0.0), (1.0, 0.0)),
        Vertex((0.0, 1.0, 0.0), (0.0, 1.0)))

images.register_texture('FOO1', 'path1.ktx', BILINEAR_WRAP)
images.register_texture('FOO2', 'path2.ktx', TRILINEAR_WRAP)

gltf = build_gltf([mesh], materials, images)
print(json.dumps(gltf, indent = 4))

