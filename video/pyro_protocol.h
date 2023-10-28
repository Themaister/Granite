#ifndef PYRO_PROTOCOL_H_
#define PYRO_PROTOCOL_H_

#include <stdint.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

// Endian: All wire-messages are in little-endian.

#define PYRO_MAKE_MESSAGE_TYPE(t, s) ((((uint32_t)'P') << 26) | (((uint32_t)'Y') << 20) | (((uint32_t)'R') << 14) | (t) | ((s) << 6))
#define PYRO_MESSAGE_MAGIC_MASK (~(uint32_t)0 << 14)
#define PYRO_MAX_PAYLOAD_SIZE 1024

typedef enum pyro_video_codec_type
{
	PYRO_VIDEO_CODEC_NONE = 0,
	PYRO_VIDEO_CODEC_H264 = 1,
	PYRO_VIDEO_CODEC_H265 = 2,
	PYRO_VIDEO_CODEC_AV1 = 3,
	PYRO_VIDEO_CODEC_MAX_INT = INT32_MAX
} pyro_video_codec_type;

typedef enum pyro_audio_codec_type
{
	PYRO_AUDIO_CODEC_NONE = 0,
	PYRO_AUDIO_CODEC_OPUS = 1,
	PYRO_AUDIO_CODEC_AAC = 2,
	PYRO_AUDIO_CODEC_RAW_S16LE = 3,
	PYRO_AUDIO_CODEC_MAX_INT = INT32_MAX
} pyro_audio_codec_type;

struct pyro_codec_parameters
{
	pyro_video_codec_type video_codec;
	pyro_audio_codec_type audio_codec;
	uint16_t frame_rate_num;
	uint16_t frame_rate_den;
	uint16_t width;
	uint16_t height;
	uint32_t channels;
	uint32_t rate;
};

struct pyro_progress_report
{
	uint64_t total_received_packets;
	uint64_t total_dropped_packets;
	uint64_t total_received_key_frames;
};

#define PYRO_MAX_UDP_DATAGRAM_SIZE (PYRO_MAX_PAYLOAD_SIZE + sizeof(struct pyro_codec_parameters))

// TCP: Server to client
// UDP / TCP: client to server
typedef enum pyro_message_type
{
	PYRO_MESSAGE_OK = PYRO_MAKE_MESSAGE_TYPE(0, 0),
	PYRO_MESSAGE_NAK = PYRO_MAKE_MESSAGE_TYPE(1, 0),
	PYRO_MESSAGE_AGAIN = PYRO_MAKE_MESSAGE_TYPE(2, 0),
	// First message sent to server, server replies with COOKIE.
	PYRO_MESSAGE_HELLO = PYRO_MAKE_MESSAGE_TYPE(3, 0),
	// Returns a unique 64-bit cookie to client.
	// Client must re-send that cookie over UDP.
	PYRO_MESSAGE_COOKIE = PYRO_MAKE_MESSAGE_TYPE(4, sizeof(uint64_t)),
	// Sent by client: Replies: CODEC_PARAMETERS if UDP cookie was received, NAK if not yet received or invalid.
	// AGAIN is sent if UDP client is acknowledged, but stream is not ready yet (i.e. codec parameters are not known yet).
	PYRO_MESSAGE_KICK = PYRO_MAKE_MESSAGE_TYPE(5, 0),
	// Returns nothing. Must be received by server every 5 seconds or connection is dropped.
	PYRO_MESSAGE_PROGRESS = PYRO_MAKE_MESSAGE_TYPE(6, sizeof(struct pyro_progress_report)),
	PYRO_MESSAGE_CODEC_PARAMETERS = PYRO_MAKE_MESSAGE_TYPE(7, sizeof(struct pyro_codec_parameters)),
	PYRO_MESSAGE_MAX_INT = INT32_MAX,
} pyro_message_type;

#define PYRO_MAX_MESSAGE_BUFFER_LENGTH (255 + sizeof(pyro_message_type))

static inline bool pyro_message_validate_magic(uint32_t v)
{
	return PYRO_MAKE_MESSAGE_TYPE(0, 0) == (v & PYRO_MESSAGE_MAGIC_MASK);
}

static inline uint32_t pyro_message_get_length(uint32_t v)
{
	return (v >> 6) & 0xff;
}

// UDP: server to client. Size is implied by datagram.
enum pyro_payload_flag_bits
{
	PYRO_PAYLOAD_KEY_FRAME_BIT = 1 << 0, // For video, useful to know when clean recovery can be made, or when to start the stream
	PYRO_PAYLOAD_STREAM_TYPE_BIT = 1 << 1, // 0: video, 1: audio
	PYRO_PAYLOAD_PACKET_DONE_BIT = 1 << 2, // Set on last subpacket within a packet
	PYRO_PAYLOAD_PACKET_BEGIN_BIT = 1 << 3, // Set on first subpacket within a packet
	PYRO_PAYLOAD_PACKET_SEQ_OFFSET = 4, // Sequence increases by one on a per-stream basis.
	PYRO_PAYLOAD_PACKET_SEQ_BITS = 14,
	PYRO_PAYLOAD_SUBPACKET_SEQ_OFFSET = 18,
	PYRO_PAYLOAD_SUBPACKET_SEQ_BITS = 14,
};
typedef uint32_t pyro_payload_flags;

#define PYRO_PAYLOAD_PACKET_SEQ_MASK ((1 << PYRO_PAYLOAD_PACKET_SEQ_BITS) - 1)
#define PYRO_PAYLOAD_SUBPACKET_SEQ_MASK ((1 << PYRO_PAYLOAD_SUBPACKET_SEQ_BITS) - 1)

static inline uint32_t pyro_payload_get_packet_seq(pyro_payload_flags flags)
{
	return (flags >> PYRO_PAYLOAD_PACKET_SEQ_OFFSET) & PYRO_PAYLOAD_PACKET_SEQ_MASK;
}

static inline uint32_t pyro_payload_get_subpacket_seq(pyro_payload_flags flags)
{
	return (flags >> PYRO_PAYLOAD_SUBPACKET_SEQ_OFFSET) & PYRO_PAYLOAD_SUBPACKET_SEQ_MASK;
}

static inline int pyro_payload_get_seq_delta(uint32_t a, uint32_t b, uint32_t mask)
{
	uint32_t d = (a - b) & mask;
	if (d <= (mask >> 1))
		return int(d);
	else
		return int(d) - int(mask + 1);
}

static inline int pyro_payload_get_packet_seq_delta(uint32_t a, uint32_t b)
{
	return pyro_payload_get_seq_delta(a, b, PYRO_PAYLOAD_PACKET_SEQ_MASK);
}

static inline int pyro_payload_get_subpacket_seq_delta(uint32_t a, uint32_t b)
{
	return pyro_payload_get_seq_delta(a, b, PYRO_PAYLOAD_SUBPACKET_SEQ_MASK);
}

struct pyro_payload_header
{
	uint32_t pts_lo, pts_hi;
	uint32_t dts_delta; // dts = pts - dts_delta
	pyro_payload_flags encoded;
};

#ifdef __cplusplus
}
#endif

#endif
