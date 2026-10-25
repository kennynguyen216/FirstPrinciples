#pragma once

#include "Tests/TestFramework.h"
#include "Engine/Paths.h"
#include "Engine/Project.h"
#include "Core/Misc/Guid.h"

#include <filesystem>
#include <fstream>

namespace FP::Test
{
	class ScopedProject
	{
	public:
		ScopedProject() : m_Original(Paths::GetProject().GetFilePath())
		{
			m_Root = std::filesystem::temp_directory_path() / ("FPTest-" + std::string(Guid::New().ToString().View()));
			std::filesystem::create_directories(m_Root / "Content");
			std::ofstream descriptor(m_Root / "Test.fpproject");
			descriptor << "Name Test\nStartScene main.scene\n";
			descriptor.close();
			FP_EXPECT(Paths::OpenProject(String(m_Root.generic_string())));
		}

		~ScopedProject()
		{
			FP_EXPECT(Paths::OpenProject(m_Original));
			std::error_code ec;
			std::filesystem::remove_all(m_Root, ec);
		}

	private:
		String m_Original;
		std::filesystem::path m_Root;
	};
}
