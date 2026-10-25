-- FirstPrinciples workspace.
-- macOS: premake5 gmake (or xcode4). Windows: Scripts/GenerateProjects.bat. See README.md.
-- --project=Projects/Name picks the game to build. Projects/Sample is the default.

local Root = path.getabsolute(".")
local IsWindows = os.target() == "windows"
local IsMac = os.target() == "macosx"

-- Third party locations ------------------------------------------------------

-- On macOS everything comes from Homebrew. On Windows SDL comes from Scripts/Setup.ps1
-- and Qt from the official installer, found through QT_DIR or C:/Qt.
local BrewPrefix = "/opt/homebrew"
if IsMac then
	local out = os.outputof("brew --prefix")
	if out and #out > 0 then BrewPrefix = out end
end

local SdlDir = IsWindows and (Root .. "/ThirdParty/SDL3") or (BrewPrefix .. "/opt/sdl3")

-- Header only libraries, pulled in as git submodules.
local EnttInclude = Root .. "/ThirdParty/entt/src"
local StbInclude = Root .. "/ThirdParty/stb"
-- Small audited crypto library for encrypting network packets. One .c file built into Engine.
local MonocypherDir = Root .. "/ThirdParty/monocypher/src"
-- Logging backend, header only.
local SpdlogInclude = Root .. "/ThirdParty/spdlog/include"
-- Rigid body physics. Built from source as its own static library.
local JoltDir = Root .. "/ThirdParty/JoltPhysics"
-- glTF parser for skeletal meshes and animations. One header, built once by CgltfImpl.cpp.
local CgltfInclude = Root .. "/ThirdParty/cgltf"
local OpusDir = Root .. "/ThirdParty/opus"
local DrLibsDir = Root .. "/ThirdParty/dr_libs"
-- Game UI in HTML, CSS and JavaScript. A binary SDK fetched by Scripts/GetUltralight, so it is
-- optional: without it everything builds and UI components draw nothing.
local UltralightDir = Root .. "/ThirdParty/Ultralight"
local HasUltralight = os.isdir(UltralightDir .. "/include/Ultralight")
if not HasUltralight then
	premake.warn("Ultralight is missing, so game UI will not draw. Run Scripts/GetUltralight to get it.")
end
local ImGuiDir = Root .. "/ThirdParty/imgui"
if not os.isdir(EnttInclude) or not os.isdir(StbInclude) or not os.isdir(MonocypherDir) or not os.isdir(SpdlogInclude) or not os.isdir(CgltfInclude) or not os.isfile(ImGuiDir .. "/imgui.h") or not os.isdir(JoltDir .. "/Jolt") or not os.isfile(DrLibsDir .. "/dr_mp3.h") then
	premake.warn("Submodules are missing. Run: git submodule update --init")
end

local QtDir = os.getenv("QT_DIR")
if not QtDir and IsWindows then
	for _, dir in ipairs(os.matchdirs("C:/Qt/*/msvc*_64")) do
		if os.isfile(dir .. "/lib/Qt6Core.lib") and os.isfile(dir .. "/lib/Qt6Cored.lib") then
			QtDir = dir
			break
		end
	end
end
if not QtDir and IsMac then QtDir = BrewPrefix .. "/opt/qt" end
if QtDir then QtDir = QtDir:gsub("\\", "/") end
if IsWindows and QtDir then
	if not os.isfile(QtDir .. "/lib/Qt6Core.lib") or not os.isfile(QtDir .. "/lib/Qt6Cored.lib") then
		error("QT_DIR must point to a 64-bit MSVC Qt kit with Debug and Release libraries (not MinGW): " .. QtDir)
	end
	print("Using Qt: " .. QtDir)
end

local MocPath = nil
if QtDir then
	if IsWindows then
		MocPath = QtDir .. "/bin/moc.exe"
	elseif os.isfile(QtDir .. "/share/qt/libexec/moc") then
		MocPath = QtDir .. "/share/qt/libexec/moc"
	else
		MocPath = QtDir .. "/libexec/moc"
	end
end

local QtModules = { "Core", "Gui", "Widgets" }

-- The game being built. Its Source folder becomes the Game and GameScripts projects and
-- everything generated or built lands in its Build, Binaries and Intermediate folders.
newoption {
	trigger = "project",
	value = "path",
	description = "Project folder to build (default Projects/Sample)",
}

local ProjectDir = _OPTIONS["project"] and path.getabsolute(_OPTIONS["project"], _WORKING_DIR) or (Root .. "/Projects/Sample")
local ProjectFile = os.matchfiles(ProjectDir .. "/*.fpproject")[1]
if not ProjectFile then
	error("No .fpproject file in " .. ProjectDir)
end
local ProjectName = io.readfile(ProjectFile):match('Name%s+"?([%w_]+)"?') or path.getbasename(ProjectFile)

-- Helpers --------------------------------------------------------------------

-- Include paths every project that touches engine headers needs.
local function EngineIncludes()
	externalincludedirs { SdlDir .. "/include", EnttInclude, StbInclude, MonocypherDir, SpdlogInclude, CgltfInclude, ImGuiDir, JoltDir, OpusDir .. "/include", DrLibsDir }
	if HasUltralight then
		externalincludedirs { UltralightDir .. "/include" }
		defines { "FP_WITH_ULTRALIGHT=1" }
	end
end

-- Links Ultralight and copies its libraries and resources next to the binary that needs them.
local function UseUltralight()
	if not HasUltralight then return end
	links { "Ultralight", "UltralightCore", "WebCore", "AppCore" }
	if IsWindows then
		libdirs { UltralightDir .. "/lib" }
		postbuildcommands {
			'{COPYFILE} "' .. UltralightDir .. '/bin/*.dll" "%{cfg.targetdir}"',
			'{COPYDIR} "' .. UltralightDir .. '/resources" "%{cfg.targetdir}/UltralightResources"',
		}
	else
		libdirs { UltralightDir .. "/bin" }
		-- The libraries use @rpath, so look next to whatever binary loads them.
		linkoptions { "-Wl,-rpath,@loader_path" }
		postbuildcommands {
			'cp -f "' .. UltralightDir .. '"/bin/*.dylib "%{cfg.targetdir}/"',
			'mkdir -p "%{cfg.targetdir}/UltralightResources" && cp -Rf "' .. UltralightDir .. '/resources/." "%{cfg.targetdir}/UltralightResources/"',
		}
	end
end

local function UseSDL()
	if IsWindows then
		libdirs { SdlDir .. "/lib/x64" }
		links { "SDL3" }
		-- Scripts rebuild while the editor has SDL loaded, so only copy a newer runtime.
		postbuildcommands { 'xcopy /D /Y "' .. path.translate(SdlDir .. '/lib/x64/SDL3.dll', '\\') .. '" "%{cfg.targetdir}"' }
	else
		libdirs { SdlDir .. "/lib" }
		links { "SDL3" }
		linkoptions { "-Wl,-rpath," .. SdlDir .. "/lib" }
	end
end

local function UseQt()
	if not QtDir then
		premake.warn("Qt was not found. Install a 64-bit MSVC Qt kit on Windows and set QT_DIR if it is outside C:/Qt. The Editor project will not build without Qt.")
		return
	end

	externalincludedirs { QtDir .. "/include" }
	for _, m in ipairs(QtModules) do
		externalincludedirs { QtDir .. "/include/Qt" .. m }
	end

	if IsWindows then
		libdirs { QtDir .. "/lib" }
		buildoptions { "/Zc:__cplusplus", "/permissive-" }
		filter "configurations:Debug"
			for _, m in ipairs(QtModules) do links { "Qt6" .. m .. "d" } end
		filter "configurations:Release"
			for _, m in ipairs(QtModules) do links { "Qt6" .. m } end
		filter {}
		-- Copies the Qt DLLs and plugins next to the exe.
		postbuildcommands { '"' .. QtDir .. '/bin/windeployqt.exe" --no-translations --no-compiler-runtime "%{cfg.buildtarget.abspath}"' }
	else
		-- Homebrew Qt ships frameworks.
		local lib = QtDir .. "/lib"
		buildoptions { "-F" .. lib }
		linkoptions { "-F" .. lib, "-Wl,-rpath," .. lib }
		for _, m in ipairs(QtModules) do
			linkoptions { "-framework Qt" .. m }
		end
	end
end

-- Runs moc on every header with Q_OBJECT and compiles the result.
local function AddMocRules(dir)
	if not MocPath then return end
	for _, header in ipairs(os.matchfiles(dir .. "/**.h")) do
		local text = io.readfile(header)
		if text and text:find("Q_OBJECT") then
			local name = path.getbasename(header)
			local includeName = path.getrelative(Root .. "/src", path.getabsolute(header))
			-- moc output is the same for every configuration so it lives in one shared folder.
			local output = ProjectDir .. "/Intermediate/Moc/moc_" .. name .. ".cpp"
			filter { "files:" .. header }
				buildmessage("moc " .. includeName)
				buildcommands {
					'{MKDIR} "' .. ProjectDir .. '/Intermediate/Moc"',
					'"' .. MocPath .. '" "%{file.abspath}" -f"' .. includeName .. '" -o "' .. output .. '"',
				}
				buildoutputs { output }
				compilebuildoutputs "On"
			filter {}
		end
	end
end

-- Workspace ------------------------------------------------------------------

workspace (ProjectName)
	location (ProjectDir .. "/Build")
	configurations { "Debug", "Release" }
	startproject "Editor"

	language "C++"
	cppdialect "C++20"
	staticruntime "Off"
	multiprocessorcompile "On"

	targetdir (ProjectDir .. "/Binaries/%{cfg.buildcfg}")
	objdir (ProjectDir .. "/Intermediate/%{cfg.buildcfg}/%{prj.name}")
	debugdir (ProjectDir)

	includedirs { Root .. "/src", ProjectDir .. "/Source" }

	filter "configurations:Debug"
		defines { "FP_DEBUG" }
		runtime "Debug"
		symbols "On"
		optimize "Off"

	filter "configurations:Release"
		defines { "NDEBUG" }
		runtime "Release"
		symbols "On"
		optimize "Speed"

	filter "system:windows"
		architecture "x86_64"
		systemversion "latest"
		defines { "_CRT_SECURE_NO_WARNINGS", "NOMINMAX", "WIN32_LEAN_AND_MEAN" }
		buildoptions { "/utf-8", "/Zc:preprocessor" }

	filter "system:macosx"
		systemversion "13.0"

	filter {}

-- Apps link the engine through this so static lib dependencies stay in one place.
local function UseEngine(withGame)
	EngineIncludes()
	if withGame then
		links { "Game" }
	end
	links { "Engine", "Jolt", "ImGui", "Opus" }
	UseSDL()
	UseUltralight()
end

-- Use upstream's portable floating-point source lists; SIMD back ends are optional.
project "Opus"
	kind "StaticLib"
	language "C"
	warnings "Off"
	includedirs { OpusDir, OpusDir .. "/include", OpusDir .. "/celt", OpusDir .. "/silk", OpusDir .. "/silk/float" }
	defines { "OPUS_BUILD", "USE_ALLOCA", 'PACKAGE_VERSION="1.5.2"' }
	for _, list in ipairs({ { "opus", "OPUS_SOURCES", "OPUS_SOURCES_FLOAT" }, { "celt", "CELT_SOURCES" }, { "silk", "SILK_SOURCES", "SILK_SOURCES_FLOAT" } }) do
		local text = "\n" .. assert(io.readfile(OpusDir .. "/" .. list[1] .. "_sources.mk"), "Opus is missing. Run git submodule update --init."):gsub("\\\r?\n", " ")
		for i = 2, #list do
			local sources = assert(text:match("\n" .. list[i] .. "%s*=%s*([^\r\n]+)"), "Missing upstream Opus source list: " .. list[i])
			for source in sources:gmatch("[%w_/]+%.c") do files { OpusDir .. "/" .. source } end
		end
	end
	filter "system:not windows"
		defines { "HAVE_ALLOCA_H" }
	filter "configurations:Debug"
		optimize "Speed"
		runtimechecks "Off"
	filter {}

-- Jolt: physics. Every optional Jolt define stays off here and in the engine, since the
-- library and its users must agree on them. GPU compute back ends are left out.
project "Jolt"
	kind "StaticLib"
	files { JoltDir .. "/Jolt/**.h", JoltDir .. "/Jolt/**.cpp", JoltDir .. "/Jolt/**.inl" }
	removefiles { JoltDir .. "/Jolt/Compute/CPU/**", JoltDir .. "/Jolt/Compute/DX12/**", JoltDir .. "/Jolt/Compute/MTL/**", JoltDir .. "/Jolt/Compute/VK/**", JoltDir .. "/Jolt/Shaders/**" }
	includedirs { JoltDir }
	warnings "Off"
	-- Physics is slow unoptimised, so even Debug builds optimise it.
	filter "configurations:Debug"
		optimize "Speed"
		runtimechecks "Off"
	filter {}

project "ImGui"
	kind "StaticLib"
	files { ImGuiDir .. "/*.h", ImGuiDir .. "/*.cpp" }

-- Engine: HAL, Core, CoreObject and Engine in one static lib.
project "Engine"
	kind "StaticLib"
	files {
		"src/HAL/**.h", "src/HAL/**.cpp",
		"src/Core/**.h", "src/Core/**.cpp",
		"src/CoreObject/**.h", "src/CoreObject/**.cpp",
		"src/Engine/**.h", "src/Engine/**.cpp",
	}
	files { MonocypherDir .. "/monocypher.c" }
	-- Module entry points belong to the scripts library only.
	removefiles { "src/Engine/Scripting/Module/**" }
	-- Shader sources are compiled by Scripts/CompileShaders, not by the C++ build.
	files { "src/Engine/Shaders/**" }
	filter "files:src/Engine/Shaders/**"
		buildaction "None"
	-- Lets the engine find this project when nothing else points it at one.
	filter "files:src/Engine/Paths.cpp"
		defines { 'FP_PROJECT_DIR="' .. ProjectDir .. '"' }
	filter {}
	EngineIncludes()

-- Game: the project's code, linked into Editor and Runtime.
project "Game"
	kind "StaticLib"
	files { ProjectDir .. "/Source/**.h", ProjectDir .. "/Source/**.cpp" }
	-- Scripts build into the GameScripts library instead.
	removefiles { ProjectDir .. "/Source/Scripts/**" }
	EngineIncludes()

-- HeaderTool: reads FPCLASS, FPPROPERTY and FPFUNCTION out of the scripts, like Unreal's header tool.
-- Plain standard C++ with no engine code so it builds in seconds.
project "HeaderTool"
	kind "ConsoleApp"
	files { "src/Tools/HeaderTool/**.h", "src/Tools/HeaderTool/**.cpp" }

local ReflectionHeader = ProjectDir .. "/Intermediate/Generated/ScriptReflection.gen.h"

local function MacAudioMetadata(name)
	if not IsMac then return end
	local plist = ProjectDir .. "/Intermediate/Mac/" .. name .. "-Info.plist"
	os.mkdir(path.getdirectory(plist))
	io.writefile(plist, string.format([[<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>%s</string>
<key>CFBundleIdentifier</key><string>org.firstprinciples.%s</string>
<key>CFBundleName</key><string>FirstPrinciples %s</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>NSHighResolutionCapable</key><true/>
<key>NSMicrophoneUsageDescription</key><string>Use your microphone for proximity voice chat when you enable voice.</string>
</dict></plist>
]], name, name:lower(), name))
	infoplist(plist)
	files { plist }
	if name == "Editor" then
		local bundle = ProjectDir .. '/Binaries/%{cfg.buildcfg}/Editor.app/Contents'
		postbuildcommands {
			'{MKDIR} "' .. bundle .. '/MacOS"',
			'if [ "%{cfg.buildtarget.abspath}" != "' .. bundle .. '/MacOS/Editor" ]; then cp -f "%{cfg.buildtarget.abspath}" "' .. bundle .. '/MacOS/Editor"; fi',
			'{COPYFILE} "' .. plist .. '" "' .. bundle .. '/Info.plist"',
		}
	end
end

-- GameScripts: C++ scripts in the project's Source/Scripts, built as a shared library the editor can hot reload.
project "GameScripts"
	kind "SharedLib"
	targetprefix ""
	files { ProjectDir .. "/Source/Scripts/**.h", ProjectDir .. "/Source/Scripts/**.cpp", "src/Engine/Scripting/Module/**.cpp" }
	UseEngine(true)
	-- Every build regenerates the reflection first, and every script sees it without an include.
	dependson { "HeaderTool" }
	prebuildmessage "Reflecting scripts"
	prebuildcommands { '"%{wks.location}/../Binaries/%{cfg.buildcfg}/HeaderTool" "' .. ProjectDir .. '/Source/Scripts" "' .. ReflectionHeader .. '"' }
	forceincludes { ReflectionHeader }
	filter "system:windows"
		-- A new PDB name per build so a debugger holding the old one never blocks a rebuild.
		linkoptions { '/PDB:"$(OutDir)GameScripts_$([System.DateTime]::Now.Ticks).pdb"' }
	filter {}

project "Runtime"
	kind "ConsoleApp"
	files { "src/Runtime/**.h", "src/Runtime/**.cpp" }
	UseEngine(true)
	dependson { "GameScripts" }
	MacAudioMetadata("Runtime")

project "Editor"
	kind "WindowedApp"
	files { "src/Editor/**.h", "src/Editor/**.cpp" }
	UseEngine(true)
	dependson { "GameScripts" }
	UseQt()
	AddMocRules("src/Editor")
	MacAudioMetadata("Editor")
	filter "system:windows"
		entrypoint "mainCRTStartup"
	-- The viewport warps the mouse with CoreGraphics while a game holds it.
	filter "system:macosx"
		links { "CoreGraphics.framework" }
	filter {}

project "Tests"
	kind "ConsoleApp"
	files { "src/Tests/**.h", "src/Tests/**.cpp" }
	files { "src/Editor/AudioImport.h", "src/Editor/AudioImport.cpp" }
	files { "src/Tools/HeaderTool/HeaderParser.h", "src/Tools/HeaderTool/HeaderParser.cpp" }
	UseEngine(false)
	dependson { "GameScripts" }
