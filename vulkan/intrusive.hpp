#pragma once

#include <stddef.h>
#include <utility>

template <typename T>
class IntrusivePtrEnabled
{
public:
	void release_reference()
	{
		size_t count = --reference_count;
		if (count == 0)
			delete static_cast<T *>(this);
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

template <typename T>
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
			static_cast<IntrusivePtrEnabled<T> *>(data)->release_reference();
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
				static_cast<IntrusivePtrEnabled<T> *>(data)->add_reference();
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
