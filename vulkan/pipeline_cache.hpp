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

#include <stddef.h>
#include "vulkan_headers.hpp"
#include "small_vector.hpp"
#include "intrusive_hash_map.hpp"
#include <memory>
#include <utility>

namespace Vulkan
{
class Device;
class PipelineCache
{
public:
	explicit PipelineCache(Device *device);
	~PipelineCache();

	bool init_from_payload(const void *payload, size_t size, bool persistent_mapping);
	bool has_new_binary_entries() const;
	size_t get_serialized_size() const;
	bool serialize(void *data, size_t size) const;

	VkResult create_pipeline(void *info, VkPipelineCache cache, VkPipeline *pipe);

private:
	Device &device;
	std::unique_ptr<uint8_t []> payload_holder;

	struct PipelineBinaryMapping : Util::IntrusiveHashMapEnabled<PipelineBinaryMapping>
	{
		explicit PipelineBinaryMapping(Util::SmallVector<Util::Hash> hashes_)
			: hashes(std::move(hashes_)) {}
		Util::SmallVector<Util::Hash> hashes;
	};
	Util::ThreadSafeIntrusiveHashMap<PipelineBinaryMapping> binary_mapping;

	struct Binary : Util::IntrusiveHashMapEnabled<Binary>
	{
		Binary(Device &device, const VkPipelineBinaryKeyKHR &key, VkPipelineBinaryKHR binary);
		Binary(const VkPipelineBinaryKeyKHR &key, const void *payload, size_t payload_size);
		~Binary();

		Device *device;
		VkPipelineBinaryKeyKHR key = { VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR };
		VkPipelineBinaryKHR binary = VK_NULL_HANDLE;
		const void *payload = nullptr;
		size_t payload_size = 0;
	};
	Util::ThreadSafeIntrusiveHashMap<Binary> binaries;

	bool place_binary(VkPipelineBinaryKHR binary, Util::Hash *hash);
	bool parse(const void *payload, size_t size);
	std::atomic_bool new_entries;

	Util::Hash get_create_info_key(const void *create_info) const;
	bool find_pipeline_binaries(Util::Hash hash,
	                            Util::SmallVector<VkPipelineBinaryKHR> &binaries,
	                            Util::SmallVector<bool> &binaries_owned);

	bool find_pipeline_binaries_from_internal_cache(const void *create_info,
	                                                Util::SmallVector<VkPipelineBinaryKHR> &binaries,
	                                                Util::SmallVector<bool> &binaries_owned);

	void place_pipeline(Util::Hash hash, VkPipeline pipeline);
	VkPipeline create_pipeline_from_binaries(
			void *info, const VkPipelineBinaryKHR *binaries, const bool *binaries_owned, size_t binary_count);
	VkPipeline create_pipeline_and_place(Util::Hash pso_key, void *info);
};
}