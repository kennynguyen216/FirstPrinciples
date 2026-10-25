#include "Editor/ScriptBuilder.h"
#include "Engine/Project.h"
#include "Engine/Paths.h"
#include "Engine/Scripting/ScriptRuntime.h"
#include "Core/Log.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>

namespace FP::Editor
{
	namespace
	{
		QString ProjectDir() { return QString::fromUtf8(Paths::GetProjectDir().CStr()); }

		// Folder with premake5.lua.
		QString RootDir() { return QFileInfo(QString::fromUtf8(Paths::GetEngineDir().CStr())).absolutePath(); }

		// The project's generated makefiles or solution.
		QString BuildDir() { return ProjectDir() + "/Build"; }

		// Records which script files the projects were generated with.
		QString StampPath() { return BuildDir() + "/ScriptFiles.txt"; }

		// GUI apps on macOS do not inherit the shell PATH, so add the usual Homebrew locations.
		QProcessEnvironment BuildEnvironment()
		{
			QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
#if !defined(_WIN32)
			env.insert("PATH", "/opt/homebrew/bin:/usr/local/bin:" + env.value("PATH"));
#endif
			return env;
		}

		QString FindProgram(const QString& name, const QStringList& extraDirs = {})
		{
			QStringList dirs = extraDirs;
			for (const QString& dir : BuildEnvironment().value("PATH").split(QDir::listSeparator()))
				dirs.append(dir);
			const QString found = QStandardPaths::findExecutable(name, dirs);
			return found.isEmpty() ? name : found;
		}

#if defined(_WIN32)
		// Latest MSBuild from any installed Visual Studio or Build Tools.
		QString FindMSBuild()
		{
			const QString vswhere = QString::fromLocal8Bit(qgetenv("ProgramFiles(x86)")) + "/Microsoft Visual Studio/Installer/vswhere.exe";
			QProcess query;
			query.start(vswhere, { "-latest", "-products", "*", "-requires", "Microsoft.Component.MSBuild", "-find", "MSBuild\\**\\Bin\\MSBuild.exe" });
			query.waitForFinished(10000);
			const QString path = QString::fromLocal8Bit(query.readAllStandardOutput()).split('\n').value(0).trimmed();
			return path.isEmpty() ? "MSBuild.exe" : path;
		}
#endif
	}

	ScriptBuilder::ScriptBuilder(QObject* parent) : QObject(parent) {}

	QString ScriptBuilder::ScriptsDirectory()
	{
		return QString::fromUtf8(Paths::GetProject().GetScriptsDir().CStr());
	}

	QString ScriptBuilder::CreateScriptFile(const QString& className, QString& error)
	{
		// Class names become C++ identifiers and file names, so keep them simple.
		static const QRegularExpression valid("^[A-Za-z_][A-Za-z0-9_]*$");
		if (!valid.match(className).hasMatch())
		{
			error = "Use letters, digits and underscores, starting with a letter.";
			return {};
		}
		const QString path = ScriptsDirectory() + "/" + className + ".cpp";
		if (QFileInfo::exists(path))
		{
			error = className + ".cpp already exists.";
			return {};
		}

		QString code = QString::fromUtf8(R"(#include "Engine/Scripting/Script.h"

using namespace FP;

// Say what NAME does here. This comment shows in the editor when the script is picked.
FPCLASS()
class NAME : public Script
{
public:
	// FPPROPERTY fields show in the inspector, save with the scene and keep their value on reload.
	FPPROPERTY(Range = "0, 10")
	float Speed = 1; // a comment here becomes the field's tooltip

	void OnStart() override
	{
		// Runs once when play starts. A few helpers to reach for:
		//   FindEntity("Player"), Spawn("Bullet", position), SpawnScene("Prefabs/Enemy.scene", position)
		//   GetParent(), GetChildren(), FindChild("Gun"), SetParent(entity), GetScript<OtherScript>(entity)
		//   After(2.0f, [this]() { ... }), Every(1.0f, [this]() { ... }), LoadScene("Scenes/Level2.scene")
	}

	void OnUpdate(float deltaTime) override
	{
		// Runs every frame. GetTransform() moves this entity and Input::IsKeyDown(SDL_SCANCODE_SPACE) reads keys.
		(void)deltaTime;
	}

	// FPFUNCTION makes a button in the inspector that calls this while playing.
	FPFUNCTION()
	void Test()
	{
		FP_LOG("NAME says hello from %s", GetName(GetEntity()).CStr());
	}
};

FP_SCRIPT(NAME);
)");
		code.replace("NAME", className);

		QDir().mkpath(ScriptsDirectory());
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		{
			error = "Could not write " + path;
			return {};
		}
		file.write(code.toUtf8());
		return path;
	}

	QString ScriptBuilder::ResolveSourcePath(const QString& path)
	{
		if (QFileInfo(path).isAbsolute())
			return QDir::cleanPath(path);
		// Makefiles compile from Build, so relative paths start there.
		for (const QString& base : { BuildDir(), ProjectDir(), RootDir() })
		{
			const QString candidate = QDir(base).absoluteFilePath(path);
			if (QFileInfo::exists(candidate))
				return QDir::cleanPath(candidate);
		}
		return path;
	}

	QStringList ScriptBuilder::ListScriptFiles()
	{
		QStringList files;
		QDirIterator it(ScriptsDirectory(), { "*.h", "*.cpp" }, QDir::Files, QDirIterator::Subdirectories);
		const QDir project(ProjectDir());
		while (it.hasNext())
			files.append(project.relativeFilePath(it.next()));
		files.sort();
		return files;
	}

	bool ScriptBuilder::PlanSteps(QList<Step>& steps, const QString& projectDir, const QString& target, bool regenerate)
	{
		const QString root = RootDir();
		const QString buildDir = projectDir + "/Build";
		const QStringList premakeArgs = { "--file=" + root + "/premake5.lua", "--project=" + projectDir };

#if defined(_WIN32)
		// The solution format tells which Visual Studio generated it. A project without one
		// matches the open project's.
		const QString name = QDir(projectDir).dirName();
		const QString current = BuildDir() + "/" + QDir(ProjectDir()).dirName();
		const bool vs2026 = QFileInfo::exists(buildDir + "/" + name + ".slnx")
			|| (!QFileInfo::exists(buildDir + "/" + name + ".sln") && QFileInfo::exists(current + ".slnx"));
		const QString solution = buildDir + "/" + name + (vs2026 ? ".slnx" : ".sln");
		if (!regenerate && !QFileInfo::exists(solution))
		{
			FP_LOG_ERROR("No solution in the project's Build folder. Run Scripts\\GenerateProjects.bat first.");
			return false;
		}
		if (regenerate)
			steps.append({ root + "/ThirdParty/premake/premake5.exe", premakeArgs + QStringList{ vs2026 ? "vs2026" : "vs2022" } });
		const QString config = FP_BUILD_DEBUG ? "Debug" : "Release";
		steps.append({ FindMSBuild(), { solution, "/t:" + target, "/p:Configuration=" + config, "/p:Platform=x64", "/m", "/nologo", "/v:minimal" } });
#else
		if (regenerate)
			steps.append({ FindProgram("premake5"), premakeArgs + QStringList{ "gmake" } });
		const QString config = FP_BUILD_DEBUG ? "debug" : "release";
		steps.append({ FindProgram("make"), { "-C", buildDir, target, "config=" + config, QString("-j%1").arg(QThread::idealThreadCount()) } });
#endif
		return true;
	}

	QString ScriptBuilder::EditorPath(const QString& projectDir)
	{
#if defined(_WIN32)
		return projectDir + (FP_BUILD_DEBUG ? "/Binaries/Debug/Editor.exe" : "/Binaries/Release/Editor.exe");
#elif defined(__APPLE__)
		return projectDir + (FP_BUILD_DEBUG ? "/Binaries/Debug/Editor.app/Contents/MacOS/Editor" : "/Binaries/Release/Editor.app/Contents/MacOS/Editor");
#else
		return projectDir + (FP_BUILD_DEBUG ? "/Binaries/Debug/Editor" : "/Binaries/Release/Editor");
#endif
	}

	void ScriptBuilder::BuildProject(const QString& projectDir)
	{
		if (m_Process)
		{
			FP_LOG_WARN("Wait for the current build to finish first.");
			emit ProjectBuilt(false, projectDir);
			return;
		}
		m_Steps.clear();
		m_OtherProject = projectDir;
		m_RegeneratedThisBuild = false;
		// The editor target depends on GameScripts, so one build makes both.
		if (!PlanSteps(m_Steps, projectDir, "Editor", true))
		{
			m_OtherProject.clear();
			emit ProjectBuilt(false, projectDir);
			return;
		}
		FP_LOG("Building %s. The first build of a project compiles the whole engine and can take a few minutes.", QDir(projectDir).dirName().toUtf8().constData());
		emit Started();
		RunNext();
	}

	void ScriptBuilder::Build()
	{
		if (m_Process)
		{
			m_Pending = true;
			return;
		}
		m_Steps.clear();
		const QString files = ListScriptFiles().join('\n');
		QFile stamp(StampPath());
		const bool regenerate = !stamp.open(QIODevice::ReadOnly) || QString::fromUtf8(stamp.readAll()) != files;
		if (!PlanSteps(m_Steps, ProjectDir(), "GameScripts", regenerate))
		{
			emit Finished(false);
			return;
		}
		m_RegeneratedThisBuild = m_Steps.size() > 1;
		if (m_RegeneratedThisBuild)
		{
			// A removed script leaves nothing newer than the old library, so make would skip
			// the link and keep the deleted script. Deleting the output forces a relink.
			// The editor runs a copy, so the original is never locked.
			QFile::remove(QString::fromUtf8(ScriptRuntime::GetDefaultModulePath().CStr()));
		}
		FP_LOG("Building scripts...");
		emit Started();
		RunNext();
	}

	void ScriptBuilder::Rebuild()
	{
		if (m_Process)
		{
			FP_LOG_WARN("Wait for the current build to finish first.");
			return;
		}
		const QString config = FP_BUILD_DEBUG ? "Debug" : "Release";
		QDir(ProjectDir() + "/Intermediate/" + config + "/GameScripts").removeRecursively();
		QFile::remove(ProjectDir() + "/Intermediate/Generated/ScriptReflection.gen.h");
		// Removing the stamp makes the build regenerate the projects as well.
		QFile::remove(StampPath());
		FP_LOG("Cleaned the scripts. Rebuilding from scratch...");
		Build();
	}

	void ScriptBuilder::RunNext()
	{
		const Step step = m_Steps.takeFirst();
		m_Process = new QProcess(this);
		m_Process->setProcessEnvironment(BuildEnvironment());
		m_Process->setWorkingDirectory(RootDir());
		m_Process->setProcessChannelMode(QProcess::MergedChannels);
		connect(m_Process, &QProcess::readyReadStandardOutput, this, &ScriptBuilder::OnOutput);
		connect(m_Process, &QProcess::finished, this, &ScriptBuilder::OnStepFinished);
		connect(m_Process, &QProcess::errorOccurred, this, [this, step](QProcess::ProcessError error) {
			if (error == QProcess::FailedToStart)
			{
				FP_LOG_ERROR("Could not run %s", step.Program.toUtf8().constData());
				OnStepFinished(-1, QProcess::CrashExit);
			}
		});
		m_Process->start(step.Program, step.Arguments);
	}

	void ScriptBuilder::OnOutput()
	{
		while (m_Process && m_Process->canReadLine())
		{
			const QString line = QString::fromLocal8Bit(m_Process->readLine()).trimmed();
			if (line.isEmpty())
				continue;
			const QByteArray utf8 = line.toUtf8();
			if (line.contains("error", Qt::CaseInsensitive))
				FP_LOG_ERROR("%s", utf8.constData());
			else if (line.contains("warning", Qt::CaseInsensitive))
				FP_LOG_WARN("%s", utf8.constData());
			else
				FP_LOG_VERBOSE("%s", utf8.constData());
		}
	}

	void ScriptBuilder::OnStepFinished(int exitCode, QProcess::ExitStatus status)
	{
		if (!m_Process)
			return;
		OnOutput();
		m_Process->deleteLater();
		m_Process = nullptr;

		const bool ok = status == QProcess::NormalExit && exitCode == 0;
		if (ok && !m_Steps.isEmpty())
		{
			RunNext();
			return;
		}
		if (ok && m_RegeneratedThisBuild)
		{
			QFile stamp(StampPath());
			if (stamp.open(QIODevice::WriteOnly))
				stamp.write(ListScriptFiles().join('\n').toUtf8());
		}
		m_Steps.clear();
		if (!m_OtherProject.isEmpty())
		{
			const QString projectDir = m_OtherProject;
			m_OtherProject.clear();
			if (!ok)
				FP_LOG_ERROR("Project build failed");
			emit ProjectBuilt(ok, projectDir);
		}
		else
		{
			if (ok)
				FP_LOG("Scripts built");
			else
				FP_LOG_ERROR("Script build failed");
			emit Finished(ok);
		}

		if (m_Pending)
		{
			m_Pending = false;
			Build();
		}
	}
}
