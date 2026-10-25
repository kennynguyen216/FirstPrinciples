#include "Engine/Render/TextureCache.h"
#include "Engine/Paths.h"
#include "Engine/Assets/AssetDatabase.h"
#include "Engine/Render/ImageDecoder.h"

#include <stb_image.h>

namespace FP
{
	namespace
	{
		bool UseSrgb(const AssetRecord* asset, bool requested)
		{
			return !asset || asset->Settings.ColorSpace == AssetColorSpace::Auto ? requested : asset->Settings.ColorSpace == AssetColorSpace::SRGB;
		}
	}

	void TextureCache::CheckRevision(const String& path, const String& key)
	{
		const uint64 revision = Paths::GetAssets().Fingerprint(path);
		if (const uint64* previous = m_Revisions.Find(key); previous && *previous == revision) return;
		m_Textures.Remove(key);
		m_Failed.Remove(key);
		m_Revisions.Add(key, revision);
	}

	GPUTexture* TextureCache::Get(const String& reference, bool srgb)
	{
		const String path = Paths::AssetReference(reference);
		if (path.IsEmpty())
			return nullptr;

		// The same file can be used as color and as data, so the color space is part of the key.
		const String key = srgb ? path + "|srgb" : path;
		CheckRevision(path, key);
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

		const AssetRecord* asset = Paths::GetAssets().Find(path);
		RefPtr<GPUTexture> texture = m_Device.CreateTexture(pixels, static_cast<uint32>(width), static_cast<uint32>(height), UseSrgb(asset, srgb), !asset || asset->Settings.GenerateMipmaps);
		stbi_image_free(pixels);
		if (!texture)
		{
			m_Failed.Add(key);
			return nullptr;
		}
		m_Textures.Add(key, texture);
		return texture.Get();
	}

	int32 TextureCache::Preload(const Array<Request>& input)
	{
		Array<Request> requests = input;
		for (Request& request : requests) request.Path = Paths::AssetReference(request.Path);
		// Decode each file once even when it is used as both color and data.
		Array<String> toDecode;
		for (const Request& request : requests)
		{
			const String key = request.Srgb ? request.Path + "|srgb" : request.Path;
			CheckRevision(request.Path, key);
			if (request.Path.IsEmpty() || m_Textures.Contains(key) || m_Failed.Contains(key))
				continue;
			toDecode.AddUnique(request.Path);
		}
		if (toDecode.IsEmpty())
			return 0;

		const Array<DecodedImage> images = DecodeImages(toDecode);
		int32 loaded = 0;
		for (const Request& request : requests)
		{
			const String key = request.Srgb ? request.Path + "|srgb" : request.Path;
			const int32 index = toDecode.Find(request.Path);
			if (index == INDEX_NONE || m_Textures.Contains(key) || m_Failed.Contains(key))
				continue;
			const DecodedImage& image = images[index];
			const AssetRecord* asset = Paths::GetAssets().Find(request.Path);
			RefPtr<GPUTexture> texture = image.Pixels
				? m_Device.CreateTexture(image.Pixels, static_cast<uint32>(image.Width), static_cast<uint32>(image.Height), UseSrgb(asset, request.Srgb), !asset || asset->Settings.GenerateMipmaps)
				: nullptr;
			if (texture)
			{
				m_Textures.Add(key, texture);
				++loaded;
			}
			else
			{
				m_Failed.Add(key);
			}
		}
		return loaded;
	}

	void TextureCache::Clear()
	{
		m_Textures.Clear();
		m_Revisions.Clear();
		m_Failed.Clear();
	}
}
