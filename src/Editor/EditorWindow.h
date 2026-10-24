#pragma once

#include "Engine/Render/RenderView.h"
#include "Engine/SceneHistory.h"
#include "Engine/World.h"

#include <QMainWindow>

#include <memory>

class QAction;
class QComboBox;
class QFileSystemWatcher;
class QTabWidget;
class QDockWidget;
class QListWidget;
class QPlainTextEdit;
class QTimer;

namespace FP
{
	class GPUDevice;
}

namespace FP::Editor
{
	class CodeEditor;
	class ContentBrowser;
	class InspectorPanel;
	class ScriptBuilder;
	class ViewportWidget;

	class EditorWindow : public QMainWindow
	{
		Q_OBJECT

	public:
		explicit EditorWindow(GPUDevice& device);
		~EditorWindow() override;

		void OpenScene(const QString& path);
		World& GetWorld() { return *m_World.Get(); }
		void Set3DView(bool enabled);
		void SetViewMode(ViewMode mode);
		void Play();
		void Stop();
		void Select(Entity entity);
		ScriptBuilder* GetScriptBuilder() const { return m_ScriptBuilder; }

		// Opens a script source file for editing.
		void OpenScript(const QString& path);

		// Creates Game/Scripts/<className>.cpp and opens it. Returns false and explains why on failure.
		bool NewScript(const QString& className);

		ContentBrowser* GetContentBrowser() const { return m_ContentBrowser; }

		// Creates or changes entities for a file dropped onto the viewport. Absolute path.
		void DropAsset(const QString& path, Vec3 point, Entity target);

		// Forgets unsaved scene and script edits so closing never prompts or saves.
		// Smoke test runs call this so they can never write to the project.
		void DiscardUnsavedChanges();

		// Shows an asset file in the inspector, clearing the entity selection. Absolute path.
		void ShowAsset(const QString& path);

	protected:
		void closeEvent(QCloseEvent* event) override;
		bool eventFilter(QObject* watched, QEvent* event) override;

	private:
		void BuildMenus();
		void BuildDocks();

		void Tick();
		void RefreshHierarchy();
		void MarkDirty();

		// Turns the edits since the last step into one undo step.
		void CommitHistory();
		void ResetHistory();
		void UpdateTitle();
		bool ConfirmDiscard();

		void NewScene();
		void OpenSceneDialog();
		bool SaveScene();
		bool SaveSceneAs();

		void CreateEntity(bool withSprite);
		void DuplicateSelected();
		void DeleteSelected();

		static void LogSink(LogLevel level, const char* message, void* userData);

		// Watches Game/Scripts and rebuilds after edits settle when auto reload is on.
		void WatchScripts();
		void OnScriptsChanged();
		void OnScriptsBuilt(bool success);

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

		StrongObjectPtr<World> m_World;
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
		QListWidget* m_Hierarchy = nullptr;
		QPlainTextEdit* m_Console = nullptr;
		QTimer* m_Timer = nullptr;
		QAction* m_PlayAction = nullptr;
		QAction* m_StopAction = nullptr;
		QAction* m_View3DAction = nullptr;
		QComboBox* m_ViewModeBox = nullptr;
		ScriptBuilder* m_ScriptBuilder = nullptr;
		QTabWidget* m_CentralTabs = nullptr;
		ContentBrowser* m_ContentBrowser = nullptr;
		QDockWidget* m_ConsoleDock = nullptr;
		QFileSystemWatcher* m_ScriptWatcher = nullptr;
		QTimer* m_ScriptDebounce = nullptr;
		bool m_AutoReloadScripts = true;

		uint64 m_LastTicks = 0;
		int32 m_HierarchyCount = -1;
		int32 m_Frame = 0;
	};
}
