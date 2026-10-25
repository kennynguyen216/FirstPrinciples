#include "Editor/EditorWindow.h"
#include "Editor/MeshletDebugWidget.h"
#include "Editor/AudioImport.h"
#include "Engine/Audio/AudioDevice.h"
#include "Engine/Audio/AudioWorld.h"
#include "Engine/Audio/VoiceChat.h"
#include "Editor/CodeEditor.h"
#include "Editor/ContentBrowser.h"
#include "Editor/DockTab.h"
#include "Editor/EditorIcons.h"
#include "Editor/EditorStyle.h"
#include "Editor/InspectorPanel.h"
#include "Editor/HierarchyPanel.h"
#include "Editor/PlaceEntitiesPanel.h"
#include "Editor/ScriptBuilder.h"
#include "Editor/ViewportToolbar.h"
#include "Editor/ViewportWidget.h"
#include "Engine/ComponentRegistry.h"
#include "Engine/Input.h"
#include "Engine/Material.h"
#include "Engine/Net/NetworkSystem.h"
#include "Engine/Render/MeshData.h"
#include "Engine/Paths.h"
#include "Engine/Project.h"
#include "Engine/SceneSerializer.h"
#include "Engine/Scripting/ScriptRuntime.h"
#include "Engine/Scripting/ScriptSystem.h"
#include "Engine/Animation/AnimationSystem.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Physics/PlayerInputSystem.h"
#include "Engine/UI/UISystem.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Animation/SkeletalModel.h"
#include "Game.h"

#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_events.h>

#include <type_traits>
#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <QDockWidget>
#include <QFileDialog>
#include <QDir>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QInputDialog>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QTextBlock>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QStatusBar>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QProgressBar>

namespace FP::Editor
{
	namespace
	{

		QString ToQString(const String& text) { return QString::fromUtf8(text.CStr(), text.Length()); }

		String ToString(const QString& text)
		{
			const QByteArray utf8 = text.toUtf8();
			return String(utf8.constData(), static_cast<int32>(utf8.size()));
		}
	}

	EditorWindow::EditorWindow(GPUDevice& device, AudioDevice& audio) : m_Audio(audio)
	{
		m_World.Reset(NewObject<World>(nullptr, "EditorWorld"));
		m_World->GetAudio().Attach(&audio);
		m_History = std::make_unique<SceneHistory>(*m_World.Get());
		m_HistoryTimer = new QTimer(this);
		m_HistoryTimer->setSingleShot(true);
		m_HistoryTimer->setInterval(300);
		connect(m_HistoryTimer, &QTimer::timeout, this, [this]() {
			// Still dragging, so the edit is not finished yet.
			if (QGuiApplication::mouseButtons() != Qt::NoButton)
				m_HistoryTimer->start();
			else
				CommitHistory();
		});

		m_Viewport = new ViewportWidget(device, this);
		m_Viewport->SetWorld(m_World.Get());
		m_ViewportToolbar = new ViewportToolbar();
		m_Viewport->SetOverlay(m_ViewportToolbar);
		// The viewport is the first tab and scripts open beside it.
		m_CentralTabs = new QTabWidget(this);
		m_CentralTabs->setDocumentMode(true);
		m_CentralTabs->setTabsClosable(true);
		m_CentralTabs->setMovable(true);
		m_CentralTabs->addTab(m_Viewport, EditorIcons::Get(EditorIcon::Level), "Viewport");
		m_CentralTabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
		m_CentralTabs->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
		connect(m_CentralTabs, &QTabWidget::tabCloseRequested, this, [this](int index) { CloseScriptTab(index); });
		connect(m_CentralTabs, &QTabWidget::currentChanged, this, [this]() {
			if (m_UndoAction)
				UpdateUndoActions();
		});
		setCentralWidget(m_CentralTabs);

		BuildDocks();
		BuildMenus();
		Log::AddSink(&EditorWindow::LogSink, this);

		connect(m_Viewport, &ViewportWidget::EntityPicked, this, [this](Entity entity) { Select(entity); });
		connect(m_Viewport, &ViewportWidget::EntityMoved, this, [this](Entity) { MarkDirty(); });
		connect(m_Viewport, &ViewportWidget::AssetDropped, this, &EditorWindow::DropAsset);
		connect(m_Viewport, &ViewportWidget::EntityDropped, this, [this](const QString& kind, Vec3 point) {
			const Entity entity = CreateEntity(kind);
			if (entity != NullEntity)
			{
				// Characters stand on the drop point rather than sinking halfway into it.
				const float lift = kind == "Character" ? 1.0f : 0.0f;
				m_World->Get<TransformComponent>(entity).Position = point + Vec3(0, lift, 0);
			}
		});

		// Drives the world and the viewport at about 60 fps.
		m_Timer = new QTimer(this);
		m_Timer->setTimerType(Qt::PreciseTimer);
		connect(m_Timer, &QTimer::timeout, this, &EditorWindow::Tick);
		m_LastTicks = SDL_GetTicksNS();
		m_Timer->start(16);

		resize(1600, 900);
		const QString startScene = ToQString(Paths::Content(Paths::GetProject().StartScene));
		if (QFileInfo::exists(startScene))
			OpenScene(startScene);
		else
			NewScene();
	}

	EditorWindow::~EditorWindow()
	{
		// Dock teardown can emit signals after the derived window has been destroyed.
		for (QDockWidget* dock : findChildren<QDockWidget*>())
			disconnect(dock, nullptr, this, nullptr);
		m_Timer->stop();
		m_Inspector->StopPreview();
		m_World->EndPlay();
		m_Inspector->SetTarget(nullptr, NullEntity);
		m_World->GetAudio().Attach(nullptr);
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
		m_Hierarchy = new HierarchyPanel();
		connect(m_Hierarchy, &HierarchyPanel::EntityClicked, this, [this](const Guid& id) { Select(m_World->FindEntityByGuid(id)); });
		connect(m_Hierarchy, &HierarchyPanel::SceneClicked, this, [this]() {
			// Made on first use, so scenes without scene scripts stay as they were.
			const bool existed = m_World->GetSceneEntity() != NullEntity;
			const Entity scene = m_World->GetOrCreateSceneEntity();
			if (!existed)
			{
				MarkDirty();
				RefreshHierarchy();
			}
			Select(scene);
		});
		connect(m_Hierarchy, &HierarchyPanel::ParentRequested, this, [this](const Guid& child, const Guid& parent) {
			const Entity parentEntity = parent == Guid() ? NullEntity : m_World->FindEntityByGuid(parent);
			if (!m_World->SetParent(m_World->FindEntityByGuid(child), parentEntity))
			{
				FP_LOG_WARN("An entity cannot be parented to itself or one of its children");
				return;
			}
			MarkDirty();
			RefreshHierarchy();
		});
		connect(m_Hierarchy, &HierarchyPanel::CreateChildRequested, this, [this](const Guid& parent) {
			const Entity entity = m_World->CreateEntity("Child");
			m_World->SetParent(entity, m_World->FindEntityByGuid(parent), false);
			MarkDirty();
			RefreshHierarchy();
			Select(entity);
		});
		PlaceEntitiesPanel* placeEntities = new PlaceEntitiesPanel();
		connect(placeEntities, &PlaceEntitiesPanel::CreateRequested, this, [this](const QString& kind) { CreateEntity(kind); });
		QDockWidget* placeDock = DockTab::Create(this, "Place Entities", EditorIcon::PlaceEntities, placeEntities);
		addDockWidget(Qt::LeftDockWidgetArea, placeDock);

		m_Inspector = new InspectorPanel();
		connect(m_Inspector, &InspectorPanel::ImportAudioRequested, this, [this]() { ImportAudio(true); });
		connect(m_Inspector, &InspectorPanel::OpenScriptRequested, this, &EditorWindow::OpenScript);
		connect(m_Inspector, &InspectorPanel::NewScriptRequested, this, &EditorWindow::NewScriptFor);
		connect(m_Inspector, &InspectorPanel::Modified, this, [this]() {
			MarkDirty();
			RefreshHierarchy();
		});

		// Details and Hierarchy share one narrow panel on the right as tabs, and the bottom panels run the full width.
		QDockWidget* detailsDock = DockTab::Create(this, "Details", EditorIcon::Details, m_Inspector);
		QDockWidget* hierarchyDock = DockTab::Create(this, "Hierarchy", EditorIcon::Hierarchy, m_Hierarchy);
		setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
		setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
		setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
		addDockWidget(Qt::RightDockWidgetArea, detailsDock);
		tabifyDockWidget(detailsDock, hierarchyDock);
		detailsDock->raise();

		m_Console = new QPlainTextEdit();
		m_Console->setReadOnly(true);
		m_Console->setToolTip("Double click a build error to open it");
		m_Console->viewport()->installEventFilter(this);
		m_Console->setMaximumBlockCount(2000);
		QDockWidget* consoleDock = DockTab::Create(this, "Output Log", EditorIcon::OutputLog, m_Console);
		m_ConsoleDock = consoleDock;

		// The content browser, log and profiler share the bottom area.
		m_ContentBrowser = new ContentBrowser();
		QDockWidget* contentDock = DockTab::Create(this, "Content Browser", EditorIcon::ContentBrowser, m_ContentBrowser);
		m_ContentDock = contentDock;
		addDockWidget(Qt::BottomDockWidgetArea, contentDock);
		tabifyDockWidget(contentDock, consoleDock);
		QWidget* profilerBody = new QWidget();
		QVBoxLayout* profilerLayout = new QVBoxLayout(profilerBody);
		m_ProfilerSummary = new QLabel("Press Play to profile game updates");
		m_ProfilerSummary->setToolTip("CPU wall time for World::Update. Rendering and the editor UI are not included.");
		profilerLayout->addWidget(m_ProfilerSummary);
		m_ProfilerTable = new QTableWidget(0, 3);
		m_ProfilerTable->setHorizontalHeaderLabels({ "System", "Last ms", "Avg ms" });
		m_ProfilerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
		m_ProfilerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
		m_ProfilerTable->setAlternatingRowColors(true);
		m_ProfilerTable->verticalHeader()->hide();
		m_ProfilerTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
		m_ProfilerTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
		m_ProfilerTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
		profilerLayout->addWidget(m_ProfilerTable);
		m_ProfilerDock = DockTab::Create(this, "Profiler", EditorIcon::Profiler, profilerBody);
		tabifyDockWidget(contentDock, m_ProfilerDock);
		connect(m_ProfilerDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
			if (visible) RefreshProfiler();
		});
		contentDock->raise();
		// Sized last because adding docks redistributes the space.
		resizeDocks({ placeDock, detailsDock }, { 250, 340 }, Qt::Horizontal);
		resizeDocks({ contentDock }, { 280 }, Qt::Vertical);

		// Grouped dragging lets a tab be pulled out of a shared spot and dropped anywhere,
		// and titles follow whatever layout the user makes.
		setDockOptions(AnimatedDocks | AllowNestedDocks | AllowTabbedDocks | GroupedDragging);
		for (QDockWidget* dock : findChildren<QDockWidget*>())
		{
			connect(dock, &QDockWidget::topLevelChanged, this, &EditorWindow::UpdateDockTitlesLater);
			connect(dock, &QDockWidget::dockLocationChanged, this, &EditorWindow::UpdateDockTitlesLater);
			connect(dock, &QDockWidget::visibilityChanged, this, &EditorWindow::UpdateDockTitlesLater);
		}
		UpdateDockTitles();

		connect(m_ContentBrowser, &ContentBrowser::AssetSelected, this, &EditorWindow::ShowAsset);
		connect(m_ContentBrowser, &ContentBrowser::SaveAllRequested, this, &EditorWindow::SaveAll);
		connect(m_ContentBrowser, &ContentBrowser::NewScriptRequested, this, [this]() {
			const QString name = QInputDialog::getText(this, "New Script", "Class name:");
			if (!name.isEmpty())
				NewScript(name.trimmed());
		});
		connect(m_ContentBrowser, &ContentBrowser::ReferencesChanged, this, [this]() {
			// Rename rewrote paths on disk. Reload the open scene so it sees them, unless that would lose edits.
			if (m_ScenePath.isEmpty() || m_World->IsPlaying())
				return;
			if (m_Dirty)
				FP_LOG_WARN("Assets were renamed. Save and reopen the scene to pick up the new paths.");
			else
				OpenScene(m_ScenePath);
		});
		connect(m_ContentBrowser, &ContentBrowser::AssetActivated, this, [this](const QString& path) {
			const QString kind = ContentBrowser::AssetKind(path);
			if (kind == "Scene")
			{
				if (ConfirmDiscard())
					OpenScene(path);
			}
			else if (kind == "Script" || kind == "Page" || kind == "Web")
			{
				// Pages, styles and page scripts are text, so they open in a code tab too.
				OpenScript(path);
			}
			else
			{
				ShowAsset(path);
			}
		});
	}

	void EditorWindow::BuildMenus()
	{
		// The menu sits inside the window on every OS, like UE, with the project name on the right.
		menuBar()->setNativeMenuBar(false);
		QLabel* project = new QLabel(ToQString(Paths::GetProject().Name));
		project->setStyleSheet("background: #0f0f0f; color: #c0c0c0; border-radius: 4px; padding: 3px 14px; margin: 2px 6px;");
		menuBar()->setCornerWidget(project, Qt::TopRightCorner);

		QMenu* file = menuBar()->addMenu("&File");
		file->addAction("New &Project...", this, [this]() {
			const QString name = QInputDialog::getText(this, "New Project", "Project name:");
			if (!name.isEmpty())
				NewProject(name.trimmed());
		});
		file->addAction("Open P&roject...", this, [this]() {
			const QString projects = QFileInfo(ToQString(Paths::GetProjectDir())).absolutePath();
			const QString path = QFileDialog::getOpenFileName(this, "Open Project", projects, "Projects (*.fpproject)");
			if (!path.isEmpty())
				OpenProject(QFileInfo(path).absolutePath());
		});
		file->addSeparator();
		file->addAction("&New Scene", QKeySequence::New, this, &EditorWindow::NewScene);
		file->addAction("&Open Scene...", QKeySequence::Open, this, &EditorWindow::OpenSceneDialog);
		file->addAction("&Save", QKeySequence::Save, this, &EditorWindow::SaveCurrent);
		file->addAction("Save Scene &As...", QKeySequence::SaveAs, this, &EditorWindow::SaveSceneAs);
		m_ImportAudioAction = file->addAction("&Import Audio...", this, [this]() { ImportAudio(false); });
		file->addSeparator();
		file->addAction("E&xit", QKeySequence::Quit, this, &QWidget::close);

		QMenu* edit = menuBar()->addMenu("&Edit");
		m_UndoAction = edit->addAction("&Undo", QKeySequence::Undo, this, &EditorWindow::Undo);
		m_RedoAction = edit->addAction("&Redo", this, &EditorWindow::Redo);
		// Ctrl+Shift+Z everywhere, plus the platform key (Ctrl+Y on Windows). Mac already uses Cmd+Shift+Z.
		QList<QKeySequence> redoKeys = { QKeySequence("Ctrl+Shift+Z") };
		for (const QKeySequence& key : QKeySequence::keyBindings(QKeySequence::Redo))
		{
			if (!redoKeys.contains(key))
				redoKeys.append(key);
		}
		m_RedoAction->setShortcuts(redoKeys);

		QMenu* entity = menuBar()->addMenu("&Entity");
		entity->addAction("Create &Empty", QKeySequence("Ctrl+Shift+N"), this, [this]() { CreateEntity("Empty Entity"); });
		entity->addAction("Create &Sprite", QKeySequence("Ctrl+Shift+A"), this, [this]() { CreateEntity("Sprite"); });
		QMenu* meshes = entity->addMenu("Create &Mesh");
		for (const char* shape : Primitives::GetNames())
		{
			const QString name = shape;
			meshes->addAction(name, this, [this, name]() { CreateEntity(name); });
		}
		entity->addAction("Create &Point Light", this, [this]() { CreateEntity("Point Light"); });
		entity->addAction("Create &Spot Light", this, [this]() { CreateEntity("Spot Light"); });
		entity->addAction("Create &Character", this, [this]() { CreateEntity("Character"); });
		entity->addAction("Create Particle E&mitter", this, [this]() { CreateEntity("Particle Emitter"); });
		entity->addAction("Create &Directional Light", this, [this]() { CreateEntity("Directional Light"); });
		// Text fields take these keys first, so they still copy, paste and erase text there.
		entity->addAction("&Copy", QKeySequence::Copy, this, &EditorWindow::CopySelected);
		entity->addAction("&Paste", QKeySequence::Paste, this, &EditorWindow::Paste);
		entity->addAction("&Duplicate", QKeySequence("Ctrl+D"), this, &EditorWindow::DuplicateSelected);
		// Explicit keys, since the platform Delete sequence on macOS also holds Ctrl+D.
		QAction* deleteAction = entity->addAction("De&lete", this, &EditorWindow::DeleteSelected);
		deleteAction->setShortcuts({ QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace) });

		// Closed panels come back from here, and Reset Layout undoes any docking.
		QMenu* window = menuBar()->addMenu("&Window");
		for (QDockWidget* dock : findChildren<QDockWidget*>())
		{
			QAction* toggle = dock->toggleViewAction();
			toggle->setIcon(dock->windowIcon());
			window->addAction(toggle);
		}
		window->addSeparator();
		window->addAction("Nanite / Meshlet Debugger", this, [this]() { ShowMeshletDebugger(*m_Viewport, this); });
		window->addAction("Audio Debugger", this, &EditorWindow::ShowAudioDebugger);
		window->addAction("&Reset Layout", this, &EditorWindow::ResetLayout);
		// Remembered once the window has its real size so the reset matches what opened.
		QTimer::singleShot(0, this, [this]() { m_DefaultLayout = saveState(); });

		m_ScriptBuilder = new ScriptBuilder(this);
		connect(m_ScriptBuilder, &ScriptBuilder::Finished, this, &EditorWindow::OnScriptsBuilt);
		connect(m_ScriptBuilder, &ScriptBuilder::ProjectBuilt, this, &EditorWindow::OnProjectBuilt);
		QMenu* scripts = menuBar()->addMenu("&Scripts");
		scripts->addAction("&New Script...", this, [this]() {
			const QString name = QInputDialog::getText(this, "New Script", "Class name:");
			if (!name.isEmpty())
				NewScript(name.trimmed());
		});
		scripts->addSeparator();
		scripts->addAction("&Reload Scripts", QKeySequence("Ctrl+R"), m_ScriptBuilder, &ScriptBuilder::Build);
		scripts->addAction("Re&build Scripts (Clean)", m_ScriptBuilder, &ScriptBuilder::Rebuild)
			->setToolTip("Deletes everything built from the scripts and builds them again from scratch");
		scripts->addAction("Rebuild &Editor and Restart", this, &EditorWindow::RebuildEditorAndRestart)
			->setToolTip("Builds this editor from the current engine code, then reopens it on this scene");
		scripts->setToolTipsVisible(true);
		QAction* autoReload = scripts->addAction("&Auto Reload");
		autoReload->setCheckable(true);
		autoReload->setChecked(m_AutoReloadScripts);
		connect(autoReload, &QAction::toggled, this, [this](bool on) { m_AutoReloadScripts = on; });
		WatchScripts();
		// Scripts built from other engine code were refused at startup. Building them again
		// fixes it when they are the older side, and OnScriptsBuilt handles the other case.
		if (ScriptRuntime::Get().HadAbiMismatch())
			m_ScriptBuilder->Build();

		QToolBar* toolbar = addToolBar("Main");
		toolbar->setObjectName("MainToolbar");
		toolbar->setMovable(false);
		toolbar->setIconSize({ 18, 18 });
		QAction* save = toolbar->addAction(EditorIcons::Get(EditorIcon::Save), "Save", this, &EditorWindow::SaveCurrent);
		save->setToolTip("Save (Ctrl+S)");
		toolbar->addSeparator();

		// Quick create, the same list as the Entity menu.
		QToolButton* create = new QToolButton();
		create->setIcon(EditorIcons::Get(EditorIcon::Add, EditorStyle::Play));
		create->setText("Create");
		create->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		create->setPopupMode(QToolButton::InstantPopup);
		create->setMenu(entity);
		create->setToolTip("Create an entity");
		toolbar->addWidget(create);
		toolbar->addSeparator();

		m_PlayAction = toolbar->addAction(EditorIcons::Get(EditorIcon::Play), "Play", this, &EditorWindow::Play);
		m_PlayAction->setShortcut(QKeySequence("Ctrl+P"));
		m_PlayAction->setToolTip("Play (Ctrl+P)");
		m_PauseAction = toolbar->addAction(EditorIcons::Get(EditorIcon::Pause), "Pause", this, [this]() {
			SetPaused(!m_World->IsPaused());
		});
		m_PauseAction->setShortcut(QKeySequence("Ctrl+Shift+P"));
		m_PauseAction->setEnabled(false);
		m_PauseAction->setToolTip("Pause (Ctrl+Shift+P)");
		m_StepAction = toolbar->addAction(EditorIcons::Get(EditorIcon::Step), "Step", this, &EditorWindow::StepFrame);
		m_StepAction->setShortcut(QKeySequence("F10"));
		m_StepAction->setEnabled(false);
		m_StepAction->setToolTip("Advance one game frame while paused (F10)");
		m_StopAction = toolbar->addAction(EditorIcons::Get(EditorIcon::Stop, QColor(0xe0, 0x50, 0x50)), "Stop", this, &EditorWindow::Stop);
		m_StopAction->setEnabled(false);
		m_StopAction->setToolTip("Stop");

		QWidget* spacer = new QWidget();
		spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		toolbar->addWidget(spacer);

		QToolButton* settings = new QToolButton();
		settings->setIcon(EditorIcons::Get(EditorIcon::Settings));
		settings->setText("Settings");
		settings->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		settings->setPopupMode(QToolButton::InstantPopup);
		QMenu* settingsMenu = new QMenu(settings);
		settingsMenu->addAction(autoReload);
		settingsMenu->addAction("Collect Garbage", this, []() {
			const GarbageCollectionStats stats = GarbageCollector::Get().Collect();
			FP_LOG("GC freed %d objects, %d alive, %.3f ms", stats.NumFreed, stats.NumAlive, stats.Milliseconds);
		});
		settings->setMenu(settingsMenu);
		toolbar->addWidget(settings);

		// The projection and view mode live on the viewport toolbar. Ctrl+3 still flips the projection.
		m_View3DAction = new QAction(this);
		m_View3DAction->setCheckable(true);
		m_View3DAction->setShortcut(QKeySequence("Ctrl+3"));
		addAction(m_View3DAction);
		connect(m_View3DAction, &QAction::toggled, this, [this](bool on) {
			m_Viewport->Set3D(on);
			m_ViewportToolbar->Set3D(on);
		});
		connect(m_ViewportToolbar, &ViewportToolbar::ProjectionChanged, m_View3DAction, &QAction::setChecked);
		// Opens in perspective, which is what the projection pill shows before anything is picked.
		m_View3DAction->setChecked(true);
		connect(m_ViewportToolbar, &ViewportToolbar::ViewModeChanged, this, &EditorWindow::SetViewMode);
		connect(m_ViewportToolbar, &ViewportToolbar::SnapChanged, this, [this](bool on, float size) { m_Viewport->SetSnap(on, size); });
		connect(m_ViewportToolbar, &ViewportToolbar::GizmoModeChanged, m_Viewport, &ViewportWidget::SetGizmoMode);
		connect(m_ViewportToolbar, &ViewportToolbar::GizmoSpaceChanged, m_Viewport, &ViewportWidget::SetGizmoLocal);
		connect(m_Viewport, &ViewportWidget::GizmoModeChanged, m_ViewportToolbar, &ViewportToolbar::SetGizmoMode);

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

		BuildStatusBar();
	}

	void EditorWindow::ResetLayout()
	{
		for (QDockWidget* dock : findChildren<QDockWidget*>())
			dock->setFloating(false);
		restoreState(m_DefaultLayout);
		UpdateDockTitles();
	}

	void EditorWindow::UpdateDockTitlesLater()
	{
		// Layout changes arrive in bursts while dragging, so update once they settle.
		if (m_DockTitlesPending)
			return;
		m_DockTitlesPending = true;
		QTimer::singleShot(0, this, &EditorWindow::UpdateDockTitles);
	}

	void EditorWindow::UpdateDockTitles()
	{
		m_DockTitlesPending = false;
		for (QDockWidget* dock : findChildren<QDockWidget*>())
		{
			// A dock alone needs its own title to be dragged. Docks sharing a spot use the tab bar.
			if (DockTab* tab = qobject_cast<DockTab*>(dock->titleBarWidget()))
				tab->SetCompact(!dock->isFloating() && !tabifiedDockWidgets(dock).isEmpty());
		}
	}

	void EditorWindow::BuildStatusBar()
	{
		// UE style bottom bar: buttons that bring panels forward, then a console command box.
		auto addButton = [this](EditorIcon icon, const QString& text, QDockWidget* dock) {
			QToolButton* button = new QToolButton();
			button->setIcon(EditorIcons::Get(icon));
			button->setText(text);
			button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
			button->setAutoRaise(true);
			connect(button, &QToolButton::clicked, this, [dock]() {
				dock->show();
				dock->raise();
			});
			statusBar()->addWidget(button);
		};
		addButton(EditorIcon::ContentBrowser, "Content Browser", m_ContentDock);
		addButton(EditorIcon::OutputLog, "Output Log", m_ConsoleDock);
		addButton(EditorIcon::Profiler, "Profiler", m_ProfilerDock);

		QLabel* cmd = new QLabel();
		cmd->setPixmap(EditorIcons::Get(EditorIcon::Console).pixmap(16, 16));
		cmd->setToolTip("Console command");
		statusBar()->addWidget(cmd);
		QLineEdit* command = new QLineEdit();
		command->setPlaceholderText("Enter Console Command (help lists them)");
		command->setMinimumWidth(320);
		connect(command, &QLineEdit::returnPressed, this, [this, command]() {
			RunCommand(command->text().trimmed());
			command->clear();
		});
		statusBar()->addWidget(command);

		QLabel* hint = new QLabel("Hold right mouse and WASD to fly, Alt drag orbits, W E R move rotate scale, F focuses");
		hint->setStyleSheet("color: #7a7a7a; padding-right: 8px;");
		statusBar()->addPermanentWidget(hint);
		QPushButton* audioSettings = new QPushButton("Audio...");
		audioSettings->setToolTip("Output diagnostics, audio mixer and proximity voice chat");
		connect(audioSettings, &QPushButton::clicked, this, &EditorWindow::ShowAudioSettings);
		statusBar()->addPermanentWidget(audioSettings);
		m_AudioStatus = new QLabel();
		m_AudioStatus->setAccessibleName("Audio output status");
		statusBar()->addPermanentWidget(m_AudioStatus);
		QPushButton* retry = new QPushButton("Retry Audio");
		connect(retry, &QPushButton::clicked, this, [this]() {
			const AudioResult result = m_Audio.Initialize();
			if (!result)
				QMessageBox::warning(this, "Audio output", ToQString(result.Message));
		});
		statusBar()->addPermanentWidget(retry);
	}

	void EditorWindow::RunCommand(const QString& line)
	{
		if (line.isEmpty())
			return;
		FP_LOG("> %s", line.toUtf8().constData());
		const QString name = line.section(' ', 0, 0).toLower();
		const QString argument = line.section(' ', 1).trimmed();
		if (name == "help")
			FP_LOG("Commands: play, pause, resume, step, stop, save, undo, redo, gc, 2d, 3d, viewmode lit|unlit|wireframe, select <name>, create <kind>");
		else if (name == "play")
			Play();
		else if (name == "pause")
			SetPaused(true);
		else if (name == "resume")
			SetPaused(false);
		else if (name == "step")
			StepFrame();
		else if (name == "stop")
			Stop();
		else if (name == "save")
			SaveCurrent();
		else if (name == "undo")
			Undo();
		else if (name == "redo")
			Redo();
		else if (name == "gc")
		{
			const GarbageCollectionStats stats = GarbageCollector::Get().Collect();
			FP_LOG("GC freed %d objects, %d alive, %.3f ms", stats.NumFreed, stats.NumAlive, stats.Milliseconds);
		}
		else if (name == "2d" || name == "3d")
			Set3DView(name == "3d");
		else if (name == "viewmode")
		{
			const QString mode = argument.toLower();
			SetViewMode(mode == "unlit" ? ViewMode::Unlit : mode == "wireframe" ? ViewMode::Wireframe : ViewMode::Lit);
		}
		else if (name == "select")
		{
			const Entity entity = m_World->FindEntityByName(ToString(argument).CStr());
			if (entity == NullEntity)
				FP_LOG_WARN("No entity named %s", argument.toUtf8().constData());
			Select(entity);
		}
		else if (name == "create")
			CreateEntity(argument);
		else
			FP_LOG_WARN("Unknown command %s. Type help for the list.", name.toUtf8().constData());
	}

	void EditorWindow::Set3DView(bool enabled)
	{
		m_View3DAction->setChecked(enabled);
	}

	void EditorWindow::SetViewMode(ViewMode mode)
	{
		m_Viewport->SetViewMode(mode);
		m_ViewportToolbar->SetViewMode(mode);
	}

	void EditorWindow::DropAsset(const QString& path, Vec3 point, Entity target)
	{
		// Scripts live in Source/Scripts rather than Content, so they come before the check below.
		if (ContentBrowser::AssetKind(path) == "Script")
		{
			if (!m_World->IsValid(target))
			{
				FP_LOG_WARN("Drop a script onto an entity to add it");
				return;
			}
			// The class written in that file, falling back to the file name like New Script makes.
			String script = ToString(QFileInfo(path).completeBaseName());
			const QString dropped = QFileInfo(path).canonicalFilePath();
			for (const ScriptType* type : ScriptRuntime::Get().GetTypes())
			{
				if (type->SourceFile && QFileInfo(ScriptBuilder::ResolveSourcePath(QString::fromUtf8(type->SourceFile))).canonicalFilePath() == dropped)
					script = type->Name;
			}
			AttachScript(target, script);
			return;
		}
		const QString relative = QDir(ToQString(Paths::GetContentDir())).relativeFilePath(path);
		if (relative.startsWith(".."))
		{
			FP_LOG_WARN("Only assets inside Content can be dropped");
			return;
		}
		const QString kind = ContentBrowser::AssetKind(path);
		const String name = ToString(QFileInfo(path).completeBaseName());

		Entity changed = NullEntity;
		if (kind == "Texture")
		{
			changed = m_World->CreateEntity(name);
			m_World->Get<TransformComponent>(changed).Position = point;
			m_World->Add<SpriteComponent>(changed).Texture = ToString(relative);
		}
		else if (kind == "Mesh")
		{
			changed = m_World->CreateEntity(name);
			m_World->Get<TransformComponent>(changed).Position = point;
			// A glTF with a skin becomes a skeletal mesh that plays its first clip.
			const SkeletalModel* model = IsGltfPath(ToString(relative).View()) ? SkeletalModelLibrary::Get().Load(ToString(relative)) : nullptr;
			if (model && model->IsSkinned())
			{
				m_World->Add<SkeletalMeshComponent>(changed).Mesh = ToString(relative);
				m_World->Add<AnimatorComponent>(changed).Clip = model->Clips.IsEmpty() ? String() : model->Clips[0].Name;
			}
			else
			{
				m_World->Add<MeshComponent>(changed).Mesh = ToString(relative);
			}
		}
		else if (kind == "Page")
		{
			// Onto an entity, or onto the primary camera so it shows over the game.
			changed = target;
			if (!m_World->IsValid(changed))
			{
				for (auto [entity, camera] : m_World->View<CameraComponent>().each())
				{
					if (camera.Primary)
						changed = entity;
				}
			}
			if (!m_World->IsValid(changed))
			{
				FP_LOG_WARN("Drop a page onto an entity, or add a camera to put it on");
				return;
			}
			m_World->Add<UIComponent>(changed).Page = ToString(relative);
			FP_LOG("Added UI page %s to %s. It shows while playing.", ToString(relative).CStr(), m_World->GetEntityName(changed).CStr());
		}
		else if (kind == "Material")
		{
			if (!m_World->IsValid(target))
			{
				FP_LOG_WARN("Drop a material onto a sprite or mesh");
				return;
			}
			changed = target;
			m_World->Add<MaterialComponent>(changed).Material = ToString(relative);
		}
		else if (kind == "Scene")
		{
			if (ConfirmDiscard())
				OpenScene(path);
			return;
		}
		else
		{
			FP_LOG_WARN("%s files cannot be dropped into the scene", kind.toUtf8().constData());
			return;
		}

		MarkDirty();
		RefreshHierarchy();
		m_Selected = NullEntity; // force the inspector to rebuild with the new component
		Select(changed);
	}

	void EditorWindow::DiscardUnsavedChanges()
	{
		m_Dirty = false;
		for (int i = 0; i < m_CentralTabs->count(); ++i)
		{
			if (CodeEditor* editor = qobject_cast<CodeEditor*>(m_CentralTabs->widget(i)))
				editor->document()->setModified(false);
		}
	}

	void EditorWindow::ShowAsset(const QString& path)
	{
		Select(NullEntity);
		m_Inspector->SetAsset(path);
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

	bool EditorWindow::OpenBuildMessage(const QString& line)
	{
		// Clang and GCC write path:line:column. MSVC writes path(line,column).
		static const QRegularExpression pattern(R"(((?:[A-Za-z]:)?[^\s:(]+\.(?:cpp|h|hpp|inl))(?::(\d+)(?::(\d+))?|\((\d+)(?:,(\d+))?\)))");
		const QRegularExpressionMatch match = pattern.match(line);
		if (!match.hasMatch())
			return false;
		const QString path = ScriptBuilder::ResolveSourcePath(match.captured(1));
		if (!QFileInfo::exists(path))
			return false;
		const int lineNumber = (match.captured(2).isEmpty() ? match.captured(4) : match.captured(2)).toInt();
		const int column = (match.captured(2).isEmpty() ? match.captured(5) : match.captured(3)).toInt();
		OpenScript(path);
		if (CodeEditor* editor = FindScriptTab(path))
			editor->GoToLine(std::max(1, lineNumber), std::max(1, column));
		return true;
	}

	bool EditorWindow::eventFilter(QObject* watched, QEvent* event)
	{
		if (watched == m_Console->viewport() && event->type() == QEvent::MouseButtonDblClick)
		{
			const QPoint position = static_cast<QMouseEvent*>(event)->position().toPoint();
			if (OpenBuildMessage(m_Console->cursorForPosition(position).block().text()))
				return true;
		}
		return QMainWindow::eventFilter(watched, event);
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

	void EditorWindow::SaveAll()
	{
		for (int i = 0; i < m_CentralTabs->count(); ++i)
		{
			CodeEditor* editor = qobject_cast<CodeEditor*>(m_CentralTabs->widget(i));
			if (editor && editor->IsDirty() && !editor->Save())
				FP_LOG_ERROR("Could not save %s", editor->GetPath().toUtf8().constData());
		}
		// Saving the scene also writes every changed material.
		if (m_Dirty || m_ScenePath.isEmpty())
			SaveScene();
		else
			MaterialLibrary::Get().SaveDirty();
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

	void EditorWindow::OpenProject(const QString& dir)
	{
		const QString path = QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
		if (path == QDir::cleanPath(ToQString(Paths::GetProjectDir())))
			return;
		Project project;
		if (!project.Load(path.toUtf8().constData()))
		{
			QMessageBox::warning(this, "Open Project", "There is no project in " + path);
			return;
		}
		if (m_ScriptBuilder->IsBuilding())
		{
			QMessageBox::information(this, "Open Project", "Wait for the current build to finish first.");
			return;
		}
		Stop();
		m_ConsoleDock->raise();
		m_ScriptBuilder->BuildProject(path);
	}

	bool EditorWindow::NewProject(const QString& name)
	{
		// Projects live side by side, usually in Projects.
		const QString parent = QFileInfo(ToQString(Paths::GetProjectDir())).absolutePath();
		Project project;
		String error;
		if (!Project::Create(parent.toUtf8().constData(), name.toUtf8().constData(), project, error))
		{
			QMessageBox::warning(this, "New Project", ToQString(error));
			return false;
		}
		FP_LOG("Created project %s", project.Dir.CStr());
		OpenProject(ToQString(project.Dir));
		return true;
	}

	void EditorWindow::OnProjectBuilt(bool success, const QString& dir)
	{
		if (!success)
		{
			QMessageBox::warning(this, "Open Project", "The project did not build. The Output Log has the errors.");
			return;
		}
		// Restarting this project's editor reopens the scene it was on.
		QStringList arguments;
		const QDir content(ToQString(Paths::GetContentDir()));
		if (QDir(dir) == QDir(ToQString(Paths::GetProjectDir())) && !m_ScenePath.isEmpty() && !content.relativeFilePath(m_ScenePath).startsWith(".."))
			arguments = { "--scene", content.relativeFilePath(m_ScenePath) };
		// Closing asks about unsaved work first, and the other editor only starts once this one is gone.
		if (!close())
			return;
		if (!QProcess::startDetached(ScriptBuilder::EditorPath(dir), arguments, dir))
			FP_LOG_ERROR("Could not start %s", ScriptBuilder::EditorPath(dir).toUtf8().constData());
	}

	void EditorWindow::OnScriptsBuilt(bool success)
	{
		if (!success)
		{
			// Build errors live in the console, so bring it forward.
			m_ConsoleDock->raise();
			return;
		}
		// Running scripts move to the new code with their field values.
		if (ScriptRuntime::Get().Reload())
			FP_LOG("Scripts reloaded%s", m_World->IsPlaying() ? " while playing" : "");
		else if (ScriptRuntime::Get().HadAbiMismatch())
		{
			// The scripts were just built from the current code, so this editor is the old side.
			FP_LOG_WARN("This editor is older than the engine code the scripts were built from. Use Scripts > Rebuild Editor and Restart.");
			const auto answer = QMessageBox::question(this, "Editor out of date",
				"The scripts were built from newer engine code than this editor, so they cannot load.\n\n"
				"Rebuild the editor and restart it on this scene? This can take a few minutes.",
				QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
			if (answer == QMessageBox::Yes)
				RebuildEditorAndRestart();
			return;
		}
		AttachPendingScripts();
	}

	void EditorWindow::RebuildEditorAndRestart()
	{
		if (m_World->IsPlaying())
			Stop();
		FP_LOG("Rebuilding the editor. It restarts on this scene when the build is done.");
		m_ScriptBuilder->BuildProject(ToQString(Paths::GetProjectDir()));
	}

	void EditorWindow::NewScriptFor(const Guid& entity)
	{
		const QString name = QInputDialog::getText(this, "New Script", "Class name:").trimmed();
		if (name.isEmpty() || !NewScript(name))
			return;
		// It attaches itself once it compiles, so the user can keep editing in the meantime.
		m_PendingAttach.append({ entity, ToString(name) });
		FP_LOG("%s will be added to the entity once it compiles", name.toUtf8().constData());
		m_ScriptBuilder->Build();
	}

	void EditorWindow::AttachPendingScripts()
	{
		for (int i = m_PendingAttach.size() - 1; i >= 0; --i)
		{
			const PendingAttach pending = m_PendingAttach[i];
			const Entity entity = m_World->FindEntityByGuid(pending.Entity);
			if (!ScriptRuntime::Get().FindType(pending.Script.View()) && entity != NullEntity)
				continue; // not built yet, try again after the next build
			m_PendingAttach.removeAt(i);
			if (entity != NullEntity)
				AttachScript(entity, pending.Script);
		}
	}

	void EditorWindow::AttachScript(Entity entity, const String& name)
	{
		ScriptComponent* scripts = m_World->TryGet<ScriptComponent>(entity);
		if (!scripts)
			scripts = &m_World->Add<ScriptComponent>(entity);
		scripts->Scripts.Emplace(name);
		MarkDirty();
		if (entity == m_Selected)
			m_Inspector->SetTarget(m_World.Get(), m_Selected);
		FP_LOG("Added %s to %s", name.CStr(), m_World->GetEntityName(entity).CStr());
	}

	void EditorWindow::Tick()
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			m_Audio.ProcessEvent(event);
			// Qt brings the keyboard and mouse. Gamepads come through SDL.
			switch (event.type)
			{
				case SDL_EVENT_GAMEPAD_ADDED:
				case SDL_EVENT_GAMEPAD_REMOVED:
				case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
				case SDL_EVENT_GAMEPAD_BUTTON_UP:
				case SDL_EVENT_GAMEPAD_AXIS_MOTION:
					Input::ProcessEvent(event);
					break;
				default:
					break;
			}
		}
		m_Audio.Update();
		m_AudioStatus->setText(m_Audio.IsReady() ? "Audio ready" : "Audio unavailable");
		m_AudioStatus->setToolTip(ToQString(m_Audio.GetStatusText()));
		const uint64 now = SDL_GetTicksNS();
		float deltaTime = static_cast<float>(now - m_LastTicks) / 1e9f;
		m_LastTicks = now;
		if (deltaTime > 0.1f)
			deltaTime = 0.1f;

		m_Viewport->UpdateCursor();
		// Emitters run while editing too, so effects can be tuned without playing.
		if (!m_World->IsPlaying())
			Particles::Simulate(*m_World.Get(), deltaTime, true);
		Input::SetDeltaTime(deltaTime);
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
		if (m_Frame % 12 == 0)
			RefreshProfiler();

		// No point drawing the scene while a script tab covers it.
		if (m_CentralTabs->currentWidget() == m_Viewport)
			m_Viewport->RenderFrame();
		if (++m_Frame % 120 == 0)
			GarbageCollector::Get().Collect();
	}

	void EditorWindow::RefreshProfiler()
	{
		if (!m_ProfilerDock->isVisible())
			return;
		if (!m_World->IsPlaying())
		{
			m_ProfilerSummary->setText("Press Play to profile game updates");
			m_ProfilerTable->setRowCount(0);
			return;
		}
		m_ProfilerSummary->setText(QString("Game update CPU: %1 ms last | %2 ms avg | frame %3%4")
			.arg(m_World->GetLastUpdateMilliseconds(), 0, 'f', 3)
			.arg(m_World->GetAverageUpdateMilliseconds(), 0, 'f', 3)
			.arg(static_cast<qulonglong>(m_World->GetProfileFrame()))
			.arg(m_World->IsPaused() ? " [Paused]" : ""));
		const auto& timings = m_World->GetSystemTimings();
		m_ProfilerTable->setRowCount(timings.Num());
		auto put = [this](int row, int column, const QString& value) {
			if (QTableWidgetItem* item = m_ProfilerTable->item(row, column))
			{
				if (item->text() != value) item->setText(value);
			}
			else m_ProfilerTable->setItem(row, column, new QTableWidgetItem(value));
		};
		for (int row = 0; row < timings.Num(); ++row)
		{
			put(row, 0, ToQString(timings[row].Name));
			put(row, 1, QString::number(timings[row].LastMilliseconds, 'f', 3));
			put(row, 2, QString::number(timings[row].AverageMilliseconds, 'f', 3));
		}
	}

	void EditorWindow::ShowProfiler()
	{
		m_ProfilerDock->show();
		m_ProfilerDock->raise();
		RefreshProfiler();
	}

	void EditorWindow::Select(Entity entity)
	{
		if (!m_World->IsValid(entity))
			entity = NullEntity;
		const bool changed = entity != m_Selected;
		m_Selected = entity;
		m_SelectedGuid = entity != NullEntity ? m_World->Get<IdComponent>(entity).Id : Guid();
		m_Viewport->SetSelected(entity);
		// Undo brings back what was selected before the edit, so only track it between edits.
		if (!m_EditPending)
			m_History->SetSelection(m_SelectedGuid);

		// Keep the hierarchy row in sync without feeding back into Select.
		m_Hierarchy->SetSelected(m_SelectedGuid);

		if (changed)
			m_Inspector->SetTarget(m_World.Get(), entity);
	}

	void EditorWindow::RefreshHierarchy()
	{
		m_Hierarchy->Rebuild(*m_World.Get(), m_ScenePath.isEmpty() ? "Untitled" : QFileInfo(m_ScenePath).completeBaseName());
		m_Hierarchy->SetSelected(m_SelectedGuid);
		m_HierarchyCount = m_World->GetEntities().Num();
	}

	void EditorWindow::MarkDirty()
	{
		if (m_World->IsPlaying())
			return;
		m_EditPending = true;
		m_HistoryTimer->start();
		UpdateUndoActions();
		if (m_Dirty)
			return;
		m_Dirty = true;
		UpdateTitle();
	}

	void EditorWindow::CommitHistory()
	{
		m_HistoryTimer->stop();
		if (!m_EditPending)
			return;
		m_EditPending = false;
		m_History->Commit(m_SelectedGuid);
		UpdateUndoActions();
	}

	void EditorWindow::ResetHistory()
	{
		m_HistoryTimer->stop();
		m_EditPending = false;
		m_History->Reset(m_SelectedGuid);
		UpdateUndoActions();
	}

	void EditorWindow::Undo()
	{
		if (CodeEditor* editor = qobject_cast<CodeEditor*>(m_CentralTabs->currentWidget()))
		{
			editor->undo();
			return;
		}
		if (m_World->IsPlaying())
			return;
		m_Inspector->StopPreview();
		CommitHistory();
		if (m_History->Undo())
			RestoreHistoryStep();
	}

	void EditorWindow::Redo()
	{
		if (CodeEditor* editor = qobject_cast<CodeEditor*>(m_CentralTabs->currentWidget()))
		{
			editor->redo();
			return;
		}
		if (m_World->IsPlaying())
			return;
		m_Inspector->StopPreview();
		CommitHistory();
		if (m_History->Redo())
			RestoreHistoryStep();
	}

	void EditorWindow::RestoreHistoryStep()
	{
		// The world was reloaded, so every entity handle is new.
		m_Selected = NullEntity;
		RefreshHierarchy();
		Select(m_World->FindEntityByGuid(m_History->GetSelection()));
		m_Inspector->SetTarget(m_World.Get(), m_Selected);
		// Not an edit to record, but the scene no longer matches the file.
		m_Dirty = true;
		UpdateTitle();
		UpdateUndoActions();
	}

	void EditorWindow::UpdateUndoActions()
	{
		if (qobject_cast<CodeEditor*>(m_CentralTabs->currentWidget()))
		{
			m_UndoAction->setEnabled(true);
			m_RedoAction->setEnabled(true);
			return;
		}
		m_UndoAction->setEnabled(m_EditPending || m_History->CanUndo());
		m_RedoAction->setEnabled(!m_EditPending && m_History->CanRedo());
	}

	void EditorWindow::UpdateTitle()
	{
		const QString name = m_ScenePath.isEmpty() ? "Untitled" : QFileInfo(m_ScenePath).fileName();
		setWindowTitle(QString("%1%2 - %3 - FirstPrinciples%4").arg(name, m_Dirty ? "*" : "", ToQString(Paths::GetProject().Name),
			m_World->IsPaused() ? " [Paused]" : m_World->IsPlaying() ? " [Playing]" : ""));
		// The viewport tab is the open scene, like UE's level tab.
		m_CentralTabs->setTabText(m_CentralTabs->indexOf(m_Viewport), QFileInfo(name).completeBaseName() + (m_Dirty ? "*" : ""));
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
		ResetHistory();
		UpdateTitle();
	}

	void EditorWindow::OpenScene(const QString& path)
	{
		StrongObjectPtr<World> candidate(NewObject<World>(nullptr, "SceneCandidate"));
		if (!SceneSerializer::LoadFromFile(*candidate.Get(), ToString(path).CStr()))
			return;
		Stop();
		m_World->ReplaceScene(*candidate.Get());
		if (Renderer* renderer = m_Viewport->GetRenderer())
			renderer->PreloadTextures(*m_World.Get());
		m_ScenePath = path;
		m_Dirty = false;
		FP_LOG("Opened %s", ToString(path).CStr());
		m_Selected = NullEntity;
		Select(NullEntity);
		m_Inspector->SetTarget(m_World.Get(), NullEntity);
		RefreshHierarchy();
		ResetHistory();
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

	Entity EditorWindow::CreateEntity(const QString& kind)
	{
		const String name = ToString(kind);
		Entity entity = NullEntity;
		if (kind == "Empty Entity")
		{
			entity = m_World->CreateEntity("Entity");
		}
		else if (kind == "Sprite")
		{
			entity = m_World->CreateEntity(name);
			m_World->Add<SpriteComponent>(entity);
		}
		else if (kind == "Character")
		{
			// A pill that plays straight away: it walks, jumps and looks around, and the game sees
			// it through a camera that follows behind.
			entity = m_World->CreateEntity(name);
			m_World->Get<TransformComponent>(entity).Position.Y = 1;
			m_World->Add<MeshComponent>(entity).Mesh = "Capsule";
			m_World->Add<CharacterControllerComponent>(entity);
			const String cameraName = m_World->Add<PlayerInputComponent>(entity).Camera;

			const Entity camera = m_World->CreateEntity(cameraName);
			TransformComponent& view = m_World->Get<TransformComponent>(camera);
			view.Position = { 0, 1.5f, 5 };
			view.Rotation = { -12, 0, 0 };
			CameraComponent& lens = m_World->Add<CameraComponent>(camera);
			lens.Perspective = true;
			m_World->SetParent(camera, entity, false);
			m_World->SetActiveCamera(camera);
		}
		else if (kind == "Particle Emitter")
		{
			entity = m_World->CreateEntity(name);
			m_World->Add<ParticleEmitterComponent>(entity);
		}
		else if (kind == "Camera")
		{
			entity = m_World->CreateEntity(name);
			m_World->Add<CameraComponent>(entity);
		}
		else if (kind == "Point Light")
		{
			entity = m_World->CreateEntity(name);
			m_World->Get<TransformComponent>(entity).Position.Z = 1.5f; // above the sprites so it lights their faces
			m_World->Add<PointLightComponent>(entity);
		}
		else if (kind == "Spot Light")
		{
			entity = m_World->CreateEntity(name);
			m_World->Get<TransformComponent>(entity).Rotation = { -40, 0, 0 };
			m_World->Add<SpotLightComponent>(entity);
		}
		else if (kind == "Directional Light")
		{
			entity = m_World->CreateEntity(name);
			m_World->Get<TransformComponent>(entity).Rotation = { -40, 30, 0 };
			m_World->Add<DirectionalLightComponent>(entity);
		}
		else if (kind == "Ambient Light")
		{
			entity = m_World->CreateEntity(name);
			m_World->Add<AmbientLightComponent>(entity);
		}
		else if (Primitives::GetNames().FindIf([&](const char* shape) { return kind == shape; }) != INDEX_NONE)
		{
			entity = m_World->CreateEntity(name);
			m_World->Add<MeshComponent>(entity).Mesh = name;
		}
		else
		{
			FP_LOG_WARN("Unknown entity kind %s", name.CStr());
			return NullEntity;
		}
		MarkDirty();
		RefreshHierarchy();
		Select(entity);
		return entity;
	}

	namespace
	{
		// First line of entities on the clipboard, so pasting ignores any other text.
		constexpr const char* ClipboardHeader = "# FirstPrinciples entities\n";
	}

	void EditorWindow::CopySelected()
	{
		if (!m_World->IsValid(m_Selected))
			return;
		const String text = SceneSerializer::SaveEntityTreeToString(*m_World.Get(), m_Selected);
		QApplication::clipboard()->setText(QString(ClipboardHeader) + ToQString(text));
	}

	void EditorWindow::Paste()
	{
		const QString text = QApplication::clipboard()->text();
		if (!text.startsWith(ClipboardHeader))
			return;
		PasteEntities(ToString(text), "");
	}

	void EditorWindow::DuplicateSelected()
	{
		if (!m_World->IsValid(m_Selected))
			return;
		PasteEntities(SceneSerializer::SaveEntityTreeToString(*m_World.Get(), m_Selected), " Copy");
	}

	void EditorWindow::PasteEntities(const String& text, const char* nameSuffix)
	{
		// Copies get new ids, keep their children and land next to the original.
		const Array<Entity> copies = SceneSerializer::LoadEntitiesAsCopies(*m_World.Get(), text.View());
		if (copies.IsEmpty())
			return;
		const Entity root = copies[0];
		if (NameComponent* name = m_World->TryGet<NameComponent>(root))
			name->Name += nameSuffix;
		if (TransformComponent* transform = m_World->TryGet<TransformComponent>(root))
			transform->Position += Vec3(0.5f, -0.5f, 0);
		MarkDirty();
		RefreshHierarchy();
		Select(root);
	}

	void EditorWindow::DeleteSelected()
	{
		if (!m_World->IsValid(m_Selected))
			return;
		// Mark first so undo remembers the deleted entity as the selection.
		MarkDirty();
		m_World->DestroyEntity(m_Selected);
		Select(NullEntity);
		RefreshHierarchy();
	}

	void EditorWindow::Play()
	{
		if (m_World->IsPlaying())
			return;
		m_Inspector->StopPreview();
		CommitHistory();
		// Snapshot the scene so Stop can put everything back.
		m_PlaySnapshot = SceneSerializer::SaveToString(*m_World.Get());
		// Picks up edits to the controls since the last play.
		Input::LoadActionsFile(Paths::Content("Input.actions").CStr());
		// Play starts from no particles, so bursts fire again.
		Particles::Clear(*m_World.Get());
		m_World->AddSystem<ScriptSystem>();
		m_World->AddSystem<PlayerInputSystem>();
		m_World->AddSystem<PhysicsSystem>();
		m_World->AddSystem<AnimationSystem>();
		m_World->AddSystem<UISystem>();
		m_World->AddSystem<ParticleSystem>();
		Game::AddSystems(*m_World.Get());
		m_World->BeginPlay();
		m_Viewport->SetPlaying(true);
		m_PlayAction->setEnabled(false);
		m_PauseAction->setEnabled(true);
		m_StepAction->setEnabled(false);
		m_StopAction->setEnabled(true);
		m_ImportAudioAction->setEnabled(false);
		m_Inspector->RefreshValues();
		UpdateTitle();
		RefreshProfiler();
		FP_LOG("Play. Esc frees the mouse and a click on the viewport takes it back");
	}

	void EditorWindow::SetPaused(bool paused)
	{
		if (!m_World->IsPlaying() || m_World->IsPaused() == paused)
			return;
		m_World->SetPaused(paused);
		m_Viewport->SetPaused(paused);
		m_PauseAction->setIcon(EditorIcons::Get(paused ? EditorIcon::Play : EditorIcon::Pause));
		m_PauseAction->setText(paused ? "Resume" : "Pause");
		m_PauseAction->setToolTip(paused ? "Resume (Ctrl+Shift+P)" : "Pause (Ctrl+Shift+P)");
		m_StepAction->setEnabled(paused);
		UpdateTitle();
		RefreshProfiler();
		FP_LOG(paused ? "Pause" : "Resume");
	}

	void EditorWindow::StepFrame()
	{
		if (!m_World->IsPlaying() || !m_World->IsPaused())
			return;
		Input::SetDeltaTime(1.0f / 60.0f);
		m_World->Step(1.0f / 60.0f);
		RefreshProfiler();
		FP_LOG("Step frame");
	}

	void EditorWindow::Stop()
	{
		m_Inspector->StopPreview();
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
		m_PauseAction->setEnabled(false);
		m_PauseAction->setIcon(EditorIcons::Get(EditorIcon::Pause));
		m_PauseAction->setText("Pause");
		m_PauseAction->setToolTip("Pause (Ctrl+Shift+P)");
		m_StepAction->setEnabled(false);
		m_StopAction->setEnabled(false);
		m_ImportAudioAction->setEnabled(true);
		UpdateTitle();
		RefreshProfiler();
		FP_LOG("Stop");
	}

	void EditorWindow::ImportAudio(bool assign)
	{
		if (m_World->IsPlaying())
			return;
		const QString source = QFileDialog::getOpenFileName(this, "Import Audio", {}, "Audio (*.wav *.mp3)");
		if (source.isEmpty())
			return;
		const QDir content(ToQString(Paths::GetContentDir()));
		if (!content.mkpath("Audio"))
		{
			QMessageBox::warning(this, "Import Audio", "Cannot create Content/Audio. Check folder access.");
			return;
		}
		QString destination = QFileDialog::getSaveFileName(this, "Import Audio into Content/Audio",
			content.filePath("Audio/" + QFileInfo(source).fileName()), "Audio (*.wav *.mp3)", nullptr, QFileDialog::DontConfirmOverwrite);
		if (destination.isEmpty())
			return;
		if (QFileInfo(destination).suffix().isEmpty())
			destination += "." + QFileInfo(source).suffix();
		String relative;
		const AudioResult result = ImportAudioFile(ToString(source), ToString(destination), relative);
		if (!result)
		{
			QMessageBox::warning(this, "Import Audio", QString("%1\n\n%2").arg(QFileInfo(source).fileName(), ToQString(result.Message)));
			return;
		}
		m_Audio.GetClipCache().Load(relative, true);
		m_ContentBrowser->Refresh();
		if (assign && m_World->IsValid(m_Selected))
		{
			if (AudioSourceComponent* audio = m_World->TryGet<AudioSourceComponent>(m_Selected))
			{
				m_Inspector->StopPreview();
				audio->Clip = relative;
				MarkDirty();
				m_Inspector->SetTarget(m_World.Get(), m_Selected);
			}
		}
		statusBar()->showMessage(QString("Imported %1").arg(ToQString(relative)), 5000);
	}

	void EditorWindow::ShowAudioDebugger()
	{
		if (m_AudioDebugger) { m_AudioDebugger->show(); m_AudioDebugger->raise(); return; }
		m_AudioDebugger = new QDockWidget("Audio Debugger", this);
		m_AudioDebugger->setObjectName("AudioDebugger");
		QWidget* body = new QWidget(m_AudioDebugger);
		QVBoxLayout* layout = new QVBoxLayout(body);
		QCheckBox* capture = new QCheckBox("Capture paths and draw rays during Play");
		capture->setChecked(true);
		layout->addWidget(capture);
		QLabel* legend = new QLabel("Green: direct   Orange: obstructed   Cyan: reflections   Yellow: material hits. Double-click a material hit to inspect its object.");
		legend->setWordWrap(true); layout->addWidget(legend);
		QProgressBar* peak = new QProgressBar();
		peak->setRange(0, 600); peak->setValue(0); peak->setAccessibleName("Audio output peak in decibels");
		peak->setToolTip("Peak display decays at 20 dB per second so short sounds remain readable");
		layout->addWidget(peak);
		QLabel* meter = new QLabel(); meter->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(meter);
		QTabWidget* tabs = new QTabWidget(); layout->addWidget(tabs);
		auto table = [tabs](const char* title, QStringList headers) {
			QTableWidget* widget = new QTableWidget(0, headers.size());
			widget->setHorizontalHeaderLabels(headers);
			widget->setEditTriggers(QAbstractItemView::NoEditTriggers);
			widget->setSelectionBehavior(QAbstractItemView::SelectRows);
			widget->horizontalHeader()->setStretchLastSection(true);
			widget->verticalHeader()->hide();
			tabs->addTab(widget, title);
			return widget;
		};
		QTableWidget* voices = table("Voices", {"Clip / stream", "Bus", "State", "Peak dBFS", "RMS dBFS", "Low-pass Hz", "Wet", "Pitch x"});
		voices->setColumnWidth(0, 260);
		QTableWidget* paths = table("Paths", {"Source", "Distance m", "Transmission", "Applied gain dB", "Low-pass Hz", "Wet", "Decay s", "Pitch x"});
		QTableWidget* hits = table("Material hits", {"Source", "Object", "Material", "Path", "Transmission", "Absorption", "Damping"});
		hits->setColumnWidth(2, 300);
		connect(hits, &QTableWidget::cellDoubleClicked, this, [this, hits](int row, int) {
			const auto* item = hits->item(row, 1);
			if (!item) return;
			const Entity entity = Entity(item->data(Qt::UserRole).toUInt());
			if (m_World->IsValid(entity)) Select(entity);
		});
		auto enabled = [this, capture]() {
			m_World->GetAudio().SetDebugEnabled(m_AudioDebugger->isVisible() && capture->isChecked());
		};
		connect(capture, &QCheckBox::toggled, m_AudioDebugger, enabled);
		connect(m_AudioDebugger, &QDockWidget::visibilityChanged, this, enabled);
		auto refresh = [this, voices, paths, hits, peak, meter]() {
			if (!m_AudioDebugger->isVisible()) return;
			auto db = [](float value) { return value > 0.000001f ? QString::number(20 * std::log10(value), 'f', 1) : QString("-inf"); };
			auto put = [](QTableWidget* target, int row, int column, const QString& text) {
				if (auto* item = target->item(row, column)) { if (item->text() != text) item->setText(text); }
				else target->setItem(row, column, new QTableWidgetItem(text));
			};
			const auto levels = m_Audio.GetMixLevels();
			const int level = levels.OutputPeak > 0 ? int((60 + 20 * std::log10(levels.OutputPeak)) * 10) : 0;
			peak->setValue(std::clamp(level, 0, 600));
			peak->setFormat(QString("Output peak %1 dBFS").arg(db(levels.OutputPeak)));
			meter->setText(QString("Input %1 dBFS | RMS %2 dBFS | Limiter reduction %3 dB | Limited samples %4 | Nonfinite %5")
				.arg(db(levels.InputPeak), db(levels.Rms)).arg(levels.ReductionDb, 0, 'f', 1).arg(levels.LimitedSamples).arg(levels.NonfiniteSamples));
			const auto active = m_Audio.GetVoices();
			voices->setRowCount(active.Num());
			const char* buses[] = {"SFX", "Music", "UI", "Voice"};
			for (int row = 0; row < active.Num(); ++row)
			{
				const auto& voice = active[row];
				put(voices, row, 0, ToQString(voice.Clip)); put(voices, row, 1, buses[size_t(voice.Params.Bus)]);
				put(voices, row, 2, voice.State == VoiceState::Paused ? "Paused" : voice.State == VoiceState::Draining ? "Draining" : "Playing");
				put(voices, row, 3, db(voice.Peak)); put(voices, row, 4, db(voice.Rms));
				put(voices, row, 5, QString::number(voice.Params.LowPassHz, 'f', 0)); put(voices, row, 6, QString::number(voice.Params.Room.Wet, 'f', 2));
				put(voices, row, 7, QString::number(voice.Params.Pitch, 'f', 3));
			}
			const auto& sources = m_World->GetAudio().GetDebugSources();
			paths->setRowCount(int(sources.size()));
			int hitCount = 0;
			for (const auto& source : sources) hitCount += int(source.Hits.size());
			hitCount = std::min(hitCount, 256); hits->setRowCount(hitCount);
			int hitRow = 0;
			for (size_t row = 0; row < sources.size(); ++row)
			{
				const auto& source = sources[row];
				const QString name = m_World->IsValid(source.Owner) ? ToQString(m_World->GetEntityName(source.Owner)) : QString("One-shot");
				put(paths, int(row), 0, name);
				put(paths, int(row), 1, QString::number((source.Position - source.Listener).Length(), 'f', 2));
				put(paths, int(row), 2, QString::number(source.Transmission, 'f', 3));
				put(paths, int(row), 3, db(source.Params.Volume * std::hypot(source.Params.LeftGain, source.Params.RightGain)));
				put(paths, int(row), 4, QString::number(source.Params.LowPassHz, 'f', 0));
				put(paths, int(row), 5, QString::number(source.Params.Room.Wet, 'f', 2)); put(paths, int(row), 6, QString::number(source.Params.Room.Decay, 'f', 2));
				put(paths, int(row), 7, QString::number(source.Params.Pitch, 'f', 3));
				for (const auto& hit : source.Hits)
				{
					if (hitRow == hitCount) break;
					put(hits, hitRow, 0, name); put(hits, hitRow, 1, ToQString(m_World->GetEntityName(hit.Object)));
					hits->item(hitRow, 1)->setData(Qt::UserRole, uint32(hit.Object));
					put(hits, hitRow, 2, ToQString(hit.Material)); put(hits, hitRow, 3, hit.Reflection ? "Reflection" : "Direct");
					put(hits, hitRow, 4, QString::number(hit.Transmission, 'f', 3)); put(hits, hitRow, 5, QString::number(hit.Absorption, 'f', 3));
					put(hits, hitRow, 6, QString::number(hit.Damping, 'f', 3));
					++hitRow;
				}
			}
		};
		QTimer* timer = new QTimer(m_AudioDebugger);
		connect(timer, &QTimer::timeout, m_AudioDebugger, refresh); timer->start(100);
		m_AudioDebugger->setWidget(body);
		addDockWidget(Qt::BottomDockWidgetArea, m_AudioDebugger);
		tabifyDockWidget(m_ContentDock, m_AudioDebugger);
		m_AudioDebugger->show(); m_AudioDebugger->raise(); enabled(); refresh();
	}

	void EditorWindow::ShowAudioSettings()
	{
		QDialog dialog(this);
		dialog.setWindowTitle("Audio output, mixer and voice chat");
		QVBoxLayout* layout = new QVBoxLayout(&dialog);
		QLabel* diagnostics = new QLabel(ToQString(m_Audio.GetStatusText()));
		diagnostics->setWordWrap(true);
		diagnostics->setTextInteractionFlags(Qt::TextSelectableByMouse);
		layout->addWidget(diagnostics);
		QFormLayout* form = new QFormLayout();
		layout->addLayout(form);
		const char* names[] = { "Master", "SFX", "Music", "UI", "Voice" };
		constexpr AudioBus buses[] = { AudioBus::SFX, AudioBus::Music, AudioBus::UI, AudioBus::Voice };
		static_assert(std::size(names) == std::extent_v<decltype(m_AudioGains)> && std::size(names) == std::extent_v<decltype(m_AudioMutes)>);
		for (int i = 0; i < static_cast<int>(std::size(names)); ++i)
		{
			QDoubleSpinBox* gain = new QDoubleSpinBox();
			gain->setRange(0, 1);
			gain->setSingleStep(0.05);
			gain->setValue(m_AudioGains[i]);
			gain->setAccessibleName(QString("%1 volume").arg(names[i]));
			connect(gain, &QDoubleSpinBox::valueChanged, &dialog, [this, i, buses](double value) {
				m_AudioGains[i] = static_cast<float>(value);
				if (i == 0) m_Audio.SetMasterGain(m_AudioGains[i]);
				else m_Audio.SetBusGain(buses[i - 1], m_AudioGains[i]);
			});
			form->addRow(QString("%1 volume").arg(names[i]), gain);
			QCheckBox* mute = new QCheckBox(QString("Mute %1").arg(names[i]));
			mute->setChecked(m_AudioMutes[i]);
			connect(mute, &QCheckBox::toggled, &dialog, [this, i, buses](bool checked) {
				m_AudioMutes[i] = checked;
				if (i == 0) m_Audio.SetMasterMuted(checked);
				else m_Audio.SetBusMuted(buses[i - 1], checked);
			});
			form->addRow(mute);
		}
		QLabel* voiceHint = new QLabel("Proximity voice: enable your microphone, then hold V in the focused game viewport to talk. Use headphones to avoid feedback. VoiceChat on the network player sets speaking distance and volume.");
		voiceHint->setWordWrap(true);
		layout->addWidget(voiceHint);
		QCheckBox* microphone = new QCheckBox("Enable microphone for this network session");
		microphone->setToolTip("Off by default. Hold push-to-talk V to transmit; opening this dialog does not enable microphone capture.");
		layout->addWidget(microphone);
		QLabel* voiceStatus = new QLabel();
		voiceStatus->setWordWrap(true);
		voiceStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
		voiceStatus->setAccessibleName("Voice chat status");
		layout->addWidget(voiceStatus);
		QListWidget* peers = new QListWidget();
		peers->setAccessibleName("Voice chat peer mute controls");
		peers->setSelectionMode(QAbstractItemView::NoSelection);
		peers->setMaximumHeight(130);
		layout->addWidget(peers);
		auto networkSystem = [this]() -> NetworkSystem* {
			for (const auto& system : m_World->GetSystems())
				if (NetworkSystem* network = dynamic_cast<NetworkSystem*>(system.Get()))
					return network;
			return nullptr;
		};
		connect(microphone, &QCheckBox::toggled, &dialog, [networkSystem](bool enabled) {
			if (NetworkSystem* network = networkSystem(); network && network->GetSession().IsRunning())
				network->GetVoiceChat().SetEnabled(enabled);
		});
		connect(peers, &QListWidget::itemChanged, &dialog, [networkSystem](QListWidgetItem* item) {
			if (NetworkSystem* network = networkSystem(); network && network->GetSession().IsRunning())
			{
				const uint64 peer = item->data(Qt::UserRole).toULongLong();
				if (network->GetSession().GetPeers().Contains(peer))
					network->GetVoiceChat().SetPeerMuted(peer, item->checkState() == Qt::Checked);
			}
		});
		auto refresh = [this, diagnostics, microphone, voiceStatus, peers, networkSystem]() {
			diagnostics->setText(ToQString(m_Audio.GetStatusText()));
			NetworkSystem* network = networkSystem();
			const bool running = network && network->GetSession().IsRunning();
			QSignalBlocker microphoneBlock(microphone), peersBlock(peers);
			microphone->setEnabled(running);
			microphone->setChecked(running && network->GetVoiceChat().IsEnabled());
			if (!running)
			{
				voiceStatus->setText("Start a network session to use voice; runtime --host/--join --voice. Microphone is off.");
				peers->clear();
				return;
			}
			voiceStatus->setText(ToQString(network->GetVoiceChat().GetStatusText()));
			const auto connected = network->GetSession().GetPeers();
			bool changed = peers->count() != connected.Num();
			for (int i = 0; !changed && i < connected.Num(); ++i)
				changed = peers->item(i)->data(Qt::UserRole).toULongLong() != connected[i];
			if (changed)
			{
				peers->clear();
				for (uint64 peer : connected)
				{
					QListWidgetItem* item = new QListWidgetItem(QString("Mute peer %1").arg(peer), peers);
					item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(peer));
					item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
				}
			}
			for (int i = 0; i < connected.Num(); ++i)
				peers->item(i)->setCheckState(network->GetVoiceChat().IsPeerMuted(connected[i]) ? Qt::Checked : Qt::Unchecked);
		};
		refresh();
		QTimer statusTimer(&dialog);
		connect(&statusTimer, &QTimer::timeout, &dialog, refresh);
		statusTimer.start(250);
		QPushButton* debugger = new QPushButton("Open Audio Debugger");
		connect(debugger, &QPushButton::clicked, &dialog, [this, &dialog]() { dialog.accept(); ShowAudioDebugger(); });
		layout->addWidget(debugger);
		QPushButton* close = new QPushButton("Close");
		connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
		layout->addWidget(close);
		dialog.exec();
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
