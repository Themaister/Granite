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

#include "rgtc_compressor.hpp"
#include <algorithm>
#include <iterator>
#include <assert.h>

using namespace std;

namespace Granite
{
static const int range_threshold = 16;
static const int div_7 = (0x100000) / 7;
static const int div_5 = (0x100000) / 5;

class DividerLut
{
public:
	DividerLut() noexcept
	{
		lut_5[0] = 0;
		lut_7[0] = 0;

		for (int range = 1; range < 256; range++)
		{
			lut_7[range] = (0x700000 + (range >> 1)) / range;
			assert(((lut_7[range] * range + 0x80000) >> 20) == 7);
		}

		for (int range = 1; range < 256; range++)
		{
			lut_5[range] = (0x500000 + (range >> 1)) / range;
			assert(((lut_5[range] * range + 0x80000) >> 20) == 5);
		}
	}

	int lut7(int index) const
	{
		return lut_7[index];
	}

	int lut5(int index) const
	{
		return lut_5[index];
	}

private:
	int lut_5[256];
	int lut_7[256];
};
static DividerLut divider_lut;

void decompress_rgtc_red_block(uint8_t *output_r, const uint8_t *block)
{
	uint8_t red0 = block[0];
	uint8_t red1 = block[1];
	uint64_t bits = 0;

	for (int i = 0; i < 6; i++)
		bits |= uint64_t(block[2 + i]) << (8 * i);

	if (red0 > red1)
	{
		for (int i = 0; i < 16; i++)
		{
			int code = int((bits >> (3 * i)) & 7);
			switch (code)
			{
			default:
			case 0:
				output_r[i] = red0;
				break;

			case 1:
				output_r[i] = red1;
				break;

			case 2:
				output_r[i] = uint8_t(((red0 * 6 + red1 * 1) * div_7 + 0x80000) >> 20);
				break;

			case 3:
				output_r[i] = uint8_t(((red0 * 5 + red1 * 2) * div_7 + 0x80000) >> 20);
				break;

			case 4:
				output_r[i] = uint8_t(((red0 * 4 + red1 * 3) * div_7 + 0x80000) >> 20);
				break;

			case 5:
				output_r[i] = uint8_t(((red0 * 3 + red1 * 4) * div_7 + 0x80000) >> 20);
				break;

			case 6:
				output_r[i] = uint8_t(((red0 * 2 + red1 * 5) * div_7 + 0x80000) >> 20);
				break;

			case 7:
				output_r[i] = uint8_t(((red0 * 1 + red1 * 6) * div_7 + 0x80000) >> 20);
				break;
			}
		}
	}
	else
	{
		for (int i = 0; i < 16; i++)
		{
			int code = int((bits >> (3 * i)) & 7);
			switch (code)
			{
			default:
			case 0:
				output_r[i] = red0;
				break;

			case 1:
				output_r[i] = red1;
				break;

			case 2:
				output_r[i] = uint8_t(((red0 * 4 + red1 * 1) * div_5 + 0x80000) >> 20);
				break;

			case 3:
				output_r[i] = uint8_t(((red0 * 3 + red1 * 2) * div_5 + 0x80000) >> 20);
				break;

			case 4:
				output_r[i] = uint8_t(((red0 * 2 + red1 * 3) * div_5 + 0x80000) >> 20);
				break;

			case 5:
				output_r[i] = uint8_t(((red0 * 1 + red1 * 4) * div_5 + 0x80000) >> 20);
				break;

			case 6:
				output_r[i] = 0;
				break;

			case 7:
				output_r[i] = 255;
				break;
			}
		}
	}
}

void compress_rgtc_red_block(uint8_t *output_r, const uint8_t *input_r)
{
	int block_lo = 255;
	int block_hi = 0;

	for (int i = 0; i < 16; i++)
	{
		block_lo = min<int>(block_lo, input_r[i]);
		block_hi = max<int>(block_hi, input_r[i]);
	}

	uint64_t block = 0;
	uint8_t encode_0 = 0;
	uint8_t encode_1 = 0;

	int range = block_hi - block_lo;

	if (range == 0)
	{
		encode_0 = uint8_t(block_hi);
		encode_1 = uint8_t(block_lo);
	}
	else if (range < range_threshold)
	{
		// Simple case, range is small enough that we can directly quantize and be done with it.
		int divider = divider_lut.lut7(range);

		encode_0 = uint8_t(block_hi);
		encode_1 = uint8_t(block_lo);

		for (int i = 0; i < 16; i++)
		{
			int code = ((input_r[i] - block_lo) * divider + 0x80000) >> 20;
			assert(code <= 7);

			if (code == 7)
				code = 0;
			else if (code == 0)
				code = 1;
			else
				code = 8 - code;

			block |= uint64_t(code) << (3 * i);
		}
	}
	else
	{
		int divider = divider_lut.lut7(range);
		encode_0 = uint8_t(block_hi);
		encode_1 = uint8_t(block_lo);

		int best_error = 0;
		int lo_index = 0;
		int hi_index = 15;
		bool use_5_weight = false;

		for (int i = 0; i < 16; i++)
		{
			int code = ((input_r[i] - block_lo) * divider + 0x80000) >> 20;
			assert(code <= 7);
			int interpolated_value = block_lo + ((range * code * div_7 + 0x80000) >> 20);

			if (code == 7)
				code = 0;
			else if (code == 0)
				code = 1;
			else
				code = 8 - code;

			int diff = interpolated_value - input_r[i];
			best_error += diff * diff;

			block |= uint64_t(code) << (3 * i);
		}

		int sorted_block[16];
		for (int i = 0; i < 16; i++)
			sorted_block[i] = input_r[i];
		sort(begin(sorted_block), end(sorted_block));

		for (int lo = 0; lo < 15; lo++)
		{
			for (int hi = lo; hi < 15; hi++)
			{
				int partition_lo = sorted_block[lo];
				int partition_hi = sorted_block[hi];
				assert(partition_hi >= partition_lo);
				int partition_range = partition_hi - partition_lo;
				int partition_divider = divider_lut.lut5(partition_range);

				int error = 0;

				// Consider that we can quantize to 0.0 as well.
				for (int i = 0; i < lo; i++)
				{
					int diff = min(sorted_block[i] - 0, partition_lo - sorted_block[i]);
					error += diff * diff;
				}

				for (int i = lo; i <= hi; i++)
				{
					int code = ((sorted_block[i] - partition_lo) * partition_divider + 0x80000) >> 20;
					assert(code <= 7);
					int interpolated_value = partition_lo + ((partition_range * code * div_5 + 0x80000) >> 20);
					int diff = interpolated_value - sorted_block[i];
					error += diff * diff;
				}

				// Consider that we can quantize to 1.0 as well.
				for (int i = hi + 1; i <= 15; i++)
				{
					int diff = min(255 - sorted_block[i], sorted_block[i] - partition_hi);
					error += diff * diff;
				}

				if (error < best_error)
				{
					lo_index = lo;
					hi_index = hi;
					best_error = error;
					use_5_weight = true;
				}
			}
		}

		// Did we find a better partition?
		if (use_5_weight)
		{
			int partition_lo = sorted_block[lo_index];
			int partition_hi = sorted_block[hi_index];
			encode_0 = uint8_t(partition_lo);
			encode_1 = uint8_t(partition_hi);

			block = 0;
			assert(partition_hi >= partition_lo);
			int partition_range = partition_hi - partition_lo;
			int partition_divider = divider_lut.lut5(partition_range);

			for (int i = 0; i < 16; i++)
			{
				int code;
				if (input_r[i] < partition_lo)
				{
					if ((input_r[i] - 0) < (partition_lo - input_r[i]))
						code = 6;
					else
						code = 0;
				}
				else if (input_r[i] > partition_hi)
				{
					if ((255 - input_r[i]) < (input_r[i] - partition_hi))
						code = 7;
					else
						code = 1;
				}
				else
				{
					code = ((input_r[i] - partition_lo) * partition_divider + 0x80000) >> 20;
					assert(code <= 5);
					if (code == 5)
						code = 1;
					else if (code != 0)
						code++;
				}

				block |= uint64_t(code) << (3 * i);
			}
		}
	}

	output_r[0] = encode_0;
	output_r[1] = encode_1;
	for (int i = 0; i < 6; i++)
		output_r[2 + i] = uint8_t((block >> (8 * i)) & 0xff);
}

void compress_rgtc_red_green_block(uint8_t *output_rg, const uint8_t *input_r, const uint8_t *input_g)
{
	compress_rgtc_red_block(output_rg, input_r);
	compress_rgtc_red_block(output_rg + 8, input_g);
}
}