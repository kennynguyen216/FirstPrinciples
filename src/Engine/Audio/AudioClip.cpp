#include "Engine/Audio/AudioClip.h"
#include "Engine/Paths.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <thread>

namespace FP
{
	namespace
	{
		namespace fs = std::filesystem;
		constexpr uint64 MiB = 1024 * 1024;
		AudioClipResult Error(AudioError code, const char* message) { return {{code, message}, {}}; }
		String Utf8(const fs::path& path)
		{
			const auto text = path.generic_u8string();
			return {reinterpret_cast<const char*>(text.data()), static_cast<int32>(text.size())};
		}
		fs::path Native(const String& text) { return fs::path(reinterpret_cast<const char8_t*>(text.CStr())); }
		uint16 U16(const uint8* p) { return uint16(p[0]) | (uint16(p[1]) << 8); }
		uint32 U32(const uint8* p) { return uint32(U16(p)) | (uint32(U16(p + 2)) << 16); }
		bool Tag(const uint8* p, const char* tag) { return std::memcmp(p, tag, 4) == 0; }

		AudioClipResult Decode(const fs::path& path, uint64 transientBudget)
		{
			std::error_code stampError;
			const auto stamp = fs::last_write_time(path, stampError);
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file) return Error(AudioError::MissingClip, "Cannot open WAV file; check its path and permissions.");
			const auto end = file.tellg();
			if (end < 12 || uint64(end) > 64 * MiB) return Error(AudioError::UnsupportedFormat, "WAV must be a RIFF file of at most 64 MiB.");
			const uint64 size = uint64(end);
			if (size > transientBudget) return Error(AudioError::BudgetExceeded, "WAV input exceeds the temporary loading budget.");
			std::vector<uint8> bytes(static_cast<size_t>(size));
			file.seekg(0);
			if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
				return Error(AudioError::DecodeFailed, "Could not read the complete WAV snapshot.");
			if (!Tag(bytes.data(), "RIFF") || !Tag(bytes.data() + 8, "WAVE") || uint64(U32(bytes.data() + 4)) + 8 != size)
				return Error(AudioError::UnsupportedFormat, "Expected a complete little-endian RIFF/WAVE file.");
			const uint8* format = nullptr;
			uint32 formatSize = 0, dataSize = 0;
			bool hasData = false;
			for (uint64 offset = 12; offset < size;)
			{
				if (size - offset < 8) return Error(AudioError::DecodeFailed, "Truncated WAV chunk header.");
				const uint8* chunk = bytes.data() + offset;
				const uint64 length = U32(chunk + 4);
				const uint64 next = offset + 8 + length + (length & 1);
				if (next > size) return Error(AudioError::DecodeFailed, "WAV chunk or padding exceeds the file.");
				if (Tag(chunk, "fmt "))
				{
					if (format) return Error(AudioError::UnsupportedFormat, "WAV contains multiple format chunks.");
					format = chunk + 8;
					formatSize = static_cast<uint32>(length);
				}
				else if (Tag(chunk, "data"))
				{
					if (hasData) return Error(AudioError::UnsupportedFormat, "WAV contains multiple data chunks.");
					hasData = true;
					dataSize = static_cast<uint32>(length);
				}
				offset = next;
			}
			if (!format || !hasData || (formatSize != 16 && formatSize != 18) || (formatSize == 18 && U16(format + 16) != 0))
				return Error(AudioError::UnsupportedFormat, "WAV needs one ordinary PCM16 or float32 format and one data chunk.");
			const uint16 encoding = U16(format), channels = U16(format + 2), bits = U16(format + 14);
			const uint32 rate = U32(format + 4);
			if (!((encoding == 1 && bits == 16) || (encoding == 3 && bits == 32)) || channels < 1 || channels > 2 || rate < 8000 || rate > 96000)
				return Error(AudioError::UnsupportedFormat, "Export PCM16 or float32 mono/stereo WAV at 8,000-96,000 Hz.");
			const uint32 alignment = channels * (bits / 8);
			if (U16(format + 12) != alignment || U32(format + 8) != rate * alignment || !dataSize || dataSize % alignment)
				return Error(AudioError::DecodeFailed, "WAV data is empty or has invalid frame/block alignment.");
			const uint64 frames = dataSize / alignment, decodedBytes = frames * channels * sizeof(float);
			if (frames > uint64(rate) * 60 || decodedBytes > 32 * MiB)
				return Error(AudioError::BudgetExceeded, "Clip exceeds 60 seconds or 32 MiB of decoded samples; split it into shorter clips.");
			// SDL decode + conversion + immutable publication can coexist with the input snapshot.
			if (size + dataSize + decodedBytes * 2 + MiB > transientBudget)
				return Error(AudioError::BudgetExceeded, "Clip exceeds the temporary loading reservation.");
			SDL_AudioSpec source{};
			Uint8* sourceData = nullptr;
			Uint32 sourceBytes = 0;
			if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(bytes.data(), bytes.size()), true, &source, &sourceData, &sourceBytes))
				return {{AudioError::DecodeFailed, String::Format("SDL WAV decode failed: %s", SDL_GetError())}, {}};
			std::unique_ptr<Uint8, decltype(&SDL_free)> sourceOwner(sourceData, SDL_free);
			if (source.freq != int(rate) || source.channels != channels || sourceBytes != dataSize ||
				source.format != (encoding == 1 ? SDL_AUDIO_S16LE : SDL_AUDIO_F32LE))
				return Error(AudioError::DecodeFailed, "Decoded WAV does not match its validated format.");
			SDL_AudioSpec target{SDL_AUDIO_F32, int(channels), int(rate)};
			Uint8* converted = nullptr;
			int convertedBytes = 0;
			if (!SDL_ConvertAudioSamples(&source, sourceData, static_cast<int>(sourceBytes), &target, &converted, &convertedBytes))
				return {{AudioError::DecodeFailed, String::Format("SDL sample conversion failed: %s", SDL_GetError())}, {}};
			std::unique_ptr<Uint8, decltype(&SDL_free)> convertedOwner(converted, SDL_free);
			if (convertedBytes < 0 || uint64(convertedBytes) != decodedBytes)
				return Error(AudioError::DecodeFailed, "Unexpected decoded WAV frame count.");
			auto clip = std::make_shared<AudioClip>();
			clip->Path = Utf8(path);
			clip->SampleRate = int(rate);
			clip->Channels = channels;
			clip->Frames = frames;
			clip->Duration = double(frames) / rate;
			clip->Encoding = encoding == 1 ? "PCM16" : "Float32";
			clip->SourceBytes = size;
			clip->LastWriteTicks = stampError ? 0 : static_cast<int64>(stamp.time_since_epoch().count());
			const auto samples = reinterpret_cast<const float*>(converted);
			clip->Samples.assign(samples, samples + decodedBytes / sizeof(float));
			for (float& sample : clip->Samples)
			{
				if (!std::isfinite(sample)) return Error(AudioError::DecodeFailed, "Float WAV contains NaN or infinity; export finite samples.");
				if (sample < -1 || sample > 1) { ++clip->ClippedSamples; sample = std::clamp(sample, -1.f, 1.f); }
			}
			return {{}, std::move(clip)};
		}
	}

	struct AudioClipCache::Impl
	{
		struct Entry { std::shared_ptr<const AudioClip> Clip; uint64 Used = 0; };
		fs::path Root;
		uint64 Budget, Transient, Serial = 0, Revision = 0;
		std::map<std::string, Entry> Clips;
		std::map<std::string, AudioResult> Failures;
		std::map<std::string, std::string> Aliases;
		std::vector<std::weak_ptr<const AudioClip>> Retired;
		std::thread::id Thread = std::this_thread::get_id();
		void CheckThread() const { FP_ASSERT(Thread == std::this_thread::get_id()); }
	};

	AudioClipCache::AudioClipCache(const String& root, uint64 resident, uint64 transient) : m_Impl(std::make_unique<Impl>())
	{
		m_Impl->Root = Native(root.IsEmpty() ? Paths::GetContentDir() : root);
		m_Impl->Budget = resident;
		m_Impl->Transient = transient;
	}
	AudioClipCache::~AudioClipCache() = default;
	void AudioClipCache::ClearFailures() { m_Impl->CheckThread(); m_Impl->Failures.clear(); }
	uint64 AudioClipCache::ResidentBytes() const
	{
		m_Impl->CheckThread();
		uint64 bytes = 0;
		for (const auto& [key, entry] : m_Impl->Clips) bytes += entry.Clip->Samples.size() * sizeof(float);
		for (const auto& weak : m_Impl->Retired) if (const auto clip = weak.lock()) bytes += clip->Samples.size() * sizeof(float);
		return bytes;
	}
	uint64 AudioClipCache::PinnedBytes() const
	{
		m_Impl->CheckThread();
		uint64 bytes = 0;
		for (const auto& [key, entry] : m_Impl->Clips) if (entry.Clip.use_count() > 1) bytes += entry.Clip->Samples.size() * sizeof(float);
		for (const auto& weak : m_Impl->Retired) if (const auto clip = weak.lock()) bytes += clip->Samples.size() * sizeof(float);
		return bytes;
	}
	AudioClipResult AudioClipCache::ValidateFile(const String& path)
	{
		try { return Decode(Native(path), 128 * MiB); }
		catch (const std::exception&) { return Error(AudioError::DecodeFailed, "Could not allocate or access the WAV snapshot."); }
	}
	AudioClipResult AudioClipCache::Load(const String& path, bool reload)
	{
		// shortcut: decoding is synchronous; add async loading if preloading cannot prevent measured stalls.
		m_Impl->CheckThread();
		const std::string requested(path.View());
		if (!reload) if (const auto failure = m_Impl->Failures.find(requested); failure != m_Impl->Failures.end()) return {failure->second, {}};
		auto fail = [&](AudioClipResult result)
		{
			const auto alias = m_Impl->Aliases.find(requested);
			const bool hasGood = alias != m_Impl->Aliases.end() && m_Impl->Clips.contains(alias->second);
			if (hasGood && !reload)
			{
				auto& entry = m_Impl->Clips.at(alias->second);
				entry.Used = ++m_Impl->Serial;
				return AudioClipResult{{}, entry.Clip};
			}
			if (!m_Impl->Failures.contains(requested)) FP_LOG_WARN("Audio '%s': %s", path.CStr(), result.Result.Message.CStr());
			if (!hasGood) m_Impl->Failures[requested] = result.Result;
			return result;
		};
		try
		{
			if (path.IsEmpty() || requested.find('\0') != std::string::npos || requested.find('\\') != std::string::npos || requested.find(':') != std::string::npos)
				return fail(Error(AudioError::InvalidSettings, "Clip path must use UTF-8 forward slashes relative to Content."));
			const fs::path relative = Native(path);
			if (relative.is_absolute() || relative.has_root_path()) return fail(Error(AudioError::InvalidSettings, "Absolute audio paths are not allowed."));
			std::error_code ec;
			const auto root = fs::canonical(m_Impl->Root, ec);
			if (ec) return fail(Error(AudioError::MissingClip, "Content directory does not exist."));
			const auto absolute = fs::canonical(root / relative, ec);
			if (ec || !fs::is_regular_file(absolute, ec)) return fail(Error(AudioError::MissingClip, "Clip is missing; select an existing WAV under Content."));
			auto actual = absolute.begin();
			for (auto expected = root.begin(); expected != root.end(); ++expected, ++actual)
				if (actual == absolute.end() || *actual != *expected) return fail(Error(AudioError::InvalidSettings, "Clip escapes the Content directory."));
			if (actual == absolute.end()) return fail(Error(AudioError::InvalidSettings, "Clip must name a file under Content."));
			// Match saved spelling on case-insensitive hosts so the same scene works on macOS.
			fs::path walk = root;
			for (const auto& part : relative.lexically_normal())
			{
				if (part == ".") continue;
				if (part == "..") return fail(Error(AudioError::InvalidSettings, "Parent traversal is not allowed in audio paths."));
				bool exact = false;
				for (const auto& entry : fs::directory_iterator(walk)) if (entry.path().filename().native() == part.native()) { exact = true; break; }
				if (!exact) return fail(Error(AudioError::InvalidSettings, "Clip path capitalization differs from disk; select it again."));
				walk /= part;
			}
			const std::string key(Utf8(absolute).View());
			auto found = m_Impl->Clips.find(key);
			if (!reload && found != m_Impl->Clips.end()) { found->second.Used = ++m_Impl->Serial; return {{}, found->second.Clip}; }
			auto decoded = Decode(absolute, m_Impl->Transient);
			if (!decoded) return fail(std::move(decoded));
			const uint64 required = decoded.Clip->Samples.size() * sizeof(float);
			std::erase_if(m_Impl->Retired, [](const auto& weak) { return weak.expired(); });
			uint64 resident = ResidentBytes();
			// An unpinned revision being replaced needs no residency after publication.
			const uint64 replaceBytes = found != m_Impl->Clips.end() && found->second.Clip.use_count() == 1 ? found->second.Clip->Samples.size() * sizeof(float) : 0;
			while (resident - replaceBytes + required > m_Impl->Budget)
			{
				auto oldest = m_Impl->Clips.end();
				for (auto it = m_Impl->Clips.begin(); it != m_Impl->Clips.end(); ++it)
					if (it != found && it->second.Clip.use_count() == 1 && (oldest == m_Impl->Clips.end() || it->second.Used < oldest->second.Used)) oldest = it;
				if (oldest == m_Impl->Clips.end()) return fail(Error(AudioError::BudgetExceeded, "Decoded audio cache is full; stop pinned sounds or use shorter clips."));
				resident -= oldest->second.Clip->Samples.size() * sizeof(float);
				m_Impl->Clips.erase(oldest);
			}
			auto publication = std::const_pointer_cast<AudioClip>(decoded.Clip);
			publication->Path = Utf8(absolute.lexically_relative(root));
			publication->Revision = ++m_Impl->Revision;
			if (found != m_Impl->Clips.end() && found->second.Clip.use_count() > 1) m_Impl->Retired.emplace_back(found->second.Clip);
			m_Impl->Clips[key] = {decoded.Clip, ++m_Impl->Serial};
			m_Impl->Aliases[requested] = key;
			m_Impl->Failures.erase(requested);
			if (publication->ClippedSamples) FP_LOG_WARN("Audio '%s': clamped %llu out-of-range float samples.", path.CStr(), static_cast<unsigned long long>(publication->ClippedSamples));
			return decoded;
		}
		catch (const std::exception&) { return fail(Error(AudioError::DecodeFailed, "Could not access or allocate audio data; check the path and available memory.")); }
	}
}
