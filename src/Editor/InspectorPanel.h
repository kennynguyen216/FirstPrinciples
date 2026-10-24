#pragma once

#include "Editor/EditorIcons.h"
#include "Engine/ComponentRegistry.h"
#include "Engine/World.h"
#include "Engine/Audio/AudioTypes.h"
#include "Engine/Scripting/ScriptComponent.h"

#include <QWidget>

#include <deque>
#include <functional>
#include <string>
#include <vector>

class QFormLayout;
class QLabel;
class QLineEdit;
class QToolButton;
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
		~InspectorPanel() override;

		// Rebuilds the widgets. Call when the selection or the set of components changes.
		void SetTarget(World* world, Entity entity);

		// Shows an asset file from the content browser instead of an entity. Absolute path.
		void SetAsset(const QString& path);

		// Pulls current values into the widgets. Skips the widget being edited.
		void RefreshValues();
		void StopPreview();

	signals:
		// A field was edited. Name changes matter to the hierarchy.
		void Modified();
		void ImportAudioRequested();

		// The user asked to edit a script's source file.
		void OpenScriptRequested(const QString& path);

	public:
		using DataFn = std::function<void*()>;
		using ChangedFn = std::function<void()>;

		// Adds a row that edits one reflected field. data is called every time because the
		// object can move in memory. changed runs after each edit. The refresher is stored so
		// RefreshValues keeps the row up to date.
		void AddFieldEditor(QFormLayout* form, const FieldInfo& field, DataFn data, ChangedFn changed);

	private:
		void Rebuild();
		void RebuildLater();
		void SetTitle(const QString& text, EditorIcon icon);
		void ApplyFilter();
		void AddMaterialSection();
		void AddMaterialSlots(QFormLayout* form);
		void AddAnimatorClips(QFormLayout* form);
		void AddAudioSection();
		void AddMaterialEditor(const String& contentPath, const QString& title);
		void AddAssetView();
		void AddScriptsSection(QVBoxLayout* layout);
		ScriptSlot* SlotAt(int32 index) const;

		// Throwaway instances that hold a script's field values while editing outside play.
		void ClearEditScripts();

		World* m_World = nullptr;
		Entity m_Entity = NullEntity;
		QWidget* m_Content = nullptr;
		QLabel* m_TitleIcon = nullptr;
		QLabel* m_Title = nullptr;
		QToolButton* m_AddButton = nullptr;
		QLineEdit* m_Filter = nullptr;
		QVBoxLayout* m_Layout = nullptr;
		std::vector<std::function<void()>> m_Refreshers;
		std::deque<std::string> m_SlotLabels; // deque so the label pointers stay put
		VoiceHandle m_Preview;

		struct EditScript
		{
			const ScriptType* Type;
			Script* Instance;
		};
		std::vector<EditScript> m_EditScripts;
		int32 m_RuntimeListener = 0;
		bool m_ScriptsUnloaded = false;
		QString m_AssetPath;
	};
}
