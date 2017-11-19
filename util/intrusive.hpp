/* Copyright (c) 2017 Hans-Kristian Arntzen
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
#include <utility>
#include <memory>
#include <atomic>
#include <type_traits>

namespace Util
{
class SingleThreadCounter
{
public:
	inline void add_ref()
	{
		count++;
	}

	inline bool release()
	{
		return --count == 0;
	}

private:
	size_t count = 1;
};

class MultiThreadCounter
{
public:
	MultiThreadCounter()
	{
		count.store(1, std::memory_order_relaxed);
	}

	inline void add_ref()
	{
		count.fetch_add(1, std::memory_order_relaxed);
	}

	inline bool release()
	{
		auto result = count.fetch_sub(1, std::memory_order_acq_rel);
		return result == 1;
	}

private:
	std::atomic_size_t count;
};

template <typename T, typename U>
struct PointerCompare
{
	inline static bool notequal(const T *, const U *)
	{
		return true;
	}
};

template <typename T>
struct PointerCompare<T, T>
{
	inline static bool notequal(const T *a, const T *b)
	{
		return a != b;
	}
};

template <typename T, typename Deleter, typename ReferenceOps>
class IntrusivePtr;

template <typename T, typename Deleter = std::default_delete<T>, typename ReferenceOps = SingleThreadCounter>
class IntrusivePtrEnabled
{
public:
	using IntrusivePtrType = IntrusivePtr<T, Deleter, ReferenceOps>;
	using EnabledBase = T;
	using EnabledDeleter = Deleter;
	using EnabledReferenceOp = ReferenceOps;

	void release_reference()
	{
		if (reference_count.release())
			Deleter()(static_cast<T *>(this));
	}

	void add_reference()
	{
		reference_count.add_ref();
	}

	IntrusivePtrEnabled() = default;

	IntrusivePtrEnabled(const IntrusivePtrEnabled &) = delete;

	void operator=(const IntrusivePtrEnabled &) = delete;

protected:
	Util::IntrusivePtr<T, Deleter, ReferenceOps> reference_from_this();

private:
	ReferenceOps reference_count;
};

template <typename T, typename Deleter = std::default_delete<T>, typename ReferenceOps = SingleThreadCounter>
class IntrusivePtr
{
public:
	template <typename U, typename UDel, typename URef>
	friend class IntrusivePtr;

	IntrusivePtr() = default;

	explicit IntrusivePtr(T *handle)
		: data(handle)
	{
	}

	T &operator*()
	{
		return *data;
	}

	const T &operator*() const
	{
		return *data;
	}

	T *operator->()
	{
		return data;
	}

	const T *operator->() const
	{
		return data;
	}

	explicit operator bool() const
	{
		return data != nullptr;
	}

	bool operator==(const IntrusivePtr &other) const
	{
		return data == other.data;
	}

	bool operator!=(const IntrusivePtr &other) const
	{
		return data != other.data;
	}

#if 0
	template <typename U>
	bool operator==(const IntrusivePtr<U, Deleter, ReferenceOps> &other) const
	{
		return data == static_cast<const T *>(other.data);
	}

	template <typename U>
	bool operator!=(const IntrusivePtr<U, Deleter, ReferenceOps> &other) const
	{
		return data != static_cast<const T *>(other.data);
	}
#endif

	T *get()
	{
		return data;
	}

	const T *get() const
	{
		return data;
	}

	void reset()
	{
		using ReferenceBase = IntrusivePtrEnabled<
				typename T::EnabledBase,
				typename T::EnabledDeleter,
				typename T::EnabledReferenceOp>;

		// Static up-cast here to avoid potential issues with multiple intrusive inheritance.
		// Also makes sure that the pointer type actually inherits from this type.
		if (data)
			static_cast<ReferenceBase *>(data)->release_reference();
		data = nullptr;
	}

	template <typename U>
	IntrusivePtr &operator=(const IntrusivePtr<U, Deleter, ReferenceOps> &other)
	{
		static_assert(std::is_base_of<T, U>::value,
		              "Cannot safely assign downcasted intrusive pointers.");

		using ReferenceBase = IntrusivePtrEnabled<
				typename T::EnabledBase,
				typename T::EnabledDeleter,
				typename T::EnabledReferenceOp>;

		reset();
		data = static_cast<T *>(other.data);

		// Static up-cast here to avoid potential issues with multiple intrusive inheritance.
		// Also makes sure that the pointer type actually inherits from this type.
		if (data)
			static_cast<ReferenceBase *>(data)->add_reference();
		return *this;
	}

	IntrusivePtr &operator=(const IntrusivePtr &other)
	{
		using ReferenceBase = IntrusivePtrEnabled<
				typename T::EnabledBase,
				typename T::EnabledDeleter,
				typename T::EnabledReferenceOp>;

		if (this != &other)
		{
			reset();
			data = other.data;
			if (data)
				static_cast<ReferenceBase *>(data)->add_reference();
		}
		return *this;
	}

	template <typename U>
	IntrusivePtr(const IntrusivePtr<U, Deleter, ReferenceOps> &other)
	{
		*this = other;
	}

	IntrusivePtr(const IntrusivePtr &other)
	{
		*this = other;
	}

	~IntrusivePtr()
	{
		reset();
	}

	template <typename U>
	IntrusivePtr &operator=(IntrusivePtr<U, Deleter, ReferenceOps> &&other) noexcept
	{
		reset();
		data = other.data;
		other.data = nullptr;
		return *this;
	}

	IntrusivePtr &operator=(IntrusivePtr &&other) noexcept
	{
		if (this != &other)
		{
			reset();
			data = other.data;
			other.data = nullptr;
		}
		return *this;
	}

	template <typename U>
	IntrusivePtr(IntrusivePtr<U, Deleter, ReferenceOps> &&other) noexcept
	{
		*this = std::move(other);
	}

	template <typename U>
	IntrusivePtr(IntrusivePtr &&other) noexcept
	{
		*this = std::move(other);
	}

private:
	T *data = nullptr;
};

template <typename T, typename Deleter, typename ReferenceOps>
IntrusivePtr<T, Deleter, ReferenceOps> IntrusivePtrEnabled<T, Deleter, ReferenceOps>::reference_from_this()
{
	add_reference();
	return IntrusivePtr<T, Deleter, ReferenceOps>(static_cast<T *>(this));
}

template <typename Derived>
using DerivedIntrusivePtrType = IntrusivePtr<
		Derived,
		typename Derived::EnabledDeleter,
		typename Derived::EnabledReferenceOp>;

template <typename T, typename... P>
DerivedIntrusivePtrType<T> make_handle(P &&... p)
{
	return DerivedIntrusivePtrType<T>(new T(std::forward<P>(p)...));
}

template <typename Base, typename Derived, typename... P>
typename Base::IntrusivePtrType make_derived_handle(P &&... p)
{
	return typename Base::IntrusivePtrType(new Derived(std::forward<P>(p)...));
}

template <typename T>
using ThreadSafeIntrusivePtr = IntrusivePtr<T, std::default_delete<T>, MultiThreadCounter>;

template <typename T>
using ThreadSafeIntrusivePtrEnabled = IntrusivePtrEnabled<T, std::default_delete<T>, MultiThreadCounter>;
}
