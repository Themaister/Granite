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

namespace Util
{

template <typename T, typename Deleter = std::default_delete<T>>
class IntrusivePtrEnabled
{
public:
	void release_reference()
	{
		size_t count = --reference_count;
		if (count == 0)
			Deleter()(static_cast<T *>(this));
	}

	void add_reference()
	{
		++reference_count;
	}

	IntrusivePtrEnabled() = default;

	IntrusivePtrEnabled(const IntrusivePtrEnabled &) = delete;

	void operator=(const IntrusivePtrEnabled &) = delete;

private:
	size_t reference_count = 1;
};

template <typename T, typename Deleter = std::default_delete<T>>
class IntrusivePtr
{
public:
	IntrusivePtr() = default;

	IntrusivePtr(T *handle)
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
		// Static up-cast here to avoid potential issues with multiple intrusive inheritance.
		// Also makes sure that the pointer type actually inherits from this type.
		if (data)
			static_cast<IntrusivePtrEnabled<T, Deleter> *>(data)->release_reference();
		data = nullptr;
	}

	IntrusivePtr &operator=(const IntrusivePtr &other)
	{
		if (this != &other)
		{
			reset();
			data = other.data;

			// Static up-cast here to avoid potential issues with multiple intrusive inheritance.
			// Also makes sure that the pointer type actually inherits from this type.
			if (data)
				static_cast<IntrusivePtrEnabled<T, Deleter> *>(data)->add_reference();
		}
		return *this;
	}

	IntrusivePtr(const IntrusivePtr &other)
	{
		*this = other;
	}

	~IntrusivePtr()
	{
		reset();
	}

	IntrusivePtr &operator=(IntrusivePtr &&other)
	{
		if (this != &other)
		{
			reset();
			data = other.data;
			other.data = nullptr;
		}
		return *this;
	}

	IntrusivePtr(IntrusivePtr &&other)
	{
		*this = std::move(other);
	}

private:
	T *data = nullptr;
};

template <typename T, typename... P>
IntrusivePtr<T> make_handle(P &&... p)
{
	return IntrusivePtr<T>(new T(std::forward<P>(p)...));
}

template <typename Base, typename Derived, typename... P>
IntrusivePtr<Base> make_abstract_handle(P &&... p)
{
	return IntrusivePtr<Base>(new Derived(std::forward<P>(p)...));
}
}
