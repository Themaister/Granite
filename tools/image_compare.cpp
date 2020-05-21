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

#include "cli_parser.hpp"
#include "logging.hpp"
#include "texture_files.hpp"
#include "filesystem.hpp"
#include "thread_group.hpp"
#include "path.hpp"
#include <vector>
#include <algorithm>
#include "stb_image_write.h"
#include "muglm/muglm_impl.hpp"
#include <string.h>

using namespace Util;
using namespace Granite;
using namespace std;

static void save_diff_image(const string &path,
                            const SceneFormats::MemoryMappedTexture &a,
                            const SceneFormats::MemoryMappedTexture &b)
{
	if (a.get_layout().get_format() != b.get_layout().get_format())
	{
		LOGE("Format mismatch.\n");
		return;
	}

	if (a.get_layout().get_format() != VK_FORMAT_R8G8B8A8_UNORM &&
	    a.get_layout().get_format() != VK_FORMAT_R8G8B8A8_SRGB)
	{
		LOGE("Unsupported format.\n");
		return;
	}

	int width = a.get_layout().get_width();
	int height = a.get_layout().get_height();
	vector<uint8_t> buffer(width * height * 4);

	auto *src_a = static_cast<const uint8_t *>(a.get_layout().data());
	auto *src_b = static_cast<const uint8_t *>(b.get_layout().data());
	auto *dst = buffer.data();

	for (int pix = 0; pix < width * height; pix++, dst += 4, src_a += 4, src_b += 4)
	{
		int diff_r = src_a[0] - src_b[0];
		int diff_g = src_a[1] - src_b[1];
		int diff_b = src_a[2] - src_b[2];
		dst[0] = min(diff_r * 16, 255);
		dst[1] = min(diff_g * 16, 255);
		dst[2] = min(diff_b * 16, 255);
		dst[3] = 255;
	}

	if (!stbi_write_png(path.c_str(), width, height, 4, buffer.data(), width * 4))
		LOGE("Failed to save diff-png to %s.\n", path.c_str());
}

static double compare_images(const SceneFormats::MemoryMappedTexture &a, const SceneFormats::MemoryMappedTexture &b)
{
	if (a.get_layout().get_format() != b.get_layout().get_format())
	{
		LOGE("Format mismatch.\n");
		return 0.0;
	}

	if (a.get_layout().get_format() != VK_FORMAT_R8G8B8A8_SRGB &&
	    a.get_layout().get_format() != VK_FORMAT_R8G8B8A8_UNORM)
	{
		LOGE("Unsupported format.\n");
		return 0.0;
	}

	if (a.get_layout().get_width() != b.get_layout().get_width() ||
	    a.get_layout().get_height() != b.get_layout().get_height())
	{
		LOGE("Dimension mismatch.\n");
		return 0.0;
	}

	int width = a.get_layout().get_width();
	int height = a.get_layout().get_height();

	auto *src_a = static_cast<const uint8_t *>(a.get_layout().data());
	auto *src_b = static_cast<const uint8_t *>(b.get_layout().data());

	double peak_energy = 255.0 * 255.0 * width * height * 3.0;
	double error_energy = 0.0;
	for (int pix = 0; pix < width * height; pix++, src_a += 4, src_b += 4)
	{
		int diff_r = src_a[0] - src_b[0];
		int diff_g = src_a[1] - src_b[1];
		int diff_b = src_a[2] - src_b[2];
		error_energy += diff_r * diff_r;
		error_energy += diff_g * diff_g;
		error_energy += diff_b * diff_b;
	}

	return 10.0 * muglm::log10(peak_energy / error_energy);
}

int main(int argc, char *argv[])
{
	struct Arguments
	{
		vector<string> inputs;
		string diff;
		double threshold = -1.0;
	} args;
	CLICallbacks cbs;

	cbs.add("--threshold", [&](CLIParser &parser) {
		args.threshold = parser.next_double();
	});
	cbs.add("--diff", [&](CLIParser &parser) {
		args.diff = parser.next_string();
	});
	cbs.default_handler = [&](const char *arg) {
		args.inputs.push_back(arg);
	};

	CLIParser parser(move(cbs), argc - 1, argv + 1);
	if (!parser.parse())
		return 1;

	if (args.inputs.size() != 2)
	{
		LOGE("Need two inputs.\n");
		return 1;
	}

	ThreadGroup workers;
	workers.start(thread::hardware_concurrency());

	FileStat a_stat, b_stat;
	if (Global::filesystem()->stat(args.inputs[0], a_stat) && a_stat.type == PathType::Directory &&
	    Global::filesystem()->stat(args.inputs[1], b_stat) && b_stat.type == PathType::Directory)
	{
		auto a_list = Global::filesystem()->list(args.inputs[0]);
		auto b_list = Global::filesystem()->list(args.inputs[1]);

		std::sort(begin(a_list), end(a_list), [](const ListEntry &a, const ListEntry &b) {
			return strcmp(a.path.c_str(), b.path.c_str()) < 0;
		});
		std::sort(begin(b_list), end(b_list), [](const ListEntry &a, const ListEntry &b) {
			return strcmp(a.path.c_str(), b.path.c_str()) < 0;
		});

		if (a_list.size() != b_list.size())
		{
			LOGE("Folder size is not identical.\n");
			return 1;
		}

		vector<double> psnrs(a_list.size());
		vector<bool> ignore(a_list.size());

		auto task = workers.create_task();

		for (unsigned i = 0; i < a_list.size(); i++)
		{
			task->enqueue_task([&a_list, &b_list, &psnrs, &ignore, i]() {
				auto a = load_texture_from_file(a_list[i].path);
				auto b = load_texture_from_file(b_list[i].path);
				if (a.empty() || b.empty())
				{
					psnrs[i] = 0.0;
					ignore[i] = true;
				}

				psnrs[i] = compare_images(a, b);

			});
		}

		task->flush();
		task->wait();

		for (unsigned i = 0; i < a_list.size(); i++)
		{
			if (ignore[i])
				continue;

			LOGI("%s | %s | PSNR: %.f dB\n", a_list[i].path.c_str(), b_list[i].path.c_str(), psnrs[i]);

			if (args.threshold >= 0.0)
			{
				if (psnrs[i] < args.threshold)
				{
					LOGE("PSNR is too low, failure!\n");
					return 1;
				}
			}
		}
	}
	else
	{
		auto a = load_texture_from_file(args.inputs[0]);
		auto b = load_texture_from_file(args.inputs[1]);

		if (a.empty())
		{
			LOGE("Failed to load texture: %s\n", args.inputs[0].c_str());
			return 1;
		}

		if (b.empty())
		{
			LOGE("Failed to load texture: %s\n", args.inputs[1].c_str());
			return 1;
		}

		if (!args.diff.empty())
		{
			save_diff_image(args.diff, a, b);
		}

		double psnr = compare_images(a, b);
		LOGI("PSNR: %.f dB\n", psnr);

		if (args.threshold >= 0.0)
		{
			if (psnr < args.threshold)
			{
				LOGE("PSNR is too low, failure!\n");
				return 1;
			}
		}
	}

	return 0;
}