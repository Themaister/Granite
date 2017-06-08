#!/usr/bin/env python3

from gltf_tools.MeshBuilder import *
from gltf_tools.MaterialBuilder import *
from gltf_tools.ImageResolver import *
from gltf_tools.BufferResolver import *
import json
import sys
import struct

class JSONDumper(json.JSONEncoder):
    def default(self, obj):
        if isinstance(obj, GLTFObject):
            return obj.to_json_object()
        else:
            return json.JSONEncoder.default(obj)

buffers = BufferResolver()
buffers.build_index_buffer([0, 1, 2])
buffers.build_float_buffer([(1.0, 2.0), (4.0, -1.0)])
print(buffers.buffers)
print(buffers.accessors)
print(buffers.views)

images = ImageResolver()
images.register_texture('FOO1', 'path1.ktx', BILINEAR_WRAP)
images.register_texture('FOO2', 'path2.ktx', TRILINEAR_WRAP)
gltf = {}
gltf['images'] = images.to_images()
gltf['samplers'] = images.to_samplers()
gltf['textures'] = images.to_textures()
print(json.dumps(gltf, cls = JSONDumper))

material = MaterialBuilder()
print(json.dumps(material, cls = JSONDumper))

res = b''
res += struct.pack('f', 1.0)
res += struct.pack('f', 2.0)
print(res)

with open('test.bin', 'wb') as f:
    f.write(res)

builder = MeshBuilder()
builder.add_triangle(
        Vertex((0.0, 0.0, 0.0), (0.0, 0.0)),
        Vertex((1.0, 0.0, 0.0), (0.0, 0.1)),
        Vertex((0.0, 1.0, 0.0), (-0.1, 0.0)))

builder.build_normals()
print(builder.normals)
print(builder.tangents)

