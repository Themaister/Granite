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
#include <stdint.h>
#include "dynamic_array.hpp"
#include <memory>

namespace Util
{
template <int offset, int bits, typename ValueT, typename IndexT>
static inline void radix_sort_pass(ValueT * __restrict outputs, const ValueT * __restrict inputs,
                                   IndexT * __restrict output_indices,
                                   const IndexT * __restrict input_indices,
                                   IndexT * __restrict scratch_indices,
                                   size_t count)
{
	constexpr int num_values = 1 << bits;
	IndexT per_value_counts[num_values] = {};
	for (size_t i = 0; i < count; i++)
	{
		ValueT c = (inputs[i] >> offset) & ((ValueT(1) << bits) - ValueT(1));
		scratch_indices[i] = per_value_counts[c]++;
	}

	IndexT per_value_counts_prefix[num_values];
	IndexT prefix_sum = 0;
	for (int i = 0; i < num_values; i++)
	{
		per_value_counts_prefix[i] = prefix_sum;
		prefix_sum += per_value_counts[i];
	}

	for (size_t i = 0; i < count; i++)
	{
		ValueT inp = inputs[i];
		ValueT c = (inp >> offset) & ((ValueT(1) << bits) - ValueT(1));
		IndexT effective_index = scratch_indices[i] + per_value_counts_prefix[c];
		IndexT input_index = input_indices ? input_indices[i] : i;

		output_indices[effective_index] = input_index;
		outputs[effective_index] = inp;
	}
}

template <typename CodeT, int... pattern>
class RadixSorter
{
public:
	static_assert(sizeof...(pattern) % 2 == 0, "Need even number of radix passes.");
	static_assert(sizeof...(pattern) > 0, "Need at least one radix pass.");

	void resize(size_t count)
	{
		codes.reserve(count * 2);
		indices.reserve(count * 3);
		N = count;
	}

	void sort()
	{
		sort_inner_first<pattern...>();
	}

	size_t size() const
	{
		return N;
	}

	CodeT *code_data()
	{
		return codes.data();
	}

	const CodeT *code_data() const
	{
		return codes.data();
	}

	const uint32_t *indices_data() const
	{
		return indices.data();
	}

private:
	DynamicArray<CodeT> codes;
	DynamicArray<uint32_t> indices;
	size_t N = 0;

	template <int offset>
	void sort_inner(CodeT *, CodeT *, uint32_t *, uint32_t *, uint32_t *)
	{
	}

	template <int offset, int count, int... counts>
	void sort_inner(CodeT *output_values, CodeT *input_values,
	                uint32_t *output_indices, uint32_t *input_indices,
	                uint32_t *scratch_indices)
	{
		radix_sort_pass<offset, count>(output_values, input_values,
		                               output_indices, input_indices,
		                               scratch_indices, N);

		sort_inner<offset + count, counts...>(input_values, output_values,
		                                      input_indices, output_indices,
		                                      scratch_indices);
	}

	template <int count, int... counts>
	void sort_inner_first()
	{
		auto *output_values = codes.data();
		auto *input_values = codes.data() + N;

		auto *output_indices = indices.data();
		auto *input_indices = indices.data() + 1 * N;
		auto *scratch_indices = indices.data() + 2 * N;

		radix_sort_pass<0, count>(input_values, output_values,
		                          input_indices, static_cast<const uint32_t *>(nullptr),
		                          scratch_indices, N);

		sort_inner<count, counts...>(output_values, input_values,
		                             output_indices, input_indices,
		                             scratch_indices);
	}
};
}