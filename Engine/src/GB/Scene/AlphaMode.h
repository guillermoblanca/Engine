#pragma once

namespace GB
{
	// How a surface uses its alpha (glTF alphaMode): shared by the models' CPU materials (GB/Assets/Model.h) and the render's (GB/Renderer/Material.h).
	enum class AlphaMode { Opaque, Mask, Blend };
}
