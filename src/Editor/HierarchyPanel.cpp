#include "Editor/HierarchyPanel.h"
#include "Editor/EditorIcons.h"
#include "Engine/Components.h"

#include <QDropEvent>
#include <QHeaderView>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

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
			if (type == "Spot Light") return EditorIcon::DirectionalLight;
			if (type == "Directional Light") return EditorIcon::DirectionalLight;
			if (type == "Ambient Light") return EditorIcon::AmbientLight;
			if (type == "Mesh" || type == "Skeletal Mesh") return EditorIcon::Mesh;
			if (type == "Sprite") return EditorIcon::Sprite;
			return EditorIcon::Entity;
		}

		QString ToQString(const String& text) { return QString::fromUtf8(text.CStr(), text.Length()); }

		Guid ItemGuid(const QTreeWidgetItem* item)
		{
			Guid id;
			if (item)
				Guid::Parse(item->data(LabelColumn, GuidRole).toString().toUtf8().constData(), id);
			return id;
		}

		void ForEachItem(QTreeWidgetItem* item, const std::function<void(QTreeWidgetItem*)>& fn)
		{
			for (int i = 0; i < item->childCount(); ++i)
			{
				fn(item->child(i));
				ForEachItem(item->child(i), fn);
			}
		}
	}

	// Turns drops into parent requests instead of letting Qt move the rows itself, so the
	// world stays the only source of truth. The panel rebuilds from it afterwards.
	class HierarchyTree : public QTreeWidget
	{
	public:
		std::function<void(const Guid& child, const Guid& parent)> OnDropped;

	protected:
		void dropEvent(QDropEvent* event) override
		{
			const QTreeWidgetItem* dragged = currentItem();
			QTreeWidgetItem* target = itemAt(event->position().toPoint());
			event->setDropAction(Qt::IgnoreAction);
			event->accept();
			if (!dragged || !dragged->parent())
				return;
			// On a row makes a child of it. Between rows makes a sibling. Empty space or the world row is top level.
			QTreeWidgetItem* parent = target;
			if (target && dropIndicatorPosition() != QAbstractItemView::OnItem)
				parent = target->parent();
			const Guid parentId = parent ? ItemGuid(parent) : Guid();
			if (OnDropped)
				OnDropped(ItemGuid(dragged), parentId);
		}
	};

	HierarchyPanel::HierarchyPanel(QWidget* parent) : QWidget(parent)
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

		m_Tree = new HierarchyTree();
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
		m_Tree->setDragDropMode(QAbstractItemView::InternalMove);
		m_Tree->setDragEnabled(true);
		m_Tree->setAcceptDrops(true);
		m_Tree->setDropIndicatorShown(true);
		m_Tree->setContextMenuPolicy(Qt::CustomContextMenu);
		m_Tree->OnDropped = [this](const Guid& child, const Guid& parent) { emit ParentRequested(child, parent); };
		layout->addWidget(m_Tree, 1);

		m_Footer = new QLabel();
		m_Footer->setStyleSheet("padding: 2px 4px;");
		layout->addWidget(m_Footer);

		connect(m_Tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
			const Guid id = ItemGuid(item);
			if (item && !item->parent())
				emit SceneClicked();
			else if (id != Guid())
				emit EntityClicked(id);
			UpdateFooter();
		});
		connect(m_Tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) { m_Collapsed.insert(item->data(LabelColumn, GuidRole).toString()); });
		connect(m_Tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) { m_Collapsed.remove(item->data(LabelColumn, GuidRole).toString()); });
		connect(m_Tree, &QTreeWidget::customContextMenuRequested, this, &HierarchyPanel::ShowContextMenu);
		connect(m_Search, &QLineEdit::textChanged, this, &HierarchyPanel::ApplyFilter);
	}

	QString HierarchyPanel::TypeName(World& world, Entity entity)
	{
		if (world.Has<CameraComponent>(entity)) return "Camera";
		if (world.Has<PointLightComponent>(entity)) return "Point Light";
		if (world.Has<SpotLightComponent>(entity)) return "Spot Light";
		if (world.Has<DirectionalLightComponent>(entity)) return "Directional Light";
		if (world.Has<AmbientLightComponent>(entity)) return "Ambient Light";
		if (world.Has<MeshComponent>(entity)) return "Mesh";
		if (world.Has<SkeletalMeshComponent>(entity)) return "Skeletal Mesh";
		if (world.Has<SpriteComponent>(entity)) return "Sprite";
		return "Entity";
	}

	void HierarchyPanel::Rebuild(World& world, const QString& sceneName)
	{
		QSignalBlocker block(m_Tree);
		m_Tree->clear();

		QTreeWidgetItem* root = new QTreeWidgetItem(m_Tree);
		root->setText(LabelColumn, sceneName);
		root->setIcon(LabelColumn, EditorIcons::Get(EditorIcon::Level));
		root->setText(TypeColumn, "Scene");
		root->setToolTip(LabelColumn, "Click to add scene scripts, which run for the whole scene");
		root->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled);
		// The scene entity is this row, so it is not listed again below.
		const Entity scene = world.GetSceneEntity();
		if (scene != NullEntity)
			root->setData(LabelColumn, GuidRole, ToQString(world.Get<IdComponent>(scene).Id.ToString()));

		// Make every row first, then hang each under its parent so order in the file does not matter.
		const Array<Entity> entities = world.GetEntities();
		QHash<QString, QTreeWidgetItem*> items;
		for (Entity entity : entities)
		{
			if (entity == scene)
				continue;
			const QString type = TypeName(world, entity);
			QTreeWidgetItem* item = new QTreeWidgetItem();
			item->setText(LabelColumn, ToQString(world.GetEntityName(entity)));
			item->setIcon(LabelColumn, EditorIcons::Get(IconForType(type)));
			item->setText(TypeColumn, type);
			item->setForeground(TypeColumn, QColor(0x9a, 0x9a, 0x9a));
			item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
			const QString id = ToQString(world.Get<IdComponent>(entity).Id.ToString());
			item->setData(LabelColumn, GuidRole, id);
			items.insert(id, item);
		}
		for (Entity entity : entities)
		{
			if (entity == scene)
				continue;
			QTreeWidgetItem* item = items.value(ToQString(world.Get<IdComponent>(entity).Id.ToString()));
			const Entity parent = world.GetParent(entity);
			QTreeWidgetItem* parentItem = parent != NullEntity ? items.value(ToQString(world.Get<IdComponent>(parent).Id.ToString())) : nullptr;
			(parentItem ? parentItem : root)->addChild(item);
		}
		root->setExpanded(true);
		ForEachItem(root, [this](QTreeWidgetItem* item) { item->setExpanded(!m_Collapsed.contains(item->data(LabelColumn, GuidRole).toString())); });
		m_Count = entities.Num() - (scene != NullEntity ? 1 : 0);
		ApplyFilter();
		UpdateFooter();
	}

	void HierarchyPanel::SetSelected(const Guid& id)
	{
		QSignalBlocker block(m_Tree);
		m_Tree->setCurrentItem(nullptr);
		m_Tree->clearSelection();
		const QString key = ToQString(id.ToString());
		if (QTreeWidgetItem* root = m_Tree->topLevelItem(0))
		{
			if (!key.isEmpty() && root->data(LabelColumn, GuidRole).toString() == key)
				m_Tree->setCurrentItem(root);
			ForEachItem(root, [this, &key](QTreeWidgetItem* item) {
				if (item->data(LabelColumn, GuidRole).toString() == key)
				{
					// Unfold the parents so the selection is visible.
					for (QTreeWidgetItem* up = item->parent(); up; up = up->parent())
						up->setExpanded(true);
					m_Tree->setCurrentItem(item);
				}
			});
		}
		UpdateFooter();
	}

	void HierarchyPanel::ApplyFilter()
	{
		const QString filter = m_Search->text().trimmed();
		// A row stays visible when it or anything under it matches, so matches keep their path.
		std::function<bool(QTreeWidgetItem*)> filterItem = [&](QTreeWidgetItem* item) {
			bool anyChild = false;
			for (int i = 0; i < item->childCount(); ++i)
				anyChild |= filterItem(item->child(i));
			const bool matches = filter.isEmpty() || item->text(LabelColumn).contains(filter, Qt::CaseInsensitive)
				|| item->text(TypeColumn).contains(filter, Qt::CaseInsensitive);
			item->setHidden(!matches && !anyChild);
			if (!filter.isEmpty() && anyChild)
				item->setExpanded(true);
			return matches || anyChild;
		};
		if (QTreeWidgetItem* root = m_Tree->topLevelItem(0))
		{
			for (int i = 0; i < root->childCount(); ++i)
				filterItem(root->child(i));
		}
	}

	void HierarchyPanel::ShowContextMenu(const QPoint& position)
	{
		QTreeWidgetItem* item = m_Tree->itemAt(position);
		const Guid id = ItemGuid(item);
		if (id == Guid())
			return;
		QMenu menu(this);
		menu.addAction("Create Child", this, [this, id]() { emit CreateChildRequested(id); });
		QAction* detach = menu.addAction("Detach from Parent", this, [this, id]() { emit ParentRequested(id, Guid()); });
		detach->setEnabled(item->parent() && item->parent()->parent());
		menu.exec(m_Tree->viewport()->mapToGlobal(position));
	}

	void HierarchyPanel::UpdateFooter()
	{
		const int selected = m_Tree->currentItem() && m_Tree->currentItem()->parent() ? 1 : 0;
		m_Footer->setText(QString("%1 entities (%2 selected)").arg(m_Count).arg(selected));
	}
}
