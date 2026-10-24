#pragma once

#include "Engine/Render/RenderView.h"
#include "Engine/World.h"

#include <QMainWindow>

class QAction;
class QComboBox;
class QListWidget;
class QPlainTextEdit;
class QTimer;

namespace FP
{
	class GPUDevice;
}

namespace FP::Editor
{
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

	protected:
		void closeEvent(QCloseEvent* event) override;

	private:
		void BuildMenus();
		void BuildDocks();

		void Tick();
		void RefreshHierarchy();
		void MarkDirty();
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

		StrongObjectPtr<World> m_World;
		Entity m_Selected = NullEntity;
		Guid m_SelectedGuid;

		QString m_ScenePath;
		bool m_Dirty = false;
		String m_PlaySnapshot;

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

		uint64 m_LastTicks = 0;
		int32 m_HierarchyCount = -1;
		int32 m_Frame = 0;
	};
}
