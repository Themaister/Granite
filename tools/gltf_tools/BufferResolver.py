import struct
import base64

GL_BYTE = 0x1400
GL_UNSIGNED_BYTE = 0x1401
GL_SHORT = 0x1402
GL_UNSIGNED_SHORT = 0x1403
GL_INT = 0x1404
GL_UNSIGNED_INT = 0x1405
GL_FLOAT = 0x1406

def maximum(vertices):
    res = [0.0] * len(vertices[0])
    for c in range(len(res)):
        res[c] = max([x[c] for x in vertices])
    return res

def minimum(vertices):
    res = [0.0] * len(vertices[0])
    for c in range(len(res)):
        res[c] = min([x[c] for x in vertices])
    return res

class BufferResolver():
    def __init__(self):
        self.buffers = []
        self.views = []
        self.accessors = []

    def build_float_buffer(self, vertices):
        size = len(vertices) * len(vertices[0]) * 4
        packed_data = b''
        for v in vertices:
            for c in v:
                packed_data += struct.pack('f', c)
        uri = 'data:application/octet-stream;base64,' + base64.b64encode(packed_data).decode('utf-8')

        buffer_index = len(self.buffers)
        self.buffers.append({ 'byteLength' : size, 'uri' : uri })
        self.views.append({ 'buffer' : buffer_index, 'byteLength' : size, 'byteOffset' : 0 })

        vector_types = ['SCALAR', 'VEC2', 'VEC3', 'VEC4']

        self.accessors.append({
            'bufferView' : buffer_index,
            'byteOffset' : 0,
            'componentType' : GL_FLOAT,
            'type' : vector_types[len(vertices[0]) - 1],
            'count' : len(vertices),
            'max' : maximum(vertices),
            'min' : minimum(vertices)
            })

        return buffer_index

    def build_index_buffer(self, indices):
        min_index = min(indices)
        max_index = max(indices)

        full_range = max_index >= 0x10000
        size = len(indices) * (4 if full_range else 2)

        packed_data = b''

        if full_range:
            for i in indices:
                packed_data += struct.pack('u', i)
        else:
            for i in indices:
                packed_data += struct.pack('h', i)

        uri = 'data:application/octet-stream;base64,' + base64.b64encode(packed_data).decode('utf-8')

        buffer_index = len(self.buffers)
        self.buffers.append({ 'byteLength' : size, 'uri' : uri })
        self.views.append({ 'buffer' : buffer_index, 'byteLength' : size, 'byteOffset' : 0 })

        self.accessors.append({
            'bufferView' : buffer_index,
            'byteOffset' : 0,
            'componentType' : GL_UNSIGNED_INT if full_range else GL_UNSIGNED_SHORT,
            'type' : 'SCALAR',
            'count' : len(indices),
            'max' : [max_index],
            'min' : [min_index]
            })

        return buffer_index
