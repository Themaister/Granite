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

#include "texture_utils.hpp"

namespace Granite
{
namespace SceneFormats
{
struct TextureFormatUnorm8
{
	inline vec4 sample(const Vulkan::TextureFormatLayout &layout, const uvec2 &coord,
	                   uint32_t layer, uint32_t mip) const
	{
		uint8_t &v = *layout.data_generic<uint8_t>(coord.x, coord.y, layer, mip);
		return vec4(float(v) * (1.0f / 255.0f), 0.0f, 0.0f, 1.0f);
	}

	inline void write(const Vulkan::TextureFormatLayout &layout, const uvec2 &coord,
	                  uint32_t layer, uint32_t mip, const vec4 &v) const
	{
		float q = muglm::clamp(muglm::round(v.x * 255.0f), 0.0f, 255.0f);
		*layout.data_generic<uint8_t>(coord.x, coord.y, layer, mip) = uint8_t(q);
	}
};

struct TextureFormatRG8Unorm
{
	inline vec4 sample(const Vulkan::TextureFormatLayout &layout, const uvec2 &coord,
	                   uint32_t layer, uint32_t mip) const
	{
		u8vec2 &v = *layout.data_generic<u8vec2>(coord.x, coord.y, layer, mip);
		return vec4(vec2(v) * (1.0f / 255.0f), 0.0f, 1.0f);
	}

	inline void write(const Vulkan::TextureFormatLayout &layout, const uvec2 &coord,
	                  uint32_t layer, uint32_t mip, const vec4 &v) const
	{
		auto q = clamp(round(v.xy() * 255.0f), vec2(0.0f), vec2(255.0f));
		*layout.data_generic<u8vec2>(coord.x, coord.y, layer, mip) = u8vec2(q);
	}
};

struct TextureFormatRGBA8Unorm
{
	inline vec4 sample(const Vulkan::TextureFormatLayout &layout, const uvec2 &coord,
	                   uint32_t layer, uint32_t mip) const
	{
		u8vec4 &v = *layout.data_generic<u8vec4>(coord.x, coord.y, layer, mip);
		return vec4(v) * (1.0f / 255.0f);
	}

	inline void write(const Vulkan::TextureFormatLayout &layout, const uvec2 &coord,
	                  uint32_t layer, uint32_t mip, const vec4 &v) const
	{
		auto q = clamp(round(v * 255.0f), vec4(0.0f), vec4(255.0f));
		*layout.data_generic<u8vec4>(coord.x, coord.y, layer, mip) = u8vec4(q);
	}
};

struct TextureFormatRGBA8Srgb
{
	static inline float srgb_gamma_to_linear(float v)
	{
		if (v <= 0.04045f)
			return v * (1.0f / 12.92f);
		else
			return muglm::pow((v + 0.055f) / (1.0f + 0.055f), 2.4f);
	}

	static inline float srgb_linear_to_gamma(float v)
	{
		if (v <= 0.0031308f)
			return 12.92f * v;
		else
			return (1.0f + 0.055f) * muglm::pow(v, 1.0f / 2.4f) - 0.055f;
	}

	static inline vec4 srgb_gamma_to_linear(const vec4 &v)
	{
		return vec4(srgb_gamma_to_linear(v.x),
		            srgb_gamma_to_linear(v.y),
		            srgb_gamma_to_linear(v.z),
		            v.w);
	}

	static inline vec4 srgb_linear_to_gamma(const vec4 &v)
	{
		return vec4(srgb_linear_to_gamma(v.x),
		            srgb_linear_to_gamma(v.y),
		            srgb_linear_to_gamma(v.z),
		            v.w);
	}

	inline vec4 sample(const Vulkan::TextureFormatLayout &layout, const uvec2 &coord,
	                   uint32_t layer, uint32_t mip) const
	{
		u8vec4 &v = *layout.data_generic<u8vec4>(coord.x, coord.y, layer, mip);
		return srgb_gamma_to_linear(vec4(v) * (1.0f / 255.0f));
	}

	inline void write(const Vulkan::TextureFormatLayout &layout, const uvec2 &coord,
	                  uint32_t layer, uint32_t mip, const vec4 &v) const
	{
		auto q = clamp(round(srgb_linear_to_gamma(v) * 255.0f), vec4(0.0f), vec4(255.0f));
		*layout.data_generic<u8vec4>(coord.x, coord.y, layer, mip) = u8vec4(q);
	}
};

template <typename Ops>
inline void generate_mipmaps(const Vulkan::TextureFormatLayout &dst_layout,
                             const Vulkan::TextureFormatLayout &layout, const Ops &op)
{
	memcpy(dst_layout.data(0, 0), layout.data(0, 0), dst_layout.get_layer_size(0) * layout.get_layers());

	for (uint32_t level = 1; level < dst_layout.get_levels(); level++)
	{
		auto &dst_mip = dst_layout.get_mip_info(level);
		auto &src_mip = dst_layout.get_mip_info(level - 1);

		uint32_t dst_width = dst_mip.block_row_length;
		uint32_t dst_height = dst_mip.block_image_height;

		uint32_t src_width = src_mip.block_row_length;
		uint32_t src_height = src_mip.block_image_height;
		uvec2 max_coord(src_width - 1u, src_height - 1u);

		float src_width_f = float(src_mip.block_row_length);
		float src_height_f = float(src_mip.block_image_height);

		float rescale_width = src_width_f / float(dst_width);
		float rescale_height = src_height_f / float(dst_height);

		for (uint32_t layer = 0; layer < dst_layout.get_layers(); layer++)
		{
			for (uint32_t y = 0; y < dst_height; y++)
			{
				float coord_y = (float(y) + 0.5f) * rescale_height - 0.5f;
				for (uint32_t x = 0; x < dst_width; x++)
				{
					float coord_x = (float(x) + 0.5f) * rescale_width - 0.5f;
					vec2 base_coord = vec2(coord_x, coord_y);
					vec2 floor_coord = floor(base_coord);
					vec2 uv = base_coord - floor_coord;
					uvec2 c0(floor_coord);
					uvec2 c1 = min(c0 + uvec2(1, 0), max_coord);
					uvec2 c2 = min(c0 + uvec2(0, 1), max_coord);
					uvec2 c3 = min(c0 + uvec2(1, 1), max_coord);

					auto v0 = op.sample(dst_layout, c0, layer, level - 1);
					auto v1 = op.sample(dst_layout, c1, layer, level - 1);
					auto v2 = op.sample(dst_layout, c2, layer, level - 1);
					auto v3 = op.sample(dst_layout, c3, layer, level - 1);

					auto x0 = mix(v0, v1, uv.x);
					auto x1 = mix(v2, v3, uv.x);
					auto filtered = mix(x0, x1, uv.y);
					op.write(dst_layout, uvec2(x, y), layer, level, filtered);
				}
			}
		}
	}
}

static void copy_dimensions(MemoryMappedTexture &mapped, const Vulkan::TextureFormatLayout &layout, MemoryMappedTextureFlags flags, unsigned levels = 0)
{
	switch (layout.get_image_type())
	{
	case VK_IMAGE_TYPE_1D:
		mapped.set_1d(layout.get_format(), layout.get_width(), layout.get_layers(), levels);
		break;

	case VK_IMAGE_TYPE_2D:
		if (flags & MEMORY_MAPPED_TEXTURE_CUBE_MAP_COMPATIBLE_BIT)
			mapped.set_cube(layout.get_format(), layout.get_width(), layout.get_layers() / 6, levels);
		else
			mapped.set_2d(layout.get_format(), layout.get_width(), layout.get_height(), layout.get_layers(), levels);
		break;

	case VK_IMAGE_TYPE_3D:
		throw std::logic_error("3D is not supported for generate_mipmaps.");

	default:
		throw std::logic_error("Unknown image type.");
	}

	mapped.set_flags(flags & ~MEMORY_MAPPED_TEXTURE_GENERATE_MIPMAP_ON_LOAD_BIT);
}

static void generate(const MemoryMappedTexture &mapped, const Vulkan::TextureFormatLayout &layout)
{
	auto &dst_layout = mapped.get_layout();

	switch (layout.get_format())
	{
	case VK_FORMAT_R8_UNORM:
		generate_mipmaps(dst_layout, layout, TextureFormatUnorm8());
		break;

	case VK_FORMAT_R8G8_UNORM:
		generate_mipmaps(dst_layout, layout, TextureFormatRG8Unorm());
		break;

	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_B8G8R8A8_SRGB:
		generate_mipmaps(dst_layout, layout, TextureFormatRGBA8Srgb());
		break;

	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_UNORM:
		generate_mipmaps(dst_layout, layout, TextureFormatRGBA8Unorm());
		break;

	default:
		throw std::logic_error("Unsupported format for generate_mipmaps.");
	}
}

template <typename Ops>
inline void fixup_edges(const Vulkan::TextureFormatLayout &dst_layout,
                        const Vulkan::TextureFormatLayout &layout, const Ops &op)
{
	for (uint32_t layer = 0; layer < dst_layout.get_layers(); layer++)
	{
		for (uint32_t level = 0; level < dst_layout.get_levels(); level++)
		{
			auto &mip = dst_layout.get_mip_info(level);
			int width = mip.block_row_length;
			int height = mip.block_image_height;
			ivec2 max_coord(width - 1, height - 1);

			for (int y = 0; y < height; y++)
			{
				for (int x = 0; x < width; x++)
				{
					vec4 source = op.sample(layout, uvec2(x, y), layer, level);
					if (source.w == 1.0f)
					{
						op.write(dst_layout, uvec2(x, y), layer, level, source);
					}
					else
					{
						vec3 rgb = vec3(0.0f);
						float w = 0.0f;
						for (int off_y = -1; off_y <= 1; off_y++)
						{
							for (int off_x = -1; off_x <= 1; off_x++)
							{
								if (off_x == 0 && off_y == 0)
									continue;

								auto coord = uvec2(clamp(ivec2(x + off_x, y + off_y), ivec2(0), max_coord));
								vec4 v = op.sample(layout, coord, layer, level);
								rgb += v.xyz() * v.w;
								w += v.w;
							}
						}

						rgb *= 1.0f / muglm::max(0.0000001f, w);
						vec3 filtered = mix(rgb, source.xyz(), source.w);
						op.write(dst_layout, uvec2(x, y), layer, level, vec4(filtered, source.w));
					}
				}
			}
		}
	}
}

static void fixup_edges(const MemoryMappedTexture &mapped, const Vulkan::TextureFormatLayout &layout)
{
	auto &dst_layout = mapped.get_layout();

	switch (layout.get_format())
	{
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_UNORM:
		fixup_edges(dst_layout, layout, TextureFormatRGBA8Unorm());
		break;

	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_B8G8R8A8_SRGB:
		fixup_edges(dst_layout, layout, TextureFormatRGBA8Srgb());
		break;

	default:
		throw std::logic_error("Unsupported format for fixup_edges.");
	}
}

MemoryMappedTexture generate_mipmaps_to_file(const std::string &path, const Vulkan::TextureFormatLayout &layout, MemoryMappedTextureFlags flags)
{
	MemoryMappedTexture mapped;
	copy_dimensions(mapped, layout, flags);
	if (!mapped.map_write(path))
		return {};
	generate(mapped, layout);
	return mapped;
}

MemoryMappedTexture generate_mipmaps(const Vulkan::TextureFormatLayout &layout, MemoryMappedTextureFlags flags)
{
	MemoryMappedTexture mapped;
	copy_dimensions(mapped, layout, flags);
	if (!mapped.map_write_scratch())
		return {};
	generate(mapped, layout);
	return mapped;
}

MemoryMappedTexture fixup_alpha_edges(const Vulkan::TextureFormatLayout &layout, MemoryMappedTextureFlags flags)
{
	MemoryMappedTexture mapped;
	copy_dimensions(mapped, layout, flags, layout.get_levels());
	if (!mapped.map_write_scratch())
		return {};
	fixup_edges(mapped, layout);
	return mapped;
}

static bool component_is_identity(VkComponentSwizzle swiz, VkComponentSwizzle expected)
{
	return swiz == expected || swiz == VK_COMPONENT_SWIZZLE_IDENTITY;
}

static inline uint16_t swizzle_to_one(uint16_t)
{
	return 0x3c00;
}

static inline uint8_t swizzle_to_one(uint8_t)
{
	return 0xff;
}

template <typename T>
static inline T extract_component(const tvec4<T> &t, int swiz)
{
	if (swiz < 4)
		return t[swiz];
	else if (swiz == 4)
		return swizzle_to_one(T());
	else
		return 0;
}

template <typename T>
static inline T extract_component(const tvec3<T> &t, int swiz)
{
	if (swiz < 3)
		return t[swiz];
	else if (swiz == 3 || swiz == 4)
		return swizzle_to_one(T());
	else
		return 0;
}

template <typename T>
static inline T extract_component(const tvec2<T> &t, int swiz)
{
	if (swiz < 2)
		return t[swiz];
	else if (swiz == 3 || swiz == 4)
		return swizzle_to_one(T());
	else
		return 0;
}

template <typename T>
static inline T extract_component(const T &t, int swiz)
{
	if (swiz < 1)
		return t;
	else if (swiz == 3 || swiz == 4)
		return swizzle_to_one(T());
	else
		return 0;
}

template <typename T>
static inline void swizzle_image_inner(const Vulkan::TextureFormatLayout &layout, ivec4 swizzles)
{
	transform_texture_layout<T>(layout, [swizzles](const T &v) {
		auto r = extract_component(v, swizzles.x);
	    auto g = extract_component(v, swizzles.y);
	    auto b = extract_component(v, swizzles.z);
	    auto a = extract_component(v, swizzles.w);
		return T(r, g, b, a);
	});
}

bool swizzle_image(MemoryMappedTexture &texture, const VkComponentMapping &swizzle)
{
	if (!component_is_identity(swizzle.r, VK_COMPONENT_SWIZZLE_R) ||
		!component_is_identity(swizzle.g, VK_COMPONENT_SWIZZLE_G) ||
		!component_is_identity(swizzle.b, VK_COMPONENT_SWIZZLE_B) ||
		!component_is_identity(swizzle.a, VK_COMPONENT_SWIZZLE_A))
	{
		texture.make_local_copy();

		auto &layout = texture.get_layout();

		ivec4 swizzles = {};
		const auto conv_swizzle = [](VkComponentSwizzle swiz, int identity_component) -> int {
			switch (swiz)
			{
			case VK_COMPONENT_SWIZZLE_IDENTITY:
				return identity_component;
			case VK_COMPONENT_SWIZZLE_R:
				return 0;
			case VK_COMPONENT_SWIZZLE_G:
				return 1;
			case VK_COMPONENT_SWIZZLE_B:
				return 2;
			case VK_COMPONENT_SWIZZLE_A:
				return 3;
			case VK_COMPONENT_SWIZZLE_ONE:
				return 4;
			case VK_COMPONENT_SWIZZLE_ZERO:
				return 5;
			default:
				LOGE("Unrecognized swizzle parameter.");
				return false;
			}
		};

		swizzles.x = conv_swizzle(swizzle.r, 0);
		swizzles.y = conv_swizzle(swizzle.g, 1);
		swizzles.z = conv_swizzle(swizzle.b, 2);
		swizzles.w = conv_swizzle(swizzle.a, 3);

		switch (layout.get_format())
		{
		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_R8G8B8A8_SRGB:
			swizzle_image_inner<u8vec4>(layout, swizzles);
			break;

		case VK_FORMAT_R16G16B16A16_SFLOAT:
			swizzle_image_inner<u16vec4>(layout, swizzles);
			break;

		default:
			LOGE("Unexpected swizzle format %u.\n", unsigned(layout.get_format()));
			return false;
		}
	}

	return true;
}

static TransparencyType check_transparency(const Vulkan::TextureFormatLayout &layout, unsigned layer, unsigned level)
{
	bool non_opaque_pixel = false;
	auto width = layout.get_width();
	auto height = layout.get_height();
	for (unsigned y = 0; y < height; y++)
	{
		for (unsigned x = 0; x < width; x++)
		{
			uint8_t alpha = layout.data_2d<u8vec4>(x, y, layer, level)->w;
			if (alpha != 0xff)
			{
				if (alpha == 0)
					non_opaque_pixel = true;
				else
					return TransparencyType::Floating;
			}
		}
	}

	return non_opaque_pixel ? TransparencyType::Binary : TransparencyType::None;
}

TransparencyType image_slice_contains_transparency(const Vulkan::TextureFormatLayout &layout, unsigned layer, unsigned level)
{
	switch (layout.get_format())
	{
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_SRGB:
		return check_transparency(layout, layer, level);

	default:
		throw std::logic_error("Unsupported format for image_layer_contains_transparency.");
	}
}
}
}
