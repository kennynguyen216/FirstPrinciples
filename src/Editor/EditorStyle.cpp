#include "Editor/EditorStyle.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

namespace FP::Editor::EditorStyle
{
	namespace
	{
		// Same colors as the header, written out so the sheet reads like plain CSS.
		const char* StyleSheet = R"(
QWidget { color: #c0c0c0; }
QMainWindow, QMainWindow::separator { background: #0f0f0f; }
QMainWindow::separator { width: 4px; height: 4px; }

QMenuBar { background: #151515; padding: 2px 4px; }
QMenuBar::item { padding: 4px 10px; background: transparent; border-radius: 3px; }
QMenuBar::item:selected { background: #383838; color: #ffffff; }
QMenu { background: #1a1a1a; border: 1px solid #383838; padding: 4px; }
QMenu::item { padding: 5px 24px 5px 20px; border-radius: 3px; }
QMenu::item:selected { background: #0070e0; color: #ffffff; }
QMenu::item:disabled { color: #5a5a5a; }
QMenu::separator { height: 1px; background: #383838; margin: 4px 6px; }

QToolBar { background: #242424; border: none; border-bottom: 1px solid #0f0f0f; padding: 4px 6px; spacing: 4px; }
QToolBar::separator { width: 1px; background: #383838; margin: 4px 6px; }

QDockWidget { titlebar-close-icon: none; }
QDockWidget > QWidget { background: #242424; }

QStatusBar { background: #151515; border-top: 1px solid #0f0f0f; }
QStatusBar::item { border: none; }

QPushButton, QToolButton {
	background: #383838; border: 1px solid #2a2a2a; border-radius: 4px; padding: 4px 10px; color: #c0c0c0;
}
QPushButton:hover, QToolButton:hover { background: #575757; color: #ffffff; }
QPushButton:pressed, QToolButton:pressed { background: #0f0f0f; }
QPushButton:disabled, QToolButton:disabled { color: #5a5a5a; background: #2a2a2a; }
QPushButton:flat { background: transparent; border: none; }
QPushButton:flat:hover { background: #383838; }
QToolButton:checked { background: #0f0f0f; color: #ffffff; border-color: #0070e0; }
QToolButton::menu-indicator { image: none; width: 0; }
QToolBar QToolButton { background: transparent; border: 1px solid transparent; padding: 4px 6px; }
QToolBar QToolButton:hover { background: #383838; border-color: #383838; }
QToolBar QToolButton:checked { background: #0f0f0f; border-color: #383838; }

QLineEdit, QSpinBox, QDoubleSpinBox, QPlainTextEdit, QTextEdit {
	background: #0f0f0f; border: 1px solid #383838; border-radius: 4px; padding: 2px 4px;
	selection-background-color: #0070e0; selection-color: #ffffff;
}
QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover { border-color: #575757; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: #0070e0; }
QLineEdit[search="true"] { border-radius: 12px; padding: 3px 10px; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; border: none; }

QComboBox { background: #383838; border: 1px solid #2a2a2a; border-radius: 4px; padding: 3px 8px; }
QComboBox:hover { background: #575757; color: #ffffff; }
QComboBox::drop-down { border: none; width: 16px; }
QComboBox QAbstractItemView { background: #1a1a1a; border: 1px solid #383838; selection-background-color: #0070e0; }

QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #575757; border-radius: 3px; background: #0f0f0f; }
QCheckBox::indicator:checked { background: #0070e0; border-color: #0070e0; }

QTabWidget::pane { border: none; }
QTabBar { background: #151515; }
QTabBar::tab {
	background: #151515; color: #9a9a9a; padding: 6px 14px; border: none;
	border-top-left-radius: 4px; border-top-right-radius: 4px; margin-right: 1px;
}
QTabBar::tab:selected { background: #242424; color: #ffffff; }
QTabBar::tab:hover:!selected { background: #1f1f1f; color: #ffffff; }

QTreeView, QListView, QTableView {
	background: #1a1a1a; alternate-background-color: #1e1e1e; border: none; outline: none;
	selection-background-color: #1d4f8a; selection-color: #ffffff;
}
QTreeView::item, QListView::item { padding: 2px; }
QTreeView::item:hover, QListView::item:hover { background: #2f2f2f; }
QTreeView::item:selected, QListView::item:selected { background: #1d4f8a; color: #ffffff; }
QHeaderView::section { background: #2f2f2f; color: #c0c0c0; border: none; border-right: 1px solid #1a1a1a; padding: 4px 6px; }

QScrollArea { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle { background: #4a4a4a; border-radius: 4px; min-height: 24px; min-width: 24px; margin: 2px; }
QScrollBar::handle:hover { background: #6a6a6a; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QGroupBox { background: #1a1a1a; border: none; border-radius: 0; margin-top: 22px; padding-top: 4px; font-weight: bold; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 0; padding: 4px 8px; background: #2f2f2f; color: #ffffff; }

QSplitter::handle { background: #0f0f0f; }
QToolTip { background: #1a1a1a; color: #ffffff; border: 1px solid #575757; padding: 4px; }
)";
	}

	void Apply(QApplication& app)
	{
		app.setStyle(QStyleFactory::create("Fusion"));

		QPalette palette;
		palette.setColor(QPalette::Window, Panel);
		palette.setColor(QPalette::WindowText, Foreground);
		palette.setColor(QPalette::Base, QColor(0x1a, 0x1a, 0x1a));
		palette.setColor(QPalette::AlternateBase, QColor(0x1e, 0x1e, 0x1e));
		palette.setColor(QPalette::Text, Foreground);
		palette.setColor(QPalette::Button, QColor(0x38, 0x38, 0x38));
		palette.setColor(QPalette::ButtonText, Foreground);
		palette.setColor(QPalette::BrightText, Bright);
		palette.setColor(QPalette::Highlight, Accent);
		palette.setColor(QPalette::HighlightedText, Bright);
		palette.setColor(QPalette::ToolTipBase, QColor(0x1a, 0x1a, 0x1a));
		palette.setColor(QPalette::ToolTipText, Bright);
		palette.setColor(QPalette::PlaceholderText, QColor(0x6a, 0x6a, 0x6a));
		palette.setColor(QPalette::Link, Accent);
		palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x5a, 0x5a, 0x5a));
		palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x5a, 0x5a, 0x5a));
		palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x5a, 0x5a, 0x5a));
		app.setPalette(palette);

		// UE uses a small, dense font. The system UI font at 12px is close on every OS.
		QFont font = app.font();
		font.setPixelSize(12);
		app.setFont(font);

		app.setStyleSheet(StyleSheet);
	}
}
