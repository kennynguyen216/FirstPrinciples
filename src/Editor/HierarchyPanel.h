#pragma once

#include "Engine/World.h"

#include <QSet>
#include <QWidget>

class QLabel;
class QLineEdit;
class QTreeWidgetItem;

namespace FP::Editor
{
	class HierarchyTree;

	// Scene hierarchy: every entity under the scene's world row, nested under its parent, with a
	// label and type column, a search box on top and a count at the bottom. Drag an entity onto
	// another to make it a child, or onto the world row to make it top level again.
	class HierarchyPanel : public QWidget
	{
		Q_OBJECT

	public:
		explicit HierarchyPanel(QWidget* parent = nullptr);

		// Lists the world's entities again. sceneName labels the top row.
		void Rebuild(World& world, const QString& sceneName);

		// Highlights an entity without emitting EntityClicked. A null Guid clears it.
		void SetSelected(const Guid& id);

		// What kind of entity this is, from its components. Shown in the Type column.
		static QString TypeName(World& world, Entity entity);

	signals:
		void EntityClicked(const FP::Guid& id);

		// The scene row at the top, which stands for the scene entity and its scripts.
		void SceneClicked();

		// parent is a null Guid to make child top level.
		void ParentRequested(const FP::Guid& child, const FP::Guid& parent);
		void CreateChildRequested(const FP::Guid& parent);

	private:
		void ApplyFilter();
		void UpdateFooter();
		void ShowContextMenu(const QPoint& position);

		QLineEdit* m_Search = nullptr;
		HierarchyTree* m_Tree = nullptr;
		QLabel* m_Footer = nullptr;
		int m_Count = 0;
		QSet<QString> m_Collapsed; // ids of entities whose children are folded away
	};
}
