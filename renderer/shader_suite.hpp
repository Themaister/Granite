/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#pragma once

#include "shader_manager.hpp"
#include "intrusive_hash_map.hpp"
#include "mesh.hpp"

namespace Granite
{
// A variant signature key is essentially a signature of all unique renderable types.
struct VariantSignatureKey
{
	union
	{
		struct
		{
			DrawPipelineCoverage coverage;
			unsigned char attribute_mask, texture_mask, variant_id;
		} flags;
		// This can also be used as the hash value.
		uint32_t word;
	};

	static inline VariantSignatureKey build(DrawPipelineCoverage coverage,
	                                        uint32_t attribute_mask,
	                                        uint32_t texture_mask,
	                                        uint32_t variant_id = 0)
	{
		VK_ASSERT(attribute_mask <= 0xff);
		VK_ASSERT(texture_mask <= 0xff);
		VK_ASSERT(variant_id <= 0xff);
		VariantSignatureKey key = {};
		key.flags.coverage = coverage;
		key.flags.attribute_mask = attribute_mask;
		key.flags.texture_mask = texture_mask;
		key.flags.variant_id = variant_id;
		return key;
	}

	static inline VariantSignatureKey build(DrawPipeline coverage,
	                                        uint32_t attribute_mask,
	                                        uint32_t texture_mask,
	                                        uint32_t variant_id = 0)
	{
		return build(coverage == DrawPipeline::AlphaTest ? DrawPipelineCoverage::Modifies : DrawPipelineCoverage::Full,
		             attribute_mask, texture_mask, variant_id);
	}
};
static_assert(sizeof(VariantSignatureKey) == sizeof(uint32_t), "Signature key is not packed in u32.");

class ShaderSuite
{
public:
	void init_graphics(Vulkan::ShaderManager *manager, const std::string &vertex, const std::string &fragment);
	void init_compute(Vulkan::ShaderManager *manager, const std::string &compute);

	Vulkan::Program *get_program(VariantSignatureKey signature);

	std::vector<std::pair<std::string, int>> &get_base_defines()
	{
		return base_defines;
	}

	void bake_base_defines();
	void promote_read_write_cache_to_read_only();

	struct VariantSignature : Util::IntrusiveHashMapEnabled<VariantSignature>
	{
		explicit VariantSignature(const VariantSignatureKey &key_) : key(key_) {}
		VariantSignatureKey key;
	};

	// Can be used for serialization, and the variant map can be pre-warmed using known signatures.
	const Util::ThreadSafeIntrusiveHashMap<VariantSignature> &get_variant_signatures() const;

private:
	Util::Hash base_define_hash = 0;
	Vulkan::ShaderManager *manager = nullptr;
	Vulkan::ShaderProgram *program = nullptr;

	struct Variant : Util::IntrusiveHashMapEnabled<Variant>
	{
		Variant(Vulkan::Program *cached_program_, Vulkan::ShaderProgramVariant *indirect_variant_)
		    : cached_program(cached_program_), indirect_variant(indirect_variant_)
		{}
		Vulkan::Program *cached_program;
		Vulkan::ShaderProgramVariant *indirect_variant;
	};
	Util::ThreadSafeIntrusiveHashMapReadCached<Variant> variants;
	std::vector<std::pair<std::string, int>> base_defines;

	Util::ThreadSafeIntrusiveHashMap<VariantSignature> variant_signature_cache;
	void register_variant_signature(const VariantSignatureKey &key);
};
}