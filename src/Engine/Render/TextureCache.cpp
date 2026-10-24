#include "Engine/Render/TextureCache.h"
#include "Engine/Paths.h"

#include <stb_image.h>

namespace FP
{
	GPUTexture* TextureCache::Get(const String& path, bool srgb)
	{
		if (path.IsEmpty())
			return nullptr;

		// The same file can be used as color and as data, so the color space is part of the key.
		const String key = srgb ? path + "|srgb" : path;
		if (RefPtr<GPUTexture>* cached = m_Textures.Find(key))
			return cached->Get();
		if (m_Failed.Contains(key))
			return nullptr;

		const String fullPath = Paths::Content(path);
		int width = 0;
		int height = 0;
		int channels = 0;
		stbi_uc* pixels = stbi_load(fullPath.CStr(), &width, &height, &channels, 4);
		if (!pixels)
		{
			FP_LOG_WARN("Could not load texture %s: %s", fullPath.CStr(), stbi_failure_reason());
			m_Failed.Add(key);
			return nullptr;
		}

		RefPtr<GPUTexture> texture = m_Device.CreateTexture(pixels, static_cast<uint32>(width), static_cast<uint32>(height), srgb, true);
		stbi_image_free(pixels);
		if (!texture)
		{
			m_Failed.Add(key);
			return nullptr;
		}
		m_Textures.Add(key, texture);
		return texture.Get();
	}

	void TextureCache::Clear()
	{
		m_Textures.Clear();
		m_Failed.Clear();
	}
}
