#include "Editor/ContentBrowser.h"
#include "Editor/EditorStyle.h"
#include "Editor/EditorWindow.h"
#include "Editor/ScriptBuilder.h"
#include "Engine/Engine.h"
#include "Engine/Audio/AudioDevice.h"
#include "Core/Jobs/JobSystem.h"
#include "Engine/Render/GPUDevice.h"
#include "Engine/Scripting/ScriptRuntime.h"
#include "Game.h"

#include <SDL3/SDL.h>

#include <QApplication>
#include <QTimer>

int main(int argc, char** argv)
{
	QApplication app(argc, argv);

	// SDL_GPU needs the video subsystem even though the editor draws offscreen.
	// Qt goes first so it owns the native application object.
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		FP_LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
		return 1;
	}

	QApplication::setApplicationName("FirstPrinciples Editor");
	FP::Editor::EditorStyle::Apply(app);

	FP::Engine::RegisterEngineComponents();
	Game::RegisterComponents();
	FP::ScriptRuntime::Get().LoadModule(FP::ScriptRuntime::GetDefaultModulePath());

	FP::JobSystem::Initialize();

	FP::GPUDevice* device = FP::GPUDevice::Create();
	if (!device)
		return 1;

	FP::AudioDevice audio;
	if (const FP::AudioResult initialized = audio.Initialize(); !initialized)
		FP_LOG_WARN("Audio unavailable: %s", initialized.Message.CStr());

	int result = 0;
	{
		FP::Editor::EditorWindow window(*device, audio);
		window.show();

		// Smoke test flags. --scene path opens a scene relative to Content, --3d switches the view,
		// --viewmode lit, unlit or wireframe picks the shading,
		// --select Name picks an entity, --play starts play mode and --screenshot out.png grabs the
		// window after a second and quits.
		const QStringList args = QApplication::arguments();
		const int sceneIndex = static_cast<int>(args.indexOf("--scene"));
		if (sceneIndex >= 0 && sceneIndex + 1 < args.size())
			window.OpenScene(QString::fromUtf8(FP::Paths::Content(args[sceneIndex + 1].toUtf8().constData()).CStr()));
		if (args.contains("--3d"))
			window.Set3DView(true);
		const int modeIndex = static_cast<int>(args.indexOf("--viewmode"));
		if (modeIndex >= 0 && modeIndex + 1 < args.size())
		{
			const QString mode = args[modeIndex + 1].toLower();
			window.SetViewMode(mode == "unlit" ? FP::ViewMode::Unlit : mode == "wireframe" ? FP::ViewMode::Wireframe : FP::ViewMode::Lit);
		}
		// --browse folder shows a folder in the content browser, relative to Content.
		const int browseIndex = static_cast<int>(args.indexOf("--browse"));
		if (browseIndex >= 0 && browseIndex + 1 < args.size())
			window.GetContentBrowser()->ShowFolder(QString::fromUtf8(FP::Paths::Content(args[browseIndex + 1].toUtf8().constData()).CStr()));

		// --asset path shows an asset in the inspector, relative to Content.
		const int assetIndex = static_cast<int>(args.indexOf("--asset"));
		if (assetIndex >= 0 && assetIndex + 1 < args.size())
			window.ShowAsset(QString::fromUtf8(FP::Paths::Content(args[assetIndex + 1].toUtf8().constData()).CStr()));

		// --open-script path opens a script tab, relative to the project.
		const int openIndex = static_cast<int>(args.indexOf("--open-script"));
		if (openIndex >= 0 && openIndex + 1 < args.size())
			window.OpenScript(QString::fromUtf8(FP::Paths::GetProjectDir().CStr()) + "/" + args[openIndex + 1]);

		const int selectIndex = static_cast<int>(args.indexOf("--select"));
		if (selectIndex >= 0 && selectIndex + 1 < args.size())
			window.Select(window.GetWorld().FindEntityByName(args[selectIndex + 1].toUtf8().constData()));
		if (args.contains("--play"))
			window.Play();
		// --new-script Name writes a script from the template, then builds like --build-scripts.
		const int newScriptIndex = static_cast<int>(args.indexOf("--new-script"));
		if (newScriptIndex >= 0 && newScriptIndex + 1 < args.size() && !window.NewScript(args[newScriptIndex + 1]))
			return 1;

		// --new-project Name creates a project next to this one and opens it. --open-project dir opens one.
		const int newProjectIndex = static_cast<int>(args.indexOf("--new-project"));
		if (newProjectIndex >= 0 && newProjectIndex + 1 < args.size() && !window.NewProject(args[newProjectIndex + 1]))
			return 1;
		const int openProjectIndex = static_cast<int>(args.indexOf("--open-project"));
		if (openProjectIndex >= 0 && openProjectIndex + 1 < args.size())
			window.OpenProject(args[openProjectIndex + 1]);

		// --build-scripts builds GameScripts and exits with 0 on success.
		if (args.contains("--build-scripts"))
		{
			QObject::connect(window.GetScriptBuilder(), &FP::Editor::ScriptBuilder::Finished, &app, [&window](bool ok) {
				window.DiscardUnsavedChanges();
				QApplication::exit(ok ? 0 : 1);
			});
			window.GetScriptBuilder()->Build();
		}

		// --drop path drops an asset at the origin onto the selected entity, like a drag from the browser.
		const int dropIndex = static_cast<int>(args.indexOf("--drop"));
		if (dropIndex >= 0 && dropIndex + 1 < args.size())
		{
			const FP::Entity target = window.GetWorld().FindEntityByName(selectIndex >= 0 && selectIndex + 1 < args.size() ? args[selectIndex + 1].toUtf8().constData() : "");
			window.DropAsset(QString::fromUtf8(FP::Paths::Content(args[dropIndex + 1].toUtf8().constData()).CStr()), {}, target);
		}

		// --undo then --redo step the history once each after the edits above, and log what is left.
		if (args.contains("--undo"))
			window.Undo();
		if (args.contains("--redo"))
			window.Redo();
		if (args.contains("--undo") || args.contains("--redo"))
			FP_LOG("Scene has %d entities after undo and redo", window.GetWorld().GetEntities().Num());

		const int shotIndex = static_cast<int>(args.indexOf("--screenshot"));
		if (shotIndex >= 0 && shotIndex + 1 < args.size())
		{
			const QString path = args[shotIndex + 1];
			// --wait seconds delays the screenshot, for example to catch a hot reload.
			const int waitIndex = static_cast<int>(args.indexOf("--wait"));
			const int waitMs = waitIndex >= 0 && waitIndex + 1 < args.size() ? static_cast<int>(args[waitIndex + 1].toDouble() * 1000) : 1000;
			QTimer::singleShot(waitMs, &window, [&window, path]() {
				window.grab().save(path);
				FP_LOG("Saved screenshot %s", path.toUtf8().constData());
				// Quitting closes the window, which would offer to save. Test runs must never save.
				window.DiscardUnsavedChanges();
				QApplication::quit();
			});
		}
		result = app.exec();
	}

	FP::GarbageCollector::Get().Collect();
	audio.Shutdown();
	FP::ScriptRuntime::Get().UnloadModule();
	delete device;
	FP::JobSystem::Shutdown();
	SDL_Quit();
	return result;
}
