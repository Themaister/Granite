#pragma once

#include <string>

namespace Vulkan
{
static inline const char *layout_to_string(VkImageLayout layout)
{
	switch (layout)
	{
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		return "SHADER_READ_ONLY";
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
		return "DS_READ_ONLY";
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		return "DS";
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		return "COLOR";
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		return "TRANSFER_DST";
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		return "TRANSFER_SRC";
	case VK_IMAGE_LAYOUT_GENERAL:
		return "GENERAL";
	case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
		return "PRESENT";
	default:
		return "UNDEFINED";
	}
}

static inline std::string access_flags_to_string(VkAccessFlags flags)
{
	std::string result;

	if (flags & VK_ACCESS_SHADER_READ_BIT)
		result += "SHADER_READ ";
	if (flags & VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
		result += "DS_WRITE ";
	if (flags & VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT)
		result += "DS_READ ";
	if (flags & VK_ACCESS_COLOR_ATTACHMENT_READ_BIT)
		result += "COLOR_READ ";
	if (flags & VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
		result += "COLOR_WRITE ";
	if (flags & VK_ACCESS_INPUT_ATTACHMENT_READ_BIT)
		result += "INPUT_READ ";
	if (flags & VK_ACCESS_TRANSFER_WRITE_BIT)
		result += "TRANSFER_WRITE ";
	if (flags & VK_ACCESS_TRANSFER_READ_BIT)
		result += "TRANSFER_READ ";

	return result;
}
}