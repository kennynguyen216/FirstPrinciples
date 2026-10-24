#include "Editor/EditorWindow.h"
#include "Engine/Engine.h"
#include "Game/Game.h"

#include <SDL3/SDL.h>

#include <QApplication>
#include <QStyleFactory>
#include <QTimer>

int main(int argc, char** argv)
{
	// SDL is only used for rendering into memory and timing, so no video subsystem is needed.
	SDL_Init(0);

	QApplication app(argc, argv);
	QApplication::setApplicationName("FirstPrinciples Editor");
	QApplication::setStyle(QStyleFactory::create("Fusion"));

	FP::Engine::RegisterEngineComponents();
	Game::RegisterComponents();

	int result = 0;
	{
		FP::Editor::EditorWindow window;
		window.show();

		// Smoke test flags. --select Name picks an entity, --play starts play mode and
		// --screenshot out.png grabs the window after a second and quits.
		const QStringList args = QApplication::arguments();
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
	SDL_Quit();
	return result;
}
