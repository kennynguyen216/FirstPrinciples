#include "Editor/OutlinerPanel.h"
#include "Editor/EditorIcons.h"
#include "Engine/Components.h"

#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace FP::Editor
{
	namespace
	{
		constexpr int GuidRole = Qt::UserRole + 1;
		enum Column { LabelColumn, TypeColumn };

		EditorIcon IconForType(const QString& type)
		{
			if (type == "Camera") return EditorIcon::Camera;
			if (type == "Point Light") return EditorIcon::PointLight;
			if (type == "Directional Light") return EditorIcon::DirectionalLight;
			if (type == "Ambient Light") return EditorIcon::AmbientLight;
			if (type == "Mesh") return EditorIcon::Mesh;
			if (type == "Sprite") return EditorIcon::Sprite;
			return EditorIcon::Entity;
		}

		QString ToQString(const String& text) { return QString::fromUtf8(text.CStr(), text.Length()); }
	}

	OutlinerPanel::OutlinerPanel(QWidget* parent) : QWidget(parent)
	{
		QVBoxLayout* layout = new QVBoxLayout(this);
		layout->setContentsMargins(6, 6, 6, 6);
		layout->setSpacing(6);

		m_Search = new QLineEdit();
		m_Search->setPlaceholderText("Search...");
		m_Search->setProperty("search", true);
		m_Search->addAction(EditorIcons::Get(EditorIcon::Search), QLineEdit::LeadingPosition);
		m_Search->setClearButtonEnabled(true);
		layout->addWidget(m_Search);

		m_Tree = new QTreeWidget();
		m_Tree->setColumnCount(2);
		m_Tree->setHeaderLabels({ "Item Label", "Type" });
		m_Tree->header()->setSectionResizeMode(LabelColumn, QHeaderView::Stretch);
		m_Tree->header()->setSectionResizeMode(TypeColumn, QHeaderView::Interactive);
		m_Tree->header()->setStretchLastSection(false);
		m_Tree->header()->resizeSection(TypeColumn, 110);
		m_Tree->setRootIsDecorated(true);
		m_Tree->setIndentation(14);
		m_Tree->setUniformRowHeights(true);
		m_Tree->setIconSize({ 14, 14 });
		layout->addWidget(m_Tree, 1);

		m_Footer = new QLabel();
		m_Footer->setStyleSheet("padding: 2px 4px;");
		layout->addWidget(m_Footer);

		connect(m_Tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
			Guid id;
			if (item && Guid::Parse(item->data(LabelColumn, GuidRole).toString().toUtf8().constData(), id))
				emit EntityClicked(id);
			UpdateFooter();
		});
		connect(m_Search, &QLineEdit::textChanged, this, &OutlinerPanel::ApplyFilter);
	}

	QString OutlinerPanel::TypeName(World& world, Entity entity)
	{
		if (world.Has<CameraComponent>(entity)) return "Camera";
		if (world.Has<PointLightComponent>(entity)) return "Point Light";
		if (world.Has<DirectionalLightComponent>(entity)) return "Directional Light";
		if (world.Has<AmbientLightComponent>(entity)) return "Ambient Light";
		if (world.Has<MeshComponent>(entity)) return "Mesh";
		if (world.Has<SpriteComponent>(entity)) return "Sprite";
		return "Entity";
	}

	void OutlinerPanel::Rebuild(World& world, const QString& sceneName)
	{
		QSignalBlocker block(m_Tree);
		m_Tree->clear();

		QTreeWidgetItem* root = new QTreeWidgetItem(m_Tree);
		root->setText(LabelColumn, sceneName);
		root->setIcon(LabelColumn, EditorIcons::Get(EditorIcon::Level));
		root->setText(TypeColumn, "World");
		root->setFlags(Qt::ItemIsEnabled);

		const Array<Entity> entities = world.GetEntities();
		for (Entity entity : entities)
		{
			const QString type = TypeName(world, entity);
			QTreeWidgetItem* item = new QTreeWidgetItem(root);
			item->setText(LabelColumn, ToQString(world.GetEntityName(entity)));
			item->setIcon(LabelColumn, EditorIcons::Get(IconForType(type)));
			item->setText(TypeColumn, type);
			item->setForeground(TypeColumn, QColor(0x9a, 0x9a, 0x9a));
			item->setData(LabelColumn, GuidRole, ToQString(world.Get<IdComponent>(entity).Id.ToString()));
		}
		root->setExpanded(true);
		m_Count = entities.Num();
		ApplyFilter();
		UpdateFooter();
	}

	void OutlinerPanel::SetSelected(const Guid& id)
	{
		QSignalBlocker block(m_Tree);
		m_Tree->setCurrentItem(nullptr);
		m_Tree->clearSelection();
		const QString key = ToQString(id.ToString());
		if (QTreeWidgetItem* root = m_Tree->topLevelItem(0))
		{
			for (int i = 0; i < root->childCount(); ++i)
			{
				if (root->child(i)->data(LabelColumn, GuidRole).toString() == key)
					m_Tree->setCurrentItem(root->child(i));
			}
		}
		UpdateFooter();
	}

	void OutlinerPanel::ApplyFilter()
	{
		const QString filter = m_Search->text().trimmed();
		if (QTreeWidgetItem* root = m_Tree->topLevelItem(0))
		{
			for (int i = 0; i < root->childCount(); ++i)
			{
				QTreeWidgetItem* item = root->child(i);
				item->setHidden(!filter.isEmpty() && !item->text(LabelColumn).contains(filter, Qt::CaseInsensitive)
					&& !item->text(TypeColumn).contains(filter, Qt::CaseInsensitive));
			}
		}
	}

	void OutlinerPanel::UpdateFooter()
	{
		const int selected = m_Tree->currentItem() && m_Tree->currentItem()->parent() ? 1 : 0;
		m_Footer->setText(QString("%1 entities (%2 selected)").arg(m_Count).arg(selected));
	}
}
