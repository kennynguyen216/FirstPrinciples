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
		bool IsBuilding() const { return m_Process != nullptr; }

		// Every .h and .cpp under Game/Scripts, relative to the project.
		static QStringList ListScriptFiles();
		static QString ScriptsDirectory();

	signals:
		void Started();
		void Finished(bool success);

	private:
		struct Step
		{
			QString Program;
			QStringList Arguments;
		};

		bool PlanSteps(QList<Step>& steps) const;
		void RunNext();
		void OnOutput();
		void OnStepFinished(int exitCode, QProcess::ExitStatus status);

		QProcess* m_Process = nullptr;
		QList<Step> m_Steps;
		bool m_Pending = false;
		bool m_RegeneratedThisBuild = false;
	};
}
