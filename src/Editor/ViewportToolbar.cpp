#include "Editor/ViewportToolbar.h"
#include "Editor/EditorIcons.h"

#include <QHBoxLayout>
#include <QMenu>
#include <QToolButton>

namespace FP::Editor
{
	namespace
	{
		// Rounded dark pills that read well over any scene.
		const char* PillStyle =
			"QToolButton { background: rgba(21, 21, 21, 215); border: 1px solid rgba(0, 0, 0, 120); border-radius: 12px;"
			" padding: 3px 10px; color: #e0e0e0; }"
			"QToolButton:hover { background: rgba(70, 70, 70, 230); color: #ffffff; }"
			"QToolButton:checked { background: rgba(0, 112, 224, 220); color: #ffffff; }";

		QToolButton* MakePill(const QIcon& icon, const QString& text, const QString& tip)
		{
			QToolButton* button = new QToolButton();
			button->setIcon(icon);
			button->setText(text);
			button->setToolTip(tip);
			button->setIconSize({ 14, 14 });
			button->setToolButtonStyle(text.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
			button->setStyleSheet(PillStyle);
			button->setCursor(Qt::ArrowCursor);
			return button;
		}

		const char* ViewModeNames[] = { "Lit", "Unlit", "Wireframe" };
	}

	ViewportToolbar::ViewportToolbar(QWidget* parent) : QWidget(parent)
	{
		setAttribute(Qt::WA_TranslucentBackground);
		QHBoxLayout* layout = new QHBoxLayout(this);
		layout->setContentsMargins(8, 6, 8, 6);
		layout->setSpacing(6);

		m_Projection = MakePill(EditorIcons::Get(EditorIcon::Perspective), "Perspective", "Projection (Ctrl+3)");
		m_Projection->setPopupMode(QToolButton::InstantPopup);
		QMenu* projection = new QMenu(m_Projection);
		projection->addAction("Perspective", this, [this]() { Set3D(true); emit ProjectionChanged(true); });
		projection->addAction("Top (2D)", this, [this]() { Set3D(false); emit ProjectionChanged(false); });
		m_Projection->setMenu(projection);
		layout->addWidget(m_Projection);

		m_ViewMode = MakePill(EditorIcons::Get(EditorIcon::Lit), "Lit", "View mode (Alt+4 Lit, Alt+3 Unlit, Alt+2 Wireframe)");
		m_ViewMode->setPopupMode(QToolButton::InstantPopup);
		QMenu* modes = new QMenu(m_ViewMode);
		for (int i = 0; i < 3; ++i)
		{
			const ViewMode mode = static_cast<ViewMode>(i);
			modes->addAction(ViewModeNames[i], this, [this, mode]() { SetViewMode(mode); emit ViewModeChanged(mode); });
		}
		m_ViewMode->setMenu(modes);
		layout->addWidget(m_ViewMode);
		layout->addStretch(1);

		// Off by default so drags move freely. Shift flips it while dragging either way.
		m_Snap = MakePill(EditorIcons::Get(EditorIcon::Grid), "", "Snap moves to the grid (hold Shift to invert)");
		m_Snap->setCheckable(true);
		m_Snap->setChecked(false);
		m_SnapSize = MakePill(QIcon(), "0.25", "Grid size");
		m_SnapSize->setPopupMode(QToolButton::InstantPopup);
		QMenu* sizes = new QMenu(m_SnapSize);
		for (float size : { 0.1f, 0.25f, 0.5f, 1.0f, 5.0f, 10.0f })
		{
			sizes->addAction(QString::number(size), this, [this, size]() {
				m_SnapStep = size;
				m_SnapSize->setText(QString::number(size));
				emit SnapChanged(m_Snap->isChecked(), m_SnapStep);
			});
		}
		m_SnapSize->setMenu(sizes);
		connect(m_Snap, &QToolButton::toggled, this, [this](bool on) { emit SnapChanged(on, m_SnapStep); });
		layout->addWidget(m_Snap);
		layout->addWidget(m_SnapSize);
	}

	void ViewportToolbar::Set3D(bool enabled)
	{
		m_Projection->setText(enabled ? "Perspective" : "Top (2D)");
	}

	void ViewportToolbar::SetViewMode(ViewMode mode)
	{
		m_ViewMode->setText(ViewModeNames[static_cast<int>(mode)]);
	}
}
