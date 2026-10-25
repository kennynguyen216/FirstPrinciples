#include "Tests/TestFramework.h"
#include "Core/Misc/Pak.h"

#include <chrono>
#include <fstream>
#include <iterator>

using namespace FP;

namespace
{
	namespace fs = std::filesystem;

	struct PakFixture
	{
		fs::path Root = fs::temp_directory_path() / ("FPPakTest_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		fs::path Content = Root / "Content";
		fs::path Archive = Root / "Content.pak";
		fs::path Extracted = Root / "Extracted";

		PakFixture()
		{
			fs::create_directories(Content / "Scenes");
			fs::create_directories(Extracted);
		}

		~PakFixture()
		{
			std::error_code ec;
			fs::remove_all(Root, ec);
		}

		bool Cook(std::string& error) { return Pak::Cook({ { Content, "Content" } }, Archive, error); }
		bool Extract(std::string& error) { return Pak::Extract(Archive, Extracted, error); }
	};

	std::string Read(const fs::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
	}

	void Write(const fs::path& path, const std::string& bytes)
	{
		std::ofstream file(path, std::ios::binary);
		file.write(bytes.data(), bytes.size());
	}
}

FP_TEST(Pak_RoundTripAndDeterministicOutput)
{
	PakFixture fixture;
	const std::string binary("\0\1\2\xff", 4);
	Write(fixture.Content / "Scenes/Main.scene", "version 2\n");
	Write(fixture.Content / "Texture.bin", binary);
	Write(fixture.Content / "Empty", "");
	Write(fixture.Content / fs::path(u8"音声.wav"), binary);
	std::string error;
	FP_EXPECT(fixture.Cook(error));
	const std::string first = Read(fixture.Archive);
	FP_EXPECT(fixture.Cook(error));
	FP_EXPECT(first == Read(fixture.Archive));
	FP_EXPECT(fixture.Extract(error));
	FP_EXPECT(Read(fixture.Extracted / "Content/Texture.bin") == binary);
	FP_EXPECT(Read(fixture.Extracted / "Content/Scenes/Main.scene") == "version 2\n");
	FP_EXPECT(fs::file_size(fixture.Extracted / "Content/Empty") == 0);
	FP_EXPECT(Read(fixture.Extracted / fs::path(u8"Content/音声.wav")) == binary);
}

FP_TEST(Pak_RejectsCorruptionAndTruncation)
{
	PakFixture fixture;
	Write(fixture.Content / "Data", "payload");
	std::string error;
	FP_EXPECT(fixture.Cook(error));
	const std::string original = Read(fixture.Archive);
	std::string bytes = original;
	bytes.back() ^= 1;
	Write(fixture.Archive, bytes);
	FP_EXPECT(!fixture.Extract(error));
	fs::remove_all(fixture.Extracted);
	fs::create_directory(fixture.Extracted);
	Write(fixture.Archive, original.substr(0, original.size() - 1));
	FP_EXPECT(!fixture.Extract(error));
}

FP_TEST(Pak_RejectsTraversalAndInvalidHeaders)
{
	PakFixture fixture;
	Write(fixture.Content / "Data", "payload");
	std::string error;
	FP_EXPECT(fixture.Cook(error));
	const std::string original = Read(fixture.Archive);
	std::string bytes = original;
	// The first path starts after the archive header and the entry sizes and checksum.
	bytes.replace(32, 12, "../Escape.me");
	Write(fixture.Archive, bytes);
	FP_EXPECT(!fixture.Extract(error));
	FP_EXPECT(!fs::exists(fixture.Root / "Escape.me"));
	Write(fixture.Archive, "FPPAK999");
	FP_EXPECT(!fixture.Extract(error));
	bytes = original;
	bytes[8] = static_cast<char>(255);
	Write(fixture.Archive, bytes);
	FP_EXPECT(!fixture.Extract(error));
}

FP_TEST(Pak_RejectsCollisionsAndUnsafeMounts)
{
	PakFixture fixture;
	Write(fixture.Content / "Data", "payload");
	std::string error;
	FP_EXPECT(!Pak::Cook({ { fixture.Content, "Content" }, { fixture.Content, "content" } }, fixture.Archive, error));
	FP_EXPECT(!Pak::Cook({ { fixture.Content, "../Content" } }, fixture.Archive, error));
	FP_EXPECT(!Pak::Cook({ { fixture.Content, "Content" } }, fixture.Content / "recursive.pak", error));
	FP_EXPECT(fixture.Cook(error));
	Write(fixture.Extracted / "Existing", "keep me");
	FP_EXPECT(!fixture.Extract(error));
	FP_EXPECT(Read(fixture.Extracted / "Existing") == "keep me");
}

FP_TEST(Pak_RejectsSymlinkedContent)
{
	PakFixture fixture;
	Write(fixture.Root / "Private", "outside content");
	std::error_code ec;
	fs::create_symlink(fixture.Root / "Private", fixture.Content / "Link", ec);
	// Windows can require developer mode to create symlinks.
	if (!ec)
	{
		std::string error;
		FP_EXPECT(!fixture.Cook(error));
	}
}
