#include "Engine/Audio/AudioFileDecoder.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DR_MP3_ONLY_MP3
#define DR_MP3_FLOAT_OUTPUT
#include <dr_mp3.h>

namespace FP
{
	namespace
	{
		uint16 U16(const uint8* p) { return uint16(p[0]) | (uint16(p[1]) << 8); }
		uint32 U32(const uint8* p) { return uint32(U16(p)) | (uint32(U16(p + 2)) << 16); }
		bool Tag(const uint8* p, const char* tag) { return std::memcmp(p, tag, 4) == 0; }
	}
	struct AudioFileDecoder::Impl
	{
		std::ifstream File;
		AudioClip Info;
		AudioResult Status;
		drmp3 Mp3{};
		bool IsMp3 = false, EndChecked = false;
		uint64 Position = 0, DataOffset = 0;
		uint16 Bits = 0;
		~Impl() { if (IsMp3) drmp3_uninit(&Mp3); }
		AudioResult Fail(AudioError code, const char* message) { return Status = {code, message}; }
		static size_t ReadBytes(void* user, void* output, size_t count)
		{
			auto& file = static_cast<Impl*>(user)->File;
			file.read(static_cast<char*>(output), std::streamsize(count));
			const auto read = file.gcount();
			if (file.eof()) file.clear();
			return size_t(read);
		}
		static drmp3_bool32 Seek(void* user, int offset, drmp3_seek_origin origin)
		{
			auto& file = static_cast<Impl*>(user)->File;
			file.clear();
			file.seekg(offset, origin == DRMP3_SEEK_SET ? std::ios::beg : origin == DRMP3_SEEK_CUR ? std::ios::cur : std::ios::end);
			return bool(file);
		}
		static drmp3_bool32 Tell(void* user, drmp3_int64* position)
		{
			const auto value = static_cast<Impl*>(user)->File.tellg();
			if (value < 0) return false;
			*position = drmp3_int64(value); return true;
		}
	};
	AudioFileDecoder::AudioFileDecoder() : m_Impl(std::make_unique<Impl>()) {}
	AudioFileDecoder::~AudioFileDecoder() = default;
	const AudioClip& AudioFileDecoder::Info() const { return m_Impl->Info; }
	const AudioResult& AudioFileDecoder::Result() const { return m_Impl->Status; }
	AudioResult AudioFileDecoder::Open(const String& absolutePath)
	{
		m_Impl = std::make_unique<Impl>();
		auto& d = *m_Impl;
		const std::filesystem::path path(reinterpret_cast<const char8_t*>(absolutePath.CStr()));
		d.File.open(path, std::ios::binary | std::ios::ate);
		if (!d.File) return d.Fail(AudioError::MissingClip, "Cannot open audio file; check its path and permissions.");
		const auto size = d.File.tellg();
		if (size < 12 || uint64(size) > 4ull * 1024 * 1024 * 1024)
			return d.Fail(AudioError::UnsupportedFormat, "Audio must be a WAV or MP3 file of at most 4 GiB.");
		d.Info.Path = absolutePath; d.Info.SourceBytes = uint64(size);
		std::error_code ec;
		const auto stamp = std::filesystem::last_write_time(path, ec);
		if (ec) return d.Fail(AudioError::MissingClip, "Cannot read audio file modification time.");
		d.Info.LastWriteTicks = int64(stamp.time_since_epoch().count());
		d.File.seekg(0);
		std::array<uint8, 18> header{};
		d.File.read(reinterpret_cast<char*>(header.data()), 12);
		if (Tag(header.data(), "RIFF"))
		{
			if (!Tag(header.data() + 8, "WAVE") || uint64(U32(header.data() + 4)) + 8 != uint64(size))
				return d.Fail(AudioError::UnsupportedFormat, "Expected a complete little-endian RIFF/WAVE file.");
			bool hasFormat = false, hasData = false;
			uint32 dataSize = 0, rate = 0;
			uint16 channels = 0, encoding = 0, alignment = 0;
			for (uint64 offset = 12; offset < uint64(size);)
			{
				if (uint64(size) - offset < 8) return d.Fail(AudioError::DecodeFailed, "Truncated WAV chunk header.");
				d.File.seekg(std::streamoff(offset)); d.File.read(reinterpret_cast<char*>(header.data()), 8);
				if (!d.File) return d.Fail(AudioError::DecodeFailed, "Could not read WAV chunk header.");
				const uint32 length = U32(header.data() + 4);
				const uint64 next = offset + 8 + length + (length & 1);
				if (next > uint64(size)) return d.Fail(AudioError::DecodeFailed, "WAV chunk or padding exceeds the file.");
				if (Tag(header.data(), "fmt "))
				{
					if (hasFormat || (length != 16 && length != 18)) return d.Fail(AudioError::UnsupportedFormat, "WAV needs one ordinary PCM16 or float32 format.");
					hasFormat = true;
					d.File.read(reinterpret_cast<char*>(header.data()), length);
					if (!d.File || (length == 18 && U16(header.data() + 16))) return d.Fail(AudioError::UnsupportedFormat, "Unsupported WAV format extension.");
					encoding = U16(header.data()); channels = U16(header.data() + 2); rate = U32(header.data() + 4); d.Bits = U16(header.data() + 14);
					if (!((encoding == 1 && d.Bits == 16) || (encoding == 3 && d.Bits == 32)) || channels < 1 || channels > 2 || rate < 8000 || rate > 96000)
						return d.Fail(AudioError::UnsupportedFormat, "Export PCM16 or float32 mono/stereo WAV at 8,000-96,000 Hz.");
					alignment = channels * (d.Bits / 8);
					if (U16(header.data() + 12) != alignment || U32(header.data() + 8) != rate * alignment)
						return d.Fail(AudioError::DecodeFailed, "WAV has invalid frame/block alignment.");
				}
				else if (Tag(header.data(), "data"))
				{
					if (hasData) return d.Fail(AudioError::UnsupportedFormat, "WAV contains multiple data chunks.");
					hasData = true; dataSize = length; d.DataOffset = offset + 8;
				}
				offset = next;
			}
			if (!hasFormat || !hasData || !dataSize || dataSize % alignment)
				return d.Fail(AudioError::DecodeFailed, "WAV needs a format and nonempty, frame-aligned data chunk.");
			d.Info.SampleRate = int(rate); d.Info.Channels = channels; d.Info.Frames = dataSize / alignment;
			d.Info.Encoding = encoding == 1 ? "PCM16" : "Float32";
		}
		else
		{
			d.File.clear(); d.File.seekg(0);
			if (!drmp3_init(&d.Mp3, Impl::ReadBytes, Impl::Seek, Impl::Tell, nullptr, &d, nullptr))
				return d.Fail(AudioError::UnsupportedFormat, "Expected a PCM16/float32 WAV or a valid MP3 file.");
			d.IsMp3 = true;
			d.Info.SampleRate = int(d.Mp3.sampleRate); d.Info.Channels = int(d.Mp3.channels);
			if (d.Info.Channels < 1 || d.Info.Channels > 2 || d.Info.SampleRate < 8000 || d.Info.SampleRate > 96000)
				return d.Fail(AudioError::UnsupportedFormat, "MP3 must be mono or stereo at 8,000-96,000 Hz.");
			d.Info.Frames = drmp3_get_pcm_frame_count(&d.Mp3); d.Info.Encoding = "MP3";
			if (!d.Info.Frames) return d.Fail(AudioError::DecodeFailed, "MP3 contains no decodable audio frames.");
		}
		d.Info.Duration = double(d.Info.Frames) / d.Info.SampleRate;
		if (!Rewind()) return d.Status;
		return {};
	}
	bool AudioFileDecoder::Rewind()
	{
		auto& d = *m_Impl;
		if (!d.Status) return false;
		bool ok;
		if (d.IsMp3) ok = drmp3_seek_to_pcm_frame(&d.Mp3, 0);
		else { d.File.clear(); d.File.seekg(std::streamoff(d.DataOffset)); ok = bool(d.File); }
		if (!ok) { d.Fail(AudioError::DecodeFailed, "Could not rewind the audio file."); return false; }
		d.Position = 0; d.EndChecked = false; return true;
	}
	uint64 AudioFileDecoder::Read(float* samples, uint64 frames)
	{
		auto& d = *m_Impl;
		if (!d.Status) return 0;
		const uint64 count = std::min(frames, d.Info.Frames - d.Position);
		std::array<uint8, 576 * 2 * 4> bytes{};
		uint64 offset = 0;
		while (offset < count)
		{
			const uint64 batch = std::min<uint64>(576, count - offset);
			float* output = samples + offset * d.Info.Channels;
			if (d.IsMp3)
			{
				const auto read = drmp3_read_pcm_frames_f32(&d.Mp3, batch, output);
				if (d.Mp3.mp3FrameChannels != unsigned(d.Info.Channels) || d.Mp3.mp3FrameSampleRate != unsigned(d.Info.SampleRate))
					{ d.Fail(AudioError::UnsupportedFormat, "MP3 channel count and sample rate must remain constant."); return 0; }
				if (read != batch) { d.Fail(AudioError::DecodeFailed, "MP3 ended before its declared audio frame count."); return 0; }
			}
			else
			{
				d.File.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(batch * d.Info.Channels * (d.Bits / 8)));
				if (!d.File) { d.Fail(AudioError::DecodeFailed, "Could not read complete WAV samples."); return 0; }
				for (size_t i = 0; i < batch * d.Info.Channels; ++i)
					output[i] = d.Bits == 16 ? float(std::bit_cast<int16>(U16(bytes.data() + i * 2))) / 32768.f : std::bit_cast<float>(U32(bytes.data() + i * 4));
			}
			for (size_t i = 0; i < batch * d.Info.Channels; ++i)
			{
				if (!std::isfinite(output[i])) { d.Fail(AudioError::DecodeFailed, "Audio contains NaN or infinity; export finite samples."); return 0; }
				if (output[i] < -1 || output[i] > 1) { ++d.Info.ClippedSamples; output[i] = std::clamp(output[i], -1.f, 1.f); }
			}
			offset += batch;
		}
		d.Position += count;
		if (d.IsMp3 && d.Position == d.Info.Frames && !d.EndChecked)
		{
			d.EndChecked = true;
			if (drmp3_read_pcm_frames_f32(&d.Mp3, 1, nullptr)) { d.Fail(AudioError::DecodeFailed, "MP3 frame count does not match its decoded audio."); return 0; }
		}
		return count;
	}
}
