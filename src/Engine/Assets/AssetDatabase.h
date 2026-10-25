#pragma once

#include "Core/Misc/Guid.h"

#include <map>
#include <set>
#include <string>

namespace FP
{
	enum class AssetColorSpace { Auto, SRGB, Linear };

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
		bool Refresh();
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
		std::map<std::string, std::string, std::less<>> m_Ids;
		std::map<std::string, uint64, std::less<>> m_Fingerprints;
		// Old paths keep unsaved edits usable after a move
		std::map<std::string, std::string, std::less<>> m_Aliases;
		Array<AssetIssue> m_Issues;
	};
}
