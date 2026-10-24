#pragma once

#include <QColor>

class QApplication;

namespace FP::Editor
{
	// The editor's look, modeled on the Unreal Engine 5 dark theme so people coming from UE feel at home.
	namespace EditorStyle
	{
		// Colors shared by widgets that paint themselves.
		inline const QColor Recessed{ 0x0f, 0x0f, 0x0f }; // inputs and the space between panels
		inline const QColor Background{ 0x15, 0x15, 0x15 };
		inline const QColor Panel{ 0x24, 0x24, 0x24 };
		inline const QColor Header{ 0x2f, 0x2f, 0x2f };
		inline const QColor Hover{ 0x57, 0x57, 0x57 };
		inline const QColor Foreground{ 0xc0, 0xc0, 0xc0 };
		inline const QColor Bright{ 0xff, 0xff, 0xff };
		inline const QColor Accent{ 0x00, 0x70, 0xe0 };
		inline const QColor Selection{ 0x1d, 0x4f, 0x8a };
		inline const QColor Play{ 0x2e, 0xc9, 0x3b };
		inline const QColor Folder{ 0xc8, 0xa2, 0x6a };
		inline const QColor AxisX{ 0xc8, 0x3a, 0x3a };
		inline const QColor AxisY{ 0x5e, 0xb0, 0x3a };
		inline const QColor AxisZ{ 0x3a, 0x7a, 0xe0 };

		// Sets the palette, style sheet and font for the whole app. Call once after QApplication exists.
		void Apply(QApplication& app);
	}
}
