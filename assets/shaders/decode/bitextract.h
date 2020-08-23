/* Copyright (c) 2020 Hans-Kristian Arntzen
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

#ifndef BITEXTRACT_H_
#define BITEXTRACT_H_

int extract_bits(uvec4 payload, int offset, int bits)
{
	int last_offset = offset + bits - 1;
	int result;

	if (bits <= 0)
		result = 0;
	else if ((last_offset >> 5) == (offset >> 5))
		result = int(bitfieldExtract(payload[offset >> 5], offset & 31, bits));
	else
	{
		int first_bits = 32 - (offset & 31);
		int result_first = int(bitfieldExtract(payload[offset >> 5], offset & 31, first_bits));
		int result_second = int(bitfieldExtract(payload[(offset >> 5) + 1], 0, bits - first_bits));
		result = result_first | (result_second << first_bits);
	}
	return result;
}

int extract_bits_sign(uvec4 payload, int offset, int bits)
{
	int last_offset = offset + bits - 1;
	int result;

	if (bits <= 0)
		result = 0;
	else if ((last_offset >> 5) == (offset >> 5))
		result = bitfieldExtract(int(payload[offset >> 5]), offset & 31, bits);
	else
	{
		int first_bits = 32 - (offset & 31);
		int result_first = int(bitfieldExtract(payload[offset >> 5], offset & 31, first_bits));
		int result_second = bitfieldExtract(int(payload[(offset >> 5) + 1]), 0, bits - first_bits);
		result = result_first | (result_second << first_bits);
	}
	return result;
}

int extract_bits_reverse(uvec4 payload, int offset, int bits)
{
	int last_offset = offset + bits - 1;
	int result;

	if (bits <= 0)
		result = 0;
	else if ((last_offset >> 5) == (offset >> 5))
		result = int(bitfieldReverse(bitfieldExtract(payload[offset >> 5], offset & 31, bits)) >> (32 - bits));
	else
	{
		int first_bits = 32 - (offset & 31);
		uint result_first = bitfieldExtract(payload[offset >> 5], offset & 31, first_bits);
		uint result_second = bitfieldExtract(payload[(offset >> 5) + 1], 0, bits - first_bits);
		result = int(bitfieldReverse(result_first | (result_second << first_bits)) >> (32 - bits));
	}
	return result;
}

#endif