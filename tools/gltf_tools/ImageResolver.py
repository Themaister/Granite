NEAREST_WRAP = 0
BILINEAR_WRAP = 1
TRILINEAR_WRAP = 2
NEAREST_CLAMP = 3
BILINEAR_CLAMP = 4
TRILINEAR_CLAMP = 5

GL_REPEAT = 0x2901
GL_CLAMP_TO_EDGE = 0x812F
GL_NEAREST = 0x2600
GL_LINEAR = 0x2601
GL_NEAREST_MIPMAP_NEAREST = 0x2700
GL_LINEAR_MIPMAP_NEAREST = 0x2701
GL_NEAREST_MIPMAP_LINEAR = 0x2702
GL_LINEAR_MIPMAP_LINEAR = 0x2703

def to_image_json(path):
    return { 'uri' : path }

def to_sampler_json(sampler):
    if sampler == NEAREST_WRAP:
        return { 'magFilter' : GL_NEAREST, 'minFilter' : GL_NEAREST_MIPMAP_NEAREST, 'wrapS' : GL_REPEAT, 'wrapT' : GL_REPEAT }
    elif sampler == BILINEAR_WRAP:
        return { 'magFilter' : GL_LINEAR, 'minFilter' : GL_LINEAR_MIPMAP_NEAREST, 'wrapS' : GL_REPEAT, 'wrapT' : GL_REPEAT }
    elif sampler == TRILINEAR_WRAP:
        return { 'magFilter' : GL_LINEAR, 'minFilter' : GL_LINEAR_MIPMAP_LINEAR, 'wrapS' : GL_REPEAT, 'wrapT' : GL_REPEAT }
    elif sampler == NEAREST_CLAMP:
        return { 'magFilter' : GL_NEAREST, 'minFilter' : GL_NEAREST_MIPMAP_NEAREST, 'wrapS' : GL_CLAMP_TO_EDGE, 'wrapT' : GL_CLAMP_TO_EDGE }
    elif sampler == BILINEAR_CLAMP:
        return { 'magFilter' : GL_LINEAR, 'minFilter' : GL_LINEAR_MIPMAP_NEAREST, 'wrapS' : GL_CLAMP_TO_EDGE, 'wrapT' : GL_CLAMP_TO_EDGE }
    elif sampler == TRILINEAR_CLAMP:
        return { 'magFilter' : GL_LINEAR, 'minFilter' : GL_LINEAR_MIPMAP_LINEAR, 'wrapS' : GL_CLAMP_TO_EDGE, 'wrapT' : GL_CLAMP_TO_EDGE }
    else:
        raise TypeError('Sampler is out of range')


class ImageResolver():
    def __init__(self):
        self.names = []
        self.paths = []
        self.samplers = []

    def get_index(self, name):
        return self.images.index(name)

    def register_texture(self, name, path, sampler):
        if name in self.names:
            index = self.get_index(name)
            self.paths[index] = path
            self.samplers[index] = sampler
            return index
        else:
            self.names.append(name)
            self.paths.append(path)
            self.samplers.append(sampler)
            return len(self.names) - 1

    def to_images(self):
        images = {}
        return [to_image_json(x) for x in self.paths]

    def to_samplers(self):
        return [to_sampler_json(x) for x in range(6)]

    def to_textures(self):
        tex = []
        for i in range(len(self.names)):
            tex.append({ 'sampler' : self.samplers[i], 'source' : i })
        return tex


