#include "mesh.hpp"

using namespace Util;

namespace Granite
{
Util::Hash StaticMesh::get_instance_key() const
{
	Hasher h;
	h.u64(vbo->get_cookie());
	h.u64(ibo->get_cookie());
	h.u32(index_count);
	h.u32(vertex_offset);
	h.u32(index_count);
	h.pointer(material.get());
	for (auto &attr : attributes)
	{
		h.u32(attr.format);
		h.u32(attr.offset);
	}
	return h.get();
}
}