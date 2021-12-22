#include "global_managers_init.hpp"
#include "filesystem.hpp"
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include "vulkan_video_codec_h264std.h"
#include "context.hpp"
#include "device.hpp"

static uint32_t read_b32(const uint8_t *ptr, size_t size)
{
	if (size < 4)
		return 0;
	else
		return (uint32_t(ptr[0]) << 24) | (uint32_t(ptr[1]) << 16) | (uint32_t(ptr[2]) << 8) | uint32_t(ptr[3]);
}

static uint32_t read_b24(const uint8_t *ptr, size_t size)
{
	if (size < 3)
		return 0;
	else
		return (uint32_t(ptr[0]) << 16) | (uint32_t(ptr[1]) << 8) | uint32_t(ptr[2]);
}

static bool find_start_code(const uint8_t *&ptr, size_t &size, bool *zero_byte)
{
	while (size)
	{
		bool pattern_3 = read_b24(ptr, size) == 1;
		bool pattern_4 = read_b32(ptr, size) == 1;
		if (pattern_4 || pattern_3)
		{
			if (zero_byte)
				*zero_byte = pattern_4;
			return true;
		}
		else
		{
			ptr++;
			size--;
		}
	}

	return false;
}

class BitStream
{
public:
	BitStream(const uint8_t *ptr, size_t size);

	uint32_t u(unsigned bits);
	uint32_t u();
	uint32_t ue();
	int32_t se();

	bool eof() const;
	bool more_data() const;

private:
	const uint8_t *ptr;
	size_t size;
	size_t offset = 0;
	uint8_t current_byte = 0;
	unsigned bits_left = 0;
	unsigned zero_byte_count = 0;
};

BitStream::BitStream(const uint8_t *ptr_, size_t size_)
	: ptr(ptr_), size(size_)
{
}

bool BitStream::eof() const
{
	return bits_left == 0 && offset >= size;
}

bool BitStream::more_data() const
{
	auto tmp = *this;
	if (tmp.eof())
		return false;

	if (tmp.u() == 0)
		return false;

	while (!tmp.eof())
		if (tmp.u() != 0)
			return true;

	return false;
}

uint32_t BitStream::u()
{
	if (bits_left == 0 && offset < size)
	{
		current_byte = ptr[offset++];
		bits_left = 8;

		if (current_byte == 3 && zero_byte_count == 2) // emulation_prevention_three_byte
		{
			// Skip this byte.
			if (offset < size)
				current_byte = ptr[offset++];
			else
				bits_left = 0;
		}

		if (current_byte == 0)
			zero_byte_count++;
		else
			zero_byte_count = 0;
	}

	if (bits_left != 0)
	{
		bits_left--;
		return (current_byte >> bits_left) & 1;
	}
	else
		return 0;
}

uint32_t BitStream::u(unsigned bits)
{
	assert(bits <= 32);
	uint32_t v = 0;
	for (unsigned i = 0; i < bits; i++)
		v = (v << 1u) | u();
	return v;
}

uint32_t BitStream::ue()
{
	int leading_zero_bits = -1;
	bool b;
	do
	{
		b = u() == 0 && !eof();
		leading_zero_bits++;
	} while(b);
	return (1u << leading_zero_bits) - 1 + u(leading_zero_bits);
}

int32_t BitStream::se()
{
	uint32_t exp_golomb = ue();
	if (exp_golomb == 0)
	{
		return 0;
	}
	else
	{
		int32_t signed_exp_golomb = int32_t(exp_golomb) - 1;
		bool flip_sign = (exp_golomb & 1) == 0;
		signed_exp_golomb = (signed_exp_golomb / 2) + 1;
		if (flip_sign)
			signed_exp_golomb = -signed_exp_golomb;
		return signed_exp_golomb;
	}
}

enum class NALUnitType
{
	Unspecified0 = 0,
	NonIDRSlice = 1,
	SlicePartitionA = 2,
	SlicePartitionB = 3,
	SlicePartitionC = 4,
	IDRSlice = 5,
	SEI = 6,
	SPS = 7,
	PPS = 8,
	AccessUnitDelimiter = 9,
	EndOfSequence = 10,
	EndOfStream = 11,
	FillerData = 12,
	SPSExtension = 13,
	PrefixNAL = 14,
	SubsetSPS = 15,
	SliceAux = 19,
	SliceExt = 20
};

struct SPS
{
	StdVideoH264SequenceParameterSet sps;
	StdVideoH264ScalingLists scaling_lists;
	StdVideoH264SequenceParameterSetVui vui;
	int32_t offsets[256];
	StdVideoH264HrdParameters hrd;
};

struct PPS
{
	StdVideoH264PictureParameterSet pps;
	StdVideoH264ScalingLists scaling_lists;
};

struct ReferenceInfo : StdVideoDecodeH264ReferenceInfo
{
	int32_t FrameNumWrap;
	int32_t PicNum;
	int32_t LongTermFrameIdx;
};

struct ParseState
{
	// Is there a reasonable upper limit here?
	enum { MaxSPS = 256, MaxPPS = 256, MaxReference = 16 };
	SPS sps[MaxSPS];
	PPS pps[MaxPPS];
	bool sps_valid[MaxSPS] = {};
	bool pps_valid[MaxSPS] = {};

	int32_t prev_pic_order_cnt_msb = 0;
	int32_t prev_pic_order_cnt_lsb = 0;

	ReferenceInfo references[MaxReference];
	unsigned num_references = 0;
	NALUnitType last_slice_type = NALUnitType::EndOfSequence;
};

static void parse_scaling_list(BitStream &stream, uint8_t *scaling, unsigned size, bool &use_default)
{
	int last_scale = 8;
	int next_scale = 8;
	use_default = false;

	for (unsigned j = 0; j < size; j++)
	{
		if (next_scale != 0)
		{
			auto delta_scale = stream.se();
			next_scale = (last_scale + delta_scale + 256) % 256;
			use_default = j == 0 && next_scale == 0;
		}

		scaling[j] = next_scale == 0 ? last_scale : next_scale;
		last_scale = scaling[j];
	}
}

static void parse_hrd_parameters(BitStream &stream, StdVideoH264HrdParameters &hrd)
{
	hrd.cpb_cnt_minus1 = stream.ue();
	hrd.bit_rate_scale = stream.u(4);
	hrd.cpb_size_scale = stream.u(4);
	for (unsigned sched_sel_idx = 0; sched_sel_idx <= hrd.cpb_cnt_minus1; sched_sel_idx++)
	{
		hrd.bit_rate_value_minus1[sched_sel_idx] = stream.ue();
		hrd.cpb_size_value_minus1[sched_sel_idx] = stream.ue();
		hrd.cbr_flag[sched_sel_idx] = stream.u() << sched_sel_idx;
	}

	hrd.initial_cpb_removal_delay_length_minus1 = stream.u(5);
	hrd.cpb_removal_delay_length_minus1 = stream.u(5);
	hrd.dpb_output_delay_length_minus1 = stream.u(5);
	hrd.time_offset_length = stream.u(5);
}

static bool parse_sps(BitStream &stream, ParseState &state)
{
	StdVideoH264SequenceParameterSet new_sps = {};

	new_sps.profile_idc = StdVideoH264ProfileIdc(stream.u(8));
	new_sps.flags.constraint_set0_flag = stream.u();
	new_sps.flags.constraint_set1_flag = stream.u();
	new_sps.flags.constraint_set2_flag = stream.u();
	new_sps.flags.constraint_set3_flag = stream.u();
	new_sps.flags.constraint_set4_flag = stream.u();
	new_sps.flags.constraint_set5_flag = stream.u();
	// Reserved bits.
	if (stream.u(2) != 0)
		return false;
	new_sps.level_idc = StdVideoH264Level(stream.u(8));
	new_sps.seq_parameter_set_id = stream.ue();
	new_sps.chroma_format_idc = STD_VIDEO_H264_CHROMA_FORMAT_IDC_420;

	auto &sps_data = state.sps[new_sps.seq_parameter_set_id];
	sps_data = {};
	sps_data.sps = new_sps;
	auto &sps = sps_data.sps;
	auto &scaling_lists = sps_data.scaling_lists;
	auto &vui = sps_data.vui;

	state.sps_valid[new_sps.seq_parameter_set_id] = false;

	switch (sps.profile_idc)
	{
	case STD_VIDEO_H264_PROFILE_IDC_HIGH:
	case STD_VIDEO_H264_PROFILE_IDC_HIGH_444_PREDICTIVE:
	{
		sps.chroma_format_idc = StdVideoH264ChromaFormatIdc(stream.ue());

		if (sps.chroma_format_idc == STD_VIDEO_H264_CHROMA_FORMAT_IDC_444)
			sps.flags.separate_colour_plane_flag = stream.u();

		sps.bit_depth_luma_minus8 = stream.ue();
		sps.bit_depth_chroma_minus8 = stream.ue();
		sps.flags.qpprime_y_zero_transform_bypass_flag = stream.u();

		auto seq_scaling_matrix_present_flag = stream.u();
		if (seq_scaling_matrix_present_flag)
		{
			sps.pScalingLists = &scaling_lists;
			for (unsigned i = 0; i < (sps.chroma_format_idc != STD_VIDEO_H264_CHROMA_FORMAT_IDC_444 ? 8 : 12); i++)
			{
				bool present = stream.u();
				scaling_lists.scaling_list_present_mask |= present << i;
				if (present)
				{
					bool use_default_scaling_mask = false;
					if (i < 6)
						parse_scaling_list(stream, scaling_lists.ScalingList4x4[i], 16, use_default_scaling_mask);
					else
						parse_scaling_list(stream, scaling_lists.ScalingList8x8[i - 6], 64, use_default_scaling_mask);

					if (use_default_scaling_mask)
						scaling_lists.use_default_scaling_matrix_mask |= 1u << i;
				}
			}
		}
		break;
	}

	case STD_VIDEO_H264_PROFILE_IDC_BASELINE:
	case STD_VIDEO_H264_PROFILE_IDC_MAIN:
		break;

	default:
		LOGE("Unrecognized H.264 profile_idc %u.\n", unsigned(sps.profile_idc));
		return false;
	}

	sps.log2_max_frame_num_minus4 = stream.ue();
	sps.pic_order_cnt_type = StdVideoH264PocType(stream.ue());
	if (sps.pic_order_cnt_type == STD_VIDEO_H264_POC_TYPE_0)
	{
		sps.log2_max_pic_order_cnt_lsb_minus4 = stream.ue();
	}
	else if (sps.pic_order_cnt_type == STD_VIDEO_H264_POC_TYPE_1)
	{
		sps.flags.delta_pic_order_always_zero_flag = stream.u();
		sps.offset_for_non_ref_pic = stream.se();
		sps.offset_for_top_to_bottom_field = stream.se();
		sps.num_ref_frames_in_pic_order_cnt_cycle = stream.ue();
		sps.pOffsetForRefFrame = sps_data.offsets;
		for (unsigned i = 0; i < sps.num_ref_frames_in_pic_order_cnt_cycle; i++)
			sps_data.offsets[i] = stream.se();
	}
	sps.max_num_ref_frames = stream.ue();
	sps.flags.gaps_in_frame_num_value_allowed_flag = stream.u();
	sps.pic_width_in_mbs_minus1 = stream.ue();
	sps.pic_height_in_map_units_minus1 = stream.ue();
	sps.flags.frame_mbs_only_flag = stream.u();
	if (!sps.flags.frame_mbs_only_flag)
		sps.flags.mb_adaptive_frame_field_flag = stream.u();
	sps.flags.direct_8x8_inference_flag = stream.u();
	sps.flags.frame_cropping_flag = stream.u();
	if (sps.flags.frame_cropping_flag)
	{
		sps.frame_crop_left_offset = stream.ue();
		sps.frame_crop_right_offset = stream.ue();
		sps.frame_crop_top_offset = stream.ue();
		sps.frame_crop_bottom_offset = stream.ue();
	}

	sps.flags.vui_parameters_present_flag = stream.u();
	if (sps.flags.vui_parameters_present_flag)
	{
		sps.pSequenceParameterSetVui = &vui;
		vui.flags.aspect_ratio_info_present_flag = stream.u();
		if (vui.flags.aspect_ratio_info_present_flag)
		{
			vui.aspect_ratio_idc = StdVideoH264AspectRatioIdc(stream.u(8));
			if (vui.aspect_ratio_idc == STD_VIDEO_H264_ASPECT_RATIO_IDC_EXTENDED_SAR)
			{
				vui.sar_width = stream.u(16);
				vui.sar_height = stream.u(16);
			}
		}

		vui.flags.overscan_info_present_flag = stream.u();
		if (vui.flags.overscan_info_present_flag)
		{
			vui.flags.overscan_appropriate_flag = stream.u();
		}

		vui.flags.video_signal_type_present_flag = stream.u();
		if (vui.flags.video_signal_type_present_flag)
		{
			vui.video_format = stream.u(3);
			vui.flags.video_full_range_flag = stream.u();
			vui.flags.color_description_present_flag = stream.u();
			if (vui.flags.color_description_present_flag)
			{
				vui.color_primaries = stream.u(8);
				vui.transfer_characteristics = stream.u(8);
				vui.matrix_coefficients = stream.u(8);
			}
		}

		vui.flags.chroma_loc_info_present_flag = stream.u();
		if (vui.flags.chroma_loc_info_present_flag)
		{
			/*auto chroma_sample_loc_type_top_field =*/ stream.ue();
			/*auto chroma_sample_loc_type_bottom_field =*/ stream.ue();
		}

		vui.flags.timing_info_present_flag = stream.u();
		if (vui.flags.timing_info_present_flag)
		{
			vui.num_units_in_tick = stream.u(32);
			vui.time_scale = stream.u(32);
			vui.flags.fixed_frame_rate_flag = stream.u();
		}

		vui.flags.nal_hrd_parameters_present_flag = stream.u();
		if (vui.flags.nal_hrd_parameters_present_flag)
		{
			vui.pHrdParameters = &sps_data.hrd;
			parse_hrd_parameters(stream, *vui.pHrdParameters);
		}

		vui.flags.vcl_hrd_parameters_present_flag = stream.u();
		if (vui.flags.vcl_hrd_parameters_present_flag)
		{
			vui.pHrdParameters = &sps_data.hrd;
			parse_hrd_parameters(stream, *vui.pHrdParameters);
		}

		if (vui.flags.nal_hrd_parameters_present_flag || vui.flags.vcl_hrd_parameters_present_flag)
		{
			/*auto low_delay_hrd_flag =*/ stream.u();
		}

		/*auto pic_struct_present_flag =*/ stream.u();
		vui.flags.bitstream_restriction_flag = stream.u();
		if (vui.flags.bitstream_restriction_flag)
		{
			/*auto motion_vectors_over_pic_boundaries_flag =*/ stream.u();
			/*auto max_bytes_per_pic_denom =*/ stream.ue();
			/*auto max_bits_per_mb_denom =*/ stream.ue();
			/*auto log2_max_mv_length_horizontal =*/ stream.ue();
			/*auto log2_max_mv_length_vertical =*/ stream.ue();
			vui.max_num_reorder_frames = stream.ue();
			vui.max_dec_frame_buffering = stream.ue();
		}
	}

	state.sps_valid[new_sps.seq_parameter_set_id] = true;
	return true;
}

static bool parse_pps(BitStream &stream, ParseState &state)
{
	StdVideoH264PictureParameterSet new_pps = {};

	new_pps.pic_parameter_set_id = stream.ue();
	new_pps.seq_parameter_set_id = stream.ue();
	new_pps.flags.entropy_coding_mode_flag = stream.u();
	new_pps.flags.pic_order_present_flag = stream.u();

	if (!state.sps_valid[new_pps.seq_parameter_set_id])
	{
		LOGE("PPS: SPS %u is not valid yet.\n", new_pps.seq_parameter_set_id);
		return false;
	}
	state.pps_valid[new_pps.pic_parameter_set_id] = false;

	auto &pps_data = state.pps[new_pps.pic_parameter_set_id];
	pps_data = {};
	pps_data.pps = new_pps;
	auto &pps = pps_data.pps;
	auto &scaling_lists = pps_data.scaling_lists;

	auto num_slice_groups_minus1 = stream.ue();
	if (num_slice_groups_minus1 > 0)
	{
		auto slice_group_map_type = stream.ue();
		if (slice_group_map_type == 0)
		{
			for (unsigned igroup = 0; igroup <= num_slice_groups_minus1; igroup++)
			{
				/*auto run_length_minus1 =*/ stream.ue();
			}
		}
		else if (slice_group_map_type == 2)
		{
			for (unsigned igroup = 0; igroup < num_slice_groups_minus1; igroup++)
			{
				/*auto top_left =*/ stream.ue();
				/*auto bottom_right =*/ stream.ue();
			}
		}
		else if (slice_group_map_type == 3 || slice_group_map_type == 4 || slice_group_map_type == 5)
		{
			/*auto slice_group_change_direction_flag =*/ stream.u();
			/*auto slice_group_change_rate_minus1 =*/ stream.ue();
		}
		else if (slice_group_map_type == 6)
		{
			auto pic_size_in_map_units_minus1 = stream.ue();
			for (unsigned i = 0; i <= pic_size_in_map_units_minus1; i++)
			{
				/*auto slice_group_id =*/ stream.u();
			}

			LOGE("FIXME: Unimplemented slice_group_map_type = 6\n");
			return false;
		}
	}
	pps.num_ref_idx_l0_default_active_minus1 = stream.ue();
	pps.num_ref_idx_l1_default_active_minus1 = stream.ue();
	pps.flags.weighted_pred_flag = stream.u();
	pps.weighted_bipred_idc = StdVideoH264WeightedBipredIdc(stream.u(2));
	pps.flags.weighted_bipred_idc_flag = pps.weighted_bipred_idc != STD_VIDEO_H264_WEIGHTED_BIPRED_IDC_INVALID;
	pps.pic_init_qp_minus26 = stream.se();
	pps.pic_init_qs_minus26 = stream.se();
	pps.chroma_qp_index_offset = stream.se();
	pps.flags.deblocking_filter_control_present_flag = stream.u();
	pps.flags.constrained_intra_pred_flag = stream.u();
	pps.flags.redundant_pic_cnt_present_flag = stream.u();
	if (stream.more_data())
	{
		pps.flags.transform_8x8_mode_flag = stream.u();
		pps.flags.pic_scaling_matrix_present_flag = stream.u();
		if (pps.flags.pic_scaling_matrix_present_flag)
		{
			for (unsigned i = 0; i < 6u + 2u * pps.flags.transform_8x8_mode_flag; i++)
			{
				pps.pScalingLists = &scaling_lists;
				auto scaling_list_present_flag = stream.u();
				if (scaling_list_present_flag)
				{
					scaling_lists.scaling_list_present_mask |= 1u << i;
					bool use_default = false;

					if (i < 6)
						parse_scaling_list(stream, scaling_lists.ScalingList4x4[i], 16, use_default);
					else
						parse_scaling_list(stream, scaling_lists.ScalingList8x8[i - 6], 64, use_default);

					if (use_default)
						scaling_lists.use_default_scaling_matrix_mask |= 1u << i;
				}
			}
		}

		pps.second_chroma_qp_index_offset = stream.se();
	}

	state.pps_valid[new_pps.pic_parameter_set_id] = true;
	return true;
}

enum class SliceType
{
	P,
	B,
	I,
	SP,
	SI,
	Count
};

static SliceType decode_slice_type(uint32_t slice_type)
{
	return SliceType(slice_type % unsigned(SliceType::Count));
}

static bool update_poc(BitStream &stream, ParseState &parse, const SPS &sps, const PPS &pps,
					   StdVideoDecodeH264PictureInfo &pic)
{
	auto poc_type = sps.sps.pic_order_cnt_type;
	if (poc_type == STD_VIDEO_H264_POC_TYPE_0)
	{
		int32_t pic_order_cnt_lsb = stream.u(sps.sps.log2_max_pic_order_cnt_lsb_minus4 + 4);
		// What is the default for delta_pic_order_cnt_bottom?
		int32_t delta_pic_order_cnt_bottom = 0;
		if (pps.pps.flags.pic_order_present_flag && !pic.flags.field_pic_flag)
			delta_pic_order_cnt_bottom = stream.se();

		int32_t MaxPicOrderCntLsb = 1 << (sps.sps.log2_max_pic_order_cnt_lsb_minus4 + 4);
		int32_t PicOrderCntMsb = 0;

		if (pic.flags.is_intra)
		{
			parse.prev_pic_order_cnt_lsb = 0;
			parse.prev_pic_order_cnt_msb = 0;
		}
		else
		{
			if ((pic_order_cnt_lsb < parse.prev_pic_order_cnt_lsb) &&
			    ((parse.prev_pic_order_cnt_lsb - pic_order_cnt_lsb) >= (MaxPicOrderCntLsb / 2)))
			{
				// Checks positive overflow.
				PicOrderCntMsb = parse.prev_pic_order_cnt_msb + MaxPicOrderCntLsb;
			}
			else if ((pic_order_cnt_lsb > parse.prev_pic_order_cnt_lsb) &&
			         (pic_order_cnt_lsb - parse.prev_pic_order_cnt_lsb) > (MaxPicOrderCntLsb / 2))
			{
				// Checks negative overflow.
				PicOrderCntMsb = parse.prev_pic_order_cnt_msb - MaxPicOrderCntLsb;
			}
			else
			{
				PicOrderCntMsb = parse.prev_pic_order_cnt_msb;
			}
		}

		pic.PicOrderCnt[0] = PicOrderCntMsb + pic_order_cnt_lsb;
		if (!pic.flags.field_pic_flag)
			pic.PicOrderCnt[1] = pic.PicOrderCnt[0] + delta_pic_order_cnt_bottom;
		else
			pic.PicOrderCnt[1] = PicOrderCntMsb + pic_order_cnt_lsb;

		parse.prev_pic_order_cnt_msb = PicOrderCntMsb;
		parse.prev_pic_order_cnt_lsb = pic_order_cnt_lsb;
	}
	else
	{
		LOGW("Unsupported frame order type.\n");
		return false;
	}

	return true;
}

static bool update_reference_lists(BitStream &stream, ParseState &parse, const SPS &sps, const PPS &pps,
                                   const StdVideoDecodeH264PictureInfo &pic)
{
	int32_t MaxFrameNum = 1 << (sps.sps.log2_max_frame_num_minus4 + 4);

	for (unsigned i = 0; i < parse.num_references; i++)
	{
		auto &ref = parse.references[i];
		if (ref.FrameNum > pic.frame_num)
			ref.FrameNumWrap = ref.FrameNum - MaxFrameNum;
		else
			ref.FrameNumWrap = ref.FrameNum;

		if (!pic.flags.field_pic_flag)
		{
			if (ref.flags.is_long_term)
				ref.PicNum = ref.LongTermFrameIdx; // LongTermFrameIdx
			else
				ref.PicNum = ref.FrameNumWrap;
		}
		else
		{
			LOGW("Interlacing not supported.\n");
			return false;
		}
	}

	std::sort(parse.references, parse.references + parse.num_references,
	          [](const ReferenceInfo &a, const ReferenceInfo &b) -> bool {
		          if (a.flags.is_long_term != b.flags.is_long_term)
			          return a.flags.is_long_term < b.flags.is_long_term;
		          return a.PicNum > b.PicNum;
	          });

	parse.num_references = std::min<unsigned>(parse.num_references, pps.pps.num_ref_idx_l0_default_active_minus1 + 1);

	return true;
}

static bool parse_slice_header(BitStream &stream, ParseState &state, bool idr, bool is_reference)
{
	auto first_mb_in_slice = stream.ue();
	if (first_mb_in_slice != 0)
	{
		LOGW("first_mb_in_slice %u != 0. Unsupported.\n", first_mb_in_slice);
		return false;
	}

	auto slice_type = decode_slice_type(stream.ue());
	auto pic_parameter_set_id = stream.ue();

	StdVideoDecodeH264PictureInfo pic = {};

	if (!state.pps_valid[pic_parameter_set_id])
	{
		LOGE("PPS %u is not valid.\n", pic_parameter_set_id);
		return false;
	}

	auto &pps = state.pps[pic_parameter_set_id];
	auto &sps = state.sps[pps.pps.seq_parameter_set_id];

	if (sps.sps.flags.separate_colour_plane_flag)
	{
		/*auto color_plane_id =*/ stream.u(2);
	}

	pic.frame_num = stream.u(sps.sps.log2_max_frame_num_minus4 + 4);
	if (!sps.sps.flags.frame_mbs_only_flag)
	{
		pic.flags.field_pic_flag = stream.u();
		if (pic.flags.field_pic_flag)
			pic.flags.bottom_field_flag = stream.u();
		LOGW("Interlacing not supported.\n");
	}

	if (idr)
	{
		pic.idr_pic_id = stream.ue();
	}

	pic.seq_parameter_set_id = sps.sps.seq_parameter_set_id;
	pic.pic_parameter_set_id = pps.pps.pic_parameter_set_id;
	// Not sure if this should be based on NAL type or slice type.
	pic.flags.is_intra = idr;
	pic.flags.is_reference = is_reference;
	// What is complementary_field_pair? Probably interlacing related ... :V

	if (!update_poc(stream, state, sps, pps, pic))
		return false;

	if (!update_reference_lists(stream, state, sps, pps, pic))
		return false;

	return true;
}

static bool parse_idr_slice(BitStream &stream, ParseState &state, uint32_t nal_ref_idc)
{
	return parse_slice_header(stream, state, true, nal_ref_idc != 0);
}

static bool parse_non_idr_slice(BitStream &stream, ParseState &state, uint32_t nal_ref_idc)
{
	return parse_slice_header(stream, state, false, nal_ref_idc != 0);
}

static bool parse_nal(const uint8_t *ptr, size_t size, ParseState &parse_state)
{
	if (size < 1)
	{
		LOGE("Size of NALU cannot be 0.\n");
		return false;
	}

	BitStream stream{ptr, size};

	if (stream.u() != 0)
	{
		LOGE("forbidden_zero_bit != 0\n");
		return false;
	}

	auto nal_ref_idc = stream.u(2);
	auto nal_unit_type = stream.u(5);

	switch (NALUnitType(nal_unit_type))
	{
	case NALUnitType::SPS:
		if (!parse_sps(stream, parse_state))
			return false;
		break;
	case NALUnitType::PPS:
		if (!parse_pps(stream, parse_state))
			return false;
		break;
	case NALUnitType::EndOfStream:
		LOGI("End of stream!\n");
		return false;
	case NALUnitType::EndOfSequence:
		LOGI("End of sequence!\n");
		return false;
	case NALUnitType::IDRSlice:
		if (!parse_idr_slice(stream, parse_state, nal_ref_idc))
			LOGE("Failed to parse IDR slice.\n");
		break;
	case NALUnitType::NonIDRSlice:
		if (!parse_non_idr_slice(stream, parse_state, nal_ref_idc))
			LOGE("Failed to parse non-IDR slice.\n");
		break;
	default:
		LOGI("nal_unit_type = %u, nal_ref_idc = %u, size = %zu.\n", nal_unit_type, nal_ref_idc, size);
		break;
	}

	parse_state.last_slice_type = NALUnitType(nal_unit_type);

	return true;
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return EXIT_FAILURE;

	Granite::Global::init(Granite::Global::MANAGER_FEATURE_FILESYSTEM_BIT);
	auto *fs = GRANITE_FILESYSTEM();

	if (!Vulkan::Context::init_loader(nullptr))
		return EXIT_FAILURE;
	Vulkan::Context context;
	if (!context.init_instance_and_device(nullptr, 0, nullptr, 0))
		return EXIT_FAILURE;
	Vulkan::Device device;
	device.set_context(context);

	auto file = fs->open(argv[1]);
	if (!file)
	{
		LOGE("Failed to open file: %s.\n", argv[1]);
		return EXIT_FAILURE;
	}

	auto *mapped_file = static_cast<const uint8_t *>(file->map());
	size_t mapped_size = file->get_size();
	auto *mapped_end = mapped_file + mapped_size;
	bool zero_byte = false;

	ParseState parse_state;

	const void *idr_slice_data = nullptr;
	size_t idr_slice_size = 0;

	while (mapped_size)
	{
		if (!find_start_code(mapped_file, mapped_size, &zero_byte))
		{
			LOGE("Failed to locate NALU start code.\n");
			return EXIT_FAILURE;
		}

		size_t prefix_size = zero_byte ? 4 : 3;

		mapped_file += prefix_size;
		mapped_size -= prefix_size;
		auto *packet = mapped_file;

		auto *end_packet = mapped_end;
		if (find_start_code(mapped_file, mapped_size, nullptr))
			end_packet = mapped_file;

		ptrdiff_t packet_size = end_packet - packet;
		if (packet_size <= 0)
		{
			LOGI("EOF\n");
			break;
		}

		if (!parse_nal(packet, packet_size, parse_state))
		{
			LOGE("Failed to parse NAL.\n");
			return EXIT_FAILURE;
		}

		if (parse_state.last_slice_type == NALUnitType::IDRSlice)
		{
			idr_slice_data = packet;
			idr_slice_size = packet_size;
			break;
		}
	}

#if 1
	if (idr_slice_size)
	{
		auto &pps = parse_state.pps[0];
		auto &sps = parse_state.sps[0];

		VkDevice vk_device = device.get_device();
		auto &table = device.get_device_table();

		VkVideoCapabilitiesKHR video_caps = { VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR };
		VkVideoDecodeH264CapabilitiesEXT h264_video_caps = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_EXT };
		video_caps.pNext = &h264_video_caps;

		VkVideoProfileKHR video_profile = { VK_STRUCTURE_TYPE_VIDEO_PROFILE_KHR };
		video_profile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
		video_profile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
		video_profile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
		video_profile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_EXT;

		VkVideoDecodeH264ProfileEXT h264_profile = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_EXT };
		h264_profile.pictureLayout = VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_EXT;
		h264_profile.stdProfileIdc = sps.sps.profile_idc;
		video_profile.pNext = &h264_profile;

		auto gpa = Vulkan::Context::get_instance_proc_addr();
		auto vkGetPhysicalDeviceVideoCapabilitiesKHR = (PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR)gpa(
				context.get_instance(),
				"vkGetPhysicalDeviceVideoCapabilitiesKHR");
		auto vkGetPhysicalDeviceVideoFormatPropertiesKHR = (PFN_vkGetPhysicalDeviceVideoFormatPropertiesKHR)gpa(
				context.get_instance(),
				"vkGetPhysicalDeviceVideoFormatPropertiesKHR");

		auto res = vkGetPhysicalDeviceVideoCapabilitiesKHR(device.get_physical_device(), &video_profile, &video_caps);
		if (res != VK_SUCCESS)
		{
			LOGE("Codec not supported!\n");
			return EXIT_FAILURE;
		}

		VkVideoProfilesKHR video_profiles = {VK_STRUCTURE_TYPE_VIDEO_PROFILES_KHR };
		video_profiles.profileCount = 1;
		video_profiles.pProfiles = &video_profile;

		VkPhysicalDeviceVideoFormatInfoKHR format_info = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR };
		format_info.imageUsage = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;
		format_info.pVideoProfiles = &video_profiles;

		uint32_t props_count = 0;
		vkGetPhysicalDeviceVideoFormatPropertiesKHR(device.get_physical_device(), &format_info, &props_count, nullptr);
		Util::SmallVector<VkVideoFormatPropertiesKHR> format_properties(props_count);
		for (auto &fmt : format_properties)
			fmt.sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR;
		vkGetPhysicalDeviceVideoFormatPropertiesKHR(device.get_physical_device(), &format_info, &props_count, format_properties.data());

		VkSamplerYcbcrConversionCreateInfo conversion_info = { VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO };
		conversion_info.format = format_properties[0].format;
		conversion_info.xChromaOffset = VK_CHROMA_LOCATION_COSITED_EVEN;
		conversion_info.yChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
		conversion_info.ycbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
		conversion_info.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
		conversion_info.chromaFilter = VK_FILTER_LINEAR;
		auto *ycbcr = device.request_immutable_ycbcr_conversion(conversion_info);

		std::vector<Vulkan::ImageHandle> dbp_images(sps.sps.max_num_ref_frames);
		Vulkan::ImageCreateInfo dbp_image_info = {};
		dbp_image_info.type = VK_IMAGE_TYPE_2D;
		dbp_image_info.width = (sps.sps.pic_width_in_mbs_minus1 + 1) * 16;
		dbp_image_info.height = (sps.sps.pic_height_in_map_units_minus1 + 1) * 16;
		dbp_image_info.depth = 1;
		dbp_image_info.levels = 1;
		dbp_image_info.layers = 1;
		dbp_image_info.format = format_properties[0].format;
		dbp_image_info.usage = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;
		dbp_image_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		dbp_image_info.samples = VK_SAMPLE_COUNT_1_BIT;
		dbp_image_info.ycbcr_conversion = ycbcr;

		// Driver broken here? Spec says to use VkVideoProfilesKHR,
		// but have to pass ProfileKHR here, or driver segfaults ...
		dbp_image_info.pnext = &video_profile;
		for (auto &image : dbp_images)
			image = device.create_image(dbp_image_info);

		VkVideoSessionKHR video_session;
		VkVideoSessionCreateInfoKHR session_info = { VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR };
		session_info.maxCodedExtent.width = 1920;
		session_info.maxCodedExtent.height = 1088;
		session_info.pVideoProfile = &video_profile;
		session_info.pictureFormat = format_properties[0].format;
		session_info.referencePicturesFormat = format_properties[0].format;
		session_info.queueFamilyIndex = context.get_queue_info().family_indices[Vulkan::QUEUE_INDEX_VIDEO_DECODE];
		session_info.maxReferencePicturesActiveCount = sps.sps.max_num_ref_frames;
		session_info.maxReferencePicturesSlotsCount = sps.sps.max_num_ref_frames;
		session_info.flags = VK_VIDEO_SESSION_CREATE_DEFAULT_KHR;

		VkVideoDecodeH264SessionCreateInfoEXT h264_decode_session_info = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_CREATE_INFO_EXT };
		VkExtensionProperties h264_ext = { VK_STD_VULKAN_VIDEO_CODEC_H264_EXTENSION_NAME, VK_STD_VULKAN_VIDEO_CODEC_H264_SPEC_VERSION };
		h264_decode_session_info.pStdExtensionVersion = &h264_ext;
		session_info.pNext = &h264_decode_session_info;

		res = table.vkCreateVideoSessionKHR(vk_device, &session_info, nullptr, &video_session);
		if (res != VK_SUCCESS)
		{
			LOGE("Failed to create video session.\n");
			return EXIT_FAILURE;
		}

		uint32_t session_mem_req_count = 0;
		table.vkGetVideoSessionMemoryRequirementsKHR(vk_device, video_session, &session_mem_req_count, nullptr);
		Util::SmallVector<VkVideoGetMemoryPropertiesKHR> mem_props(session_mem_req_count);
		Util::SmallVector<VkMemoryRequirements2> mem_reqs2(session_mem_req_count);
		for (uint32_t i = 0; i < session_mem_req_count; i++)
		{
			mem_props[i].sType = VK_STRUCTURE_TYPE_VIDEO_GET_MEMORY_PROPERTIES_KHR;
			mem_props[i].pMemoryRequirements = &mem_reqs2[i];
			mem_reqs2[i].sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
		}
		res = table.vkGetVideoSessionMemoryRequirementsKHR(vk_device, video_session, &session_mem_req_count, mem_props.data());
		if (res != VK_SUCCESS)
			return EXIT_FAILURE;

		Util::SmallVector<VkVideoBindMemoryKHR> mem_binds(session_mem_req_count);
		Util::SmallVector<Vulkan::DeviceAllocationOwnerHandle> allocs(session_mem_req_count);
		for (uint32_t i = 0; i < session_mem_req_count; i++)
		{
			mem_binds[i].sType = VK_STRUCTURE_TYPE_VIDEO_BIND_MEMORY_KHR;

			Vulkan::MemoryAllocateInfo alloc_info;
			alloc_info.requirements = mem_reqs2[i].memoryRequirements;
			alloc_info.required_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			alloc_info.mode = Vulkan::AllocationMode::OptimalResource;
			auto allocation = device.allocate_memory(alloc_info);
			if (!allocation)
			{
				alloc_info.required_properties = 0;
				allocation = device.allocate_memory(alloc_info);
			}

			mem_binds[i].memory = allocation->get_allocation().get_memory();
			mem_binds[i].memoryBindIndex = mem_props[i].memoryBindIndex;
			mem_binds[i].memoryOffset = allocation->get_allocation().get_offset();
			mem_binds[i].memorySize = allocation->get_allocation().get_size();
			allocs[i] = std::move(allocation);
		}
		res = table.vkBindVideoSessionMemoryKHR(vk_device, video_session, session_mem_req_count, mem_binds.data());
		if (res != VK_SUCCESS)
			return EXIT_FAILURE;

		VkVideoSessionParametersCreateInfoKHR session_param_create_info = { VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR };
		session_param_create_info.videoSession = video_session;

		VkVideoDecodeH264SessionParametersCreateInfoEXT h264_session_parameters = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_EXT };
		h264_session_parameters.maxPpsStdCount = 1;
		h264_session_parameters.maxSpsStdCount = 1;
		session_param_create_info.pNext = &h264_session_parameters;

		VkVideoDecodeH264SessionParametersAddInfoEXT param_add_info = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_EXT };
		h264_session_parameters.pParametersAddInfo = &param_add_info;
		param_add_info.ppsStdCount = 1;
		param_add_info.spsStdCount = 1;
		param_add_info.pPpsStd = &pps.pps;
		param_add_info.pSpsStd = &sps.sps;

		VkVideoSessionParametersKHR video_session_parameters;
		res = table.vkCreateVideoSessionParametersKHR(vk_device, &session_param_create_info, nullptr, &video_session_parameters);
		if (res != VK_SUCCESS)
			return EXIT_FAILURE;

		auto cmd = device.request_command_buffer(Vulkan::CommandBuffer::Type::VideoDecode);
		auto vk_cmd = cmd->get_command_buffer();
		VkVideoBeginCodingInfoKHR begin_coding_info = { VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR };
		begin_coding_info.videoSession = video_session;
		begin_coding_info.videoSessionParameters = video_session_parameters;
		begin_coding_info.codecQualityPreset = VK_VIDEO_CODING_QUALITY_PRESET_NORMAL_BIT_KHR;
		begin_coding_info.referenceSlotCount = sps.sps.max_num_ref_frames;
		Util::SmallVector<VkVideoReferenceSlotKHR> reference_slots(begin_coding_info.referenceSlotCount);
		Util::SmallVector<VkVideoDecodeH264DpbSlotInfoEXT> h264_slots(begin_coding_info.referenceSlotCount);
		Util::SmallVector<VkVideoPictureResourceKHR> picture_resource(begin_coding_info.referenceSlotCount);

		StdVideoDecodeH264ReferenceInfo ref_info = {};

		for (unsigned i = 0; i < begin_coding_info.referenceSlotCount; i++)
		{
			reference_slots[i].sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_KHR;
			reference_slots[i].slotIndex = int8_t(i);
			reference_slots[i].pPictureResource = &picture_resource[i];

			h264_slots[i].sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_EXT;
			h264_slots[i].pStdReferenceInfo = &ref_info;
			reference_slots[i].pNext = &h264_slots[i];

			picture_resource[i].sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_KHR;
			picture_resource[i].codedExtent.width = dbp_images[i]->get_width();
			picture_resource[i].codedExtent.height = dbp_images[i]->get_height();
			picture_resource[i].imageViewBinding = dbp_images[i]->get_view().get_view();
		}

		begin_coding_info.pReferenceSlots = reference_slots.data();
		table.vkCmdBeginVideoCodingKHR(vk_cmd, &begin_coding_info);

		VkVideoDecodeInfoKHR decode_info = { VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR };
		decode_info.codedExtent.width = 1920;
		decode_info.codedExtent.height = 1080;
		decode_info.dstPictureResource.sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_KHR;
		decode_info.dstPictureResource.codedExtent = { 1920, 1080 };
		decode_info.dstPictureResource.imageViewBinding = dbp_images[0]->get_view().get_view();

		Vulkan::BufferCreateInfo decode_buffer_info = {};
		decode_buffer_info.usage = VK_BUFFER_USAGE_VIDEO_DECODE_DST_BIT_KHR;
		decode_buffer_info.domain = Vulkan::BufferDomain::Host;
		decode_buffer_info.size = idr_slice_size;
		Vulkan::BufferHandle decode_buffer = device.create_buffer(decode_buffer_info, idr_slice_data);
		decode_info.srcBuffer = decode_buffer->get_buffer();
		decode_info.srcBufferOffset = 0;
		decode_info.srcBufferRange = idr_slice_size;

		VkVideoReferenceSlotKHR setup_slot = { VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_KHR };
		setup_slot.slotIndex = -1;
		setup_slot.pPictureResource = &picture_resource[0];
		decode_info.pSetupReferenceSlot = &setup_slot;
		decode_info.pReferenceSlots = reference_slots.data();
		decode_info.referenceSlotCount = uint32_t(reference_slots.size());

		table.vkCmdDecodeVideoKHR(vk_cmd, &decode_info);

		VkVideoEndCodingInfoKHR end_coding_info = { VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR };
		table.vkCmdEndVideoCodingKHR(vk_cmd, &end_coding_info);
		device.submit(cmd);
	}
#endif
}