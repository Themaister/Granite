/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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

#include "vulkan_headers.hpp"
#include "vulkan_common.hpp"
#include "object_pool.hpp"
#include <functional>

namespace Vulkan
{
class Device;

class PerformanceQueryPool
{
public:
	void init_device(Device *device, uint32_t queue_family_index);
	~PerformanceQueryPool();
	bool init_counters(const std::vector<std::string> &enable_counter_names);

	void begin_command_buffer(VkCommandBuffer cmd);
	void end_command_buffer(VkCommandBuffer cmd);

	void report();

	uint32_t get_num_counters() const;
	const VkPerformanceCounterKHR *get_available_counters() const;
	const VkPerformanceCounterDescriptionKHR *get_available_counter_descs() const;

	static void log_available_counters(const VkPerformanceCounterKHR *counters,
	                                   const VkPerformanceCounterDescriptionKHR *descs,
	                                   uint32_t count);

private:
	Device *device = nullptr;
	uint32_t queue_family_index = 0;
	VkQueryPool pool = VK_NULL_HANDLE;
	std::vector<VkPerformanceCounterResultKHR> results;
	std::vector<VkPerformanceCounterKHR> counters;
	std::vector<VkPerformanceCounterDescriptionKHR> counter_descriptions;
	std::vector<uint32_t> active_indices;
};

class QueryPoolResult;

struct QueryPoolResultDeleter
{
	void operator()(QueryPoolResult *query);
};

class QueryPoolResult : public Util::IntrusivePtrEnabled<QueryPoolResult, QueryPoolResultDeleter, HandleCounter>
{
public:
	friend struct QueryPoolResultDeleter;

	void signal_timestamp_ticks(uint64_t ticks)
	{
		timestamp_ticks = ticks;
		has_timestamp = true;
	}

	uint64_t get_timestamp_ticks() const
	{
		return timestamp_ticks;
	}

	bool is_signalled() const
	{
		return has_timestamp;
	}

	bool is_device_timebase() const
	{
		return device_timebase;
	}

private:
	friend class Util::ObjectPool<QueryPoolResult>;

	explicit QueryPoolResult(Device *device_, bool device_timebase_)
		: device(device_), device_timebase(device_timebase_)
	{}

	Device *device;
	uint64_t timestamp_ticks = 0;
	bool has_timestamp = false;
	bool device_timebase = false;
};

using QueryPoolHandle = Util::IntrusivePtr<QueryPoolResult>;

class QueryPool
{
public:
	explicit QueryPool(Device *device);

	~QueryPool();

	void begin();

	QueryPoolHandle write_timestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage);

private:
	Device *device;
	const VolkDeviceTable &table;

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

	void add_pool();

	bool supports_timestamp = false;
};

class TimestampInterval : public Util::IntrusiveHashMapEnabled<TimestampInterval>
{
public:
	explicit TimestampInterval(std::string tag);

	void accumulate_time(double t);
	double get_time_per_iteration() const;
	double get_time_per_accumulation() const;
	const std::string &get_tag() const;
	void mark_end_of_frame_context();

	double get_total_time() const;
	uint64_t get_total_frame_iterations() const;
	uint64_t get_total_accumulations() const;
	void reset();

private:
	std::string tag;
	double total_time = 0.0;
	uint64_t total_frame_iterations = 0;
	uint64_t total_accumulations = 0;
};

struct TimestampIntervalReport
{
	double time_per_accumulation;
	double time_per_frame_context;
	double accumulations_per_frame_context;
};

using TimestampIntervalReportCallback = std::function<void (const std::string &, const TimestampIntervalReport &)>;

class TimestampIntervalManager
{
public:
	TimestampInterval *get_timestamp_tag(const char *tag);
	void mark_end_of_frame_context();
	void reset();
	void log_simple(const TimestampIntervalReportCallback &func = {}) const;

private:
	Util::IntrusiveHashMap<TimestampInterval> timestamps;
};
}
