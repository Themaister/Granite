/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "gltf_export.hpp"
#include "texture_compression.hpp"
#include "texture_files.hpp"

#include "rapidjson_wrapper.hpp"
#include "hashmap.hpp"
#include "thread_group.hpp"
#include <unordered_set>
#include "texture_utils.hpp"
#include "texture_format.hpp"
#include "stb_image_write.h"

using namespace std;
using namespace rapidjson;
using namespace Util;

namespace Granite
{
namespace SceneFormats
{
template<typename T>
struct Remap
{
	vector<unsigned> to_index;
	HashMap<unsigned> hashmap;
	vector<const T *> info;
};

struct BufferView
{
	size_t offset;
	size_t length;
};

struct EmittedMesh
{
	int index_accessor = -1;
	int material = -1;
	uint32_t attribute_mask = 0;
	int attribute_accessor[ecast(MeshAttribute::Count)] = {};
	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
	bool primitive_restart = false;
};

struct EmittedEnvironment
{
	int cube = -1;
	int reflection = -1;
	int irradiance = -1;
	float intensity = 1.0f;

	vec3 fog_color;
	float fog_falloff;
};

struct EmittedAccessor
{
	unsigned view = 0;
	unsigned count = 0;
	const char *type = nullptr;
	unsigned component = 0;
	unsigned offset = 0;

	AABB aabb;
	uint32_t uint_min = 0;
	uint32_t uint_max = 0;
	bool normalized = false;
	bool use_aabb = false;
	bool use_uint_min_max = false;
};

struct EmittedMaterial
{
	int base_color = -1;
	int normal = -1;
	int metallic_roughness = -1;
	int occlusion = -1;
	int emissive = -1;

	vec4 uniform_base_color = vec4(1.0f);
	vec3 uniform_emissive_color = vec3(0.0f);
	float uniform_metallic = 1.0f;
	float uniform_roughness = 1.0f;
	float normal_scale = 1.0f;
	DrawPipeline pipeline = DrawPipeline::Opaque;
	bool two_sided = false;
	bool bandlimited_pixel = false;
};

struct EmittedTexture
{
	unsigned image;
	unsigned sampler;
};

struct AnalysisResult
{
	std::string src_path;
	shared_ptr<MemoryMappedTexture> image;
	TextureCompression compression;
	TextureMode mode;
	Material::Textures type;
	VkComponentMapping swizzle;

	bool load_image(const string &src);
	void deduce_compression(TextureCompressionFamily family);

	enum class MetallicRoughnessMode
	{
		RoughnessMetal,
		RoughnessDielectric,
		MetallicSmooth,
		MetallicRough,
		Default
	};
	MetallicRoughnessMode deduce_metallic_roughness_mode();
};

struct EmittedImage
{
	string source_path;
	string target_relpath;
	string target_mime;

	TextureCompressionFamily compression;
	unsigned compression_quality;
	TextureMode mode;
	Material::Textures type;

	shared_ptr<AnalysisResult> loaded_image;
};

struct EmittedSampler
{
	unsigned mag_filter;
	unsigned min_filter;
	unsigned wrap_s;
	unsigned wrap_t;
};

struct EmittedAnimation
{
	struct Sampler
	{
		unsigned timestamp_accessor;
		unsigned data_accessor;
		std::string interpolation;
	};

	struct Channel
	{
		unsigned sampler;
		unsigned target_node;
		std::string path;
	};

	std::string name;
	std::vector<Sampler> samplers;
	std::vector<Channel> channels;
};

struct RemapState
{
	const ExportOptions *options = nullptr;
	Hash hash(const Mesh &m);
	Hash hash(const MaterialInfo &mesh);

	template<typename StateType, typename SceneType>
	void filter_input(StateType &output, const SceneType &input);

	unsigned emit_buffer(ArrayView<const uint8_t> view);

	unsigned emit_accessor(unsigned view_index, VkFormat format, unsigned offset, unsigned count);

	unsigned emit_texture(const MaterialInfo::Texture &texture,
	                      Vulkan::StockSampler sampler, Material::Textures type,
	                      TextureCompressionFamily compression, unsigned quality, TextureMode mode);

	unsigned emit_sampler(Vulkan::StockSampler sampler);
	unsigned emit_image(const MaterialInfo::Texture &texture, Material::Textures type,
	                    TextureCompressionFamily compression, unsigned quality, TextureMode mode);

	void emit_material(unsigned remapped_material);
	void emit_mesh(unsigned remapped_index);
	void emit_environment(const string &cube, const string &reflection, const string &irradiance, float intensity,
	                      vec3 fog_color, float fog_falloff,
	                      TextureCompressionFamily compression, unsigned quality);
	unsigned emit_meshes(ArrayView<const unsigned> meshes);
	void emit_animations(ArrayView<const Animation> animations);

	Remap<Mesh> mesh;
	Remap<MaterialInfo> material;

	vector<uint8_t> glb_buffer_data;
	HashMap<unsigned> buffer_hash;
	vector<BufferView> buffer_views;

	HashMap<unsigned> accessor_hash;
	vector<EmittedAccessor> accessor_cache;

	unordered_set<unsigned> mesh_hash;
	vector<EmittedMesh> mesh_cache;

	vector<EmittedEnvironment> environment_cache;

	unordered_set<unsigned> material_hash;
	vector<EmittedMaterial> material_cache;

	HashMap<unsigned> texture_hash;
	vector<EmittedTexture> texture_cache;

	HashMap<unsigned> image_hash;
	vector<EmittedImage> image_cache;

	HashMap<unsigned> sampler_hash;
	vector<EmittedSampler> sampler_cache;

	vector<EmittedAnimation> animations;

	HashMap<unsigned> mesh_group_hash;
	vector<vector<unsigned>> mesh_group_cache;
};

Hash RemapState::hash(const Mesh &m)
{
	Hasher h;

	h.u32(m.topology);
	h.u32(m.index_type);
	h.u32(m.attribute_stride);
	h.u32(m.position_stride);
	h.u32(m.has_material);
	h.u32(m.primitive_restart);
	if (m.has_material)
		h.u32(material.to_index[m.material_index]);
	h.data(reinterpret_cast<const uint8_t *>(m.attribute_layout), sizeof(m.attribute_layout));

	auto lo = m.static_aabb.get_minimum();
	auto hi = m.static_aabb.get_maximum();
	h.f32(lo.x);
	h.f32(lo.y);
	h.f32(lo.z);
	h.f32(hi.x);
	h.f32(hi.y);
	h.f32(hi.z);

	h.u32(0xff);
	if (!m.positions.empty())
		h.data(m.positions.data(), m.positions.size() * sizeof(m.positions[0]));
	h.u32(0xff);
	if (!m.indices.empty())
		h.data(m.indices.data(), m.indices.size() * sizeof(m.indices[0]));
	h.u32(0xff);
	if (!m.attributes.empty())
		h.data(m.attributes.data(), m.attributes.size() * sizeof(m.attributes[0]));

	h.u32(m.count);
	return h.get();
}

Hash RemapState::hash(const MaterialInfo &mat)
{
	Hasher h;
	h.string(mat.base_color.path);
	h.string(mat.normal.path);
	h.string(mat.occlusion.path);
	h.string(mat.metallic_roughness.path);
	h.string(mat.emissive.path);

	h.f32(mat.normal_scale);
	h.f32(mat.uniform_metallic);
	h.f32(mat.uniform_roughness);
	for (unsigned i = 0; i < 4; i++)
		h.f32(mat.uniform_base_color[i]);
	for (unsigned i = 0; i < 3; i++)
		h.f32(mat.uniform_emissive_color[i]);
	h.s32(mat.two_sided);
	h.s32(mat.bandlimited_pixel);
	h.u32(ecast(mat.pipeline));

	return h.get();
}

template<typename StateType, typename SceneType>
void RemapState::filter_input(StateType &output, const SceneType &input)
{
	for (auto &i : input)
	{
		auto h = hash(i);
		auto itr = output.hashmap.find(h);
		if (itr != end(output.hashmap))
			output.to_index.push_back(itr->second);
		else
		{
			unsigned index = output.info.size();
			output.to_index.push_back(output.info.size());
			output.info.push_back(&i);
			output.hashmap[h] = index;
		}
	}
}

unsigned RemapState::emit_buffer(ArrayView<const uint8_t> view)
{
	Hasher h;
	h.data(view.data(), view.size());
	auto itr = buffer_hash.find(h.get());

	if (itr == end(buffer_hash))
	{
		unsigned index = buffer_views.size();
		size_t offset = glb_buffer_data.size();
		offset = (offset + 15) & ~15;
		glb_buffer_data.resize(offset + view.size());
		memcpy(glb_buffer_data.data() + offset, view.data(), view.size());
		buffer_views.push_back({offset, view.size()});
		buffer_hash[h.get()] = index;
		return index;
	}
	else
		return itr->second;
}

#define GL_BYTE                           0x1400
#define GL_UNSIGNED_BYTE                  0x1401
#define GL_SHORT                          0x1402
#define GL_UNSIGNED_SHORT                 0x1403
#define GL_INT                            0x1404
#define GL_UNSIGNED_INT                   0x1405
#define GL_FLOAT                          0x1406
#define GL_HALF_FLOAT                     0x140B
#define GL_INT_2_10_10_10_REV             0x8D9F
#define GL_UNSIGNED_INT_2_10_10_10_REV    0x8368

#define GL_REPEAT                         0x2901
#define GL_CLAMP_TO_EDGE                  0x812F
#define GL_NEAREST                        0x2600
#define GL_LINEAR                         0x2601
#define GL_NEAREST_MIPMAP_NEAREST         0x2700
#define GL_LINEAR_MIPMAP_NEAREST          0x2701
#define GL_NEAREST_MIPMAP_LINEAR          0x2702
#define GL_LINEAR_MIPMAP_LINEAR           0x2703

static const char *get_accessor_type(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_R32_SFLOAT:
	case VK_FORMAT_R16_SFLOAT:
	case VK_FORMAT_R8_UNORM:
	case VK_FORMAT_R8_UINT:
	case VK_FORMAT_R8_SNORM:
	case VK_FORMAT_R8_SINT:
	case VK_FORMAT_R16_UNORM:
	case VK_FORMAT_R16_UINT:
	case VK_FORMAT_R16_SNORM:
	case VK_FORMAT_R16_SINT:
	case VK_FORMAT_R32_UINT:
	case VK_FORMAT_R32_SINT:
		return "SCALAR";

	case VK_FORMAT_R32G32_SFLOAT:
	case VK_FORMAT_R16G16_SFLOAT:
	case VK_FORMAT_R8G8_UNORM:
	case VK_FORMAT_R8G8_UINT:
	case VK_FORMAT_R8G8_SNORM:
	case VK_FORMAT_R8G8_SINT:
	case VK_FORMAT_R16G16_UINT:
	case VK_FORMAT_R16G16_SINT:
	case VK_FORMAT_R16G16_SNORM:
	case VK_FORMAT_R16G16_UNORM:
	case VK_FORMAT_R32G32_UINT:
	case VK_FORMAT_R32G32_SINT:
		return "VEC2";

	case VK_FORMAT_R32G32B32_SFLOAT:
	case VK_FORMAT_R16G16B16_SFLOAT:
	case VK_FORMAT_R8G8B8_UNORM:
	case VK_FORMAT_R8G8B8_UINT:
	case VK_FORMAT_R8G8B8_SNORM:
	case VK_FORMAT_R8G8B8_SINT:
	case VK_FORMAT_R16G16B16_UNORM:
	case VK_FORMAT_R16G16B16_UINT:
	case VK_FORMAT_R16G16B16_SNORM:
	case VK_FORMAT_R16G16B16_SINT:
		return "VEC3";

	case VK_FORMAT_R32G32B32A32_SFLOAT:
	case VK_FORMAT_R16G16B16A16_SFLOAT:
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_UINT:
	case VK_FORMAT_R8G8B8A8_SNORM:
	case VK_FORMAT_R8G8B8A8_SINT:
	case VK_FORMAT_R16G16B16A16_UNORM:
	case VK_FORMAT_R16G16B16A16_UINT:
	case VK_FORMAT_R16G16B16A16_SNORM:
	case VK_FORMAT_R16G16B16A16_SINT:
	case VK_FORMAT_R32G32B32_UINT:
	case VK_FORMAT_R32G32B32A32_UINT:
	case VK_FORMAT_R32G32B32_SINT:
	case VK_FORMAT_R32G32B32A32_SINT:
	case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
	case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
		return "VEC4";

	default:
		throw invalid_argument("Unsupported format.");
	}
}

static bool get_accessor_normalized(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_R8_UNORM:
	case VK_FORMAT_R8G8_UNORM:
	case VK_FORMAT_R8G8B8_UNORM:
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8_SNORM:
	case VK_FORMAT_R8G8_SNORM:
	case VK_FORMAT_R8G8B8_SNORM:
	case VK_FORMAT_R8G8B8A8_SNORM:
	case VK_FORMAT_R16_UNORM:
	case VK_FORMAT_R16G16_UNORM:
	case VK_FORMAT_R16G16B16_UNORM:
	case VK_FORMAT_R16G16B16A16_UNORM:
	case VK_FORMAT_R16_SNORM:
	case VK_FORMAT_R16G16_SNORM:
	case VK_FORMAT_R16G16B16_SNORM:
	case VK_FORMAT_R16G16B16A16_SNORM:
	case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
	case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
		return true;

	default:
		return false;
	}
}

static unsigned get_accessor_component(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_R32_SFLOAT:
	case VK_FORMAT_R32G32_SFLOAT:
	case VK_FORMAT_R32G32B32_SFLOAT:
	case VK_FORMAT_R32G32B32A32_SFLOAT:
		return GL_FLOAT;

	case VK_FORMAT_R16_SFLOAT:
	case VK_FORMAT_R16G16_SFLOAT:
	case VK_FORMAT_R16G16B16_SFLOAT:
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		return GL_HALF_FLOAT;

	case VK_FORMAT_R8_UNORM:
	case VK_FORMAT_R8G8_UNORM:
	case VK_FORMAT_R8G8B8_UNORM:
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8_UINT:
	case VK_FORMAT_R8G8_UINT:
	case VK_FORMAT_R8G8B8_UINT:
	case VK_FORMAT_R8G8B8A8_UINT:
		return GL_UNSIGNED_BYTE;

	case VK_FORMAT_R8_SNORM:
	case VK_FORMAT_R8G8_SNORM:
	case VK_FORMAT_R8G8B8_SNORM:
	case VK_FORMAT_R8G8B8A8_SNORM:
	case VK_FORMAT_R8_SINT:
	case VK_FORMAT_R8G8_SINT:
	case VK_FORMAT_R8G8B8_SINT:
	case VK_FORMAT_R8G8B8A8_SINT:
		return GL_BYTE;

	case VK_FORMAT_R16_UNORM:
	case VK_FORMAT_R16G16_UNORM:
	case VK_FORMAT_R16G16B16_UNORM:
	case VK_FORMAT_R16G16B16A16_UNORM:
	case VK_FORMAT_R16_UINT:
	case VK_FORMAT_R16G16_UINT:
	case VK_FORMAT_R16G16B16_UINT:
	case VK_FORMAT_R16G16B16A16_UINT:
		return GL_UNSIGNED_SHORT;

	case VK_FORMAT_R16_SNORM:
	case VK_FORMAT_R16G16_SNORM:
	case VK_FORMAT_R16G16B16_SNORM:
	case VK_FORMAT_R16G16B16A16_SNORM:
	case VK_FORMAT_R16_SINT:
	case VK_FORMAT_R16G16_SINT:
	case VK_FORMAT_R16G16B16_SINT:
	case VK_FORMAT_R16G16B16A16_SINT:
		return GL_SHORT;

	case VK_FORMAT_R32_UINT:
	case VK_FORMAT_R32G32_UINT:
	case VK_FORMAT_R32G32B32_UINT:
	case VK_FORMAT_R32G32B32A32_UINT:
		return GL_UNSIGNED_INT;

	case VK_FORMAT_R32_SINT:
	case VK_FORMAT_R32G32_SINT:
	case VK_FORMAT_R32G32B32_SINT:
	case VK_FORMAT_R32G32B32A32_SINT:
		return GL_INT;

	case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
		return GL_INT_2_10_10_10_REV;
	case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
		return GL_UNSIGNED_INT_2_10_10_10_REV;

	default:
		throw invalid_argument("Unsupported format.");
	}
}

static void set_accessor_type(EmittedAccessor &accessor, VkFormat format)
{
	accessor.component = get_accessor_component(format);
	accessor.type = get_accessor_type(format);
	accessor.normalized = get_accessor_normalized(format);
}

unsigned RemapState::emit_accessor(unsigned view_index, VkFormat format, unsigned offset, unsigned count)
{
	Hasher h;
	h.u32(view_index);
	h.u32(format);
	h.u32(offset);
	h.u32(count);

	auto itr = accessor_hash.find(h.get());
	if (itr == end(accessor_hash))
	{
		unsigned index = accessor_cache.size();
		EmittedAccessor acc = {};
		acc.count = count;
		acc.view = view_index;
		acc.offset = offset;
		set_accessor_type(acc, format);

		accessor_cache.push_back(acc);
		accessor_hash[h.get()] = index;
		return index;
	}
	else
		return itr->second;
}

unsigned RemapState::emit_sampler(Vulkan::StockSampler sampler)
{
	Hasher h;
	h.u32(ecast(sampler));
	auto itr = sampler_hash.find(h.get());

	if (itr == end(sampler_hash))
	{
		unsigned index = sampler_cache.size();
		sampler_hash[h.get()] = index;

		unsigned mag_filter = 0, min_filter = 0, wrap_s = 0, wrap_t = 0;

		switch (sampler)
		{
		case Vulkan::StockSampler::TrilinearWrap:
			mag_filter = GL_LINEAR;
			min_filter = GL_LINEAR_MIPMAP_LINEAR;
			wrap_s = GL_REPEAT;
			wrap_t = GL_REPEAT;
			break;

		case Vulkan::StockSampler::TrilinearClamp:
			mag_filter = GL_LINEAR;
			min_filter = GL_LINEAR_MIPMAP_LINEAR;
			wrap_s = GL_CLAMP_TO_EDGE;
			wrap_t = GL_CLAMP_TO_EDGE;
			break;

		case Vulkan::StockSampler::LinearWrap:
			mag_filter = GL_LINEAR;
			min_filter = GL_LINEAR_MIPMAP_NEAREST;
			wrap_s = GL_REPEAT;
			wrap_t = GL_REPEAT;
			break;

		case Vulkan::StockSampler::LinearClamp:
			mag_filter = GL_LINEAR;
			min_filter = GL_LINEAR_MIPMAP_NEAREST;
			wrap_s = GL_CLAMP_TO_EDGE;
			wrap_t = GL_CLAMP_TO_EDGE;
			break;

		case Vulkan::StockSampler::NearestClamp:
			mag_filter = GL_NEAREST;
			min_filter = GL_NEAREST_MIPMAP_NEAREST;
			wrap_s = GL_CLAMP_TO_EDGE;
			wrap_t = GL_CLAMP_TO_EDGE;
			break;

		case Vulkan::StockSampler::NearestWrap:
			mag_filter = GL_NEAREST;
			min_filter = GL_NEAREST_MIPMAP_NEAREST;
			wrap_s = GL_REPEAT;
			wrap_t = GL_REPEAT;
			break;

		default:
			break;
		}

		sampler_cache.push_back({ mag_filter, min_filter, wrap_s, wrap_t });
		return index;
	}
	else
		return itr->second;
}

unsigned RemapState::emit_image(const MaterialInfo::Texture &texture, Material::Textures type,
                                TextureCompressionFamily compression, unsigned quality, TextureMode mode)
{
	Hasher h;
	h.string(texture.path);
	h.u32(ecast(type));
	h.u32(ecast(compression));
	h.u32(quality);
	h.s32(ecast(mode));

	auto itr = image_hash.find(h.get());

	if (itr == end(image_hash))
	{
		unsigned index = image_cache.size();
		image_hash[h.get()] = index;

		string extension = compression == TextureCompressionFamily::PNG ? ".png" : ".gtx";
		const char *mime = compression == TextureCompressionFamily::PNG ? "image/png" : "image/custom/granite-texture";
		image_cache.push_back({ texture.path, to_string(h.get()) + extension, mime,
		                        compression, quality, mode, type, {}});
		return index;
	}
	else
		return itr->second;
}

unsigned RemapState::emit_texture(const MaterialInfo::Texture &texture,
                                  Vulkan::StockSampler sampler, Material::Textures type,
                                  TextureCompressionFamily compression, unsigned quality, TextureMode mode)
{
	unsigned image_index = emit_image(texture, type, compression, quality, mode);
	unsigned sampler_index = emit_sampler(sampler);
	Hasher h;
	h.u32(image_index);
	h.u32(sampler_index);
	auto itr = texture_hash.find(h.get());

	if (itr == end(texture_hash))
	{
		unsigned index = texture_cache.size();
		texture_hash[h.get()] = index;
		texture_cache.push_back({ image_index, sampler_index });
		return index;
	}
	else
		return itr->second;
}

void RemapState::emit_environment(const string &cube, const string &reflection, const string &irradiance,
                                  float intensity,
                                  vec3 fog_color, float fog_falloff,
                                  TextureCompressionFamily compression, unsigned quality)
{
	EmittedEnvironment env;
	if (!cube.empty())
	{
		env.cube = emit_texture(MaterialInfo::Texture(cube), Vulkan::StockSampler::LinearClamp,
		                        Material::Textures::Emissive, compression, quality, TextureMode::HDR);
	}

	if (!reflection.empty())
	{
		env.reflection = emit_texture(MaterialInfo::Texture(reflection), Vulkan::StockSampler::LinearClamp,
		                              Material::Textures::Emissive, compression, quality, TextureMode::HDR);
	}

	if (!irradiance.empty())
	{
		env.irradiance = emit_texture(MaterialInfo::Texture(irradiance), Vulkan::StockSampler::LinearClamp,
		                              Material::Textures::Emissive, compression, quality, TextureMode::HDR);
	}

	env.intensity = intensity;
	env.fog_color = fog_color;
	env.fog_falloff = fog_falloff;

	environment_cache.push_back(env);
}

void RemapState::emit_material(unsigned remapped_material)
{
	auto &mat = *material.info[remapped_material];
	material_cache.resize(std::max<size_t>(material_cache.size(), remapped_material + 1));
	auto &output = material_cache[remapped_material];

	if (!mat.normal.path.empty())
	{
		output.normal = emit_texture(mat.normal, mat.sampler, Material::Textures::Normal,
		                             options->compression, options->texcomp_quality, TextureMode::Normal);
	}

	if (!mat.occlusion.path.empty())
	{
		output.occlusion = emit_texture(mat.occlusion, mat.sampler, Material::Textures::Occlusion,
		                                options->compression, options->texcomp_quality, TextureMode::Luminance);
	}

	if (!mat.base_color.path.empty())
	{
		output.base_color = emit_texture(mat.base_color, mat.sampler, Material::Textures::BaseColor,
		                                 options->compression, options->texcomp_quality,
		                                 mat.pipeline != DrawPipeline::Opaque ? TextureMode::sRGBA : TextureMode::sRGB);
	}

	if (!mat.metallic_roughness.path.empty())
	{
		output.metallic_roughness = emit_texture(mat.metallic_roughness, mat.sampler,
		                                         Material::Textures::MetallicRoughness,
		                                         options->compression, options->texcomp_quality, TextureMode::Mask);
	}

	if (!mat.emissive.path.empty())
	{
		output.emissive = emit_texture(mat.emissive, mat.sampler, Material::Textures::Emissive,
		                               options->compression, options->texcomp_quality, TextureMode::sRGB);
	}

	output.uniform_base_color = mat.uniform_base_color;
	output.uniform_emissive_color = mat.uniform_emissive_color;
	output.uniform_metallic = mat.uniform_metallic;
	output.uniform_roughness = mat.uniform_roughness;
	output.normal_scale = mat.normal_scale;
	output.pipeline = mat.pipeline;
	output.two_sided = mat.two_sided;
	output.bandlimited_pixel = mat.bandlimited_pixel;
}

static void quantize_attribute_fp32_fp16(uint8_t *output,
                                         const uint8_t *buffer,
                                         uint32_t stride,
                                         uint32_t count)
{
	for (uint32_t i = 0; i < count; i++)
	{
		vec4 input(0.0f, 0.0f, 0.0f, 1.0f);
		memcpy(input.data, buffer + stride * i, stride);
		u16vec4 packed = floatToHalf(input);
		memcpy(output + sizeof(u16vec4) * i, packed.data, sizeof(packed.data));
	}
}

static void quantize_attribute_fp32_unorm16(uint8_t *output,
                                            const uint8_t *buffer,
                                            uint32_t stride,
                                            uint32_t count)
{
	for (uint32_t i = 0; i < count; i++)
	{
		vec4 input(0.0f, 0.0f, 0.0f, 1.0f);
		memcpy(input.data, buffer + stride * i, stride);
		input *= float(0xffff);
		input = clamp(round(input), vec4(0.0f), vec4(0xffff));
		u16vec4 packed = u16vec4(input);
		memcpy(output + sizeof(u16vec4) * i, packed.data, sizeof(packed.data));
	}
}

static void quantize_attribute_fp32_snorm16(uint8_t *output,
                                            const uint8_t *buffer,
                                            uint32_t stride,
                                            uint32_t count)
{
	for (uint32_t i = 0; i < count; i++)
	{
		vec4 input(0.0f, 0.0f, 0.0f, 1.0f);
		memcpy(input.data, buffer + stride * i, stride);
		input *= float(0x7fff);
		input = clamp(round(input), vec4(-0x7fff), vec4(0x7fff));
		i16vec4 packed = i16vec4(input);
		memcpy(output + sizeof(u16vec4) * i, packed.data, sizeof(packed.data));
	}
}

static void quantize_attribute_rg32f_rg16unorm(uint8_t *output, const uint8_t *buffer, uint32_t count)
{
	for (uint32_t i = 0; i < count; i++)
	{
		vec2 input;
		memcpy(input.data, buffer + sizeof(vec2) * i, sizeof(vec2));

		input *= float(0xffff);
		input = clamp(round(input), vec2(0.0f), vec2(0xffff));
		u16vec2 result(input);
		memcpy(output + i * sizeof(u16vec2), result.data, sizeof(u16vec2));
	}
}

static void quantize_attribute_rg32f_rg16snorm(uint8_t *output, const uint8_t *buffer, uint32_t count)
{
	for (uint32_t i = 0; i < count; i++)
	{
		vec2 input;
		memcpy(input.data, buffer + sizeof(vec2) * i, sizeof(vec2));

		input *= float(0x7fff);
		input = clamp(round(input), vec2(-0x7fff), vec2(0x7fff));
		i16vec2 result(input);
		memcpy(output + i * sizeof(i16vec2), result.data, sizeof(i16vec2));
	}
}

static void quantize_attribute_rg32f_rg16f(uint8_t *output, const uint8_t *buffer, uint32_t count)
{
	for (uint32_t i = 0; i < count; i++)
	{
		vec2 input;
		memcpy(input.data, buffer + sizeof(vec2) * i, sizeof(vec2));
		u16vec2 result = floatToHalf(input);
		memcpy(output + i * sizeof(u16vec2), result.data, sizeof(u16vec2));
	}
}

static void quantize_attribute_fp32_a2bgr10snorm(uint8_t *output, const uint8_t *buffer, uint32_t stride, uint32_t count)
{
	for (uint32_t i = 0; i < count; i++)
	{
		vec4 input(0.0f, 0.0f, 0.0f, 1.0f);
		memcpy(input.data, buffer + stride * i, stride);

		input *= vec4(0x1ff, 0x1ff, 0x1ff, 1);
		input = round(input);
		input = clamp(input, vec4(-0x1ff, -0x1ff, -0x1ff, -1), vec4(0x1ff, 0x1ff, 0x1ff, 1));
		ivec4 quantized(input);

		uint32_t result = uint32_t(quantized.w & 3) << 30;
		result |= uint32_t(quantized.z & 0x3ff) << 20;
		result |= uint32_t(quantized.y & 0x3ff) << 10;
		result |= uint32_t(quantized.x & 0x3ff) <<  0;

		memcpy(output + i * sizeof(uint32_t), &result, sizeof(uint32_t));
	}
}

static void extract_attribute(uint8_t *output,
                              uint32_t output_stride,
                              const uint8_t *buffer,
                              uint32_t stride,
                              uint32_t format_stride,
                              uint32_t count)
{
	for (size_t i = 0; i < count; i++)
		memcpy(output + output_stride * i, buffer + i * stride, format_stride);
}

void RemapState::emit_mesh(unsigned remapped_index)
{
	Mesh new_mesh;
	if (options->optimize_meshes)
		new_mesh = mesh_optimize_index_buffer(*mesh.info[remapped_index], options->stripify_meshes);
	auto &output_mesh = options->optimize_meshes ? new_mesh : *mesh.info[remapped_index];

	mesh_cache.resize(std::max<size_t>(mesh_cache.size(), remapped_index + 1));

	auto &emit = mesh_cache[remapped_index];
	emit.material = output_mesh.has_material ? int(output_mesh.material_index) : -1;
	emit.topology = output_mesh.topology;
	emit.primitive_restart = output_mesh.primitive_restart;

	if (!output_mesh.indices.empty())
	{
		unsigned index = emit_buffer(output_mesh.indices);
		emit.index_accessor = emit_accessor(index,
		                                    output_mesh.index_type == VK_INDEX_TYPE_UINT16 ? VK_FORMAT_R16_UINT
		                                                                            : VK_FORMAT_R32_UINT,
		                                    0, output_mesh.count);

		uint32_t min_index = ~0u;
		uint32_t max_index = 0;

		if (output_mesh.index_type == VK_INDEX_TYPE_UINT16)
		{
			const auto *indices = reinterpret_cast<const uint16_t *>(output_mesh.indices.data());
			for (uint32_t i = 0; i < output_mesh.count; i++)
			{
				min_index = muglm::min(min_index, uint32_t(indices[i]));
				max_index = muglm::max(max_index, uint32_t(indices[i]));
			}
		}
		else
		{
			const auto *indices = reinterpret_cast<const uint32_t *>(output_mesh.indices.data());
			for (uint32_t i = 0; i < output_mesh.count; i++)
			{
				min_index = muglm::min(min_index, indices[i]);
				max_index = muglm::max(max_index, indices[i]);
			}
		}

		accessor_cache[emit.index_accessor].use_uint_min_max = true;
		accessor_cache[emit.index_accessor].uint_min = min_index;
		accessor_cache[emit.index_accessor].uint_max = max_index;
	}
	else
		emit.index_accessor = -1;

	if (output_mesh.has_material)
	{
		unsigned remapped_material = material.to_index[output_mesh.material_index];
		if (!material_hash.count(remapped_material))
		{
			emit_material(remapped_material);
			material_hash.insert(remapped_material);
		}
	}

	const auto &layout = output_mesh.attribute_layout;

	emit.attribute_mask = 0;
	if (!output_mesh.positions.empty())
	{
		uint32_t buffer_index = 0;
		uint32_t count = uint32_t(output_mesh.positions.size() / output_mesh.position_stride);
		int &acc = emit.attribute_accessor[ecast(MeshAttribute::Position)];
		VkFormat format = layout[ecast(MeshAttribute::Position)].format;

		bool format_is_fp32 = format == VK_FORMAT_R32G32B32_SFLOAT ||
		                      format == VK_FORMAT_R32G32B32A32_SFLOAT;

		if (options->quantize_attributes && format_is_fp32 &&
		    all(greaterThanEqual(output_mesh.static_aabb.get_minimum(), vec3(0.0f))) &&
		    all(lessThanEqual(output_mesh.static_aabb.get_maximum(), vec3(1.0f))))
		{
			vector<uint8_t> output(sizeof(u16vec4) * count);
			quantize_attribute_fp32_unorm16(output.data(), output_mesh.positions.data(), output_mesh.position_stride, count);
			buffer_index = emit_buffer(output);
			acc = emit_accessor(buffer_index,
			                    VK_FORMAT_R16G16B16A16_UNORM,
			                    0, count);
		}
		else if (options->quantize_attributes && format_is_fp32 &&
		         all(greaterThanEqual(output_mesh.static_aabb.get_minimum(), vec3(-1.0f))) &&
		         all(lessThanEqual(output_mesh.static_aabb.get_maximum(), vec3(1.0f))))
		{
			vector<uint8_t> output(sizeof(i16vec4) * count);
			quantize_attribute_fp32_snorm16(output.data(), output_mesh.positions.data(), output_mesh.position_stride, count);
			buffer_index = emit_buffer(output);
			acc = emit_accessor(buffer_index,
			                    VK_FORMAT_R16G16B16A16_SNORM,
			                    0, count);
		}
		else if (options->quantize_attributes && format_is_fp32 &&
		         all(greaterThan(output_mesh.static_aabb.get_minimum(), vec3(-0x8000))) &&
		         all(lessThan(output_mesh.static_aabb.get_maximum(), vec3(0x8000))))
		{
			vector<uint8_t> output(sizeof(u16vec4) * count);

			quantize_attribute_fp32_fp16(output.data(), output_mesh.positions.data(), output_mesh.position_stride, count);

			buffer_index = emit_buffer(output);
			acc = emit_accessor(buffer_index,
			                    VK_FORMAT_R16G16B16A16_SFLOAT,
			                    0, count);
		}
		else
		{
			buffer_index = emit_buffer(output_mesh.positions);
			acc = emit_accessor(buffer_index,
			                    layout[ecast(MeshAttribute::Position)].format,
			                    0, count);
		}

		accessor_cache[acc].aabb = output_mesh.static_aabb;
		accessor_cache[acc].use_aabb = true;
		emit.attribute_mask |= 1u << ecast(MeshAttribute::Position);
	}

	if (!output_mesh.attributes.empty())
	{
		auto attr_count = unsigned(output_mesh.attributes.size() / output_mesh.attribute_stride);

		for (unsigned i = 0; i < ecast(MeshAttribute::Count); i++)
		{
			auto attr = static_cast<MeshAttribute>(i);
			if (layout[i].format == VK_FORMAT_UNDEFINED || i == ecast(MeshAttribute::Position))
				continue;

			emit.attribute_mask |= 1u << i;

			auto format_size = Vulkan::TextureFormatLayout::format_block_size(layout[i].format, 0);
			vector<uint8_t> unpacked_buffer(attr_count * format_size);

			extract_attribute(unpacked_buffer.data(), format_size,
			                  output_mesh.attributes.data() + layout[i].offset, output_mesh.attribute_stride, format_size, attr_count);

			VkFormat remapped_format = layout[i].format;

			if (options->quantize_attributes &&
			    (attr == MeshAttribute::Normal || attr == MeshAttribute::Tangent) &&
			    (layout[i].format == VK_FORMAT_R32G32B32A32_SFLOAT || layout[i].format == VK_FORMAT_R32G32B32_SFLOAT))
			{
				vector<uint8_t> quantized(attr_count * sizeof(uint32_t));
				quantize_attribute_fp32_a2bgr10snorm(quantized.data(), unpacked_buffer.data(), format_size, attr_count);
				unpacked_buffer = move(quantized);

				remapped_format = VK_FORMAT_A2B10G10R10_SNORM_PACK32;
				format_size = sizeof(uint32_t);
			}
			else if (options->quantize_attributes &&
			         attr == MeshAttribute::UV &&
			         layout[i].format == VK_FORMAT_R32G32_SFLOAT)
			{
				// Pack to RG16_UNORM if UV is within [0, 1] range.
				vec2 min_uv = vec2(1.0f);
				vec2 max_uv = vec2(0.0f);

				for (unsigned v = 0; v < attr_count; v++)
				{
					vec2 uv;
					memcpy(uv.data, unpacked_buffer.data() + format_size * v, format_size);
					min_uv = min(min_uv, uv);
					max_uv = max(max_uv, uv);
				}

				if (all(lessThanEqual(max_uv, vec2(1.0f))) && all(greaterThanEqual(min_uv, vec2(0.0f))))
				{
					vector<uint8_t> quantized(attr_count * sizeof(u16vec2));
					quantize_attribute_rg32f_rg16unorm(quantized.data(), unpacked_buffer.data(), attr_count);
					unpacked_buffer = move(quantized);
					remapped_format = VK_FORMAT_R16G16_UNORM;
					format_size = sizeof(u16vec2);
				}
				else if (all(lessThanEqual(max_uv, vec2(1.0f))) && all(greaterThanEqual(min_uv, vec2(-1.0f))))
				{
					vector<uint8_t> quantized(attr_count * sizeof(i16vec2));
					quantize_attribute_rg32f_rg16snorm(quantized.data(), unpacked_buffer.data(), attr_count);
					unpacked_buffer = move(quantized);
					remapped_format = VK_FORMAT_R16G16_SNORM;
					format_size = sizeof(i16vec2);
				}
				else if (all(lessThan(max_uv, vec2(0x8000))) && all(greaterThan(min_uv, vec2(-0x8000))))
				{
					vector<uint8_t> quantized(attr_count * sizeof(u16vec2));
					quantize_attribute_rg32f_rg16f(quantized.data(), unpacked_buffer.data(), attr_count);
					unpacked_buffer = move(quantized);
					remapped_format = VK_FORMAT_R16G16_SFLOAT;
					format_size = sizeof(u16vec2);
				}
			}

			auto buffer_index = emit_buffer(unpacked_buffer);
			emit.attribute_accessor[i] = emit_accessor(buffer_index, remapped_format, 0, attr_count);
		}
	}
}

unsigned RemapState::emit_meshes(ArrayView<const unsigned> meshes)
{
	Hasher emit_hash;
	vector<unsigned> mesh_group;
	mesh_group.reserve(meshes.size());

	for (auto &remapped_mesh : meshes)
	{
		unsigned remapped_index = mesh.to_index[remapped_mesh];
		emit_hash.u32(remapped_index);
		mesh_group.push_back(remapped_index);

		if (!mesh_hash.count(remapped_index))
		{
			emit_mesh(remapped_index);
			mesh_hash.insert(remapped_index);
		}
	}

	unsigned index;
	auto itr = mesh_group_hash.find(emit_hash.get());
	if (itr == end(mesh_group_hash))
	{
		index = mesh_group_cache.size();
		mesh_group_cache.push_back(move(mesh_group));
		mesh_group_hash[emit_hash.get()] = index;
	}
	else
		index = itr->second;

	return index;
}

void RemapState::emit_animations(ArrayView<const Animation> animation_list)
{
	for (auto &animation : animation_list)
	{
		EmittedAnimation anim;
		anim.name = animation.name;

		unsigned sampler_index = 0;

		for (auto &channel : animation.channels)
		{
			EmittedAnimation::Channel chan;
			unsigned timestamp_view = emit_buffer({ reinterpret_cast<const uint8_t *>(channel.timestamps.data()),
			                                        channel.timestamps.size() * sizeof(float) });

			unsigned timestamp_accessor = emit_accessor(timestamp_view, VK_FORMAT_R32_SFLOAT, 0,
			                                            channel.timestamps.size());

			unsigned data_view = 0;
			unsigned data_accessor = 0;

			switch (channel.type)
			{
			case AnimationChannel::Type::Rotation:
				chan.path = "rotation";
				data_view = emit_buffer({ reinterpret_cast<const uint8_t *>(channel.spherical.values.data()),
				                          channel.spherical.values.size() * sizeof(quat) });
				data_accessor = emit_accessor(data_view, VK_FORMAT_R32G32B32A32_SFLOAT, 0,
				                              channel.spherical.values.size());
				break;
			case AnimationChannel::Type::CubicTranslation:
				chan.path = "translation";
				data_view = emit_buffer({ reinterpret_cast<const uint8_t *>(channel.cubic.values.data()),
				                          channel.cubic.values.size() * sizeof(vec3) });
				data_accessor = emit_accessor(data_view, VK_FORMAT_R32G32B32_SFLOAT, 0,
				                              channel.cubic.values.size());
				break;
			case AnimationChannel::Type::Translation:
				chan.path = "translation";
				data_view = emit_buffer({ reinterpret_cast<const uint8_t *>(channel.linear.values.data()),
				                          channel.linear.values.size() * sizeof(vec3) });
				data_accessor = emit_accessor(data_view, VK_FORMAT_R32G32B32_SFLOAT, 0,
				                              channel.linear.values.size());
				break;
			case AnimationChannel::Type::CubicScale:
				chan.path = "scale";
				data_view = emit_buffer({ reinterpret_cast<const uint8_t *>(channel.cubic.values.data()),
				                          channel.cubic.values.size() * sizeof(vec3) });
				data_accessor = emit_accessor(data_view, VK_FORMAT_R32G32B32_SFLOAT, 0,
				                              channel.cubic.values.size());
				break;
			case AnimationChannel::Type::Scale:
				chan.path = "scale";
				data_view = emit_buffer({ reinterpret_cast<const uint8_t *>(channel.linear.values.data()),
				                          channel.linear.values.size() * sizeof(vec3) });
				data_accessor = emit_accessor(data_view, VK_FORMAT_R32G32B32_SFLOAT, 0,
				                              channel.linear.values.size());
				break;
			}

			chan.target_node = channel.node_index;
			chan.sampler = sampler_index;

			EmittedAnimation::Sampler samp;
			samp.interpolation = "LINEAR";
			samp.timestamp_accessor = timestamp_accessor;
			samp.data_accessor = data_accessor;

			anim.channels.push_back(chan);
			anim.samplers.push_back(samp);
			sampler_index++;
		}

		this->animations.push_back(move(anim));
	}
}

static VkFormat get_compression_format(TextureCompression compression, TextureMode mode)
{
	bool srgb = mode == TextureMode::sRGB || mode == TextureMode::sRGBA;

	switch (compression)
	{
	case TextureCompression::Uncompressed:
	case TextureCompression::PNG:
		return srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

	case TextureCompression::BC1:
		if (mode == TextureMode::sRGBA || mode == TextureMode::RGBA)
			return srgb ? VK_FORMAT_BC1_RGBA_SRGB_BLOCK : VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
		else
			return srgb ? VK_FORMAT_BC1_RGB_SRGB_BLOCK : VK_FORMAT_BC1_RGB_UNORM_BLOCK;

	case TextureCompression::BC3:
		return srgb ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;

	case TextureCompression::BC4:
		return VK_FORMAT_BC4_UNORM_BLOCK;

	case TextureCompression::BC5:
		return VK_FORMAT_BC5_UNORM_BLOCK;

	case TextureCompression::BC7:
		return srgb ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK;

	case TextureCompression::BC6H:
		return VK_FORMAT_BC6H_UFLOAT_BLOCK;

	case TextureCompression::ASTC4x4:
		return srgb ? VK_FORMAT_ASTC_4x4_SRGB_BLOCK : VK_FORMAT_ASTC_4x4_UNORM_BLOCK;

	case TextureCompression::ASTC5x5:
		return srgb ? VK_FORMAT_ASTC_5x5_SRGB_BLOCK : VK_FORMAT_ASTC_5x5_UNORM_BLOCK;

	case TextureCompression::ASTC6x6:
		return srgb ? VK_FORMAT_ASTC_6x6_SRGB_BLOCK : VK_FORMAT_ASTC_6x6_UNORM_BLOCK;

	case TextureCompression::ASTC8x8:
		return srgb ? VK_FORMAT_ASTC_8x8_SRGB_BLOCK : VK_FORMAT_ASTC_8x8_UNORM_BLOCK;

	default:
		return VK_FORMAT_UNDEFINED;
	}
}

AnalysisResult::MetallicRoughnessMode AnalysisResult::deduce_metallic_roughness_mode()
{
	auto &layout = image->get_layout();
	if (layout.get_layers() > 1)
		return MetallicRoughnessMode::Default;

	auto *src = static_cast<const u8vec4 *>(layout.data(0, 0));
	int width = layout.get_width();
	int height = layout.get_height();
	int count = width * height;

	bool metallic_zero_only = true;
	bool metallic_one_only = true;
	bool roughness_zero_only = true;
	bool roughness_one_only = true;

	for (int i = 0; i < count; i++)
	{
		if (src[i].y != 0xff)
			roughness_one_only = false;
		if (src[i].y != 0)
			roughness_zero_only = false;

		if (src[i].z != 0xff)
			metallic_one_only = false;
		if (src[i].z != 0)
			metallic_zero_only = false;
	}

	if (!metallic_zero_only && !metallic_one_only && (roughness_one_only || roughness_zero_only))
	{
		if (roughness_one_only)
			return MetallicRoughnessMode::MetallicRough;
		else
			return MetallicRoughnessMode::MetallicSmooth;
	}
	else if (!roughness_zero_only && !roughness_one_only && (metallic_one_only || metallic_zero_only))
	{
		if (metallic_one_only)
			return MetallicRoughnessMode::RoughnessMetal;
		else
			return MetallicRoughnessMode::RoughnessDielectric;
	}
	else
		return MetallicRoughnessMode::Default;
}

bool AnalysisResult::load_image(const string &src)
{
	src_path = src;
	image = make_shared<MemoryMappedTexture>();
	*image = load_texture_from_file(src,
	                                (mode == TextureMode::sRGBA || mode == TextureMode::sRGB) ? ColorSpace::sRGB
	                                                                                          : ColorSpace::Linear);

	if (image->get_layout().get_required_size() == 0)
		return false;

	swizzle = {
		VK_COMPONENT_SWIZZLE_R,
		VK_COMPONENT_SWIZZLE_G,
		VK_COMPONENT_SWIZZLE_B,
		VK_COMPONENT_SWIZZLE_A
	};
	return true;
}

void AnalysisResult::deduce_compression(TextureCompressionFamily family)
{
	// Make use of dual-color modes in ASTC like (Luminance + Alpha) to encode 2-component textures.

	switch (family)
	{
	case TextureCompressionFamily::PNG:
		compression = TextureCompression::PNG;
		break;
	case TextureCompressionFamily::ASTC:
		switch (type)
		{
		case Material::Textures::BaseColor:
		case Material::Textures::Emissive:
			compression = TextureCompression::ASTC6x6;
			break;

		case Material::Textures::Occlusion:
			compression = TextureCompression::ASTC6x6;
			mode = TextureMode::Luminance;
			swizzle_image(*image, { VK_COMPONENT_SWIZZLE_R,
			                        VK_COMPONENT_SWIZZLE_R,
			                        VK_COMPONENT_SWIZZLE_R,
			                        VK_COMPONENT_SWIZZLE_R });
			break;

		case Material::Textures::Normal:
			compression = TextureCompression::ASTC6x6;
			mode = TextureMode::NormalLA;
			swizzle_image(*image, { VK_COMPONENT_SWIZZLE_R,
			                        VK_COMPONENT_SWIZZLE_R,
			                        VK_COMPONENT_SWIZZLE_R,
			                        VK_COMPONENT_SWIZZLE_G });

			swizzle.r = VK_COMPONENT_SWIZZLE_R;
			swizzle.g = VK_COMPONENT_SWIZZLE_A;
			swizzle.b = VK_COMPONENT_SWIZZLE_ONE;
			swizzle.a = VK_COMPONENT_SWIZZLE_ONE;
			break;

		case Material::Textures::MetallicRoughness:
		{
			compression = TextureCompression::ASTC6x6;
			mode = TextureMode::MaskLA;
			auto mr_mode = deduce_metallic_roughness_mode();
			switch (mr_mode)
			{
			case MetallicRoughnessMode::Default:
				swizzle_image(*image, { VK_COMPONENT_SWIZZLE_G,
				                        VK_COMPONENT_SWIZZLE_G,
				                        VK_COMPONENT_SWIZZLE_G,
				                        VK_COMPONENT_SWIZZLE_B });
				swizzle.r = VK_COMPONENT_SWIZZLE_ZERO;
				swizzle.g = VK_COMPONENT_SWIZZLE_R;
				swizzle.b = VK_COMPONENT_SWIZZLE_A;
				swizzle.a = VK_COMPONENT_SWIZZLE_ZERO;
				break;

			case MetallicRoughnessMode::MetallicRough:
			case MetallicRoughnessMode::MetallicSmooth:
				swizzle_image(*image, { VK_COMPONENT_SWIZZLE_B,
				                        VK_COMPONENT_SWIZZLE_B,
				                        VK_COMPONENT_SWIZZLE_B,
				                        VK_COMPONENT_SWIZZLE_B });
				swizzle.r = VK_COMPONENT_SWIZZLE_ZERO;
				swizzle.g = mr_mode == MetallicRoughnessMode::MetallicRough ?
				            VK_COMPONENT_SWIZZLE_ONE : VK_COMPONENT_SWIZZLE_ZERO;
				swizzle.b = VK_COMPONENT_SWIZZLE_R;
				swizzle.a = VK_COMPONENT_SWIZZLE_ZERO;
				mode = TextureMode::Luminance;
				break;

			case MetallicRoughnessMode::RoughnessDielectric:
			case MetallicRoughnessMode::RoughnessMetal:
				swizzle_image(*image, { VK_COMPONENT_SWIZZLE_G,
				                        VK_COMPONENT_SWIZZLE_G,
				                        VK_COMPONENT_SWIZZLE_G,
				                        VK_COMPONENT_SWIZZLE_G });
				swizzle.r = VK_COMPONENT_SWIZZLE_ZERO;
				swizzle.g = VK_COMPONENT_SWIZZLE_R;
				swizzle.b = mr_mode == MetallicRoughnessMode::RoughnessMetal ?
				            VK_COMPONENT_SWIZZLE_ONE : VK_COMPONENT_SWIZZLE_ZERO;
				swizzle.a = VK_COMPONENT_SWIZZLE_ZERO;
				mode = TextureMode::Luminance;
				break;
			}
			break;
		}

		default:
			throw invalid_argument("Invalid material type.");
		}
		break;

	case TextureCompressionFamily::BC:
		switch (type)
		{
		case Material::Textures::BaseColor:
		case Material::Textures::Emissive:
			compression = TextureCompression::BC7;
			break;

		case Material::Textures::Occlusion:
			compression = TextureCompression::BC4;
			mode = TextureMode::Luminance;
			break;

		case Material::Textures::MetallicRoughness:
		{
			mode = TextureMode::Mask;
			auto mr_mode = deduce_metallic_roughness_mode();
			switch (mr_mode)
			{
			case MetallicRoughnessMode::Default:
				compression = TextureCompression::BC5;
				swizzle_image(*image, { VK_COMPONENT_SWIZZLE_G,
				                        VK_COMPONENT_SWIZZLE_B,
				                        VK_COMPONENT_SWIZZLE_B,
				                        VK_COMPONENT_SWIZZLE_A });
				swizzle.r = VK_COMPONENT_SWIZZLE_ZERO;
				swizzle.g = VK_COMPONENT_SWIZZLE_R;
				swizzle.b = VK_COMPONENT_SWIZZLE_G;
				swizzle.a = VK_COMPONENT_SWIZZLE_ZERO;
				break;

			case MetallicRoughnessMode::RoughnessDielectric:
			case MetallicRoughnessMode::RoughnessMetal:
				compression = TextureCompression::BC4;
				swizzle_image(*image, { VK_COMPONENT_SWIZZLE_G,
				                        VK_COMPONENT_SWIZZLE_G,
				                        VK_COMPONENT_SWIZZLE_G,
				                        VK_COMPONENT_SWIZZLE_G });
				swizzle.r = VK_COMPONENT_SWIZZLE_ZERO;
				swizzle.g = VK_COMPONENT_SWIZZLE_R;
				swizzle.b = mr_mode == MetallicRoughnessMode::RoughnessMetal ?
				            VK_COMPONENT_SWIZZLE_ONE : VK_COMPONENT_SWIZZLE_ZERO;
				swizzle.a = VK_COMPONENT_SWIZZLE_ZERO;
				mode = TextureMode::Luminance;
				break;

			case MetallicRoughnessMode::MetallicRough:
			case MetallicRoughnessMode::MetallicSmooth:
				compression = TextureCompression::BC4;
				swizzle_image(*image, { VK_COMPONENT_SWIZZLE_B,
				                        VK_COMPONENT_SWIZZLE_B,
				                        VK_COMPONENT_SWIZZLE_B,
				                        VK_COMPONENT_SWIZZLE_B });
				swizzle.r = VK_COMPONENT_SWIZZLE_ZERO;
				swizzle.g = mr_mode == MetallicRoughnessMode::MetallicRough ?
				            VK_COMPONENT_SWIZZLE_ONE : VK_COMPONENT_SWIZZLE_ZERO;
				swizzle.b = VK_COMPONENT_SWIZZLE_R;
				swizzle.a = VK_COMPONENT_SWIZZLE_ZERO;
				mode = TextureMode::Luminance;
				break;
			}
			break;
		}

		case Material::Textures::Normal:
			compression = TextureCompression::BC5;
			mode = TextureMode::Normal;
			break;

		default:
			throw invalid_argument("Invalid material type.");
		}

		if (mode == TextureMode::HDR)
			compression = TextureCompression::BC6H;
		break;

	case TextureCompressionFamily::Uncompressed:
		compression = TextureCompression::Uncompressed;
		break;
	}
}

static shared_ptr<AnalysisResult> analyze_image(ThreadGroup &workers,
                                                const string &src,
                                                Material::Textures type, TextureCompressionFamily family,
                                                TextureMode mode,
                                                TaskSignal *signal)
{
	auto result = make_shared<AnalysisResult>();
	result->mode = mode;
	result->type = type;

	auto group = workers.create_task([=]() {
		if (!result->load_image(src))
		{
			LOGE("Failed to load image.\n");
			return;
		}

		result->deduce_compression(family);
	});
	group->set_fence_counter_signal(signal);

	return result;
}

static void compress_image(ThreadGroup &workers, const string &target_path, shared_ptr<AnalysisResult> &result,
                           unsigned quality, TaskSignal *signal)
{
	FileStat src_stat, dst_stat;
	if (Global::filesystem()->stat(result->src_path, src_stat) && Global::filesystem()->stat(target_path, dst_stat))
	{
		if (src_stat.last_modified < dst_stat.last_modified)
		{
			LOGI("Texture %s -> %s is already compressed, skipping.\n", result->src_path.c_str(), target_path.c_str());
			if (signal)
				signal->signal_increment();
			return;
		}
	}

	CompressorArguments args;
	args.output = target_path;
	args.format = get_compression_format(result->compression, result->mode);
	args.quality = quality;
	args.mode = result->mode;
	args.output_mapping = result->swizzle;

	auto mipgen_task = workers.create_task([=]() {
		if (result->image->get_layout().get_levels() == 1 && result->mode != TextureMode::HDR)
		{
			if (result->compression == TextureCompression::PNG)
			{
				// Do nothing, we don't need mipmaps.
			}
			else if (result->compression != TextureCompression::Uncompressed)
				*result->image = generate_mipmaps(result->image->get_layout(), result->image->get_flags());
			else
				*result->image = generate_mipmaps_to_file(target_path, result->image->get_layout(), result->image->get_flags());
		}

		LOGI("Mapped input texture: %u bytes.\n", unsigned(result->image->get_required_size()));
	});

	if (result->compression == TextureCompression::PNG)
	{
		auto write_task = workers.create_task([=]() {
			if (result->image->get_layout().get_format() != VK_FORMAT_R8G8B8A8_UNORM &&
			    result->image->get_layout().get_format() != VK_FORMAT_R8G8B8A8_SRGB)
			{
				LOGE("PNG only supports RGBA8.\n");
				return;
			}

			string real_path = Global::filesystem()->get_filesystem_path(args.output);
			if (real_path.empty())
			{
				LOGE("Can only use filesystem backend paths when writing PNG.\n");
				return;
			}

			if (!stbi_write_png(real_path.c_str(),
			                    result->image->get_layout().get_width(),
			                    result->image->get_layout().get_height(), 4,
			                    result->image->get_layout().data(),
			                    result->image->get_layout().get_width() * 4))
			{
				LOGE("Failed to write PNG.\n");
			}

			LOGI("Unmapping %u bytes for texture writing.\n", unsigned(result->image->get_required_size()));
			result->image.reset();
		});
		write_task->set_fence_counter_signal(signal);
		workers.add_dependency(*write_task, *mipgen_task);
	}
	else if (result->compression != TextureCompression::Uncompressed)
		compress_texture(workers, args, result->image, mipgen_task, signal);
	else if (result->image->get_layout().get_levels() != 1 || result->mode == TextureMode::HDR)
	{
		auto write_task = workers.create_task([=]() {
			if (!result->image->copy_to_path(args.output))
				LOGE("Failed to copy image.\n");

			LOGI("Unmapping %u bytes for texture writing.\n", unsigned(result->image->get_required_size()));
			result->image.reset();
		});
		write_task->set_fence_counter_signal(signal);
		workers.add_dependency(*write_task, *mipgen_task);
	}
	else
		mipgen_task->set_fence_counter_signal(signal);
}

bool export_scene_to_glb(const SceneInformation &scene, const string &path, const ExportOptions &options)
{
	Document doc;
	doc.SetObject();
	auto &allocator = doc.GetAllocator();

	ThreadGroup workers;
	workers.start(options.threads ? options.threads : std::thread::hardware_concurrency());

	Value asset(kObjectType);
	asset.AddMember("generator", "Granite glTF 2.0 exporter", allocator);
	asset.AddMember("version", "2.0", allocator);
	doc.AddMember("asset", asset, allocator);

	if (!scene.lights.empty())
	{
		Value req(kArrayType);
		req.PushBack("KHR_lights_punctual", allocator);
		doc.AddMember("extensionsRequired", req, allocator);

		Value used(kArrayType);
		used.PushBack("KHR_lights_punctual", allocator);
		doc.AddMember("extensionsUsed", used, allocator);
	}

	RemapState state;
	state.options = &options;
	state.filter_input(state.material, scene.materials);
	state.filter_input(state.mesh, scene.meshes);

	if (!options.environment.cube.empty())
	{
		state.emit_environment(options.environment.cube, options.environment.reflection, options.environment.irradiance,
		                       options.environment.intensity,
		                       options.environment.fog_color, options.environment.fog_falloff,
		                       options.environment.compression, options.environment.texcomp_quality);
	}

	state.emit_animations(scene.animations);

	Value nodes(kArrayType);
	for (auto &node : scene.nodes)
	{
		Value n(kObjectType);
		if (!node.children.empty())
		{
			Value children(kArrayType);
			for (auto &c : node.children)
				children.PushBack(c, allocator);
			n.AddMember("children", children, allocator);
		}

		if (!node.meshes.empty())
			n.AddMember("mesh", state.emit_meshes(node.meshes), allocator);

		// TODO: Reverse mapping to avoid searching every time.
		for (auto &camera : scene.cameras)
		{
			if (camera.attached_to_node && camera.node_index == uint32_t(&node - scene.nodes.data()))
			{
				n.AddMember("camera", uint32_t(&camera - scene.cameras.data()), allocator);
				break;
			}
		}

		// TODO: Reverse mapping to avoid searching every time.
		for (auto &light : scene.lights)
		{
			if (light.attached_to_node && light.node_index == uint32_t(&node - scene.nodes.data()))
			{
				Value ext(kObjectType);
				Value cmn(kObjectType);
				cmn.AddMember("light", uint32_t(&light - scene.lights.data()), allocator);
				ext.AddMember("KHR_lights_punctual", cmn, allocator);
				n.AddMember("extensions", ext, allocator);
				break;
			}
		}

		if (node.transform.rotation.w != 1.0f ||
		    node.transform.rotation.x != 0.0f ||
		    node.transform.rotation.y != 0.0f ||
		    node.transform.rotation.z != 0.0f)
		{
			Value rot(kArrayType);
			for (unsigned i = 0; i < 4; i++)
				rot.PushBack(node.transform.rotation.as_vec4()[i], allocator);
			n.AddMember("rotation", rot, allocator);
		}

		if (any(notEqual(node.transform.scale, vec3(1.0f))))
		{
			Value s(kArrayType);
			for (unsigned i = 0; i < 3; i++)
				s.PushBack(node.transform.scale[i], allocator);
			n.AddMember("scale", s, allocator);
		}

		if (any(notEqual(node.transform.translation, vec3(0.0f))))
		{
			Value t(kArrayType);
			for (unsigned i = 0; i < 3; i++)
				t.PushBack(node.transform.translation[i], allocator);
			n.AddMember("translation", t, allocator);
		}
		nodes.PushBack(n, allocator);
	}
	doc.AddMember("nodes", nodes, allocator);

	if (options.gltf)
	{
		// The baked GLB buffer.
		Value buffers(kArrayType);
		Value buffer(kObjectType);
		buffer.AddMember("byteLength", uint32_t(state.glb_buffer_data.size()), allocator);

		auto uri = path + ".bin";
		buffer.AddMember("uri", Path::basename(uri), allocator);

		auto file = Global::filesystem()->open(uri, FileMode::WriteOnly);
		if (!file)
		{
			LOGE("Failed to open %s for writing.\n", uri.c_str());
			return false;
		}

		void *mapped = file->map_write(state.glb_buffer_data.size());
		if (!mapped)
		{
			LOGE("Failed to map buffer for writing.\n");
			return false;
		}

		memcpy(mapped, state.glb_buffer_data.data(), state.glb_buffer_data.size());
		buffers.PushBack(buffer, allocator);
		doc.AddMember("buffers", buffers, allocator);
	}
	else
	{
		// The baked GLB buffer.
		Value buffers(kArrayType);
		Value buffer(kObjectType);
		buffer.AddMember("byteLength", uint32_t(state.glb_buffer_data.size()), allocator);
		buffers.PushBack(buffer, allocator);
		doc.AddMember("buffers", buffers, allocator);
	}

	// Buffer Views
	if (!state.buffer_views.empty())
	{
		Value views(kArrayType);
		for (auto &view : state.buffer_views)
		{
			Value v(kObjectType);
			v.AddMember("buffer", 0, allocator);
			v.AddMember("byteLength", uint32_t(view.length), allocator);
			v.AddMember("byteOffset", uint32_t(view.offset), allocator);
			views.PushBack(v, allocator);
		}
		doc.AddMember("bufferViews", views, allocator);
	}

	// Accessors
	if (!state.accessor_cache.empty())
	{
		Value accessors(kArrayType);
		for (auto &accessor : state.accessor_cache)
		{
			Value acc(kObjectType);
			acc.AddMember("bufferView", accessor.view, allocator);
			acc.AddMember("componentType", accessor.component, allocator);
			acc.AddMember("type", StringRef(accessor.type), allocator);
			acc.AddMember("count", accessor.count, allocator);
			acc.AddMember("byteOffset", accessor.offset, allocator);
			if (accessor.normalized)
				acc.AddMember("normalized", accessor.normalized, allocator);
			if (accessor.use_aabb)
			{
				vec4 lo = vec4(accessor.aabb.get_minimum(), 1.0f);
				vec4 hi = vec4(accessor.aabb.get_maximum(), 1.0f);
				unsigned components = 0;
				if (strcmp(accessor.type, "SCALAR") == 0)
					components = 1;
				else if (strcmp(accessor.type, "VEC2") == 0)
					components = 2;
				else if (strcmp(accessor.type, "VEC3") == 0)
					components = 3;
				else if (strcmp(accessor.type, "VEC4") == 0)
					components = 4;

				if (components)
				{
					Value minimum(kArrayType);
					Value maximum(kArrayType);
					for (unsigned i = 0; i < components; i++)
						minimum.PushBack(lo[i], allocator);
					for (unsigned i = 0; i < components; i++)
						maximum.PushBack(hi[i], allocator);
					acc.AddMember("min", minimum, allocator);
					acc.AddMember("max", maximum, allocator);
				}
			}
			else if (accessor.use_uint_min_max)
			{
				Value minimum(kArrayType);
				Value maximum(kArrayType);
				minimum.PushBack(accessor.uint_min, allocator);
				maximum.PushBack(accessor.uint_max, allocator);
				acc.AddMember("min", minimum, allocator);
				acc.AddMember("max", maximum, allocator);
			}

			accessors.PushBack(acc, allocator);
		}
		doc.AddMember("accessors", accessors, allocator);
	}

	// Samplers
	if (!state.sampler_cache.empty())
	{
		Value samplers(kArrayType);
		for (auto &sampler : state.sampler_cache)
		{
			Value s(kObjectType);
			if (sampler.mag_filter)
				s.AddMember("magFilter", sampler.mag_filter, allocator);
			if (sampler.min_filter)
				s.AddMember("minFilter", sampler.min_filter, allocator);
			if (sampler.wrap_s)
				s.AddMember("wrapS", sampler.wrap_s, allocator);
			if (sampler.wrap_t)
				s.AddMember("wrapT", sampler.wrap_t, allocator);
			samplers.PushBack(s, allocator);
		}
		doc.AddMember("samplers", samplers, allocator);
	}

	// Images
	if (!state.image_cache.empty())
	{
		Value images(kArrayType);

		LOGI("Analyzing images ...\n");
		// Load images, swizzle, and figure out which compression type is the most appropriate.
		unsigned image_max_count = 0;
		TaskSignal image_signal;
		for (auto &image : state.image_cache)
		{
			if (image_max_count > 8)
				image_signal.wait_until_at_least(image_max_count - 8);
			image.loaded_image = analyze_image(workers,
			                                   image.source_path,
			                                   image.type, image.compression, image.mode,
			                                   &image_signal);
		}
		workers.wait_idle();
		LOGI("Analyzed images ...\n");

		TaskSignal signal;
		unsigned max_count = 0;
		for (auto &image : state.image_cache)
		{
			Value i(kObjectType);
			i.AddMember("uri", image.target_relpath, allocator);
			i.AddMember("mimeType", image.target_mime, allocator);

			images.PushBack(i, allocator);

			// Only keep a certain number of compression jobs alive at a time.
			if (max_count > 3)
				signal.wait_until_at_least(max_count - 3);

			compress_image(workers, Path::relpath(path, image.target_relpath),
			               image.loaded_image, image.compression_quality, &signal);

			max_count++;
		}
		doc.AddMember("images", images, allocator);
	}

	// Sources
	{
		Value sources(kArrayType);
		for (auto &texture : state.texture_cache)
		{
			Value t(kObjectType);
			t.AddMember("sampler", texture.sampler, allocator);
			t.AddMember("source", texture.image, allocator);
			sources.PushBack(t, allocator);
		}
		doc.AddMember("textures", sources, allocator);
	}

	// Materials
	if (!state.material_cache.empty())
	{
		Value materials(kArrayType);
		for (auto &material : state.material_cache)
		{
			Value m(kObjectType);

			if (material.pipeline == DrawPipeline::AlphaBlend)
				m.AddMember("alphaMode", "BLEND", allocator);
			else if (material.pipeline == DrawPipeline::AlphaTest)
				m.AddMember("alphaMode", "MASK", allocator);

			if (material.two_sided)
				m.AddMember("doubleSided", true, allocator);

			if (material.bandlimited_pixel)
			{
				Value extras(kObjectType);
				extras.AddMember("bandlimitedPixel", true, allocator);
				m.AddMember("extras", extras, allocator);
			}

			if (any(notEqual(material.uniform_emissive_color, vec3(0.0f))))
			{
				Value emissive(kArrayType);
				for (unsigned i = 0; i < 3; i++)
					emissive.PushBack(material.uniform_emissive_color[i], allocator);
				m.AddMember("emissiveFactor", emissive, allocator);
			}

			Value pbr(kObjectType);
			if (material.uniform_roughness != 1.0f)
				pbr.AddMember("roughnessFactor", material.uniform_roughness, allocator);
			if (material.uniform_metallic != 1.0f)
				pbr.AddMember("metallicFactor", material.uniform_metallic, allocator);

			if (any(notEqual(material.uniform_base_color, vec4(1.0f))))
			{
				Value base(kArrayType);
				for (unsigned i = 0; i < 4; i++)
					base.PushBack(material.uniform_base_color[i], allocator);
				pbr.AddMember("baseColorFactor", base, allocator);
			}

			if (material.base_color >= 0)
			{
				Value base(kObjectType);
				base.AddMember("index", material.base_color, allocator);
				pbr.AddMember("baseColorTexture", base, allocator);
			}

			if (material.metallic_roughness >= 0)
			{
				Value mr(kObjectType);
				mr.AddMember("index", material.metallic_roughness, allocator);
				pbr.AddMember("metallicRoughnessTexture", mr, allocator);
			}

			m.AddMember("pbrMetallicRoughness", pbr, allocator);

			if (material.normal >= 0)
			{
				Value n(kObjectType);
				unsigned image_index = state.texture_cache[material.normal].image;

				bool two_component =
						state.image_cache[image_index].compression != TextureCompressionFamily::Uncompressed &&
						state.image_cache[image_index].compression != TextureCompressionFamily::PNG;

				if (two_component)
				{
					Value extras(kObjectType);
					extras.AddMember("twoComponent", true, allocator);
					n.AddMember("extras", extras, allocator);
				}

				n.AddMember("index", material.normal, allocator);
				n.AddMember("scale", material.normal_scale, allocator);
				m.AddMember("normalTexture", n, allocator);
			}

			if (material.emissive >= 0)
			{
				Value e(kObjectType);
				e.AddMember("index", material.emissive, allocator);
				m.AddMember("emissiveTexture", e, allocator);
			}

			if (material.occlusion >= 0)
			{
				Value e(kObjectType);
				e.AddMember("index", material.occlusion, allocator);
				m.AddMember("occlusionTexture", e, allocator);
			}

			materials.PushBack(m, allocator);
		}
		doc.AddMember("materials", materials, allocator);
	}

	// Meshes
	if (!state.mesh_group_cache.empty())
	{
		Value meshes(kArrayType);
		for (auto &mesh : state.mesh_group_cache)
		{
			Value m(kObjectType);
			Value primitives(kArrayType);

			for (auto &submesh : mesh)
			{
				Value prim(kObjectType);
				Value attribs(kObjectType);

				auto &cached_mesh = state.mesh_cache[submesh];

				for_each_bit(cached_mesh.attribute_mask, [&](unsigned bit) {
					auto attr = static_cast<MeshAttribute>(bit);
					const char *semantic = nullptr;
					switch (attr)
					{
					case MeshAttribute::Position:
						semantic = "POSITION";
						break;
					case MeshAttribute::Normal:
						semantic = "NORMAL";
						break;
					case MeshAttribute::BoneWeights:
						semantic = "WEIGHTS_0";
						break;
					case MeshAttribute::BoneIndex:
						semantic = "JOINTS_0";
						break;
					case MeshAttribute::VertexColor:
						semantic = "COLOR_0";
						break;
					case MeshAttribute::Tangent:
						semantic = "TANGENT";
						break;
					case MeshAttribute::UV:
						semantic = "TEXCOORD_0";
						break;
					default:
						return;
					}
					attribs.AddMember(StringRef(semantic), cached_mesh.attribute_accessor[bit], allocator);
				});

				if (cached_mesh.index_accessor >= 0)
					prim.AddMember("indices", cached_mesh.index_accessor, allocator);

				if (cached_mesh.material >= 0)
					prim.AddMember("material", state.material.to_index[cached_mesh.material], allocator);

				switch (cached_mesh.topology)
				{
				case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
					prim.AddMember("mode", 0, allocator);
					break;

				case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
					prim.AddMember("mode", 1, allocator);
					break;

				case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
					prim.AddMember("mode", 3, allocator);
					break;

				case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
					prim.AddMember("mode", 4, allocator);
					break;

				case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
					prim.AddMember("mode", 5, allocator);
					break;

				case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
					prim.AddMember("mode", 6, allocator);
					break;

				default:
					break;
				}

				if (cached_mesh.primitive_restart)
				{
					Value extras(kObjectType);
					extras.AddMember("primitiveRestart", cached_mesh.primitive_restart, allocator);
					prim.AddMember("extras", extras, allocator);
				}
				prim.AddMember("attributes", attribs, allocator);
				primitives.PushBack(prim, allocator);
			}
			m.AddMember("primitives", primitives, allocator);
			meshes.PushBack(m, allocator);
		}
		doc.AddMember("meshes", meshes, allocator);
	}

	// Cameras
	if (!scene.cameras.empty())
	{
		Value cameras(kArrayType);
		for (auto &camera : scene.cameras)
		{
			Value cam(kObjectType);
			if (camera.type == CameraInfo::Type::Perspective)
			{
				cam.AddMember("type", "perspective", allocator);
				Value perspective(kObjectType);
				perspective.AddMember("aspectRatio", camera.aspect_ratio, allocator);
				perspective.AddMember("yfov", camera.yfov, allocator);
				perspective.AddMember("znear", camera.znear, allocator);
				perspective.AddMember("zfar", camera.zfar, allocator);
				cam.AddMember("perspective", perspective, allocator);
			}
			else if (camera.type == CameraInfo::Type::Orthographic)
			{
				cam.AddMember("type", "orthographic", allocator);
				Value ortho(kObjectType);
				ortho.AddMember("xmag", camera.xmag, allocator);
				ortho.AddMember("ymag", camera.ymag, allocator);
				ortho.AddMember("znear", camera.znear, allocator);
				ortho.AddMember("zfar", camera.zfar, allocator);
				cam.AddMember("orthographic", ortho, allocator);
			}
			cameras.PushBack(cam, allocator);
		}
		doc.AddMember("cameras", cameras, allocator);
	}

	// Lights
	if (!scene.lights.empty())
	{
		Value ext(kObjectType);
		Value khr_lights(kObjectType);
		Value lights(kArrayType);

		for (auto &light : scene.lights)
		{
			Value l(kObjectType);
			Value color(kArrayType);

			float intensity = muglm::max(muglm::max(light.color.x, light.color.y), light.color.z);
			if (intensity == 0.0f)
				intensity = 1.0f;

			color.PushBack(light.color.x / intensity, allocator);
			color.PushBack(light.color.y / intensity, allocator);
			color.PushBack(light.color.z / intensity, allocator);

			if (intensity != 1.0f)
				l.AddMember("intensity", intensity, allocator);
			l.AddMember("color", color, allocator);

			if (light.range != 0.0f)
				l.AddMember("range", light.range, allocator);

			switch (light.type)
			{
			case LightInfo::Type::Spot:
			{
				Value spot(kObjectType);
				spot.AddMember("innerConeAngle", muglm::acos(light.inner_cone), allocator);
				spot.AddMember("outerConeAngle", muglm::acos(light.outer_cone), allocator);
				l.AddMember("type", "spot", allocator);
				l.AddMember("spot", spot, allocator);
				break;
			}

			case LightInfo::Type::Point:
				l.AddMember("type", "point", allocator);
				break;

			case LightInfo::Type::Directional:
				l.AddMember("type", "directional", allocator);
				break;

			case LightInfo::Type::Ambient:
				l.AddMember("type", "ambient", allocator);
				break;
			}

			lights.PushBack(l, allocator);
		}

		khr_lights.AddMember("lights", lights, allocator);
		ext.AddMember("KHR_lights_punctual", khr_lights, allocator);
		doc.AddMember("extensions", ext, allocator);
	}

	// Animations
	if (!scene.animations.empty())
	{
		Value animations(kArrayType);

		for (auto &animation : state.animations)
		{
			Value anim(kObjectType);
			Value channels(kArrayType);
			Value samplers(kArrayType);

			for (auto &chan : animation.channels)
			{
				Value c(kObjectType);

				c.AddMember("sampler", chan.sampler, allocator);
				Value target(kObjectType);
				target.AddMember("node", chan.target_node, allocator);
				target.AddMember("path", chan.path, allocator);
				c.AddMember("target", target, allocator);

				channels.PushBack(c, allocator);
			}

			for (auto &samp : animation.samplers)
			{
				Value s(kObjectType);

				s.AddMember("input", samp.timestamp_accessor, allocator);
				s.AddMember("output", samp.data_accessor, allocator);
				s.AddMember("interpolation", samp.interpolation, allocator);

				samplers.PushBack(s, allocator);
			}

			anim.AddMember("channels", channels, allocator);
			anim.AddMember("samplers", samplers, allocator);
			anim.AddMember("name", animation.name, allocator);
			animations.PushBack(anim, allocator);
		}

		doc.AddMember("animations", animations, allocator);
	}

	if (!state.environment_cache.empty())
	{
		Value extras(kObjectType);
		Value environments(kArrayType);

		for (auto &env : state.environment_cache)
		{
			Value environment(kObjectType);

			if (env.cube >= 0)
				environment.AddMember("cubeTexture", env.cube, allocator);
			if (env.reflection >= 0)
				environment.AddMember("reflectionTexture", env.reflection, allocator);
			if (env.irradiance >= 0)
				environment.AddMember("irradianceTexture", env.irradiance, allocator);
			environment.AddMember("intensity", env.intensity, allocator);

			Value fog(kObjectType);
			Value color(kArrayType);
			color.PushBack(env.fog_color.x, allocator);
			color.PushBack(env.fog_color.y, allocator);
			color.PushBack(env.fog_color.z, allocator);
			fog.AddMember("color", color, allocator);
			fog.AddMember("falloff", env.fog_falloff, allocator);
			environment.AddMember("fog", fog, allocator);

			environments.PushBack(environment, allocator);
		}
		extras.AddMember("environments", environments, allocator);
		doc.AddMember("extras", extras, allocator);
	}

	// Scene nodes.
	doc.AddMember("scene", 0, allocator);
	Value scene_nodes(kArrayType);
	Value scene_info(kObjectType);
	Value scenes(kArrayType);

	if (scene.scene_nodes)
	{
		for (auto &node : scene.scene_nodes->node_indices)
			scene_nodes.PushBack(node, allocator);

		if (!scene.scene_nodes->name.empty())
			scene_info.AddMember("name", StringRef(scene.scene_nodes->name), allocator);
	}
	else
	{
		// Every node which is not a child of some other node is part of the scene.
		unordered_set<uint32_t> is_child;
		for (auto &node : scene.nodes)
			for (auto &child : node.children)
				is_child.insert(child);

		for (size_t i = 0; i < scene.nodes.size(); i++)
			if (!is_child.count(i))
				scene_nodes.PushBack(uint32_t(i), allocator);
	}

	scene_info.AddMember("nodes", scene_nodes, allocator);
	scenes.PushBack(scene_info, allocator);
	doc.AddMember("scenes", scenes, allocator);

	StringBuffer buffer;
	PrettyWriter<StringBuffer> writer(buffer);
	//Writer<StringBuffer> writer(buffer);
	doc.Accept(writer);

	const auto aligned_size = [](size_t size) {
		return (size + 3) & ~size_t(3);
	};

	const auto write_u32 = [](uint8_t *data, uint32_t v) {
		memcpy(data, &v, sizeof(uint32_t));
	};

	auto file = Global::filesystem()->open(path, FileMode::WriteOnly);
	if (!file)
	{
		LOGE("Failed to open file: %s\n", path.c_str());
		return false;
	}

	if (options.gltf)
	{
		uint8_t *mapped = static_cast<uint8_t *>(file->map_write(buffer.GetLength()));
		if (!mapped)
		{
			LOGE("Failed to map file: %s\n", path.c_str());
			return false;
		}

		const char *json_str = buffer.GetString();
		memcpy(mapped, json_str, buffer.GetLength());
	}
	else
	{
		size_t glb_size = 12 + 8 + aligned_size(buffer.GetLength()) + 8 + aligned_size(state.glb_buffer_data.size());

		uint8_t *mapped = static_cast<uint8_t *>(file->map_write(glb_size));
		if (!mapped)
		{
			LOGE("Failed to map file: %s\n", path.c_str());
			return false;
		}

		memcpy(mapped, "glTF", 4);
		mapped += 4;
		write_u32(mapped, 2);
		mapped += 4;
		write_u32(mapped, glb_size);
		mapped += 4;

		write_u32(mapped, aligned_size(buffer.GetLength()));
		mapped += 4;
		memcpy(mapped, "JSON", 4);
		mapped += 4;

		const char *json_str = buffer.GetString();
		memcpy(mapped, json_str, buffer.GetLength());
		size_t pad_length = aligned_size(buffer.GetLength()) - buffer.GetLength();
		memset(mapped + buffer.GetLength(), ' ', pad_length);
		mapped += aligned_size(buffer.GetLength());

		write_u32(mapped, aligned_size(state.glb_buffer_data.size()));
		mapped += 4;
		memcpy(mapped, "BIN\0", 4);
		mapped += 4;
		memcpy(mapped, state.glb_buffer_data.data(), state.glb_buffer_data.size());
		pad_length = aligned_size(state.glb_buffer_data.size()) - state.glb_buffer_data.size();
		memset(mapped + state.glb_buffer_data.size(), 0, pad_length);
	}

	file->unmap();
	return true;
}
}
}
