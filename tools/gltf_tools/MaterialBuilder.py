from gltf_tools.gltf_object import GLTFObject

class MaterialBuilder(GLTFObject):
    def __init__(self):
        self.name = None
        self.base_color = None
        self.normal = None
        self.metallic_roughness = None
        self.base_color_factor = (1.0, 1.0, 1.0, 1.0)
        self.metallic_factor = 1.0
        self.roughness_factor = 1.0
        self.alpha_mode = 'OPAQUE'
        self.double_sided = False
        self.texture_resolver = None
    
    def to_json_object(self):
        value = {}
        if self.name:
            value['name'] = self.name
        pbr = { 'baseColorFactor' : [x for x in self.base_color_factor],
                'roughnessFactor' : self.roughness_factor,
                'metallicFactor' : self.metallic_factor,
                'alphaMode' : self.alpha_mode,
                'doubleSided' : self.double_sided }

        if self.base_color:
            pbr['baseColor'] = { 'index' : self.texture_resolver.get_index(self.base_color) }
        if self.normal:
            value['normalTexture'] = { 'index' : self.texture_resolver.get_index(self.normal) }

        value['pbrMetallicRoughness'] = pbr

        return value

