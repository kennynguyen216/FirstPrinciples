#include "Editor/EditorWindow.h"
#include "Editor/CodeEditor.h"
#include "Editor/InspectorPanel.h"
#include "Editor/ScriptBuilder.h"
#include "Editor/ViewportWidget.h"
#include "Engine/ComponentRegistry.h"
#include "Engine/Input.h"
#include "Engine/Material.h"
#include "Engine/Render/MeshData.h"
#include "Engine/Paths.h"
#include "Engine/SceneSerializer.h"
#include "Engine/Scripting/ScriptRuntime.h"
#include "Engine/Scripting/ScriptSystem.h"
#include "Game/Game.h"

#include <SDL3/SDL_timer.h>

#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QDir>
#include <QFileSystemWatcher>
#include <QInputDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>

namespace FP::Editor
{
	namespace
	{
		constexpr int GuidRole = Qt::UserRole + 1;

		QString ToQString(const String& text) { return QString::fromUtf8(text.CStr(), text.Length()); }

		String ToString(const QString& text)
		{
			const QByteArray utf8 = text.toUtf8();
			return String(utf8.constData(), static_cast<int32>(utf8.size()));
		}
	}

	EditorWindow::EditorWindow(GPUDevice& device)
	{
		m_World.Reset(NewObject<World>(nullptr, "EditorWorld"));

		m_Viewport = new ViewportWidget(device, this);
		m_Viewport->SetWorld(m_World.Get());
		// The viewport is the first tab and scripts open beside it.
		m_CentralTabs = new QTabWidget(this);
		m_CentralTabs->setDocumentMode(true);
		m_CentralTabs->setTabsClosable(true);
		m_CentralTabs->setMovable(true);
		m_CentralTabs->addTab(m_Viewport, "Viewport");
		m_CentralTabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
		m_CentralTabs->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
		connect(m_CentralTabs, &QTabWidget::tabCloseRequested, this, [this](int index) { CloseScriptTab(index); });
		setCentralWidget(m_CentralTabs);

		BuildDocks();
		BuildMenus();
		Log::AddSink(&EditorWindow::LogSink, this);

		connect(m_Viewport, &ViewportWidget::EntityPicked, this, [this](Entity entity) { Select(entity); });
		connect(m_Viewport, &ViewportWidget::EntityMoved, this, [this](Entity) { MarkDirty(); });

		// Drives the world and the viewport at about 60 fps.
		m_Timer = new QTimer(this);
		m_Timer->setTimerType(Qt::PreciseTimer);
		connect(m_Timer, &QTimer::timeout, this, &EditorWindow::Tick);
		m_LastTicks = SDL_GetTicksNS();
		m_Timer->start(16);

		resize(1600, 900);
		const QString startScene = ToQString(Paths::Content(Game::StartScene));
		if (QFileInfo::exists(startScene))
			OpenScene(startScene);
		else
			NewScene();
	}

	EditorWindow::~EditorWindow()
	{
		Log::RemoveSink(&EditorWindow::LogSink, this);
	}

	void EditorWindow::LogSink(LogLevel level, const char* message, void* userData)
	{
		EditorWindow* self = static_cast<EditorWindow*>(userData);
		const QString text = QString::fromUtf8(message);
		const QString color = level >= LogLevel::Error ? "#ff6b6b" : level == LogLevel::Warning ? "#f5c542" : "#cccccc";
		// Logs can come from any thread so hop to the UI thread.
		QMetaObject::invokeMethod(self, [self, text, color]() {
			self->m_Console->appendHtml(QString("<span style='color:%1; white-space:pre'>%2</span>").arg(color, text.toHtmlEscaped()));
		}, Qt::QueuedConnection);
	}

	void EditorWindow::BuildDocks()
	{
		m_Hierarchy = new QListWidget();
		connect(m_Hierarchy, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
			if (!item)
				return;
			Guid id;
			if (Guid::Parse(ToString(item->data(GuidRole).toString()), id))
				Select(m_World->FindEntityByGuid(id));
		});
		QDockWidget* hierarchyDock = new QDockWidget("Hierarchy", this);
		hierarchyDock->setObjectName("Hierarchy");
		hierarchyDock->setWidget(m_Hierarchy);
		addDockWidget(Qt::LeftDockWidgetArea, hierarchyDock);

		m_Inspector = new InspectorPanel();
		connect(m_Inspector, &InspectorPanel::OpenScriptRequested, this, &EditorWindow::OpenScript);
		connect(m_Inspector, &InspectorPanel::Modified, this, [this]() {
			MarkDirty();
			RefreshHierarchy();
		});
		QDockWidget* inspectorDock = new QDockWidget("Inspector", this);
		inspectorDock->setObjectName("Inspector");
		inspectorDock->setWidget(m_Inspector);
		inspectorDock->setMinimumWidth(400);
		addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

		m_Console = new QPlainTextEdit();
		m_Console->setReadOnly(true);
		m_Console->setMaximumBlockCount(2000);
		QDockWidget* consoleDock = new QDockWidget("Console", this);
		consoleDock->setObjectName("Console");
		consoleDock->setWidget(m_Console);
		addDockWidget(Qt::BottomDockWidgetArea, consoleDock);
		resizeDocks({ consoleDock }, { 160 }, Qt::Vertical);
	}

	void EditorWindow::BuildMenus()
	{
		QMenu* file = menuBar()->addMenu("&File");
		file->addAction("&New Scene", QKeySequence::New, this, &EditorWindow::NewScene);
		file->addAction("&Open Scene...", QKeySequence::Open, this, &EditorWindow::OpenSceneDialog);
		file->addAction("&Save", QKeySequence::Save, this, &EditorWindow::SaveCurrent);
		file->addAction("Save Scene &As...", QKeySequence::SaveAs, this, &EditorWindow::SaveSceneAs);
		file->addSeparator();
		file->addAction("E&xit", QKeySequence::Quit, this, &QWidget::close);

		QMenu* entity = menuBar()->addMenu("&Entity");
		entity->addAction("Create &Empty", QKeySequence("Ctrl+Shift+N"), this, [this]() { CreateEntity(false); });
		entity->addAction("Create &Sprite", QKeySequence("Ctrl+Shift+A"), this, [this]() { CreateEntity(true); });
		QMenu* meshes = entity->addMenu("Create &Mesh");
		for (const char* shape : Primitives::GetNames())
		{
			const QString name = shape;
			meshes->addAction(name, this, [this, name]() {
				Entity mesh = m_World->CreateEntity(ToString(name));
				m_World->Add<MeshComponent>(mesh).Mesh = ToString(name);
				MarkDirty();
				RefreshHierarchy();
				Select(mesh);
			});
		}
		entity->addAction("Create &Point Light", this, [this]() {
			Entity light = m_World->CreateEntity("Point Light");
			m_World->Get<TransformComponent>(light).Position.Z = 1.5f; // above the sprites so it lights their faces
			m_World->Add<PointLightComponent>(light);
			MarkDirty();
			RefreshHierarchy();
			Select(light);
		});
		entity->addAction("Create &Directional Light", this, [this]() {
			Entity light = m_World->CreateEntity("Directional Light");
			m_World->Get<TransformComponent>(light).Rotation = { -40, 30, 0 };
			m_World->Add<DirectionalLightComponent>(light);
			MarkDirty();
			RefreshHierarchy();
			Select(light);
		});
		entity->addAction("&Duplicate", QKeySequence("Ctrl+D"), this, &EditorWindow::DuplicateSelected);
		entity->addAction("De&lete", QKeySequence::Delete, this, &EditorWindow::DeleteSelected);

		m_ScriptBuilder = new ScriptBuilder(this);
		connect(m_ScriptBuilder, &ScriptBuilder::Finished, this, &EditorWindow::OnScriptsBuilt);
		QMenu* scripts = menuBar()->addMenu("&Scripts");
		scripts->addAction("&New Script...", this, [this]() {
			const QString name = QInputDialog::getText(this, "New Script", "Class name:");
			if (!name.isEmpty())
				NewScript(name.trimmed());
		});
		scripts->addSeparator();
		scripts->addAction("&Reload Scripts", QKeySequence("Ctrl+R"), m_ScriptBuilder, &ScriptBuilder::Build);
		QAction* autoReload = scripts->addAction("&Auto Reload");
		autoReload->setCheckable(true);
		autoReload->setChecked(m_AutoReloadScripts);
		connect(autoReload, &QAction::toggled, this, [this](bool on) { m_AutoReloadScripts = on; });
		WatchScripts();

		QToolBar* toolbar = addToolBar("Play");
		toolbar->setObjectName("PlayToolbar");
		m_PlayAction = toolbar->addAction("Play", this, &EditorWindow::Play);
		m_PlayAction->setShortcut(QKeySequence("Ctrl+P"));
		m_StopAction = toolbar->addAction("Stop", this, &EditorWindow::Stop);
		m_StopAction->setEnabled(false);
		toolbar->addSeparator();
		m_View3DAction = toolbar->addAction("3D View");
		m_View3DAction->setCheckable(true);
		m_View3DAction->setShortcut(QKeySequence("Ctrl+3"));
		connect(m_View3DAction, &QAction::toggled, this, [this](bool on) { m_Viewport->Set3D(on); });

		// View mode picker. The order matches ViewMode so the index converts directly.
		m_ViewModeBox = new QComboBox();
		m_ViewModeBox->addItems({ "Lit", "Unlit", "Wireframe" });
		m_ViewModeBox->setToolTip("View mode (Alt+4 Lit, Alt+3 Unlit, Alt+2 Wireframe)");
		toolbar->addWidget(m_ViewModeBox);
		connect(m_ViewModeBox, &QComboBox::currentIndexChanged, this, [this](int index) {
			m_Viewport->SetViewMode(static_cast<ViewMode>(index));
		});
		// Same keys as Unreal so muscle memory carries over.
		const std::pair<const char*, ViewMode> modeKeys[] = { { "Alt+4", ViewMode::Lit }, { "Alt+3", ViewMode::Unlit }, { "Alt+2", ViewMode::Wireframe } };
		for (const auto& [keys, mode] : modeKeys)
		{
			QAction* action = new QAction(this);
			action->setShortcut(QKeySequence(keys));
			const ViewMode chosen = mode;
			connect(action, &QAction::triggered, this, [this, chosen]() { SetViewMode(chosen); });
			addAction(action);
		}
		toolbar->addSeparator();
		toolbar->addAction("Collect Garbage", this, []() {
			const GarbageCollectionStats stats = GarbageCollector::Get().Collect();
			FP_LOG("GC freed %d objects, %d alive, %.3f ms", stats.NumFreed, stats.NumAlive, stats.Milliseconds);
		});

		statusBar()->showMessage("2D: right drag pans, wheel zooms. 3D (Ctrl+3): right drag orbits, middle drag pans, wheel dollies. Left drag moves, Shift snaps, F focuses");
	}

	void EditorWindow::Set3DView(bool enabled)
	{
		m_View3DAction->setChecked(enabled);
	}

	void EditorWindow::SetViewMode(ViewMode mode)
	{
		m_ViewModeBox->setCurrentIndex(static_cast<int>(mode));
	}

	void EditorWindow::OpenScript(const QString& path)
	{
		const QString resolved = ScriptBuilder::ResolveSourcePath(path);
		if (CodeEditor* open = FindScriptTab(resolved))
		{
			m_CentralTabs->setCurrentWidget(open);
			open->setFocus();
			return;
		}

		CodeEditor* editor = new CodeEditor();
		if (!editor->LoadFile(resolved))
		{
			FP_LOG_ERROR("Could not open %s", resolved.toUtf8().constData());
			delete editor;
			return;
		}
		const int index = m_CentralTabs->addTab(editor, QFileInfo(resolved).fileName());
		m_CentralTabs->setTabToolTip(index, resolved);
		connect(editor->document(), &QTextDocument::modificationChanged, this, [this, editor](bool) { UpdateScriptTabTitle(editor); });
		m_CentralTabs->setCurrentIndex(index);
		editor->setFocus();
	}

	CodeEditor* EditorWindow::FindScriptTab(const QString& path) const
	{
		const QString wanted = QFileInfo(path).canonicalFilePath();
		for (int i = 0; i < m_CentralTabs->count(); ++i)
		{
			CodeEditor* editor = qobject_cast<CodeEditor*>(m_CentralTabs->widget(i));
			if (editor && QFileInfo(editor->GetPath()).canonicalFilePath() == wanted)
				return editor;
		}
		return nullptr;
	}

	void EditorWindow::UpdateScriptTabTitle(CodeEditor* editor)
	{
		const int index = m_CentralTabs->indexOf(editor);
		if (index >= 0)
			m_CentralTabs->setTabText(index, QFileInfo(editor->GetPath()).fileName() + (editor->IsDirty() ? "*" : ""));
	}

	bool EditorWindow::CloseScriptTab(int index)
	{
		CodeEditor* editor = qobject_cast<CodeEditor*>(m_CentralTabs->widget(index));
		if (!editor)
			return false;
		if (editor->IsDirty())
		{
			m_CentralTabs->setCurrentIndex(index);
			const auto answer = QMessageBox::question(this, "Unsaved script", QString("Save changes to %1?").arg(QFileInfo(editor->GetPath()).fileName()),
				QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
			if (answer == QMessageBox::Cancel)
				return false;
			if (answer == QMessageBox::Save && !editor->Save())
				return false;
		}
		m_CentralTabs->removeTab(index);
		editor->deleteLater();
		return true;
	}

	bool EditorWindow::ConfirmCloseScripts()
	{
		for (int i = m_CentralTabs->count() - 1; i >= 0; --i)
		{
			CodeEditor* editor = qobject_cast<CodeEditor*>(m_CentralTabs->widget(i));
			if (editor && editor->IsDirty() && !CloseScriptTab(i))
				return false;
		}
		return true;
	}

	void EditorWindow::ReloadScriptTabIfClean(const QString& path)
	{
		CodeEditor* editor = FindScriptTab(path);
		if (!editor || editor->IsDirty())
			return;
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
			return;
		const QString onDisk = QString::fromUtf8(file.readAll());
		if (onDisk == editor->toPlainText())
			return;
		// Changed outside the editor, so take the new text and keep the cursor near where it was.
		const int line = editor->textCursor().blockNumber() + 1;
		editor->LoadFile(path);
		editor->GoToLine(line);
	}

	void EditorWindow::SaveCurrent()
	{
		if (CodeEditor* editor = qobject_cast<CodeEditor*>(m_CentralTabs->currentWidget()))
		{
			if (editor->Save())
				FP_LOG("Saved %s", QFileInfo(editor->GetPath()).fileName().toUtf8().constData());
			else
				FP_LOG_ERROR("Could not save %s", editor->GetPath().toUtf8().constData());
			return;
		}
		SaveScene();
	}

	bool EditorWindow::NewScript(const QString& className)
	{
		QString error;
		const QString path = ScriptBuilder::CreateScriptFile(className, error);
		if (path.isEmpty())
		{
			FP_LOG_ERROR("New script failed: %s", error.toUtf8().constData());
			QMessageBox::warning(this, "New Script", error);
			return false;
		}
		FP_LOG("Created %s", path.toUtf8().constData());
		// The watcher sees the new file and builds it. Open it so the user can start writing.
		OpenScript(path);
		return true;
	}

	void EditorWindow::WatchScripts()
	{
		m_ScriptWatcher = new QFileSystemWatcher(this);
		m_ScriptDebounce = new QTimer(this);
		m_ScriptDebounce->setSingleShot(true);
		m_ScriptDebounce->setInterval(400);
		connect(m_ScriptDebounce, &QTimer::timeout, this, [this]() {
			if (m_AutoReloadScripts)
				m_ScriptBuilder->Build();
		});
		connect(m_ScriptWatcher, &QFileSystemWatcher::directoryChanged, this, &EditorWindow::OnScriptsChanged);
		connect(m_ScriptWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
			ReloadScriptTabIfClean(path);
			OnScriptsChanged();
		});
		OnScriptsChanged();
		m_ScriptDebounce->stop(); // watching starts quietly, nothing changed yet
	}

	void EditorWindow::OnScriptsChanged()
	{
		// Editors often save by replacing the file, which drops it from the watcher, so re-add every time.
		const QString directory = ScriptBuilder::ScriptsDirectory();
		QStringList paths = { directory };
		const QDir project(QString::fromUtf8(Paths::GetProjectDir().CStr()));
		for (const QString& file : ScriptBuilder::ListScriptFiles())
			paths.append(project.absoluteFilePath(file));
		if (!m_ScriptWatcher->files().isEmpty())
			m_ScriptWatcher->removePaths(m_ScriptWatcher->files());
		if (!m_ScriptWatcher->directories().isEmpty())
			m_ScriptWatcher->removePaths(m_ScriptWatcher->directories());
		m_ScriptWatcher->addPaths(paths);

		// Saves come in bursts, so wait for them to settle before building.
		m_ScriptDebounce->start();
	}

	void EditorWindow::OnScriptsBuilt(bool success)
	{
		if (!success)
			return;
		// Running scripts move to the new code with their field values.
		if (ScriptRuntime::Get().Reload())
			FP_LOG("Scripts reloaded%s", m_World->IsPlaying() ? " while playing" : "");
	}

	void EditorWindow::Tick()
	{
		const uint64 now = SDL_GetTicksNS();
		float deltaTime = static_cast<float>(now - m_LastTicks) / 1e9f;
		m_LastTicks = now;
		if (deltaTime > 0.1f)
			deltaTime = 0.1f;

		m_World->Update(deltaTime);
		Input::EndFrame();

		// Systems can create or destroy entities while playing.
		if (m_World->IsPlaying())
		{
			if (!m_World->IsValid(m_Selected))
				Select(NullEntity);
			if (m_World->GetRegistry().view<IdComponent>().size() != static_cast<usize>(m_HierarchyCount))
				RefreshHierarchy();
			m_Inspector->RefreshValues();
		}
		else if (m_Frame % 10 == 0)
		{
			// Picks up viewport drags without rebuilding every frame.
			m_Inspector->RefreshValues();
		}

		// No point drawing the scene while a script tab covers it.
		if (m_CentralTabs->currentWidget() == m_Viewport)
			m_Viewport->RenderFrame();
		if (++m_Frame % 120 == 0)
			GarbageCollector::Get().Collect();
	}

	void EditorWindow::Select(Entity entity)
	{
		if (!m_World->IsValid(entity))
			entity = NullEntity;
		const bool changed = entity != m_Selected;
		m_Selected = entity;
		m_SelectedGuid = entity != NullEntity ? m_World->Get<IdComponent>(entity).Id : Guid();
		m_Viewport->SetSelected(entity);

		// Keep the hierarchy row in sync without feeding back into Select.
		const QString key = ToQString(m_SelectedGuid.ToString());
		QSignalBlocker block(m_Hierarchy);
		m_Hierarchy->setCurrentItem(nullptr);
		for (int i = 0; i < m_Hierarchy->count(); ++i)
		{
			if (m_Hierarchy->item(i)->data(GuidRole).toString() == key)
				m_Hierarchy->setCurrentRow(i);
		}

		if (changed)
			m_Inspector->SetTarget(m_World.Get(), entity);
	}

	void EditorWindow::RefreshHierarchy()
	{
		QSignalBlocker block(m_Hierarchy);
		m_Hierarchy->clear();
		const Array<Entity> entities = m_World->GetEntities();
		for (Entity entity : entities)
		{
			QListWidgetItem* item = new QListWidgetItem(ToQString(m_World->GetEntityName(entity)));
			item->setData(GuidRole, ToQString(m_World->Get<IdComponent>(entity).Id.ToString()));
			m_Hierarchy->addItem(item);
			if (entity == m_Selected)
				m_Hierarchy->setCurrentItem(item);
		}
		m_HierarchyCount = entities.Num();
	}

	void EditorWindow::MarkDirty()
	{
		if (m_World->IsPlaying() || m_Dirty)
			return;
		m_Dirty = true;
		UpdateTitle();
	}

	void EditorWindow::UpdateTitle()
	{
		const QString name = m_ScenePath.isEmpty() ? "Untitled" : QFileInfo(m_ScenePath).fileName();
		setWindowTitle(QString("%1%2 - FirstPrinciples%3").arg(name, m_Dirty ? "*" : "", m_World->IsPlaying() ? " [Playing]" : ""));
	}

	bool EditorWindow::ConfirmDiscard()
	{
		if (!m_Dirty)
			return true;
		const auto answer = QMessageBox::question(this, "Unsaved changes", "Save changes to the current scene?",
			QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
		if (answer == QMessageBox::Save)
			return SaveScene();
		return answer == QMessageBox::Discard;
	}

	void EditorWindow::NewScene()
	{
		Stop();
		if (!ConfirmDiscard())
			return;
		m_World->Clear();
		Entity camera = m_World->CreateEntity("Main Camera");
		m_World->Add<CameraComponent>(camera);
		m_ScenePath.clear();
		m_Dirty = false;
		Select(NullEntity);
		RefreshHierarchy();
		UpdateTitle();
	}

	void EditorWindow::OpenScene(const QString& path)
	{
		Stop();
		if (SceneSerializer::LoadFromFile(*m_World.Get(), ToString(path).CStr()))
		{
			m_ScenePath = path;
			m_Dirty = false;
			FP_LOG("Opened %s", ToString(path).CStr());
		}
		m_Selected = NullEntity;
		Select(NullEntity);
		m_Inspector->SetTarget(m_World.Get(), NullEntity);
		RefreshHierarchy();
		UpdateTitle();
	}

	void EditorWindow::OpenSceneDialog()
	{
		if (!ConfirmDiscard())
			return;
		const QString path = QFileDialog::getOpenFileName(this, "Open Scene", ToQString(Paths::Content("Scenes")), "Scenes (*.scene)");
		if (!path.isEmpty())
			OpenScene(path);
	}

	bool EditorWindow::SaveScene()
	{
		Stop();
		if (m_ScenePath.isEmpty())
			return SaveSceneAs();
		MaterialLibrary::Get().SaveDirty();
		if (!SceneSerializer::SaveToFile(*m_World.Get(), ToString(m_ScenePath).CStr()))
			return false;
		m_Dirty = false;
		UpdateTitle();
		return true;
	}

	bool EditorWindow::SaveSceneAs()
	{
		Stop();
		const QString path = QFileDialog::getSaveFileName(this, "Save Scene", ToQString(Paths::Content("Scenes")), "Scenes (*.scene)");
		if (path.isEmpty())
			return false;
		m_ScenePath = path.endsWith(".scene") ? path : path + ".scene";
		return SaveScene();
	}

	void EditorWindow::CreateEntity(bool withSprite)
	{
		Entity entity = m_World->CreateEntity(withSprite ? "Sprite" : "Entity");
		if (withSprite)
			m_World->Add<SpriteComponent>(entity);
		MarkDirty();
		RefreshHierarchy();
		Select(entity);
	}

	void EditorWindow::DuplicateSelected()
	{
		if (!m_World->IsValid(m_Selected))
			return;
		// Copies every registered component through its copy hook so new components work with no extra code.
		entt::registry& registry = m_World->GetRegistry();
		const Entity source = m_Selected;
		const Entity copy = m_World->CreateEmptyEntity();
		for (const ComponentInfo* info : ComponentRegistry::Get().GetAll())
		{
			if (info->TypeId == entt::type_hash<IdComponent>::value())
				continue;
			if (!info->TryGet(registry, source))
				continue;
			// Add can move existing storage so fetch the source after it.
			void* dst = info->Add(registry, copy);
			void* src = info->TryGet(registry, source);
			info->Copy(dst, src);
		}
		if (NameComponent* name = m_World->TryGet<NameComponent>(copy))
			name->Name += " Copy";
		if (TransformComponent* transform = m_World->TryGet<TransformComponent>(copy))
			transform->Position += Vec3(0.5f, -0.5f, 0);
		MarkDirty();
		RefreshHierarchy();
		Select(copy);
	}

	void EditorWindow::DeleteSelected()
	{
		if (!m_World->IsValid(m_Selected))
			return;
		m_World->DestroyEntity(m_Selected);
		Select(NullEntity);
		MarkDirty();
		RefreshHierarchy();
	}

	void EditorWindow::Play()
	{
		if (m_World->IsPlaying())
			return;
		// Snapshot the scene so Stop can put everything back.
		m_PlaySnapshot = SceneSerializer::SaveToString(*m_World.Get());
		m_World->AddSystem<ScriptSystem>();
		Game::AddSystems(*m_World.Get());
		m_World->BeginPlay();
		m_Viewport->SetPlaying(true);
		m_PlayAction->setEnabled(false);
		m_StopAction->setEnabled(true);
		UpdateTitle();
		FP_LOG("Play");
	}

	void EditorWindow::Stop()
	{
		if (!m_World->IsPlaying())
			return;
		m_World->EndPlay();
		m_World->ClearSystems();
		m_Viewport->SetPlaying(false);
		SceneSerializer::LoadFromString(*m_World.Get(), m_PlaySnapshot.View());
		m_PlaySnapshot.Clear();

		const Guid selected = m_SelectedGuid;
		m_Selected = NullEntity;
		RefreshHierarchy();
		Select(m_World->FindEntityByGuid(selected));
		m_Inspector->SetTarget(m_World.Get(), m_Selected);
		m_PlayAction->setEnabled(true);
		m_StopAction->setEnabled(false);
		UpdateTitle();
		FP_LOG("Stop");
	}

	void EditorWindow::closeEvent(QCloseEvent* event)
	{
		Stop();
		if (ConfirmCloseScripts() && ConfirmDiscard())
		{
			m_Timer->stop();
			event->accept();
		}
		else
		{
			event->ignore();
		}
	}
}
