#pragma once

#include "Engine/ComponentRegistry.h"
#include "Engine/World.h"

#include <QWidget>

#include <functional>
#include <vector>

class QFormLayout;
class QVBoxLayout;

namespace FP::Editor
{
	// Builds editing widgets for the selected entity from the registered component fields.
	// No per component UI code is needed. Register a field and it shows up here.
	class InspectorPanel : public QWidget
	{
		Q_OBJECT

	public:
		explicit InspectorPanel(QWidget* parent = nullptr);

		// Rebuilds the widgets. Call when the selection or the set of components changes.
		void SetTarget(World* world, Entity entity);

		// Pulls current values into the widgets. Skips the widget being edited.
		void RefreshValues();

	signals:
		// A field was edited. Name changes matter to the hierarchy.
		void Modified();

	private:
		void Rebuild();
		void AddFieldEditor(QFormLayout* form, const ComponentInfo& info, const FieldInfo& field);

		World* m_World = nullptr;
		Entity m_Entity = NullEntity;
		QWidget* m_Content = nullptr;
		QVBoxLayout* m_Layout = nullptr;
		std::vector<std::function<void()>> m_Refreshers;
	};
}
