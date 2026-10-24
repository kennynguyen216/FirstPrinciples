#pragma once

#include "Editor/EditorIcons.h"

#include <QWidget>

class QDockWidget;

namespace FP::Editor
{
	// Dock title bar drawn as a UE style tab: icon, title and a close button on a dark strip.
	class DockTab : public QWidget
	{
		Q_OBJECT

	public:
		DockTab(QDockWidget* dock, EditorIcon icon);

		// Collapses the title when the dock shares a spot with others, since Qt's tab bar names it then.
		void SetCompact(bool compact);

		QSize sizeHint() const override;
		QSize minimumSizeHint() const override { return sizeHint(); }

		// Makes a dock with this title bar. The caller adds it to the window.
		static QDockWidget* Create(QWidget* parent, const QString& title, EditorIcon icon, QWidget* content);

	protected:
		void paintEvent(QPaintEvent* event) override;
		void mouseDoubleClickEvent(QMouseEvent* event) override;

	private:
		QDockWidget* m_Dock;
		EditorIcon m_Icon;
		bool m_Compact = false;
	};
}
