/* Copyright (c) 2017-2023 Hans-Kristian Arntzen
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
#include <stdint.h>

// Ad-hoc raw packet format which bypasses misc issues with existing realtime muxers.
// RTP: Needs multiple muxers, highly non-trivial to mux them together. Designed for multiple ports over UDP.
// MPEG2TS: Unacceptable muxing delay (>80ms) when muxing audio and video together.
// This is supposed to be suitable to hammer through raw packets over a stream connection
// with no reordering or anything.

namespace Granite
{
enum class VideoCodec : uint32_t
{
	None = 0,
	H264 = 1,
	H265 = 2,
	AV1 = 3
};

enum class AudioCodec : uint16_t
{
	None = 0,
	AAC = 1,
	Opus = 2,
	S16LE = 3,
	Count
};

enum class Endpoint : uint32_t
{
	None = 0,
	CodecParam = 1,
	VideoPacket = 3,
	AudioPacket = 4,
	Count
};

static constexpr uint64_t PyroMagic =
		(uint64_t('P') << 56) |
		(uint64_t('Y') << 48) |
		(uint64_t('R') << 40) |
		(uint64_t('O') << 32) |
		(0xdeull << 24) |
		(0xadull << 16) |
		(uint64_t('V') << 8) |
		(uint64_t('1') << 0);

struct PacketHeader
{
	uint64_t header_magic;
	Endpoint endpoint;
	uint32_t payload_size;
};
static_assert(sizeof(PacketHeader) == 16, "Unexpected size for PacketHeader.");

struct CodecParams
{
	VideoCodec video_codec;
	AudioCodec audio_codec;
	uint16_t frame_rate_num;
	uint16_t frame_rate_den;
	uint16_t width;
	uint16_t height;
	uint32_t channels;
	uint32_t rate;
};
static_assert(sizeof(CodecParams) == 24, "Unexpected size for CodecParams.");

enum PayloadFlagsBits : uint64_t
{
	PAYLOAD_KEY_FRAME_BIT = 1 << 0,
};
using PayloadFlags = uint64_t;

struct PayloadHeader
{
	int64_t pts; // Linear TS to be passed into AVPacket
	int64_t dts; // Linear TS to be passed into AVPacket
	PayloadFlags flags;
};
static_assert(sizeof(PayloadHeader) == 24, "Unexpected size for PayloadHeader.");
}
