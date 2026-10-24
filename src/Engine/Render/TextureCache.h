#pragma once

#include "Engine/Render/GPUDevice.h"

namespace FP
{
	// Loads images relative to Content once and hands out shared GPU textures.
	// Color maps (base color, emissive) are sRGB. Data maps (normal, roughness and so on) are linear.
	class TextureCache
	{
	public:
		explicit TextureCache(GPUDevice& device) : m_Device(device) {}

		// Returns null when the path is empty or the file fails to load (logged once).
		GPUTexture* Get(const String& path, bool srgb);

		void Clear();

	private:
		GPUDevice& m_Device;
		HashMap<String, RefPtr<GPUTexture>> m_Textures;
		Array<String> m_Failed;
	};
}
