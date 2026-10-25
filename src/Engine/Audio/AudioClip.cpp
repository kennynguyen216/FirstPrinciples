#include "Engine/Audio/AudioClip.h"
#include "Engine/Paths.h"
#include "Engine/Assets/AssetDatabase.h"

#include "Engine/Audio/AudioFileDecoder.h"
#include <array>
#include <filesystem>
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
		AudioClipResult Decode(const fs::path& path, uint64 transientBudget, bool validateAll = false)
		{
			if (transientBudget < MiB) return Error(AudioError::BudgetExceeded, "Clip exceeds the temporary loading reservation.");
			AudioFileDecoder decoder;
			if (const auto result = decoder.Open(Utf8(path)); !result) return {result, {}};
			auto clip = std::make_shared<AudioClip>(decoder.Info());
			const uint64 decodedBytes = clip->Frames * clip->Channels * sizeof(float);
			if (clip->Duration > 60 || decodedBytes > 32 * MiB)
			{
				clip->StreamPath = Utf8(path);
				if (validateAll)
				{
					std::array<float, 4096> buffer{};
					while (decoder.Read(buffer.data(), buffer.size() / clip->Channels)) {}
					if (!decoder.Result()) return {decoder.Result(), {}};
				}
			}
			else
			{
				if (decodedBytes + MiB > transientBudget) return Error(AudioError::BudgetExceeded, "Clip exceeds the temporary loading reservation.");
				clip->Samples.resize(size_t(clip->Frames * clip->Channels));
				if (decoder.Read(clip->Samples.data(), clip->Frames) != clip->Frames)
					return decoder.Result() ? Error(AudioError::DecodeFailed, "Could not decode the complete audio file.") : AudioClipResult{decoder.Result(), {}};
			}
			clip->ClippedSamples = decoder.Info().ClippedSamples;
			std::error_code ec;
			const auto stamp = fs::last_write_time(path, ec);
			if (ec || int64(stamp.time_since_epoch().count()) != clip->LastWriteTicks || fs::file_size(path) != clip->SourceBytes)
				return Error(AudioError::DecodeFailed, "Audio changed while loading; retry with a stable file.");
			return {{}, std::move(clip)};
		}
	}

	struct AudioClipCache::Impl
	{
		struct Entry { std::shared_ptr<const AudioClip> Clip; uint64 Used = 0; };
		fs::path Root;
		bool ProjectContent = false;
		std::map<std::string, uint64> AssetRevisions;
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
		m_Impl->ProjectContent = root.IsEmpty() || root == Paths::GetContentDir();
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
		try { return Decode(Native(path), 128 * MiB, true); }
		catch (const std::exception&) { return Error(AudioError::DecodeFailed, "Could not allocate or access the audio snapshot."); }
	}
	AudioClipResult AudioClipCache::Load(const String& reference, bool reload)
	{
		const String path = m_Impl->ProjectContent ? Paths::AssetPath(reference) : reference;
		m_Impl->CheckThread();
		const std::string requested(path.View());
		if (m_Impl->ProjectContent)
		{
			const uint64 revision = Paths::GetAssets().Fingerprint(reference);
			const auto previous = m_Impl->AssetRevisions.find(requested);
			if (previous != m_Impl->AssetRevisions.end() && previous->second != revision)
			{
				reload = true;
				m_Impl->Failures.erase(requested);
			}
			m_Impl->AssetRevisions[requested] = revision;
		}
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
			if (ec || !fs::is_regular_file(absolute, ec)) return fail(Error(AudioError::MissingClip, "Clip is missing; select an existing WAV or MP3 under Content."));
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
