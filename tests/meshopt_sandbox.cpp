#include "meshoptimizer.h"
#include "logging.hpp"
#include <stdint.h>
#include <vector>
#include "math.hpp"
#include "muglm/muglm_impl.hpp"
#include <assert.h>
using namespace Granite;

int main()
{
	constexpr uint32_t count = 8;
	constexpr uint32_t stride = 8;
	constexpr uint32_t block_elements_limit = 256u;
	constexpr uint32_t max_stride = 256u;
	size_t bound = meshopt_encodeVertexBufferBound(count, stride);
	std::vector<uint8_t> buffer(bound);

	const uint32_t vertices[] = {
		800, 805, 810, 750,
		710, 720, 700, 701,
		800, 890, 800, 800,
		800, 800, 800, 800
	};
	size_t encoded_size = meshopt_encodeVertexBuffer(buffer.data(), bound, vertices, count, stride);
	buffer.resize(encoded_size);

	uint32_t max_block_elements = min(block_elements_limit, (8192 / stride) & ~15u);

	std::vector<uint8_t> output_buffer(((count + 15) & ~15) * stride);

	assert(buffer[0] == 0xa0);
	uint32_t buffer_index = 1;

	for (size_t i = 0, n = buffer.size(); i < n; i++)
		LOGI("Output byte %02zu: 0x%02x\n", i, buffer[i]);

	uint8_t decode_buffer[max_stride];
	uint32_t tail_size = (stride + 31) & ~31;
	memcpy(decode_buffer, buffer.data() + buffer.size() - stride, stride);
	buffer.resize(buffer.size() - tail_size);

	for (uint32_t element = 0; element < count; element += max_block_elements)
	{
		uint32_t remaining_elements = min(max_block_elements, count - element);
		uint32_t group_count = (remaining_elements + 15u) / 16u;

		for (uint32_t data_block = 0; data_block < stride; data_block++)
		{
			uint8_t *out_ptr = output_buffer.data() + element * stride + data_block;
			uint8_t decode_value = decode_buffer[data_block];
			uint32_t out_vertex_index = 0;

			uint8_t header_bits[16];
			for (uint32_t group_index = 0; group_index < group_count; group_index++)
				header_bits[group_index] =
						(buffer[buffer_index + group_index / 4] >> (2 * (group_index & 3u))) & 3u;
			buffer_index += (group_count + 3) / 4;

			for (uint32_t group_index = 0; group_index < group_count; group_index++)
			{
				switch (header_bits[group_index])
				{
				case 0:
					for (uint32_t i = 0; i < 16; i++, out_vertex_index++)
						out_ptr[stride * out_vertex_index] = decode_value;
					break;

				case 1:
				{
					uint32_t sentinel_count = 0;
					// 2-bit sentinel decode.
					for (uint32_t i = 0; i < 16; i++, out_vertex_index++)
					{
						uint32_t bits = (buffer[buffer_index + i / 4] >> (2 * ((i ^ 3) & 3))) & 3;
						uint8_t delta;

						if (bits == 3)
							bits = buffer[buffer_index + 4 + sentinel_count++];

						delta = (bits & 1) ? uint8_t(~(bits >> 1)) : uint8_t(bits >> 1);

						uint8_t updated = decode_value + delta;
						out_ptr[out_vertex_index * stride] = decode_value = updated;
					}
					buffer_index += 4 + sentinel_count;
					break;
				}

				case 2:
				{
					uint32_t sentinel_count = 0;
					// 4-bit sentinel decode.
					for (uint32_t i = 0; i < 16; i++, out_vertex_index++)
					{
						uint32_t bits = (buffer[buffer_index + i / 2] >> (4 * ((i ^ 1) & 1))) & 0xf;
						uint8_t delta;

						if (bits == 15)
							delta = buffer[buffer_index + 8 + sentinel_count++];
						else
							delta = (bits & 1) ? uint8_t(~(bits >> 1)) : uint8_t(bits >> 1);

						uint8_t updated = decode_value + delta;
						out_ptr[out_vertex_index * stride] = decode_value = updated;
					}
					buffer_index += 8 + sentinel_count;
					break;
				}

				default:
					for (uint32_t i = 0; i < 16; i++, out_vertex_index++)
					{
						uint8_t delta = buffer[buffer_index + i];
						uint8_t updated = decode_value + delta;
						out_ptr[out_vertex_index * stride] = decode_value = updated;
					}
					buffer_index += 16;
					break;
				}
			}
		}
	}

	for (uint32_t i = 0; i < count * stride / 4; i++)
		LOGI("Output value %u: %u\n", i, reinterpret_cast<const uint32_t *>(output_buffer.data())[i]);

	int8_t output[4];
	float float_inputs[4] = { 0.5f, 0.5f, 0.5f };
	vec3 normalized = normalize(vec3(float_inputs[0], float_inputs[1], float_inputs[2]));
	float float_output[4];
	meshopt_encodeFilterOct(output, 1, 4, 8, normalized.data);
	meshopt_decodeFilterOct(output, 1, 4);

	LOGI("Input (%.3f, %.3f, %.3f)\n", normalized[0], normalized[1], normalized[2]);
	LOGI("Value (%.3f, %.3f, %.3f)\n", output[0] / 127.0f, output[1] / 127.0f, output[2] / 127.0f);
}