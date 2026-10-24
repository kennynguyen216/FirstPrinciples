#pragma once

#include <QColor>
#include <QIcon>

namespace FP::Editor
{
	// Small vector icons drawn with QPainter so the editor ships no image files and stays sharp on any DPI.
	enum class EditorIcon
	{
		Save,
		Play,
		Step,
		Stop,
		Eject,
		More,
		Settings,
		Search,
		Add,
		Folder,
		FolderOpen,
		Eye,
		Close,
		Menu,
		Entity,
		Sprite,
		Mesh,
		PointLight,
		DirectionalLight,
		AmbientLight,
		Camera,
		Script,
		Scene,
		Material,
		Texture,
		Level,
		Perspective,
		Lit,
		Grid,
		Snap,
		Select,
		Move,
		Rotate,
		Scale,
		World,
		Local,
		Details,
		Hierarchy,
		PlaceEntities,
		ContentBrowser,
		OutputLog,
		Console,
		Import,
		SaveAll,
		Back,
		Forward,
		Chevron,
		Filter,
		Lock,
		Help,
	};

	namespace EditorIcons
	{
		// Cached per icon and color. Pass an invalid color for the icon's own default.
		QIcon Get(EditorIcon icon, const QColor& color = QColor());
	}
}
