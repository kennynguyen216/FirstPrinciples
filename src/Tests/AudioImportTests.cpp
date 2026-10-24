#include "Tests/TestFramework.h"
#include "Editor/AudioImport.h"
#include "Engine/Paths.h"

#include <filesystem>
#include <fstream>

using namespace FP;

namespace
{
	namespace fs = std::filesystem;
	String Utf8(const fs::path& path)
	{
		const auto text = path.generic_u8string();
		return String(reinterpret_cast<const char*>(text.data()), static_cast<int32>(text.size()));
	}
	struct ImportFixture
	{
		fs::path Directory = fs::path(reinterpret_cast<const char8_t*>(Paths::Content("Audio").CStr())) / Guid::New().ToString().CStr();
		fs::path Source = fs::temp_directory_path() / ("fp-audio-" + std::string(Guid::New().ToString().View()) + ".wav");
		ImportFixture()
		{
			fs::create_directories(Directory);
			const unsigned char wav[] = {
				'R','I','F','F',38,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,
				1,0,1,0,0x40,0x1f,0,0,0x80,0x3e,0,0,2,0,16,0,'d','a','t','a',2,0,0,0,0,0 };
			std::ofstream file(Source, std::ios::binary);
			file.write(reinterpret_cast<const char*>(wav), sizeof(wav));
		}
		~ImportFixture()
		{
			std::error_code ec;
			fs::remove(Source, ec);
			fs::remove_all(Directory, ec);
		}
	};
}

FP_TEST(AudioImport_CopySameFileAndNoOverwrite)
{
	ImportFixture fixture;
	const auto destination = fixture.Directory / fs::path(u8"Tone 音.wav");
	String relative = "unchanged";
	FP_EXPECT(Editor::ImportAudioFile(Utf8(fixture.Source), Utf8(destination), relative));
	FP_EXPECT(fs::file_size(destination) == fs::file_size(fixture.Source));
	FP_EXPECT(relative.StartsWith("Audio/"));
	FP_EXPECT(relative.EndsWith("Tone 音.wav"));
	FP_EXPECT(Editor::ImportAudioFile(Utf8(destination), Utf8(destination), relative));
	relative = "unchanged";
	FP_EXPECT(!Editor::ImportAudioFile(Utf8(fixture.Source), Utf8(destination), relative));
	FP_EXPECT(relative == "unchanged");
	FP_EXPECT(fs::file_size(destination) == 46);
	FP_EXPECT(fs::exists(fixture.Source));
}

FP_TEST(AudioImport_RejectsCorruptOutsideCaseAndReservedNames)
{
	ImportFixture fixture;
	String relative = "unchanged";
	const auto destination = fixture.Directory / "Tone.wav";
	FP_EXPECT(!Editor::ImportAudioFile(Utf8(fixture.Source), Utf8(fixture.Source.parent_path() / "Outside.wav"), relative));
	FP_EXPECT(!Editor::ImportAudioFile(Utf8(fixture.Source), Utf8(fixture.Directory / "CON.wav"), relative));
	FP_EXPECT(!Editor::ImportAudioFile(Utf8(fixture.Source), Utf8(fixture.Directory / "CON.effect.wav"), relative));
	FP_EXPECT(!Editor::ImportAudioFile(Utf8(fixture.Source), Utf8(fixture.Directory / "Trailing .wav "), relative));
	FP_EXPECT(Editor::ImportAudioFile(Utf8(fixture.Source), Utf8(destination), relative));
	relative = "unchanged";
	FP_EXPECT(!Editor::ImportAudioFile(Utf8(fixture.Source), Utf8(fixture.Directory / "tone.wav"), relative));
	{
		std::ofstream corrupt(fixture.Source, std::ios::binary | std::ios::trunc);
		corrupt << "not a WAV";
	}
	FP_EXPECT(!Editor::ImportAudioFile(Utf8(fixture.Source), Utf8(fixture.Directory / "Corrupt.wav"), relative));
	FP_EXPECT(relative == "unchanged");
	FP_EXPECT(!fs::exists(fixture.Directory / "Corrupt.wav"));
	usize files = 0;
	for (const auto& entry : fs::directory_iterator(fixture.Directory))
		++files;
	FP_EXPECT(files == 1);
}

FP_TEST(AudioImport_RejectsLinkEscapeWhenLinksAreAvailable)
{
	ImportFixture fixture;
	std::error_code ec;
	const auto link = fixture.Directory / "Outside";
	fs::create_directory_symlink(fixture.Source.parent_path(), link, ec);
	if (ec)
		return; // Windows may require Developer Mode or symlink privilege.
	String relative = "unchanged";
	FP_EXPECT(!Editor::ImportAudioFile(Utf8(fixture.Source), Utf8(link / "Escaped.wav"), relative));
	FP_EXPECT(relative == "unchanged");
}
