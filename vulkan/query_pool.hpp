/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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

#include "vulkan.hpp"
#include "vulkan_common.hpp"
#include "object_pool.hpp"

namespace Vulkan
{
class Device;
class QueryPoolResult;

struct QueryPoolResultDeleter
{
	void operator()(QueryPoolResult *query);
};

class QueryPoolResult : public Util::IntrusivePtrEnabled<QueryPoolResult, QueryPoolResultDeleter, HandleCounter>
{
public:
	friend struct QueryPoolResultDeleter;

	void signal_timestamp(double timestamp)
	{
		this->timestamp = timestamp;
		has_timestamp = true;
	}

	double get_timestamp() const
	{
		return timestamp;
	}

	bool is_signalled() const
	{
		return has_timestamp;
	}

private:
	friend class Util::ObjectPool<QueryPoolResult>;
	QueryPoolResult(Device *device) : device(device)
	{}

	Device *device;
	double timestamp = 0.0;
	bool has_timestamp = false;
};
using QueryPoolHandle = Util::IntrusivePtr<QueryPoolResult>;

class QueryPool
{
public:
	QueryPool(Device *device);
	~QueryPool();

	void begin();
	QueryPoolHandle write_timestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage);

private:
	Device *device;

	struct Pool
	{
		VkQueryPool pool = VK_NULL_HANDLE;
		std::vector<uint64_t> query_results;
		std::vector<QueryPoolHandle> cookies;
		unsigned index = 0;
		unsigned size = 0;
	};
	std::vector<Pool> pools;
	unsigned pool_index = 0;
	double query_period = 0.0;

	void add_pool();
	void flush_callbacks();
	bool supports_timestamp = false;
};
}
