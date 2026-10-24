#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

namespace FP::Editor
{
	// Builds the GameScripts library in the background with the same tools as the command line.
	// macOS and Linux run make on the premake makefiles. Windows runs MSBuild found through vswhere.
	// Premake runs first when the set of script files changed, since projects list files explicitly.
	class ScriptBuilder : public QObject
	{
		Q_OBJECT

	public:
		explicit ScriptBuilder(QObject* parent = nullptr);

		// Starts a build. A build requested while one runs starts again once it finishes.
		void Build();

		// Deletes the scripts library, its objects and the generated reflection, then builds
		// everything again from scratch. For when an incremental build gets stuck.
		void Rebuild();
		bool IsBuilding() const { return m_Process != nullptr; }

		// Generates and builds the Editor of the project in projectDir, then emits ProjectBuilt.
		// Game code is compiled into the editor, so every project needs its own.
		void BuildProject(const QString& projectDir);

		// Where the Editor of the project in projectDir is built to.
		static QString EditorPath(const QString& projectDir);

		// Every .h and .cpp under Source/Scripts, relative to the project.
		static QStringList ListScriptFiles();
		static QString ScriptsDirectory();

		// Writes Source/Scripts/<className>.cpp from the starter template. Returns the path or empty on failure.
		static QString CreateScriptFile(const QString& className, QString& error);

		// Compilers record source paths relative to where they ran. Returns an absolute path.
		static QString ResolveSourcePath(const QString& path);

	signals:
		void Started();
		void Finished(bool success);
		void ProjectBuilt(bool success, const QString& projectDir);

	private:
		struct Step
		{
			QString Program;
			QStringList Arguments;
		};

		// Steps to build target in projectDir. Premake runs first when regenerate is set.
		static bool PlanSteps(QList<Step>& steps, const QString& projectDir, const QString& target, bool regenerate);
		void RunNext();
		void OnOutput();
		void OnStepFinished(int exitCode, QProcess::ExitStatus status);

		QProcess* m_Process = nullptr;
		QList<Step> m_Steps;
		bool m_Pending = false;
		bool m_RegeneratedThisBuild = false;
		// Set while building another project's editor instead of scripts.
		QString m_OtherProject;
	};
}
