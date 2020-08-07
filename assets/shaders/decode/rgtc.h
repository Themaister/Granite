#ifndef RGTC_H_
#define RGTC_H_

uint decode_alpha_rgtc(uvec2 payload, int linear_pixel)
{
	int ep0 = int(payload.x & 0xffu);
	int ep1 = int((payload.x >> 8) & 0xffu);
	bool range7 = ep0 > ep1;

	int bit_offset = 16 + linear_pixel * 3;
	uint bits;
	if (bit_offset <= 29)
		bits = bitfieldExtract(payload.x, bit_offset, 3);
	else if (bit_offset >= 32)
		bits = bitfieldExtract(payload.y, bit_offset - 32, 3);
	else
		bits = bitfieldExtract(payload.x, 31, 1) | ((payload.y & 3) << 1);

	uint res;

	if (bits < 2)
		res = bits != 0 ? ep1 : ep0;
	else if (range7)
		res = ep0 + (((ep1 - ep0) * int(bits - 1) * 0x2492 + 0x8000) >> 16);
	else if (bits > 5)
		res = (bits & 1) * 0xff;
	else
		res = ep0 + (((ep1 - ep0) * int(bits - 1) * 0x3333 + 0x8000) >> 16);

	return res;
}

#endif