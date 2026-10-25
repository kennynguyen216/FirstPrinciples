#pragma once

#include "Editor/TransformGizmo.h"
#include "Engine/Render/RenderView.h"

#include <QWidget>

class QButtonGroup;
class QToolButton;

namespace FP::Editor
{
	// The row of pill buttons that floats over the top of the viewport: projection, view mode,
	// gizmo mode and space, and grid snap.
	class ViewportToolbar : public QWidget
	{
		Q_OBJECT

	public:
		explicit ViewportToolbar(QWidget* parent = nullptr);

		// Keep the buttons in sync when shortcuts change these.
		void Set3D(bool enabled);
		void SetViewMode(ViewMode mode);
		void SetDIAEnabled(bool enabled);
		void SetGizmoMode(GizmoMode mode);

	signals:
		void ProjectionChanged(bool is3D);
		void ViewModeChanged(FP::ViewMode mode);
		void DIAToggled(bool enabled);
		void SnapChanged(bool enabled, float size);
		void GizmoModeChanged(FP::Editor::GizmoMode mode);
		void GizmoSpaceChanged(bool local);

	private:
		QToolButton* m_Projection = nullptr;
		QToolButton* m_ViewMode = nullptr;
		QToolButton* m_DIA = nullptr;
		QButtonGroup* m_GizmoModes = nullptr;
		QToolButton* m_Space = nullptr;
		QToolButton* m_Snap = nullptr;
		QToolButton* m_SnapSize = nullptr;
		float m_SnapStep = 0.25f;
	};
}
