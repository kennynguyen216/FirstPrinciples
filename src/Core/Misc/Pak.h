#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace FP::Pak
{
	// Version 1: little-endian header and records, UTF-8 relative paths, uncompressed bytes
	// and a per-file FNV-1a checksum. Assets are stored verbatim, including referenced files.
	struct Source
	{
		std::filesystem::path Directory;
		std::string MountPoint;
	};

	bool Cook(const std::vector<Source>& sources, const std::filesystem::path& output, std::string& error);
	// Destination must be a newly created, empty directory. Never writes outside it.
	bool Extract(const std::filesystem::path& archive, const std::filesystem::path& destination, std::string& error);
}
