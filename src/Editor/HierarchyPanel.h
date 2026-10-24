#pragma once

#include "Engine/World.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QTreeWidget;

namespace FP::Editor
{
	// Scene hierarchy: every entity under the scene's world row with a label and type column,
	// a search box on top and a count at the bottom.
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

	private:
		void ApplyFilter();
		void UpdateFooter();

		QLineEdit* m_Search = nullptr;
		QTreeWidget* m_Tree = nullptr;
		QLabel* m_Footer = nullptr;
		int m_Count = 0;
	};
}
