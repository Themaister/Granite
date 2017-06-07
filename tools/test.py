#!/usr/bin/env python3

from gltf_tools.MeshBuilder import *
import json
import sys
import struct

class JSONDumper(json.JSONEncoder):
    def default(self, obj):
        if isinstance(obj, Obj):
            return { 'a' : obj.a, 'b' : obj.b }
        return json.JSONEncoder.default(self, obj)

#print(json.dumps({ 'foo' : [1, 2, 3.5, 4], 'bar' : obj }, cls = JSONDumper))

res = b''
res += struct.pack('f', 1.0)
res += struct.pack('f', 2.0)
print(res)

with open('test.bin', 'wb') as f:
    f.write(res)

a = [];
a.append((1.0, 2.0, 3.0, 0.0, 0.25))
a.append((4.0, 5.0, 6.0, 0.0, 0.25))

if (1.0, 2.0, 3.0, 0.0, 0.25) in a:
    print(':D')
else:
    print(':o')

builder = MeshBuilder()
builder.add_triangle(
        Vertex((0.0, 0.0, 0.0), (0.0, 0.0)),
        Vertex((1.0, 0.0, 0.0), (0.0, 0.1)),
        Vertex((0.0, 1.0, 0.0), (-0.1, 0.0)))

builder.build_normals()
print(builder.normals)
print(builder.tangents)

