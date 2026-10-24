#include "Editor/EditorWindow.h"
#include "Editor/ScriptBuilder.h"
#include "Engine/Engine.h"
#include "Core/Jobs/JobSystem.h"
#include "Engine/Render/GPUDevice.h"
#include "Engine/Scripting/ScriptRuntime.h"
#include "Game/Game.h"

#include <SDL3/SDL.h>

#include <QApplication>
#include <QStyleFactory>
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
	QApplication::setStyle(QStyleFactory::create("Fusion"));

	FP::Engine::RegisterEngineComponents();
	Game::RegisterComponents();
	FP::ScriptRuntime::Get().LoadModule(FP::ScriptRuntime::GetDefaultModulePath());

	FP::JobSystem::Initialize();

	FP::GPUDevice* device = FP::GPUDevice::Create();
	if (!device)
		return 1;

	int result = 0;
	{
		FP::Editor::EditorWindow window(*device);
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

		// --build-scripts builds GameScripts and exits with 0 on success.
		if (args.contains("--build-scripts"))
		{
			QObject::connect(window.GetScriptBuilder(), &FP::Editor::ScriptBuilder::Finished, &app, [](bool ok) { QApplication::exit(ok ? 0 : 1); });
			window.GetScriptBuilder()->Build();
		}

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
				QApplication::quit();
			});
		}
		result = app.exec();
	}

	FP::GarbageCollector::Get().Collect();
	FP::ScriptRuntime::Get().UnloadModule();
	delete device;
	FP::JobSystem::Shutdown();
	SDL_Quit();
	return result;
}
