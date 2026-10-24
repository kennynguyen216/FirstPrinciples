#include "Editor/DockTab.h"
#include "Editor/EditorStyle.h"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>

namespace FP::Editor
{
	namespace
	{
		constexpr int StripHeight = 30;
		constexpr int TabPadding = 10;
		constexpr int IconSize = 14;
		constexpr int CloseSize = 18;
	}

	DockTab::DockTab(QDockWidget* dock, EditorIcon icon) : QWidget(dock), m_Dock(dock), m_Icon(icon)
	{
		QHBoxLayout* layout = new QHBoxLayout(this);
		layout->setContentsMargins(0, 4, 0, 0);
		layout->setSpacing(0);

		// The close button sits inside the tab, just after the title.
		QToolButton* close = new QToolButton(this);
		close->setIcon(EditorIcons::Get(EditorIcon::Close));
		close->setIconSize({ 10, 10 });
		close->setFixedSize(CloseSize, CloseSize);
		close->setAutoRaise(true);
		close->setStyleSheet("QToolButton { background: transparent; border: none; padding: 0; } QToolButton:hover { background: #575757; border-radius: 3px; }");
		close->setToolTip("Close");
		connect(close, &QToolButton::clicked, dock, &QDockWidget::close);

		const int titleWidth = fontMetrics().horizontalAdvance(dock->windowTitle());
		layout->addSpacing(TabPadding + IconSize + 6 + titleWidth + 8);
		layout->addWidget(close);
		layout->addStretch(1);
	}

	QSize DockTab::sizeHint() const
	{
		return { 100, StripHeight };
	}

	void DockTab::paintEvent(QPaintEvent*)
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.fillRect(rect(), EditorStyle::Background);

		// The tab itself, joined to the panel below it.
		const int titleWidth = fontMetrics().horizontalAdvance(m_Dock->windowTitle());
		const int tabWidth = TabPadding + IconSize + 6 + titleWidth + 8 + CloseSize + TabPadding;
		QPainterPath tab;
		tab.addRoundedRect(QRectF(0, 4, tabWidth, height() + 6), 5, 5);
		painter.fillPath(tab, EditorStyle::Panel);

		const int middle = 4 + (height() - 4) / 2;
		EditorIcons::Get(m_Icon).paint(&painter, QRect(TabPadding, middle - IconSize / 2, IconSize, IconSize));
		painter.setPen(EditorStyle::Bright);
		painter.drawText(QRect(TabPadding + IconSize + 6, 4, titleWidth + 4, height() - 4), Qt::AlignVCenter | Qt::AlignLeft, m_Dock->windowTitle());
	}

	void DockTab::mouseDoubleClickEvent(QMouseEvent*)
	{
		// Same as the default title bar: double click floats or docks.
		m_Dock->setFloating(!m_Dock->isFloating());
	}

	QDockWidget* DockTab::Create(QWidget* parent, const QString& title, EditorIcon icon, QWidget* content)
	{
		QDockWidget* dock = new QDockWidget(title, parent);
		dock->setObjectName(title);
		dock->setWidget(content);
		dock->setWindowIcon(EditorIcons::Get(icon)); // shown on the tab when docks share a spot
		dock->setTitleBarWidget(new DockTab(dock, icon));
		return dock;
	}
}
