#pragma once

#include "Engine/Render/RenderView.h"
#include "Engine/SceneHistory.h"
#include "Engine/World.h"

#include <QMainWindow>

#include <memory>

class QAction;
class QLabel;
class QFileSystemWatcher;
class QTabWidget;
class QDockWidget;
class QPlainTextEdit;
class QTimer;

namespace FP
{
	class GPUDevice;
	class AudioDevice;
}

namespace FP::Editor
{
	class CodeEditor;
	class ContentBrowser;
	class InspectorPanel;
	class HierarchyPanel;
	class ScriptBuilder;
	class ViewportToolbar;
	class ViewportWidget;

	class EditorWindow : public QMainWindow
	{
		Q_OBJECT

	public:
		EditorWindow(GPUDevice& device, AudioDevice& audio);
		~EditorWindow() override;

		void OpenScene(const QString& path);
		World& GetWorld() { return *m_World.Get(); }
		void Set3DView(bool enabled);
		void SetViewMode(ViewMode mode);
		void Play();
		void Stop();
		void Select(Entity entity);

		// Creates an entity from a Place Entities kind such as "Point Light" or "Cube" and selects it.
		Entity CreateEntity(const QString& kind);

		// Steps the scene back or forward. A focused script tab gets its own text undo instead.
		void Undo();
		void Redo();
		ScriptBuilder* GetScriptBuilder() const { return m_ScriptBuilder; }

		// Opens a script source file for editing.
		void OpenScript(const QString& path);

		// Creates Source/Scripts/<className>.cpp and opens it. Returns false and explains why on failure.
		bool NewScript(const QString& className);
		// Asks for a class name, makes the script and attaches it to entity once it compiles.
		void NewScriptFor(const Guid& entity);
		void AttachPendingScripts();
		// Builds this project's editor and reopens it on the current scene.
		void RebuildEditorAndRestart();
		void AttachScript(Entity entity, const String& name);

		struct PendingAttach
		{
			Guid Entity;
			String Script;
		};
		QList<PendingAttach> m_PendingAttach;

		ContentBrowser* GetContentBrowser() const { return m_ContentBrowser; }

		// Creates or changes entities for a file dropped onto the viewport. Absolute path.
		void DropAsset(const QString& path, Vec3 point, Entity target);

		// Forgets unsaved scene and script edits so closing never prompts or saves.
		// Smoke test runs call this so they can never write to the project.
		void DiscardUnsavedChanges();

		// Shows an asset file in the inspector, clearing the entity selection. Absolute path.
		void ShowAsset(const QString& path);

		// Builds the editor of the project in dir, then closes this one and starts that one.
		// Game code is compiled into the editor, so each project runs its own.
		void OpenProject(const QString& dir);

		// Creates a project next to the open one and opens it. Returns false and explains why on failure.
		bool NewProject(const QString& name);

	protected:
		void closeEvent(QCloseEvent* event) override;
		bool eventFilter(QObject* watched, QEvent* event) override;

	private:
		void BuildMenus();
		void BuildDocks();
		void BuildStatusBar();

		// Shows each dock's title only when it is not sharing a tab bar.
		void UpdateDockTitles();
		void UpdateDockTitlesLater();

		// Puts every panel back where the editor first placed it.
		void ResetLayout();

		// Runs a line typed into the status bar's console box.
		void RunCommand(const QString& line);

		void Tick();
		void RefreshHierarchy();
		void MarkDirty();

		// Turns the edits since the last step into one undo step.
		void CommitHistory();
		void ResetHistory();
		void RestoreHistoryStep();
		void UpdateUndoActions();
		void UpdateTitle();
		bool ConfirmDiscard();

		void NewScene();
		void OpenSceneDialog();
		bool SaveScene();
		bool SaveSceneAs();

		void CopySelected();
		void Paste();
		void DuplicateSelected();
		void PasteEntities(const String& text, const char* nameSuffix);
		void DeleteSelected();
		void ImportAudio(bool assign);
		void ShowAudioSettings();

		static void LogSink(LogLevel level, const char* message, void* userData);

		// Watches Source/Scripts and rebuilds after edits settle when auto reload is on.
		void WatchScripts();
		void OnScriptsChanged();
		void OnScriptsBuilt(bool success);
		void OnProjectBuilt(bool success, const QString& dir);

		// Script tabs next to the viewport.
		CodeEditor* FindScriptTab(const QString& path) const;
		void UpdateScriptTabTitle(CodeEditor* editor);
		bool CloseScriptTab(int index);
		bool ConfirmCloseScripts();
		void ReloadScriptTabIfClean(const QString& path);

		// Opens the file and line named in a compiler message. False when the text names none.
		bool OpenBuildMessage(const QString& line);

		// Ctrl+S saves the open script when one is showing, otherwise the scene.
		void SaveCurrent();

		// Saves the scene, changed materials and every edited script tab.
		void SaveAll();

		StrongObjectPtr<World> m_World;
		AudioDevice& m_Audio;
		Entity m_Selected = NullEntity;
		Guid m_SelectedGuid;

		QString m_ScenePath;
		bool m_Dirty = false;
		String m_PlaySnapshot;

		// Edits wait for a quiet moment so a drag or a typed name becomes one step.
		std::unique_ptr<SceneHistory> m_History;
		QTimer* m_HistoryTimer = nullptr;
		bool m_EditPending = false;

		ViewportWidget* m_Viewport = nullptr;
		InspectorPanel* m_Inspector = nullptr;
		HierarchyPanel* m_Hierarchy = nullptr;
		QPlainTextEdit* m_Console = nullptr;
		QTimer* m_Timer = nullptr;
		QAction* m_UndoAction = nullptr;
		QAction* m_RedoAction = nullptr;
		QAction* m_PlayAction = nullptr;
		QAction* m_StopAction = nullptr;
		QAction* m_View3DAction = nullptr;
		QAction* m_ImportAudioAction = nullptr;
		QLabel* m_AudioStatus = nullptr;
		float m_AudioGains[4] = { 1, 1, 1, 1 };
		bool m_AudioMutes[4] = {};
		ViewportToolbar* m_ViewportToolbar = nullptr;
		ScriptBuilder* m_ScriptBuilder = nullptr;
		QTabWidget* m_CentralTabs = nullptr;
		ContentBrowser* m_ContentBrowser = nullptr;
		QDockWidget* m_ConsoleDock = nullptr;
		QDockWidget* m_ContentDock = nullptr;
		bool m_DockTitlesPending = false;
		QByteArray m_DefaultLayout;
		QFileSystemWatcher* m_ScriptWatcher = nullptr;
		QTimer* m_ScriptDebounce = nullptr;
		bool m_AutoReloadScripts = true;

		uint64 m_LastTicks = 0;
		int32 m_HierarchyCount = -1;
		int32 m_Frame = 0;
	};
}
