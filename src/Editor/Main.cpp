#include "Editor/EditorWindow.h"
#include "Engine/Engine.h"
#include "Engine/Render/GPUDevice.h"
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
		const int selectIndex = static_cast<int>(args.indexOf("--select"));
		if (selectIndex >= 0 && selectIndex + 1 < args.size())
			window.Select(window.GetWorld().FindEntityByName(args[selectIndex + 1].toUtf8().constData()));
		if (args.contains("--play"))
			window.Play();
		const int shotIndex = static_cast<int>(args.indexOf("--screenshot"));
		if (shotIndex >= 0 && shotIndex + 1 < args.size())
		{
			const QString path = args[shotIndex + 1];
			QTimer::singleShot(1000, &window, [&window, path]() {
				window.grab().save(path);
				FP_LOG("Saved screenshot %s", path.toUtf8().constData());
				QApplication::quit();
			});
		}
		result = app.exec();
	}

	FP::GarbageCollector::Get().Collect();
	delete device;
	SDL_Quit();
	return result;
}
