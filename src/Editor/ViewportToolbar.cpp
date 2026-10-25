#include "Editor/ViewportToolbar.h"
#include "Editor/EditorIcons.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QMenu>
#include <QSignalBlocker>
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

		const char* ViewModeNames[] = { "Lit", "Unlit", "Wireframe", "DIA Direct", "DIA Diffuse", "DIA Reflections", "DIA Coverage", "DIA Cache" };

		// Buttons that sit side by side as one pill, rounded only at the ends of the group.
		const char* GroupStyle =
			"QToolButton { background: rgba(21, 21, 21, 215); border: 1px solid rgba(0, 0, 0, 120); border-radius: 0;"
			" padding: 3px 7px; color: #e0e0e0; }"
			"QToolButton:hover { background: rgba(70, 70, 70, 230); }"
			"QToolButton:checked { background: rgba(0, 112, 224, 220); }"
			"QToolButton[first=\"true\"] { border-top-left-radius: 12px; border-bottom-left-radius: 12px; padding-left: 10px; }"
			"QToolButton[last=\"true\"] { border-top-right-radius: 12px; border-bottom-right-radius: 12px; padding-right: 10px; }";
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
		for (int i = 0; i < static_cast<int>(sizeof(ViewModeNames) / sizeof(ViewModeNames[0])); ++i)
		{
			const ViewMode mode = static_cast<ViewMode>(i);
			modes->addAction(ViewModeNames[i], this, [this, mode]() { SetViewMode(mode); emit ViewModeChanged(mode); });
		}
		m_ViewMode->setMenu(modes);
		layout->addWidget(m_ViewMode);
		m_DIA = MakePill(EditorIcons::Get(EditorIcon::Lit), "DIA Off", "Compare DIA with standard lighting");
		m_DIA->setObjectName("DIAToggle");
		m_DIA->setAccessibleName("DIA lighting");
		m_DIA->setCheckable(true);
		connect(m_DIA, &QToolButton::toggled, this, &ViewportToolbar::DIAToggled);
		layout->addWidget(m_DIA);
		layout->addStretch(1);

		// Select, move, rotate and scale, like UE's transform tools.
		QHBoxLayout* tools = new QHBoxLayout();
		tools->setSpacing(0);
		m_GizmoModes = new QButtonGroup(this);
		const struct { GizmoMode Mode; EditorIcon Icon; const char* Tip; } toolInfo[] = {
			{ GizmoMode::Select, EditorIcon::Select, "Select (Q)" },
			{ GizmoMode::Move, EditorIcon::Move, "Move (W)" },
			{ GizmoMode::Rotate, EditorIcon::Rotate, "Rotate (E)" },
			{ GizmoMode::Scale, EditorIcon::Scale, "Scale (R)" },
		};
		for (int i = 0; i < 4; ++i)
		{
			QToolButton* button = MakePill(EditorIcons::Get(toolInfo[i].Icon), "", toolInfo[i].Tip);
			button->setCheckable(true);
			button->setProperty("first", i == 0);
			button->setProperty("last", i == 3);
			button->setStyleSheet(GroupStyle);
			m_GizmoModes->addButton(button, static_cast<int>(toolInfo[i].Mode));
			tools->addWidget(button);
		}
		m_GizmoModes->button(static_cast<int>(GizmoMode::Move))->setChecked(true);
		connect(m_GizmoModes, &QButtonGroup::idClicked, this, [this](int id) { emit GizmoModeChanged(static_cast<GizmoMode>(id)); });
		layout->addLayout(tools);

		m_Space = MakePill(EditorIcons::Get(EditorIcon::World), "", "Moving and rotating along world axes. Click for the entity's own axes.");
		connect(m_Space, &QToolButton::clicked, this, [this]() {
			const bool local = m_Space->property("local").toBool() == false;
			m_Space->setProperty("local", local);
			m_Space->setIcon(EditorIcons::Get(local ? EditorIcon::Local : EditorIcon::World));
			m_Space->setToolTip(local
				? "Moving and rotating along the entity's own axes. Click for world axes."
				: "Moving and rotating along world axes. Click for the entity's own axes.");
			emit GizmoSpaceChanged(local);
		});
		layout->addWidget(m_Space);

		// Off by default so drags move freely. Shift flips it while dragging either way.
		m_Snap = MakePill(EditorIcons::Get(EditorIcon::Grid), "", "Snap moves to the grid and turns to 15 degrees (hold Shift to invert)");
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

	void ViewportToolbar::SetGizmoMode(GizmoMode mode)
	{
		m_GizmoModes->button(static_cast<int>(mode))->setChecked(true);
	}

	void ViewportToolbar::SetDIAEnabled(bool enabled)
	{
		const QSignalBlocker blocked(m_DIA);
		m_DIA->setChecked(enabled);
		m_DIA->setText(enabled ? "DIA On" : "DIA Off");
	}
}
