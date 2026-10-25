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

		struct Request
		{
			String Path;
			bool Srgb = true;
		};

		// Loads every requested texture that is not cached yet, decoding them in parallel on the
		// job system and uploading from the calling thread. Returns how many were loaded.
		int32 Preload(const Array<Request>& requests);

		void Clear();

	private:
		GPUDevice& m_Device;
		void CheckRevision(const String& path, const String& key);
		HashMap<String, RefPtr<GPUTexture>> m_Textures;
		HashMap<String, uint64> m_Revisions;
		Array<String> m_Failed;
	};
}
