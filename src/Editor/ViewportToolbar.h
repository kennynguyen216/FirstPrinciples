#pragma once

#include "Engine/Render/RenderView.h"

#include <QWidget>

class QToolButton;

namespace FP::Editor
{
	// The row of pill buttons that floats over the top of the viewport: projection, view mode and grid snap.
	class ViewportToolbar : public QWidget
	{
		Q_OBJECT

	public:
		explicit ViewportToolbar(QWidget* parent = nullptr);

		// Keep the buttons in sync when shortcuts change these.
		void Set3D(bool enabled);
		void SetViewMode(ViewMode mode);

	signals:
		void ProjectionChanged(bool is3D);
		void ViewModeChanged(FP::ViewMode mode);
		void SnapChanged(bool enabled, float size);

	private:
		QToolButton* m_Projection = nullptr;
		QToolButton* m_ViewMode = nullptr;
		QToolButton* m_Snap = nullptr;
		QToolButton* m_SnapSize = nullptr;
		float m_SnapStep = 0.25f;
	};
}
