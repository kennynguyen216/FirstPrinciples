#include "Engine/Render/EnvironmentMap.h"
#include "Core/Jobs/JobSystem.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>

namespace FP
{
	namespace
	{
		bool ValidSize(int32 width, int32 height)
		{
			return height >= 2 && height <= 4096 && width == height * 2;
		}

		void Basis(Vec3 n, float out[9])
		{
			out[0] = 0.282095f;
			out[1] = 0.488603f * n.Y;
			out[2] = 0.488603f * n.Z;
			out[3] = 0.488603f * n.X;
			out[4] = 1.092548f * n.X * n.Y;
			out[5] = 1.092548f * n.Y * n.Z;
			out[6] = 0.315392f * (3 * n.Z * n.Z - 1);
			out[7] = 1.092548f * n.X * n.Z;
			out[8] = 0.546274f * (n.X * n.X - n.Y * n.Y);
		}

		Vec3 Direction(int32 x, int32 y, int32 width, int32 height)
		{
			const float phi = ((x + 0.5f) / width - 0.5f) * 2 * Pi;
			const float theta = (y + 0.5f) / height * Pi;
			return { std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi) };
		}

		Vec3 SamplePixels(const Vec4* pixels, int32 width, int32 height, Vec3 direction)
		{
			const float u = std::atan2(direction.Z, direction.X) / (2 * Pi) + 0.5f;
			const float v = std::acos(std::clamp(direction.Y, -1.0f, 1.0f)) / Pi;
			const float x = u * width - 0.5f;
			const float y = v * height - 0.5f;
			const int32 ix = static_cast<int32>(std::floor(x));
			const int32 iy = static_cast<int32>(std::floor(y));
			auto pixel = [&](int32 px, int32 py) {
				return pixels[std::clamp(py, 0, height - 1) * width + (px % width + width) % width].XYZ();
			};
			const float fx = x - std::floor(x), fy = y - std::floor(y);
			return (pixel(ix, iy) * (1 - fx) + pixel(ix + 1, iy) * fx) * (1 - fy)
				+ (pixel(ix, iy + 1) * (1 - fx) + pixel(ix + 1, iy + 1) * fx) * fy;
		}

		float Linear(float value)
		{
			return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
		}
	}

	bool EnvironmentMap::Load(const char* fullPath)
	{
		*this = EnvironmentMap();
		int width = 0, height = 0, channels = 0;
		if (!stbi_info(fullPath, &width, &height, &channels) || !ValidSize(width, height))
			return false;
		Array<Vec4> source;
		if (stbi_is_hdr(fullPath))
		{
			float* pixels = stbi_loadf(fullPath, &width, &height, &channels, 3);
			if (!pixels)
				return false;
			if (!ValidSize(width, height))
			{
				stbi_image_free(pixels);
				return false;
			}
			source.Resize(width * height);
			for (int32 i = 0; i < source.Num(); ++i)
				source[i] = { pixels[i * 3], pixels[i * 3 + 1], pixels[i * 3 + 2], 1 };
			stbi_image_free(pixels);
		}
		else
		{
			stbi_uc* pixels = stbi_load(fullPath, &width, &height, &channels, 3);
			if (!pixels)
				return false;
			if (!ValidSize(width, height))
			{
				stbi_image_free(pixels);
				return false;
			}
			source.Resize(width * height);
			for (int32 i = 0; i < source.Num(); ++i)
				source[i] = { Linear(pixels[i * 3] / 255.0f), Linear(pixels[i * 3 + 1] / 255.0f), Linear(pixels[i * 3 + 2] / 255.0f), 1 };
			stbi_image_free(pixels);
		}
		return Build(source, width, height);
	}

	bool EnvironmentMap::Build(const Array<Vec4>& pixels, int32 width, int32 height)
	{
		*this = EnvironmentMap();
		if (!ValidSize(width, height) || pixels.Num() != width * height)
			return false;
		for (const Vec4& pixel : pixels)
			if (!std::isfinite(pixel.X) || !std::isfinite(pixel.Y) || !std::isfinite(pixel.Z) || pixel.X < 0 || pixel.Y < 0 || pixel.Z < 0)
				return false;
		Width = std::min(width, 512);
		Height = Width / 2;
		int32 count = 0;
		for (int32 w = Width, h = Height; ; w = std::max(1, w / 2), h = std::max(1, h / 2))
		{
			count += w * h;
			++Levels;
			if (w == 1 && h == 1)
				break;
		}
		Pixels.Resize(count);
		for (int32 y = 0; y < Height; ++y)
		{
			const float weight = (std::cos(Pi * y / Height) - std::cos(Pi * (y + 1) / Height)) * 2 * Pi / Width;
			for (int32 x = 0; x < Width; ++x)
			{
				const Vec3 direction = Direction(x, y, Width, Height);
				Vec3 color = SamplePixels(pixels.Data(), width, height, direction);
				if (width > Width)
				{
					// Average source pixels so small HDR lights survive downsampling.
					color = {};
					float total = 0;
					for (int32 sy = y * height / Height; sy < (y + 1) * height / Height; ++sy)
					{
						const float area = std::cos(Pi * sy / height) - std::cos(Pi * (sy + 1) / height);
						for (int32 sx = x * width / Width; sx < (x + 1) * width / Width; ++sx)
						{
							color += pixels[sy * width + sx].XYZ() * area;
							total += area;
						}
					}
					color = color / total;
				}
				Pixels[y * Width + x] = { color, 1 };
				float basis[9];
				Basis(direction, basis);
				for (int32 i = 0; i < 9; ++i)
				{
					const float convolution = i == 0 ? 1.0f : i < 4 ? 2.0f / 3.0f : 0.25f;
					Diffuse[i] = { Diffuse[i].XYZ() + color * (weight * basis[i] * convolution), 0 };
				}
			}
		}

		int32 offset = Width * Height;
		int32 w = Width, h = Height;
		for (int32 level = 1; level < Levels; ++level)
		{
			w = std::max(1, w / 2);
			h = std::max(1, h / 2);
			const float roughness = static_cast<float>(level) / (Levels - 1);
			const float a = roughness * roughness;
			// Filter reflections with GGX so rough surfaces spread bright features.
			JobSystem::ParallelFor(h, 1, [&](int32 y) {
				for (int32 x = 0; x < w; ++x)
				{
					const Vec3 n = Direction(x, y, w, h);
					const Vec3 up = std::fabs(n.Y) < 0.99f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
					const Vec3 tangent = Vec3::Cross(up, n).Normalized();
					const Vec3 bitangent = Vec3::Cross(n, tangent);
					Vec3 sum;
					float total = 0;
					for (int32 sample = 0; sample < 256; ++sample)
					{
						uint32 bits = static_cast<uint32>(sample);
						float radical = 0, fraction = 0.5f;
						while (bits) { radical += (bits & 1) * fraction; bits >>= 1; fraction *= 0.5f; }
						const float phi = 2 * Pi * (sample / 256.0f);
						const float cosine = std::sqrt((1 - radical) / (1 + (a * a - 1) * radical));
						const float sine = std::sqrt(std::max(0.0f, 1 - cosine * cosine));
						const Vec3 halfway = tangent * (std::cos(phi) * sine) + bitangent * (std::sin(phi) * sine) + n * cosine;
						const Vec3 light = halfway * (2 * Vec3::Dot(n, halfway)) - n;
						const float weight = std::max(Vec3::Dot(n, light), 0.0f);
						sum += SamplePixels(Pixels.Data(), Width, Height, light) * weight;
						total += weight;
					}
					Pixels[offset + y * w + x] = { sum / std::max(total, 0.0001f), 1 };
				}
			});
			offset += w * h;
		}
		return true;
	}

	Vec3 EnvironmentMap::Sample(Vec3 direction, int32 level) const
	{
		if (!Levels || direction.LengthSquared() < 0.000001f)
			return {};
		int32 offset = 0, w = Width, h = Height;
		for (int32 i = 0; i < std::clamp(level, 0, Levels - 1); ++i)
		{
			offset += w * h;
			w = std::max(1, w / 2);
			h = std::max(1, h / 2);
		}
		return SamplePixels(Pixels.Data() + offset, w, h, direction.Normalized());
	}

	Vec3 EnvironmentMap::Irradiance(Vec3 normal) const
	{
		float basis[9];
		Basis(normal.Normalized(), basis);
		Vec3 color;
		for (int32 i = 0; i < 9; ++i)
			color += Diffuse[i].XYZ() * basis[i];
		return { std::max(color.X, 0.0f), std::max(color.Y, 0.0f), std::max(color.Z, 0.0f) };
	}
}
