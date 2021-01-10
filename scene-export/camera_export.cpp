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

#include "camera_export.hpp"
#include "rapidjson_wrapper.hpp"

using namespace rapidjson;
using namespace std;

namespace Granite
{
string export_cameras_to_json(const vector<RecordedCamera> &recorded_cameras)
{
	Document doc;
	doc.SetObject();
	auto &allocator = doc.GetAllocator();

	Value cameras(kArrayType);
	for (auto &cam : recorded_cameras)
	{
		Value c(kObjectType);
		c.AddMember("fovy", cam.fovy, allocator);
		c.AddMember("aspect", cam.aspect, allocator);
		c.AddMember("znear", cam.znear, allocator);
		c.AddMember("zfar", cam.zfar, allocator);

		Value dir(kArrayType);
		dir.PushBack(cam.direction.x, allocator);
		dir.PushBack(cam.direction.y, allocator);
		dir.PushBack(cam.direction.z, allocator);
		c.AddMember("direction", dir, allocator);

		Value pos(kArrayType);
		pos.PushBack(cam.position.x, allocator);
		pos.PushBack(cam.position.y, allocator);
		pos.PushBack(cam.position.z, allocator);
		c.AddMember("position", pos, allocator);

		Value up(kArrayType);
		up.PushBack(cam.up.x, allocator);
		up.PushBack(cam.up.y, allocator);
		up.PushBack(cam.up.z, allocator);
		c.AddMember("up", up, allocator);

		cameras.PushBack(c, allocator);
	}
	doc.AddMember("cameras", cameras, allocator);

	StringBuffer buffer;
	PrettyWriter<StringBuffer> writer(buffer);
	//Writer<StringBuffer> writer(buffer);
	doc.Accept(writer);
	return buffer.GetString();
}
}