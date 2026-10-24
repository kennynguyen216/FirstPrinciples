-- FirstPrinciples workspace.
-- macOS: premake5 gmake (or xcode4). Windows: premake5 vs2022. See README.md.

local Root = path.getabsolute(".")
local IsWindows = os.target() == "windows"
local IsMac = os.target() == "macosx"

-- Third party locations ------------------------------------------------------

-- On macOS everything comes from Homebrew. On Windows SDL comes from Scripts/Setup.ps1
-- and Qt from the official installer, found through the QT_DIR environment variable.
local BrewPrefix = "/opt/homebrew"
if IsMac then
	local out = os.outputof("brew --prefix")
	if out and #out > 0 then BrewPrefix = out end
end

local SdlDir = IsWindows and (Root .. "/ThirdParty/SDL3") or (BrewPrefix .. "/opt/sdl3")

local QtDir = os.getenv("QT_DIR")
if not QtDir and IsMac then QtDir = BrewPrefix .. "/opt/qt" end
if QtDir then QtDir = QtDir:gsub("\\", "/") end

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

-- Helpers --------------------------------------------------------------------

local function UseSDL()
	externalincludedirs { SdlDir .. "/include" }
	if IsWindows then
		libdirs { SdlDir .. "/lib/x64" }
		links { "SDL3" }
		postbuildcommands { '{COPYFILE} "' .. SdlDir .. '/lib/x64/SDL3.dll" "%{cfg.targetdir}"' }
	else
		libdirs { SdlDir .. "/lib" }
		links { "SDL3" }
		linkoptions { "-Wl,-rpath," .. SdlDir .. "/lib" }
	end
end

local function UseQt()
	if not QtDir then
		premake.warn("QT_DIR is not set. The Editor project will not build until it points at your Qt install.")
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
			local output = "%{cfg.objdir}/moc_" .. name .. ".cpp"
			filter { "files:" .. header }
				buildmessage("moc " .. includeName)
				buildcommands { '"' .. MocPath .. '" "%{file.abspath}" -f"' .. includeName .. '" -o "' .. output .. '"' }
				buildoutputs { output }
				compilebuildoutputs "On"
			filter {}
		end
	end
end

-- Workspace ------------------------------------------------------------------

workspace "FirstPrinciples"
	location "Build"
	configurations { "Debug", "Release" }
	startproject "Editor"

	language "C++"
	cppdialect "C++20"
	staticruntime "Off"
	multiprocessorcompile "On"

	targetdir (Root .. "/Binaries/%{cfg.buildcfg}")
	objdir (Root .. "/Intermediate/%{cfg.buildcfg}/%{prj.name}")
	debugdir (Root)

	includedirs { Root .. "/src", Root }

	filter "configurations:Debug"
		defines { "FP_DEBUG" }
		symbols "On"
		optimize "Off"

	filter "configurations:Release"
		defines { "NDEBUG" }
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

-- Engine: HAL, Core, CoreObject and Engine in one static lib.
project "Engine"
	kind "StaticLib"
	files {
		"src/HAL/**.h", "src/HAL/**.cpp",
		"src/Core/**.h", "src/Core/**.cpp",
		"src/CoreObject/**.h", "src/CoreObject/**.cpp",
		"src/Engine/**.h", "src/Engine/**.cpp",
	}
	externalincludedirs { SdlDir .. "/include" }

-- Game: the team's code, linked into Editor and Runtime.
project "Game"
	kind "StaticLib"
	files { "Game/**.h", "Game/**.cpp" }
	externalincludedirs { SdlDir .. "/include" }

project "Runtime"
	kind "ConsoleApp"
	files { "src/Runtime/**.h", "src/Runtime/**.cpp" }
	links { "Game", "Engine" }
	UseSDL()

project "Editor"
	kind "WindowedApp"
	files { "src/Editor/**.h", "src/Editor/**.cpp" }
	links { "Game", "Engine" }
	UseSDL()
	UseQt()
	AddMocRules("src/Editor")
	filter "system:windows"
		entrypoint "mainCRTStartup"
	filter {}

project "Tests"
	kind "ConsoleApp"
	files { "src/Tests/**.h", "src/Tests/**.cpp" }
	links { "Engine" }
	UseSDL()
