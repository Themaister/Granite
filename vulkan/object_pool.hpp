#pragma once

#include <memory>
#include <vector>

namespace Vulkan
{
template <typename T>
class ObjectPool
{
public:
	template <typename... P>
	T *allocate(P &&... p)
	{
		if (vacants.empty())
		{
			unsigned num_objects = 64u << memory.size();
			T *ptr = static_cast<T *>(malloc(num_objects * sizeof(T)));
			if (!ptr)
				return nullptr;

			for (unsigned i = 0; i < num_objects; i++)
				vacants.push_back(&ptr[i]);
		}

		T *ptr = vacants.back();
		vacants.pop_back();
		new (ptr) T(std::forward<P>(p)...);
		return ptr;
	}

	void free(T *ptr)
	{
		ptr->~T();
		vacants.push_back(ptr);
	}

	void clear()
	{
		vacants.clear();
		memory.clear();
	}

private:
	std::vector<T *> vacants;
	struct MallocDeleter
	{
		void operator()(T *ptr)
		{
			::free(ptr);
		}
	};
	std::vector<std::unique_ptr<T, MallocDeleter>> memory;
};

template <typename T>
struct IntrusiveListEnabled
{
	IntrusiveListEnabled<T> *prev = nullptr;
	IntrusiveListEnabled<T> *next = nullptr;
};

template <typename T>
class IntrusiveList
{
public:
	void clear()
	{
		head = nullptr;
	}

	class Iterator
	{
	public:
		friend class IntrusiveList<T>;
		Iterator(IntrusiveListEnabled<T> *node)
		    : node(node)
		{
		}

		Iterator() = default;

		explicit operator bool() const
		{
			return node != nullptr;
		}

		bool operator==(const Iterator &other) const
		{
			return node == other.node;
		}

		bool operator!=(const Iterator &other) const
		{
			return node != other.node;
		}

		T &operator*()
		{
			return *static_cast<T *>(node);
		}

		const T &operator*() const
		{
			return *static_cast<T *>(node);
		}

		T *operator->()
		{
			return static_cast<T *>(node);
		}

		const T *operator->() const
		{
			return static_cast<T *>(node);
		}

		Iterator &operator++()
		{
			node = node->next;
			return *this;
		}

	private:
		IntrusiveListEnabled<T> *node = nullptr;
		IntrusiveListEnabled<T> *get()
		{
			return node;
		}
	};

	Iterator begin()
	{
		return Iterator(head);
	}

	Iterator end()
	{
		return Iterator();
	}

	void erase(Iterator itr)
	{
		auto *node = itr.get();
		auto *next = node->next;
		auto *prev = node->prev;

		if (prev)
			prev->next = next;
		else
			head = next;

		if (next)
			next->prev = prev;
	}

	void insert_front(Iterator itr)
	{
		auto *node = itr.get();
		if (head)
			head->prev = node;

		node->next = head;
		node->prev = nullptr;
		head = node;
	}

	void move_to_front(IntrusiveList<T> &other, Iterator itr)
	{
		other.erase(itr);
		insert_front(itr);
	}

private:
	IntrusiveListEnabled<T> *head = nullptr;
};
}
