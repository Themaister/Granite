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

#include "pipeline_cache.hpp"
#include "device.hpp"

namespace Vulkan
{
PipelineCache::Binary::Binary(const VkPipelineBinaryKeyKHR &key_, const void *payload_, size_t payload_size_)
	: device(nullptr), key(key_), payload(payload_), payload_size(payload_size_)
{
}

PipelineCache::Binary::Binary(Vulkan::Device &device_, const VkPipelineBinaryKeyKHR &key_, VkPipelineBinaryKHR binary_)
	: device(&device_), key(key_), binary(binary_)
{
	VkPipelineBinaryDataInfoKHR data_info = { VK_STRUCTURE_TYPE_PIPELINE_BINARY_DATA_INFO_KHR };
	VkPipelineBinaryKeyKHR dummy_key = { VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR };
	data_info.pipelineBinary = binary;
	device->get_device_table().vkGetPipelineBinaryDataKHR(device->get_device(), &data_info, &dummy_key, &payload_size, nullptr);
}

PipelineCache::Binary::~Binary()
{
	if (device)
		device->get_device_table().vkDestroyPipelineBinaryKHR(device->get_device(), binary, nullptr);
}

PipelineCache::PipelineCache(Device *device_)
	: device(*device_), new_entries(false)
{
}

PipelineCache::~PipelineCache()
{
}

Util::Hash PipelineCache::get_create_info_key(const void *create_info) const
{
	VkPipelineCreateInfoKHR key_create_info = { VK_STRUCTURE_TYPE_PIPELINE_CREATE_INFO_KHR };
	VkPipelineBinaryKeyKHR global_key = { VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR };
	key_create_info.pNext = const_cast<void *>(create_info);
	if (device.get_device_table().vkGetPipelineKeyKHR(device.get_device(), &key_create_info, &global_key) != VK_SUCCESS)
		return false;

	Util::Hasher h;
	h.data(global_key.key, global_key.keySize);
	return h.get();
}

bool PipelineCache::place_binary(VkPipelineBinaryKHR binary, Util::Hash *hash)
{
	VkPipelineBinaryDataInfoKHR data_info = { VK_STRUCTURE_TYPE_PIPELINE_BINARY_DATA_INFO_KHR };
	VkPipelineBinaryKeyKHR key = { VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR };
	data_info.pipelineBinary = binary;
	size_t data_size = 0;

	if (device.get_device_table().vkGetPipelineBinaryDataKHR(
			device.get_device(), &data_info, &key, &data_size, nullptr) != VK_SUCCESS)
	{
		LOGE("Failed to get pipeline binary key.\n");
		return false;
	}

	VK_ASSERT(key.keySize);

	Util::Hasher h;
	h.data(key.key, key.keySize);
	*hash = h.get();

	static constexpr uint32_t AllZero[VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR] = {};
	if (memcmp(AllZero, key.key, VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR) == 0)
	{
		LOGW("Driver seems broken? Key is all zeros ...\n");
		return false;
	}

	if (binaries.find(h.get()))
		device.get_device_table().vkDestroyPipelineBinaryKHR(device.get_device(), binary, nullptr);
	else
		binaries.emplace_yield(h.get(), device, key, binary);

	return true;
}

void PipelineCache::place_pipeline(Util::Hash hash, VkPipeline pipeline)
{
	const auto release_binaries = [&]()
	{
		VkReleaseCapturedPipelineDataInfoKHR release_info = { VK_STRUCTURE_TYPE_RELEASE_CAPTURED_PIPELINE_DATA_INFO_KHR };
		release_info.pipeline = pipeline;
		device.get_device_table().vkReleaseCapturedPipelineDataKHR(device.get_device(), &release_info, nullptr);
	};

	if (binary_mapping.find(hash) != nullptr)
	{
		release_binaries();
		return;
	}

	VkPipelineBinaryCreateInfoKHR create_info = { VK_STRUCTURE_TYPE_PIPELINE_BINARY_CREATE_INFO_KHR };
	create_info.pipeline = pipeline;
	VkPipelineBinaryHandlesInfoKHR handles_info = { VK_STRUCTURE_TYPE_PIPELINE_BINARY_HANDLES_INFO_KHR };

	if (device.get_device_table().vkCreatePipelineBinariesKHR(
			device.get_device(), &create_info, nullptr, &handles_info) != VK_SUCCESS ||
	    handles_info.pipelineBinaryCount == 0)
	{
		LOGE("Failed to query pipeline binaries from pipeline.\n");
		release_binaries();
		return;
	}

	Util::SmallVector<VkPipelineBinaryKHR> out_binaries(handles_info.pipelineBinaryCount);
	handles_info.pPipelineBinaries = out_binaries.data();

	if (device.get_device_table().vkCreatePipelineBinariesKHR(
			device.get_device(), &create_info, nullptr, &handles_info) != VK_SUCCESS)
	{
		LOGE("Failed to query pipeline binaries from pipeline.\n");
		release_binaries();
		return;
	}

	release_binaries();

	Util::SmallVector<Util::Hash> keys;
	keys.resize(out_binaries.size());
	auto *pkeys = keys.data();

	for (auto &binary : out_binaries)
		if (!place_binary(binary, pkeys++))
			return;

	binary_mapping.emplace_yield(hash, std::move(keys));
	new_entries.store(true, std::memory_order_release);
}

bool PipelineCache::find_pipeline_binaries_from_internal_cache(const void *pso_create_info,
                                                               Util::SmallVector<VkPipelineBinaryKHR> &out_binaries,
                                                               Util::SmallVector<bool> &out_binaries_owned)
{
	out_binaries.clear();
	out_binaries_owned.clear();

	VkPipelineBinaryCreateInfoKHR create_info = { VK_STRUCTURE_TYPE_PIPELINE_BINARY_CREATE_INFO_KHR };
	VkPipelineCreateInfoKHR pipeline_create_info = { VK_STRUCTURE_TYPE_PIPELINE_CREATE_INFO_KHR, const_cast<void *>(pso_create_info) };
	create_info.pPipelineCreateInfo = &pipeline_create_info;

	out_binaries.resize(32);

	// Ideally we don't query twice, just assume we're not going to receive more than 32 binaries in one go.
	// For graphics and compute, this is surely fine ... :')
	VkPipelineBinaryHandlesInfoKHR handles_info = { VK_STRUCTURE_TYPE_PIPELINE_BINARY_HANDLES_INFO_KHR };
	handles_info.pPipelineBinaries = out_binaries.data();
	handles_info.pipelineBinaryCount = 32;

	auto result = device.get_device_table().vkCreatePipelineBinariesKHR(
			device.get_device(), &create_info, nullptr, &handles_info);

	out_binaries.resize(handles_info.pipelineBinaryCount);

	if (result != VK_SUCCESS)
	{
		for (auto &b : out_binaries)
			device.get_device_table().vkDestroyPipelineBinaryKHR(device.get_device(), b, nullptr);
		out_binaries.clear();
		return false;
	}

	for (uint32_t i = 0; i < handles_info.pipelineBinaryCount; i++)
		out_binaries_owned.push_back(true);

	return true;
}

bool PipelineCache::find_pipeline_binaries(Util::Hash pso_hash,
                                           Util::SmallVector<VkPipelineBinaryKHR> &out_binaries,
                                           Util::SmallVector<bool> &out_binaries_owned)
{
	auto *mapped = binary_mapping.find(pso_hash);
	if (!mapped)
		return false;

	out_binaries.clear();
	out_binaries_owned.clear();

	for (auto &hash : mapped->hashes)
	{
		auto *existing_binary = binaries.find(hash);

		if (!existing_binary)
		{
			for (auto &binary: out_binaries)
				device.get_device_table().vkDestroyPipelineBinaryKHR(device.get_device(), binary, nullptr);
			out_binaries.clear();
			return false;
		}

		VkPipelineBinaryKHR binary = VK_NULL_HANDLE;

		if (existing_binary->binary)
		{
			binary = existing_binary->binary;
			out_binaries_owned.push_back(false);
		}
		else
		{
			VkPipelineBinaryCreateInfoKHR create_info = { VK_STRUCTURE_TYPE_PIPELINE_BINARY_CREATE_INFO_KHR };
			VkPipelineBinaryKeysAndDataKHR keys_and_data_info = {};
			VkPipelineBinaryDataKHR binary_data = {};
			keys_and_data_info.binaryCount = 1;
			keys_and_data_info.pPipelineBinaryKeys = &existing_binary->key;
			VK_ASSERT(existing_binary->key.keySize);
			keys_and_data_info.pPipelineBinaryData = &binary_data;
			create_info.pKeysAndDataInfo = &keys_and_data_info;
			binary_data.pData = const_cast<void *>(existing_binary->payload);
			binary_data.dataSize = existing_binary->payload_size;

			VkPipelineBinaryHandlesInfoKHR handles_info = { VK_STRUCTURE_TYPE_PIPELINE_BINARY_HANDLES_INFO_KHR };
			handles_info.pPipelineBinaries = &binary;
			handles_info.pipelineBinaryCount = 1;

			if (device.get_device_table().vkCreatePipelineBinariesKHR(
					device.get_device(), &create_info, nullptr, &handles_info) != VK_SUCCESS ||
			    handles_info.pipelineBinaryCount != 1 ||
			    handles_info.pPipelineBinaries[0] == VK_NULL_HANDLE)
			{
				for (auto &b : out_binaries)
					device.get_device_table().vkDestroyPipelineBinaryKHR(device.get_device(), b, nullptr);
				out_binaries.clear();
				return false;
			}
		}

		out_binaries_owned.push_back(existing_binary->binary == VK_NULL_HANDLE);
		out_binaries.push_back(binary);
	}

	return true;
}

bool PipelineCache::init_from_payload(const void *payload, size_t size, bool persistent_mapping)
{
	if (!size)
		return true;

	if (!persistent_mapping)
	{
		payload_holder.reset(new uint8_t[size]);
		memcpy(payload_holder.get(), payload, size);
		payload = payload_holder.get();
	}

	if (!parse(payload, size))
		return false;

	return true;
}

static constexpr char CacheUUID[VK_UUID_SIZE] = "GraniteBinary1";

bool PipelineCache::parse(const void *payload_, size_t size)
{
	if (!device.get_device_features().pipeline_binary_features.pipelineBinaries)
		return false;

	constexpr size_t minimum_size = VK_UUID_SIZE + sizeof(uint32_t) +
	                                VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR + sizeof(uint32_t);
	if (size < minimum_size)
		return false;

	auto *payload = static_cast<const uint8_t *>(payload_);

	if (memcmp(payload, CacheUUID, sizeof(CacheUUID)) != 0)
		return false;
	payload += VK_UUID_SIZE;

	VkPipelineBinaryKeyKHR key = { VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR };
	device.get_device_table().vkGetPipelineKeyKHR(device.get_device(), nullptr, &key);

	if (memcmp(payload, &key.keySize, sizeof(uint32_t)) != 0)
	{
		LOGW("Pipeline binary global key changed, resetting the cache ...\n");
		return true;
	}

	payload += sizeof(uint32_t);

	if (memcmp(payload, key.key, key.keySize) != 0)
	{
		LOGW("Pipeline binary global key changed, resetting the cache ...\n");
		return true;
	}
	payload += VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR;

	uint32_t num_pipelines = *reinterpret_cast<const uint32_t *>(payload);
	payload += sizeof(uint32_t);
	auto *payload64 = reinterpret_cast<const uint64_t *>(payload);
	size -= minimum_size;

	const auto read_u64 = [&]() -> uint64_t {
		if (size >= sizeof(uint64_t))
		{
			auto data = *payload64++;
			size -= sizeof(uint64_t);
			return data;
		}
		else
			return 0;
	};

	for (uint32_t i = 0; i < num_pipelines; i++)
	{
		Util::SmallVector<Util::Hash> hashes;
		auto hash = read_u64();
		auto num_hashes = uint32_t(read_u64());
		for (uint32_t j = 0; j < num_hashes; j++)
			hashes.push_back(read_u64());
		binary_mapping.emplace_yield(hash, std::move(hashes));
	}

	auto num_binaries = uint32_t(read_u64());
	for (uint32_t i = 0; i < num_binaries; i++)
	{
		auto hash = read_u64();

		union
		{
			struct
			{
				uint32_t size;
				uint32_t key_size;
			};
			uint64_t word;
		} u;
		u.word = read_u64();

		if (size < VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR)
			return false;

		key.keySize = u.key_size;
		memcpy(key.key, payload64, sizeof(key.key));
		payload64 += VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR / sizeof(uint64_t);
		size -= VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR;

		auto padded_size = (u.size + sizeof(uint64_t) - 1) & ~(sizeof(uint64_t) - 1);

		if (size < padded_size)
			return false;

		binaries.emplace_yield(hash, key, payload64, u.size);
		payload64 += padded_size / sizeof(uint64_t);
		size -= padded_size;
	}

	if (size == 0)
		LOGI("Successfully parsed %u pipelines and %u binary blobs.\n", num_pipelines, num_binaries);

	return size == 0;
}

bool PipelineCache::has_new_binary_entries() const
{
	return new_entries.load(std::memory_order_acquire);
}

size_t PipelineCache::get_serialized_size() const
{
	// Granite's magic UUID.
	size_t size = VK_UUID_SIZE;

	// Driver's global key.
	size += sizeof(uint32_t);
	size += VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR;

	// Pipeline number count.
	size += sizeof(uint32_t);

	for (auto &mapping : binary_mapping.get_thread_unsafe())
	{
		// Count + Keys per pipeline.
		size += sizeof(Util::Hash) + sizeof(uint64_t) + mapping.hashes.size() * sizeof(Util::Hash);
	}

	// Binary count.
	size += sizeof(uint64_t);

	for (auto &binary : binaries.get_thread_unsafe())
	{
		size += sizeof(Util::Hash); // Hash
		size += sizeof(uint32_t); // Size
		size += sizeof(uint32_t); // Key size
		size += VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR;
		size += (binary.payload_size + 7) & ~size_t(7); // Padded payload
	}

	return size;
}

bool PipelineCache::serialize(void *data_, size_t size) const
{
	if (size < get_serialized_size())
		return false;

	auto *data = static_cast<uint8_t *>(data_);
	memcpy(data, CacheUUID, sizeof(CacheUUID));
	data += VK_UUID_SIZE;

	VkPipelineBinaryKeyKHR key = { VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR };
	device.get_device_table().vkGetPipelineKeyKHR(device.get_device(), nullptr, &key);
	memcpy(data, &key.keySize, sizeof(key.keySize));
	data += sizeof(uint32_t);
	memcpy(data, key.key, sizeof(key.key));
	data += sizeof(key.key);

	uint32_t pipeline_count = 0;
	for (auto &mapping : binary_mapping.get_thread_unsafe())
	{
		(void)mapping;
		pipeline_count++;
	}

	memcpy(data, &pipeline_count, sizeof(pipeline_count));
	data += sizeof(uint32_t);

	auto *data64 = reinterpret_cast<uint64_t *>(data);

	for (auto &mapping : binary_mapping.get_thread_unsafe())
	{
		*data64++ = mapping.get_hash();
		*data64++ = mapping.hashes.size();
		for (auto &hash : mapping.hashes)
			*data64++ = hash;
	}

	uint32_t binary_count = 0;
	for (auto &mapping : binaries.get_thread_unsafe())
	{
		(void)mapping;
		binary_count++;
	}

	*data64++ = binary_count;
	for (auto &mapping : binaries.get_thread_unsafe())
	{
		*data64++ = mapping.get_hash();
		const uint32_t words[] = { uint32_t(mapping.payload_size), mapping.key.keySize };
		memcpy(data64, words, sizeof(words));
		data64++;
		memcpy(data64, mapping.key.key, sizeof(mapping.key.key));
		data64 += VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR / sizeof(uint64_t);

		VK_ASSERT(mapping.binary || mapping.payload);

		if (mapping.binary)
		{
			// TODO: Ignore compressed property for now.
			VkPipelineBinaryDataInfoKHR data_info = { VK_STRUCTURE_TYPE_PIPELINE_BINARY_DATA_INFO_KHR };
			VkPipelineBinaryKeyKHR dummy_key = { VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR };
			data_info.pipelineBinary = mapping.binary;
			size_t payload_size = mapping.payload_size;
			device.get_device_table().vkGetPipelineBinaryDataKHR(device.get_device(), &data_info, &dummy_key,
			                                                     &payload_size, data64);
		}
		else
		{
			memcpy(data64, mapping.payload, mapping.payload_size);
		}

		data64 += (mapping.payload_size + sizeof(uint64_t) - 1) / sizeof(uint64_t);
	}

	LOGI("Serialized %u pipelines and %u binary blobs.\n", pipeline_count, binary_count);
	return true;
}

template <typename T>
static inline const T *find_pnext(VkStructureType type, const void *pNext)
{
	while (pNext != nullptr)
	{
		auto *sin = static_cast<const VkBaseInStructure *>(pNext);
		if (sin->sType == type)
			return static_cast<const T*>(pNext);

		pNext = sin->pNext;
	}

	return nullptr;
}

VkPipeline PipelineCache::create_pipeline_and_place(Util::Hash pso_key, void *plain_info)
{
	auto *graphics_info = static_cast<VkGraphicsPipelineCreateInfo *>(plain_info);
	auto *compute_info = static_cast<VkComputePipelineCreateInfo *>(plain_info);
	if (graphics_info && graphics_info->sType != VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO)
		graphics_info = nullptr;
	if (compute_info && compute_info->sType != VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO)
		compute_info = nullptr;

	VkPipelineCreateFlags2CreateInfoKHR flags2 = { VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO_KHR };
	VkPipeline pipe = VK_NULL_HANDLE;

	if (!device.get_device_features().pipeline_binary_properties.pipelineBinaryPrefersInternalCache)
	{
		auto *existing_flags2 = find_pnext<VkPipelineCreateFlags2CreateInfoKHR>(
				VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO_KHR, plain_info);

		if (existing_flags2)
		{
			const_cast<VkPipelineCreateFlags2CreateInfoKHR *>(existing_flags2)->flags |=
					VK_PIPELINE_CREATE_2_CAPTURE_DATA_BIT_KHR;
		}
		else
		{
			flags2.flags = VK_PIPELINE_CREATE_2_CAPTURE_DATA_BIT_KHR;

			if (graphics_info)
			{
				flags2.flags |= graphics_info->flags;
				flags2.pNext = graphics_info->pNext;
				graphics_info->pNext = &flags2;
			}
			else if (compute_info)
			{
				flags2.flags |= compute_info->flags;
				flags2.pNext = compute_info->pNext;
				compute_info->pNext = &flags2;
			}
		}
	}

	if ((compute_info && device.get_device_table().vkCreateComputePipelines(
			device.get_device(), VK_NULL_HANDLE, 1, compute_info, nullptr, &pipe) != VK_SUCCESS) ||
	    (graphics_info && device.get_device_table().vkCreateGraphicsPipelines(
			device.get_device(), VK_NULL_HANDLE, 1, graphics_info, nullptr, &pipe) != VK_SUCCESS))
	{
		LOGE("Failed to create pipeline from binaries.\n");
		pipe = VK_NULL_HANDLE;
	}

	if (!device.get_device_features().pipeline_binary_properties.pipelineBinaryPrefersInternalCache &&
	    pipe != VK_NULL_HANDLE)
	{
		place_pipeline(pso_key, pipe);
	}

	return pipe;
}

VkPipeline
PipelineCache::create_pipeline_from_binaries(
		void *plain_info, const VkPipelineBinaryKHR *found_binaries,
		const bool *binaries_owned, size_t binary_count)
{
	auto *graphics_info = static_cast<VkGraphicsPipelineCreateInfo *>(plain_info);
	auto *compute_info = static_cast<VkComputePipelineCreateInfo *>(plain_info);
	if (graphics_info && graphics_info->sType != VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO)
		graphics_info = nullptr;
	if (compute_info && compute_info->sType != VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO)
		compute_info = nullptr;

	// Cache hit :3
	VkPipelineBinaryInfoKHR binary_info = { VK_STRUCTURE_TYPE_PIPELINE_BINARY_INFO_KHR };
	binary_info.pPipelineBinaries = found_binaries;
	binary_info.binaryCount = binary_count;

	constexpr VkPipelineCreateFlags invalid_flags =
			VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT |
			VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT;

	if (compute_info)
	{
		compute_info->stage.module = VK_NULL_HANDLE;
		binary_info.pNext = compute_info->pNext;
		compute_info->pNext = &binary_info;
		compute_info->flags &= ~invalid_flags;
	}
	else if (graphics_info)
	{
		for (uint32_t i = 0; i < graphics_info->stageCount; i++)
			const_cast<VkPipelineShaderStageCreateInfo &>(graphics_info->pStages[i]).module = VK_NULL_HANDLE;
		binary_info.pNext = graphics_info->pNext;
		graphics_info->pNext = &binary_info;
		graphics_info->flags &= ~invalid_flags;
	}

	VkPipeline pipe = VK_NULL_HANDLE;

	if ((compute_info && device.get_device_table().vkCreateComputePipelines(
			device.get_device(), VK_NULL_HANDLE, 1, compute_info, nullptr, &pipe) != VK_SUCCESS) ||
	    (graphics_info && device.get_device_table().vkCreateGraphicsPipelines(
			    device.get_device(), VK_NULL_HANDLE, 1, graphics_info, nullptr, &pipe) != VK_SUCCESS))
	{
		LOGE("Failed to create pipeline from binaries.\n");
		pipe = VK_NULL_HANDLE;
	}

	for (size_t i = 0; i < binary_count; i++)
		if (binaries_owned[i])
			device.get_device_table().vkDestroyPipelineBinaryKHR(device.get_device(), found_binaries[i], nullptr);

	return pipe;
}

VkResult PipelineCache::create_pipeline(void *plain_info, VkPipelineCache cache, VkPipeline *pipe)
{
	*pipe = VK_NULL_HANDLE;
	auto *graphics_info = static_cast<VkGraphicsPipelineCreateInfo *>(plain_info);
	auto *compute_info = static_cast<VkComputePipelineCreateInfo *>(plain_info);
	if (graphics_info && graphics_info->sType != VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO)
		graphics_info = nullptr;
	if (compute_info && compute_info->sType != VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO)
		compute_info = nullptr;

	if (!device.get_device_features().pipeline_binary_features.pipelineBinaries)
	{
		if (compute_info)
		{
			return device.get_device_table().vkCreateComputePipelines(
					device.get_device(), cache, 1, compute_info, nullptr, pipe);
		}
		else if (graphics_info)
		{
			return device.get_device_table().vkCreateGraphicsPipelines(
					device.get_device(), cache, 1, graphics_info, nullptr, pipe);
		}
		else
			return VK_ERROR_INITIALIZATION_FAILED;
	}

	auto pso_key = get_create_info_key(plain_info);
	Util::SmallVector<VkPipelineBinaryKHR> pipeline_binaries;
	Util::SmallVector<bool> pipeline_binaries_owned;

	if (find_pipeline_binaries(pso_key, pipeline_binaries, pipeline_binaries_owned))
	{
		*pipe = create_pipeline_from_binaries(plain_info, pipeline_binaries.data(), pipeline_binaries_owned.data(),
		                                      pipeline_binaries.size());
		return *pipe ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
	}

	if (device.get_device_features().pipeline_binary_properties.pipelineBinaryInternalCache &&
	    !device.get_device_features().pipeline_binary_internal_cache_control.disableInternalCache &&
		find_pipeline_binaries_from_internal_cache(plain_info, pipeline_binaries, pipeline_binaries_owned))
	{
		*pipe = create_pipeline_from_binaries(plain_info, pipeline_binaries.data(), pipeline_binaries_owned.data(),
		                                      pipeline_binaries.size());
		return *pipe ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
	}

	if (graphics_info && (graphics_info->flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT) != 0)
		return VK_PIPELINE_COMPILE_REQUIRED;
	if (compute_info && (compute_info->flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT) != 0)
		return VK_PIPELINE_COMPILE_REQUIRED;

	*pipe = create_pipeline_and_place(pso_key, plain_info);
	return *pipe ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}
}
