#include "Engine/Render/ImageDecoder.h"
#include "Engine/Paths.h"
#include "Core/Jobs/JobSystem.h"

#include <stb_image.h>

namespace FP
{
	DecodedImage::~DecodedImage()
	{
		if (Pixels)
			stbi_image_free(Pixels);
	}

	DecodedImage::DecodedImage(DecodedImage&& other) noexcept
		: Path(std::move(other.Path)), Width(other.Width), Height(other.Height), Pixels(other.Pixels)
	{
		other.Pixels = nullptr;
	}

	DecodedImage& DecodedImage::operator=(DecodedImage&& other) noexcept
	{
		if (this != &other)
		{
			if (Pixels)
				stbi_image_free(Pixels);
			Path = std::move(other.Path);
			Width = other.Width;
			Height = other.Height;
			Pixels = other.Pixels;
			other.Pixels = nullptr;
		}
		return *this;
	}

	Array<DecodedImage> DecodeImages(const Array<String>& paths)
	{
		Array<DecodedImage> images;
		images.Resize(paths.Num());
		// Each job writes only its own slot, so no locking is needed.
		JobSystem::ParallelFor(paths.Num(), 1, [&](int32 i) {
			DecodedImage& image = images[i];
			image.Path = paths[i];
			const String fullPath = Paths::Content(paths[i]);
			int channels = 0;
			image.Pixels = stbi_load(fullPath.CStr(), &image.Width, &image.Height, &channels, 4);
			if (!image.Pixels)
				FP_LOG_WARN("Could not load texture %s: %s", fullPath.CStr(), stbi_failure_reason());
		});
		return images;
	}
}
