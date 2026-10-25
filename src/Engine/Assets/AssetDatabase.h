#pragma once

#include "Core/Misc/Guid.h"

#include <map>
#include <set>
#include <string>
#include <filesystem>

namespace FP
{
	enum class AssetColorSpace { Auto, SRGB, Linear };
	enum class AssetRefreshMode { VerifyAll, ChangedFiles };

	struct AssetScanStats
	{
		uint64 FilesHashed = 0;
		uint64 BytesHashed = 0;
	};

	struct AssetImportSettings
	{
		bool GenerateMipmaps = true;
		AssetColorSpace ColorSpace = AssetColorSpace::Auto;
		auto operator<=>(const AssetImportSettings&) const = default;
	};

	struct AssetRecord
	{
		Guid Id;
		String Path;
		AssetImportSettings Settings;
		Array<String> Dependencies;
		uint64 Fingerprint = 0;
		uint64 DependencyFingerprint = 0;
		String StableReference;
	};

	struct AssetIssue
	{
		String Path;
		String Message;
	};

	class AssetDatabase
	{
	public:
		static constexpr const char* MetadataExtension = ".fpmeta";

		// Runtime scans never create or change metadata
		bool Open(const String& contentRoot, bool writable = false);
		bool Refresh(AssetRefreshMode mode = AssetRefreshMode::VerifyAll);
		void Invalidate(std::string_view path);
		const AssetScanStats& GetLastScanStats() const { return m_ScanStats; }
		const AssetRecord* Find(std::string_view reference) const;
		// Unknown IDs stay unresolved so missing assets cannot load another file
		String Resolve(std::string_view reference) const;
		String Reference(std::string_view path) const;
		uint64 Fingerprint(std::string_view reference) const;
		Array<String> Dependents(std::string_view reference) const;
		const Array<AssetIssue>& GetIssues() const { return m_Issues; }
		const std::map<std::string, AssetRecord, std::less<>>& GetAssets() const { return m_Assets; }
		bool SetImportSettings(std::string_view reference, const AssetImportSettings& settings);
		bool Move(std::string_view source, std::string_view destination, String& error);
		static bool IsReference(std::string_view text);
		static bool IsTexture(std::string_view path);

	private:
		bool SaveMetadata(const AssetRecord& asset);
		bool ReadMetadata(AssetRecord& asset);
		void ReadDependencies(AssetRecord& asset);
		uint64 FingerprintImpl(const AssetRecord& asset, std::set<std::string>& visited) const;
		String m_Root;
		bool m_Writable = false;
		std::map<std::string, AssetRecord, std::less<>> m_Assets;
		std::map<Guid, std::string> m_Ids;
		struct SourceSnapshot
		{
			std::filesystem::file_time_type Modified;
			uintmax_t Size = 0;
			uint64 Hash = 0;
		};
		std::map<std::string, SourceSnapshot, std::less<>> m_SourceSnapshots;
		AssetScanStats m_ScanStats;
		// Old paths keep unsaved edits usable after a move
		std::map<std::string, Guid, std::less<>> m_Aliases;
		Array<AssetIssue> m_Issues;
	};
}
