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

#pragma once

#include <atomic>
#include <vector>
#include <utility>
#include <stddef.h>
#include <assert.h>
#include <memory>
#include <mutex>
#include <condition_variable>

namespace Util
{
// There can only be one concurrent reader, and one concurrent writer.
// This is useful for lock-less messaging between two threads, e.g. a worker thread and master thread.
template <typename T>
class LockFreeRingBuffer
{
public:
	LockFreeRingBuffer()
	{
		reset(1);
		assert(read_count.is_lock_free());
		assert(write_count.is_lock_free());
	}

	void reset(size_t count)
	{
		//assert((count & (count - 1)) == 0);
		ring.resize(count);
		read_count.store(0);
		write_count.store(0);
	}

	size_t read_avail() const noexcept
	{
		return write_count.load(std::memory_order_acquire) -
		       read_count.load(std::memory_order_relaxed);
	}

	size_t write_avail() const noexcept
	{
		return ring.size() -
		       (write_count.load(std::memory_order_relaxed) -
		        read_count.load(std::memory_order_acquire));
	}

	bool write_and_move(T *values, size_t count) noexcept
	{
		size_t current_written = read_count.load(std::memory_order_relaxed);
		size_t current_read = write_count.load(std::memory_order_acquire);
		if (count > ring.size() - (current_written - current_read))
			return false;

		size_t can_write_first = std::min(ring.size() - write_offset, count);
		size_t can_write_second = count - can_write_first;
		std::move(values, values + can_write_first, ring.data() + write_offset);

		write_offset += can_write_first;
		values += can_write_first;
		if (write_offset >= ring.size())
			write_offset -= ring.size();

		std::move(values, values + can_write_second, ring.data());
		write_offset += can_write_second;

		// Need release ordering so the reads here can't be ordered after the store.
		write_count.store(write_count.load(std::memory_order_relaxed) + count, std::memory_order_release);
		return true;
	}

	bool read_and_move(T *values, size_t count) noexcept
	{
		size_t current_read = read_count.load(std::memory_order_relaxed);
		size_t current_written = write_count.load(std::memory_order_acquire);
		if (count > current_written - current_read)
			return false;

		size_t can_read_first = std::min(ring.size() - read_offset, count);
		size_t can_read_second = count - can_read_first;
		std::move(ring.data() + read_offset, ring.data() + read_offset + can_read_first, values);

		read_offset += can_read_first;
		values += can_read_first;
		if (read_offset >= ring.size())
			read_offset -= ring.size();

		std::move(ring.data(), ring.data() + can_read_second, values);
		read_offset += can_read_second;

		// Need release ordering so the reads here can't be ordered after the store.
		read_count.store(read_count.load(std::memory_order_relaxed) + count, std::memory_order_release);
		return true;
	}

	bool write_and_move(T value) noexcept
	{
		return write_and_move(&value, 1);
	}

	bool read_and_move(T &value) noexcept
	{
		return read_and_move(&value, 1);
	}

private:
	std::atomic<size_t> read_count;
	std::atomic<size_t> write_count;
	size_t read_offset = 0;
	size_t write_offset = 0;
	std::vector<T> ring;
};

struct MessageQueuePayloadDeleter
{
	void operator()(void *ptr);
};

class MessageQueuePayload
{
public:
	template <typename T>
	T &as()
	{
		assert(handle);
		return *static_cast<T *>(handle);
	}

	// The handle might be slightly different from payload if we allocated
	// with multiple-inheritance and the base class we care about is not the first one in the inheritance list.
	template <typename T>
	void set_payload_handle(T *t)
	{
		handle = t;
	}

	explicit operator bool() const
	{
		return bool(payload);
	}

	size_t get_size() const
	{
		return payload_size;
	}

	void set_size(size_t size)
	{
		assert(size <= payload_capacity);
		payload_size = size;
	}

	void set_payload_data(void *ptr, size_t size)
	{
		payload.reset(ptr);
		payload_capacity = size;
	}

	void *get_payload_data() const
	{
		return payload.get();
	}

	size_t get_capacity() const
	{
		return payload_capacity;
	}

private:
	std::unique_ptr<void, MessageQueuePayloadDeleter> payload;
	void *handle = nullptr;
	size_t payload_size = 0;
	size_t payload_capacity = 0;
};

class LockFreeMessageQueue
{
public:
	LockFreeMessageQueue();

	MessageQueuePayload allocate_write_payload(size_t size) noexcept;
	bool push_written_payload(MessageQueuePayload payload) noexcept;

	size_t available_read_messages() const noexcept;
	MessageQueuePayload read_message() noexcept;
	void recycle_payload(MessageQueuePayload payload) noexcept;

private:
	LockFreeRingBuffer<MessageQueuePayload> read_ring;
	LockFreeRingBuffer<MessageQueuePayload> write_ring[8];
	size_t payload_capacity[8] = {};
};

class MessageQueue : private LockFreeMessageQueue
{
public:
	MessageQueue();
	void cork();
	void uncork();

	bool is_uncorked() const;

	MessageQueuePayload allocate_write_payload(size_t size) noexcept;
	bool push_written_payload(MessageQueuePayload payload) noexcept;

	size_t available_read_messages() const noexcept;
	MessageQueuePayload read_message() noexcept;
	void recycle_payload(MessageQueuePayload payload) noexcept;

private:
	mutable std::mutex lock;
	mutable std::condition_variable cond;
	std::atomic_bool corked;
};
}