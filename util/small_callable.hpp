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
#include <type_traits>
#include <utility>
#include <functional>

namespace Util
{
template <typename Sig, size_t PayloadSize, size_t Align>
class SmallCallable;

template <typename R, typename... Params, size_t PayloadSize, size_t Align>
class SmallCallable<R (Params...), PayloadSize, Align>
{
public:
	inline R call(Params... params)
	{
		return get_invokable().call(std::forward<Params>(params)...);
	}

	inline explicit operator bool() const
	{
		return get_invokable().active();
	}

	~SmallCallable()
	{
		get_invokable().~Invokable();
	}

	template <typename Func>
	inline explicit SmallCallable(Func&& fn)
	{
		using PlainFunc = std::remove_cv_t<std::remove_reference_t<Func>>;
		// Avoid mistaken double-wrap.
		static_assert(!std::is_same<std::function<R (Params...)>, PlainFunc>::value, "std::function not supported.");
		static_assert(sizeof(CapturedInvokable<PlainFunc>) <= sizeof(payload), "Callback payload is too large.");
		new(payload) CapturedInvokable<PlainFunc>(std::forward<Func>(fn));
	}

	inline explicit SmallCallable(R (*fn)(Params...))
	{
		static_assert(sizeof(CapturedPlain) <= sizeof(payload), "Callback payload is too large.");
		new(payload) CapturedPlain(fn);
	}

	template <typename T>
	inline SmallCallable(T *ptr, R (T::*memb)(Params...))
	{
		using MemberFunc = CapturedMemberFunc<T>;
		static_assert(sizeof(MemberFunc) <= sizeof(payload), "Callback payload is too large.");
		new(payload) MemberFunc(ptr, memb);
	}

	inline SmallCallable()
	{
		new(payload) NullInvoker();
	}

	SmallCallable(const SmallCallable &) = delete;
	SmallCallable(SmallCallable &&) = delete;
	void operator=(const SmallCallable &) = delete;
	void operator=(SmallCallable &&) = delete;

private:
	struct Invokable
	{
		virtual ~Invokable() = default;
		virtual R call(Params...) = 0;
		virtual bool active() const = 0;
	};

	template <typename I>
	struct CapturedInvokable final : Invokable
	{
		explicit CapturedInvokable(I&& holder_) : holder(std::move(holder_)) {}
		R call(Params... params) override { return holder(std::forward<Params>(params)...); };
		bool active() const override { return true; }
		I holder;
	};

	struct NullInvoker final : Invokable
	{
		R call(Params...) override { return R(); }
		bool active() const override { return false; }
	};

	struct CapturedPlain final : Invokable
	{
		explicit CapturedPlain(R (*func_)(Params...)) : func(func_) {}
		R call(Params... params) override { return func(std::forward<Params>(params)...); }
		bool active() const override { return func != nullptr; }
		R (*func)(Params...);
	};

	template <typename T>
	struct CapturedMemberFunc final : Invokable
	{
		explicit CapturedMemberFunc(T *ptr_, R (T::*func_)(Params...)) : ptr(ptr_), func(func_) {}
		R call(Params... params) override { return (ptr->*func)(std::forward<Params>(params)...); }
		bool active() const override { return ptr != nullptr && func != nullptr; }
		T *ptr;
		R (T::*func)(Params...);
	};

	Invokable &get_invokable()
	{
		return *reinterpret_cast<Invokable *>(&payload[0]);
	}

	const Invokable &get_invokable() const
	{
		return *reinterpret_cast<const Invokable *>(&payload[0]);
	}

	alignas(Align) char payload[PayloadSize];
};
}
