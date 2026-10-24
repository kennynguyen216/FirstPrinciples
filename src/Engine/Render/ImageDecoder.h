#pragma once

#include "Core/CoreMinimal.h"

namespace FP
{
	// RGBA8 pixels decoded from an image file. Frees them when destroyed.
	struct DecodedImage
	{
		String Path;
		int32 Width = 0;
		int32 Height = 0;
		uint8* Pixels = nullptr; // null when decoding failed

		DecodedImage() = default;
		~DecodedImage();
		DecodedImage(DecodedImage&& other) noexcept;
		DecodedImage& operator=(DecodedImage&& other) noexcept;
		DecodedImage(const DecodedImage&) = delete;
		DecodedImage& operator=(const DecodedImage&) = delete;
	};

	// Decodes images relative to Content, one job per image, and returns them in the same order.
	// Without the job system they decode one after another on the calling thread.
	Array<DecodedImage> DecodeImages(const Array<String>& paths);
}
