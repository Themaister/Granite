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

namespace Util
{
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

		T *get()
		{
			return static_cast<T *>(node);
		}

		const T *get() const
		{
			return static_cast<const T *>(node);
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
	};

	Iterator begin()
	{
		return Iterator(head);
	}

	Iterator end()
	{
		return Iterator();
	}

	Iterator erase(Iterator itr)
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

		return next;
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

	bool empty() const
	{
		return head == nullptr;
	}

private:
	IntrusiveListEnabled<T> *head = nullptr;
};
}