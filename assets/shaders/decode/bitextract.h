#ifndef BITEXTRACT_H_
#define BITEXTRACT_H_

int extract_bits(uvec4 payload, int offset, int bits)
{
	int last_offset = offset + bits - 1;
	int result;

	if ((last_offset >> 5) == (offset >> 5))
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

	if ((last_offset >> 5) == (offset >> 5))
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
	if ((last_offset >> 5) == (offset >> 5))
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