#include "Editor/PlaceEntitiesPanel.h"
#include "Editor/EditorIcons.h"
#include "Editor/EditorStyle.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

namespace FP::Editor
{
	namespace
	{
		constexpr int KindRole = Qt::UserRole + 1;
		constexpr int IconRole = Qt::UserRole + 2;
		constexpr int RowHeight = 60;

		struct EntityEntry
		{
			const char* Kind; // passed to EditorWindow::CreateEntity
			const char* Category;
			EditorIcon Icon;
			const char* Help;
		};

		const EntityEntry Entries[] = {
			{ "Empty Entity", "Basic", EditorIcon::Entity, "An entity with only a name and transform" },
			{ "Sprite", "Basic", EditorIcon::Sprite, "A textured quad for 2D games" },
			{ "Camera", "Basic", EditorIcon::Camera, "The view the game renders from" },
			{ "Point Light", "Lights", EditorIcon::PointLight, "Light that shines in every direction from a point" },
			{ "Directional Light", "Lights", EditorIcon::DirectionalLight, "Sun style light that comes from one direction" },
			{ "Ambient Light", "Lights", EditorIcon::AmbientLight, "Flat light that fills every shadow" },
			{ "Cube", "Shapes", EditorIcon::Mesh, "A 1 unit cube mesh" },
			{ "Sphere", "Shapes", EditorIcon::Entity, "A sphere mesh with a 0.5 unit radius" },
			{ "Capsule", "Shapes", EditorIcon::Entity, "A pill 2 units tall and 1 wide" },
			{ "Plane", "Shapes", EditorIcon::Grid, "A flat 10 unit ground plane" },
			{ "Quad", "Shapes", EditorIcon::Sprite, "A 1 unit quad facing forward" },
		};

		struct Category
		{
			const char* Name;
			EditorIcon Icon;
		};

		const Category Categories[] = {
			{ "All", EditorIcon::PlaceEntities },
			{ "Basic", EditorIcon::Entity },
			{ "Lights", EditorIcon::PointLight },
			{ "Shapes", EditorIcon::Mesh },
		};

		// Carries the entity kind when dragged so the viewport can place it under the mouse.
		class EntityList : public QListWidget
		{
		public:
			using QListWidget::QListWidget;

		protected:
			QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override
			{
				QMimeData* data = new QMimeData();
				if (!items.isEmpty())
					data->setData(PlaceEntitiesPanel::MimeType, items.first()->data(KindRole).toString().toUtf8());
				return data;
			}

			QStringList mimeTypes() const override { return { PlaceEntitiesPanel::MimeType }; }
		};

		// Draws a row like UE: a dark tile with a large icon, the name, then a help badge.
		class EntityDelegate : public QStyledItemDelegate
		{
		public:
			using QStyledItemDelegate::QStyledItemDelegate;

			QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override
			{
				return { option.rect.width(), RowHeight + 6 };
			}

			void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
			{
				painter->save();
				painter->setRenderHint(QPainter::Antialiasing);
				const QRect row = option.rect.adjusted(4, 3, -4, -3);
				const bool hovered = option.state & QStyle::State_MouseOver;
				const bool selected = option.state & QStyle::State_Selected;

				QPainterPath shape;
				shape.addRoundedRect(row, 4, 4);
				painter->fillPath(shape, selected ? EditorStyle::Selection : hovered ? QColor(0x3a, 0x3a, 0x3a) : QColor(0x2f, 0x2f, 0x2f));

				// Icon tile on the left.
				const QRect tile(row.left(), row.top(), row.height() + 8, row.height());
				QPainterPath tilePath;
				tilePath.addRoundedRect(tile, 4, 4);
				painter->fillPath(tilePath, QColor(0x1a, 0x1a, 0x1a));
				const EditorIcon icon = static_cast<EditorIcon>(index.data(IconRole).toInt());
				const int iconSize = 34;
				EditorIcons::Get(icon).paint(painter, QRect(tile.center().x() - iconSize / 2, tile.center().y() - iconSize / 2, iconSize, iconSize));

				QFont font = option.font;
				font.setPixelSize(13);
				painter->setFont(font);
				painter->setPen(EditorStyle::Bright);
				const QRect text(tile.right() + 12, row.top(), row.width() - tile.width() - 48, row.height());
				const QString name = QFontMetrics(font).elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, text.width());
				painter->drawText(text, Qt::AlignVCenter | Qt::AlignLeft, name);

				const int help = 18;
				EditorIcons::Get(EditorIcon::Help).paint(painter, QRect(row.right() - help - 12, row.center().y() - help / 2, help, help));
				painter->restore();
			}
		};
	}

	PlaceEntitiesPanel::PlaceEntitiesPanel(QWidget* parent) : QWidget(parent)
	{
		QVBoxLayout* layout = new QVBoxLayout(this);
		layout->setContentsMargins(6, 6, 6, 6);
		layout->setSpacing(6);

		m_Search = new QLineEdit();
		m_Search->setPlaceholderText("Search");
		m_Search->setProperty("search", true);
		m_Search->addAction(EditorIcons::Get(EditorIcon::Search), QLineEdit::LeadingPosition);
		m_Search->setClearButtonEnabled(true);
		layout->addWidget(m_Search);

		// Category buttons filter the list, like UE's row of category icons.
		QHBoxLayout* categories = new QHBoxLayout();
		categories->setSpacing(2);
		m_Categories = new QButtonGroup(this);
		for (int i = 0; i < static_cast<int>(std::size(Categories)); ++i)
		{
			QToolButton* button = new QToolButton();
			button->setIcon(EditorIcons::Get(Categories[i].Icon));
			button->setIconSize({ 18, 18 });
			button->setCheckable(true);
			button->setToolTip(Categories[i].Name);
			button->setStyleSheet("QToolButton { background: transparent; border: 1px solid transparent; padding: 3px; } "
				"QToolButton:hover { background: #383838; } QToolButton:checked { background: #0f0f0f; border-color: #0070e0; }");
			m_Categories->addButton(button, i);
			categories->addWidget(button);
		}
		categories->addStretch(1);
		layout->addLayout(categories);
		m_Categories->button(0)->setChecked(true);

		m_CategoryLabel = new QLabel();
		m_CategoryLabel->setAlignment(Qt::AlignCenter);
		m_CategoryLabel->setStyleSheet("font-weight: bold; color: #c0c0c0; padding: 4px;");
		layout->addWidget(m_CategoryLabel);

		m_List = new EntityList();
		m_List->setItemDelegate(new EntityDelegate(m_List));
		m_List->setMouseTracking(true);
		m_List->setDragEnabled(true);
		m_List->setDragDropMode(QAbstractItemView::DragOnly);
		m_List->setStyleSheet("QListWidget { background: transparent; }");
		m_List->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
		layout->addWidget(m_List, 1);

		connect(m_Search, &QLineEdit::textChanged, this, &PlaceEntitiesPanel::Rebuild);
		connect(m_Categories, &QButtonGroup::idClicked, this, &PlaceEntitiesPanel::Rebuild);
		connect(m_List, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
			emit CreateRequested(item->data(KindRole).toString());
			m_List->clearSelection();
		});
		Rebuild();
	}

	void PlaceEntitiesPanel::Rebuild()
	{
		const QString category = Categories[std::max(0, m_Categories->checkedId())].Name;
		m_CategoryLabel->setText(category.toUpper());
		const QString filter = m_Search->text().trimmed();
		m_List->clear();
		for (const EntityEntry& entry : Entries)
		{
			if (category != "All" && category != entry.Category)
				continue;
			if (!filter.isEmpty() && !QString(entry.Kind).contains(filter, Qt::CaseInsensitive))
				continue;
			QListWidgetItem* item = new QListWidgetItem(entry.Kind, m_List);
			item->setData(KindRole, QString(entry.Kind));
			item->setData(IconRole, static_cast<int>(entry.Icon));
			item->setToolTip(entry.Help);
		}
	}
}
