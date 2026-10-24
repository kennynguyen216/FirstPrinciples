#include "Editor/InspectorPanel.h"
#include "Engine/ComponentRegistry.h"
#include "Engine/Material.h"
#include "Engine/Paths.h"
#include "Engine/Scripting/ScriptRuntime.h"
#include "Editor/ContentBrowser.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QImage>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>

#include <cfloat>

namespace FP::Editor
{
	namespace
	{
		QDoubleSpinBox* MakeFloatBox()
		{
			QDoubleSpinBox* box = new QDoubleSpinBox();
			box->setRange(-1e9, 1e9);
			box->setDecimals(3);
			box->setSingleStep(0.1);
			box->setKeyboardTracking(false);
			box->setMinimumWidth(70);
			return box;
		}

		void SetSwatch(QPushButton* button, const Color& color)
		{
			const QColor qc = QColor::fromRgbF(color.R, color.G, color.B, color.A);
			button->setStyleSheet(QString("background-color: %1; border: 1px solid #555; min-height: 18px;").arg(qc.name()));
		}

		// Value editors that hold focus are being typed into, so refreshing them would fight the user.
		bool IsEditing(QWidget* widget)
		{
			return widget->hasFocus() || widget->isAncestorOf(QApplication::focusWidget());
		}
	}

	namespace
	{
		QString ToQString(const String& text) { return QString::fromUtf8(text.CStr(), text.Length()); }

		String ToString(const QString& text)
		{
			const QByteArray utf8 = text.toUtf8();
			return String(utf8.constData(), static_cast<int32>(utf8.size()));
		}

		// Line edit for asset paths that takes files dropped from the content browser and stores
		// them relative to Content.
		class AssetLineEdit : public QLineEdit
		{
		public:
			using QLineEdit::QLineEdit;

		protected:
			void dragEnterEvent(QDragEnterEvent* event) override
			{
				if (RelativePath(event->mimeData()).isEmpty())
					QLineEdit::dragEnterEvent(event);
				else
					event->acceptProposedAction();
			}

			void dropEvent(QDropEvent* event) override
			{
				const QString relative = RelativePath(event->mimeData());
				if (relative.isEmpty())
				{
					QLineEdit::dropEvent(event);
					return;
				}
				setText(relative);
				emit editingFinished();
				event->acceptProposedAction();
			}

		private:
			static QString RelativePath(const QMimeData* data)
			{
				if (!data->hasUrls() || data->urls().isEmpty() || !data->urls().first().isLocalFile())
					return {};
				const QString relative = QDir(ToQString(Paths::GetContentDir())).relativeFilePath(data->urls().first().toLocalFile());
				return relative.startsWith("..") ? QString() : relative;
			}
		};

		// Spin boxes for Vec2 and Vec3 share this so every axis behaves the same.
		QWidget* MakeVectorRow(int count, QDoubleSpinBox** boxes)
		{
			static const char* const labels[3] = { "X ", "Y ", "Z " };
			QWidget* row = new QWidget();
			QHBoxLayout* layout = new QHBoxLayout(row);
			layout->setContentsMargins(0, 0, 0, 0);
			layout->setSpacing(4);
			for (int i = 0; i < count; ++i)
			{
				boxes[i] = MakeFloatBox();
				boxes[i]->setPrefix(labels[i]);
				boxes[i]->setMinimumWidth(56);
				layout->addWidget(boxes[i]);
			}
			return row;
		}
	}

	InspectorPanel::InspectorPanel(QWidget* parent) : QWidget(parent)
	{
		QVBoxLayout* outer = new QVBoxLayout(this);
		outer->setContentsMargins(0, 0, 0, 0);
		QScrollArea* scroll = new QScrollArea();
		scroll->setWidgetResizable(true);
		scroll->setFrameShape(QFrame::NoFrame);
		outer->addWidget(scroll);

		m_Content = new QWidget();
		m_Layout = new QVBoxLayout(m_Content);
		m_Layout->setAlignment(Qt::AlignTop);
		scroll->setWidget(m_Content);

		// Script field rows point into script code, so let go of them before the library unloads.
		m_RuntimeListener = ScriptRuntime::Get().AddListener([this](bool loaded) {
			m_ScriptsUnloaded = !loaded;
			Rebuild();
		});
		Rebuild();
	}

	InspectorPanel::~InspectorPanel()
	{
		ScriptRuntime::Get().RemoveListener(m_RuntimeListener);
		m_Refreshers.clear();
		ClearEditScripts();
	}

	void InspectorPanel::RebuildLater()
	{
		// Rebuilding deletes the button that was clicked, so wait until its click handler returns.
		QMetaObject::invokeMethod(this, &InspectorPanel::Rebuild, Qt::QueuedConnection);
	}

	void InspectorPanel::ClearEditScripts()
	{
		for (const EditScript& edit : m_EditScripts)
			ScriptRuntime::Get().DestroyInstance(*edit.Type, edit.Instance);
		m_EditScripts.clear();
	}

	ScriptSlot* InspectorPanel::SlotAt(int32 index) const
	{
		if (!m_World || !m_World->IsValid(m_Entity))
			return nullptr;
		ScriptComponent* scripts = m_World->TryGet<ScriptComponent>(m_Entity);
		return scripts && scripts->Scripts.IsValidIndex(index) ? &scripts->Scripts[index] : nullptr;
	}

	void InspectorPanel::AddScriptsSection(QVBoxLayout* layout)
	{
		if (m_ScriptsUnloaded)
		{
			QLabel* waiting = new QLabel("Scripts are reloading...");
			waiting->setEnabled(false);
			layout->addWidget(waiting);
			return;
		}

		ScriptRuntime& runtime = ScriptRuntime::Get();
		const ScriptComponent& component = m_World->Get<ScriptComponent>(m_Entity);
		for (int32 index = 0; index < component.Scripts.Num(); ++index)
		{
			const ScriptSlot& slot = component.Scripts[index];
			const ScriptType* type = runtime.FindType(slot.Name);

			QGroupBox* box = new QGroupBox(ToQString(slot.Name));
			QVBoxLayout* boxLayout = new QVBoxLayout(box);
			QFormLayout* form = new QFormLayout();
			form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
			form->setRowWrapPolicy(QFormLayout::WrapLongRows); // script field names are often long
			boxLayout->addLayout(form);

			if (!type)
			{
				QLabel* missing = new QLabel("Not compiled. Its saved values are kept.");
				missing->setEnabled(false);
				boxLayout->addWidget(missing);
			}
			else
			{
				// While playing the live instance is shown. Otherwise edits go to a throwaway
				// instance and are written back to the slot's saved text.
				Script* edit = runtime.CreateInstance(*type, m_World, m_Entity, slot.Properties.View());
				m_EditScripts.push_back({ type, edit });
				auto data = [this, index, edit]() -> void* {
					const ScriptSlot* current = SlotAt(index);
					return current && current->Instance ? current->Instance : edit;
				};
				auto changed = [this, index, type, edit]() {
					ScriptSlot* current = SlotAt(index);
					if (current && !current->Instance)
						current->Properties = ScriptRuntime::CaptureProperties(*type, edit);
					emit Modified();
				};
				for (const FieldInfo& field : type->Fields)
					AddFieldEditor(form, field, data, changed);
				if (type->Fields.IsEmpty())
				{
					QLabel* none = new QLabel("No exposed fields");
					none->setEnabled(false);
					boxLayout->addWidget(none);
				}
			}

			QHBoxLayout* buttons = new QHBoxLayout();
			buttons->addStretch();
			if (type && type->SourceFile)
			{
				QPushButton* editButton = new QPushButton("Edit");
				editButton->setFlat(true);
				const QString source = QString::fromUtf8(type->SourceFile);
				connect(editButton, &QPushButton::clicked, this, [this, source]() { emit OpenScriptRequested(source); });
				buttons->addWidget(editButton);
			}
			QPushButton* remove = new QPushButton("Remove");
			remove->setFlat(true);
			connect(remove, &QPushButton::clicked, this, [this, index]() {
				ScriptSlot* current = SlotAt(index);
				if (!current)
					return;
				if (current->Instance)
				{
					current->Instance->OnStop();
					ScriptRuntime::Get().DestroyInstance(*current->Type, current->Instance);
					current->Instance = nullptr;
				}
				m_World->Get<ScriptComponent>(m_Entity).Scripts.RemoveAt(index);
				emit Modified();
				RebuildLater();
			});
			buttons->addWidget(remove);
			boxLayout->addLayout(buttons);
			layout->addWidget(box);
		}

		QToolButton* add = new QToolButton();
		add->setText("Add Script");
		add->setPopupMode(QToolButton::InstantPopup);
		QMenu* menu = new QMenu(add);
		for (const ScriptType* type : runtime.GetTypes())
		{
			const String name = type->Name;
			menu->addAction(ToQString(name), this, [this, name]() {
				if (ScriptComponent* scripts = m_World->TryGet<ScriptComponent>(m_Entity))
				{
					scripts->Scripts.Emplace(name);
					emit Modified();
					RebuildLater();
				}
			});
		}
		add->setMenu(menu);
		add->setEnabled(!menu->isEmpty());
		if (menu->isEmpty())
			add->setToolTip("No scripts are compiled. Use Scripts > New Script.");
		layout->addWidget(add, 0, Qt::AlignLeft);
	}

	void InspectorPanel::SetTarget(World* world, Entity entity)
	{
		m_World = world;
		m_Entity = entity;
		m_AssetPath.clear();
		Rebuild();
	}

	void InspectorPanel::SetAsset(const QString& path)
	{
		m_Entity = NullEntity;
		m_AssetPath = path;
		Rebuild();
	}

	void InspectorPanel::AddAssetView()
	{
		const QFileInfo info(m_AssetPath);
		const QString kind = ContentBrowser::AssetKind(m_AssetPath);
		const QDir content(ToQString(Paths::GetContentDir()));
		const QString relative = content.relativeFilePath(m_AssetPath);

		if (kind == "Material" && !relative.startsWith(".."))
		{
			AddMaterialEditor(ToString(relative), QString("Material: %1").arg(info.completeBaseName()));
			return;
		}

		QGroupBox* group = new QGroupBox(QString("%1: %2").arg(kind, info.fileName()));
		QVBoxLayout* layout = new QVBoxLayout(group);
		QFormLayout* form = new QFormLayout();
		layout->addLayout(form);
		form->addRow("Path", new QLabel(QDir(ToQString(Paths::GetProjectDir())).relativeFilePath(m_AssetPath)));
		form->addRow("Size", new QLabel(QString("%1 KB").arg(QString::number(info.size() / 1024.0, 'f', 1))));

		if (kind == "Texture")
		{
			const QImage image(m_AssetPath);
			if (!image.isNull())
			{
				form->addRow("Pixels", new QLabel(QString("%1 x %2").arg(image.width()).arg(image.height())));
				form->addRow("Alpha", new QLabel(image.hasAlphaChannel() ? "Yes" : "No"));
				QLabel* preview = new QLabel();
				preview->setPixmap(QPixmap::fromImage(image.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
				preview->setAlignment(Qt::AlignCenter);
				layout->addWidget(preview);
			}
		}
		m_Layout->addWidget(group);
	}

	void InspectorPanel::AddMaterialSection()
	{
		const String path = m_World->Get<MaterialComponent>(m_Entity).Material;
		Material* material = MaterialLibrary::Get().Load(path);

		if (!material)
		{
			// No usable material yet, so offer to make one and assign it.
			QPushButton* create = new QPushButton("Create Material...");
			connect(create, &QPushButton::clicked, this, [this]() {
				const QDir content(ToQString(Paths::GetContentDir()));
				content.mkpath("Materials");
				QString picked = QFileDialog::getSaveFileName(this, "Create Material", content.filePath("Materials/NewMaterial.material"), "Materials (*.material)");
				if (picked.isEmpty())
					return;
				if (!picked.endsWith(".material"))
					picked += ".material";
				const QString relative = content.relativeFilePath(picked);
				if (relative.startsWith(".."))
				{
					FP_LOG_WARN("Materials must live inside Content");
					return;
				}
				if (MaterialLibrary::Get().Create(ToString(relative)))
				{
					m_World->Get<MaterialComponent>(m_Entity).Material = ToString(relative);
					emit Modified();
					RebuildLater();
				}
			});
			m_Layout->addWidget(create);
			return;
		}

		AddMaterialEditor(path, QString("Material Asset: %1").arg(ToQString(path)));
	}

	void InspectorPanel::AddMaterialEditor(const String& path, const QString& title)
	{
		// Edits change the shared asset, so every entity using it updates at once.
		QGroupBox* group = new QGroupBox(title);
		QVBoxLayout* groupLayout = new QVBoxLayout(group);
		QFormLayout* form = new QFormLayout();
		form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
		groupLayout->addLayout(form);

		const String materialPath = path;
		auto data = [materialPath]() -> void* {
			Material* m = MaterialLibrary::Get().Load(materialPath);
			return m ? &m->GetData() : nullptr;
		};
		auto changed = [this, materialPath]() {
			if (Material* m = MaterialLibrary::Get().Load(materialPath))
				m->MarkDirty();
			emit Modified();
		};
		for (const FieldInfo& field : Material::GetFields())
			AddFieldEditor(form, field, data, changed);

		QPushButton* save = new QPushButton("Save Material");
		connect(save, &QPushButton::clicked, this, [materialPath]() {
			if (Material* m = MaterialLibrary::Get().Load(materialPath))
				m->Save();
		});
		groupLayout->addWidget(save, 0, Qt::AlignRight);
		m_Layout->addWidget(group);
	}

	void InspectorPanel::RefreshValues()
	{
		// Assets have no entity but their rows still need values.
		if (m_AssetPath.isEmpty() && (!m_World || !m_World->IsValid(m_Entity)))
			return;
		for (auto& refresh : m_Refreshers)
			refresh();
	}

	void InspectorPanel::Rebuild()
	{
		m_Refreshers.clear();
		ClearEditScripts();
		while (QLayoutItem* item = m_Layout->takeAt(0))
		{
			delete item->widget();
			delete item;
		}

		if (!m_AssetPath.isEmpty())
		{
			AddAssetView();
			RefreshValues();
			return;
		}

		if (!m_World || !m_World->IsValid(m_Entity))
		{
			QLabel* empty = new QLabel("Nothing selected");
			empty->setEnabled(false);
			m_Layout->addWidget(empty);
			return;
		}

		entt::registry& registry = m_World->GetRegistry();
		QToolButton* add = new QToolButton();
		QMenu* addMenu = new QMenu(add);

		for (const ComponentInfo* info : ComponentRegistry::Get().GetAll())
		{
			if (!info->ShowInEditor)
				continue;
			if (!info->TryGet(registry, m_Entity))
			{
				addMenu->addAction(info->Name, this, [this, info]() {
					info->Add(m_World->GetRegistry(), m_Entity);
					emit Modified();
					RebuildLater();
				});
				continue;
			}

			QGroupBox* group = new QGroupBox(info->Name);
			QVBoxLayout* groupLayout = new QVBoxLayout(group);
			QFormLayout* form = new QFormLayout();
			form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
			groupLayout->addLayout(form);
			// Looks the component up every time because EnTT may move it when others are added.
			auto data = [this, info]() -> void* {
				return m_World && m_World->IsValid(m_Entity) ? info->TryGet(m_World->GetRegistry(), m_Entity) : nullptr;
			};
			if (info->CustomInspector && info == ComponentRegistry::Get().Find<ScriptComponent>())
				AddScriptsSection(groupLayout);
			for (const FieldInfo& field : info->Fields)
				AddFieldEditor(form, field, data, [this]() { emit Modified(); });

			QPushButton* remove = new QPushButton("Remove");
			remove->setFlat(true);
			connect(remove, &QPushButton::clicked, this, [this, info]() {
				info->Remove(m_World->GetRegistry(), m_Entity);
				emit Modified();
				RebuildLater();
			});
			groupLayout->addWidget(remove, 0, Qt::AlignRight);
			m_Layout->addWidget(group);

			if (info == ComponentRegistry::Get().Find<MaterialComponent>())
				AddMaterialSection();
		}

		add->setText("Add Component");
		add->setPopupMode(QToolButton::InstantPopup);
		add->setMenu(addMenu);
		add->setEnabled(!addMenu->isEmpty());
		m_Layout->addWidget(add);
		RefreshValues();
	}

	void InspectorPanel::AddFieldEditor(QFormLayout* form, const FieldInfo& field, DataFn data, ChangedFn changed)
	{
		const FieldInfo fieldCopy = field;

		switch (field.Type)
		{
			case FieldType::Bool:
			{
				QCheckBox* box = new QCheckBox();
				connect(box, &QCheckBox::toggled, this, [=](bool value) {
					if (void* c = data()) { fieldCopy.Get<bool>(c) = value; changed(); }
				});
				m_Refreshers.push_back([=]() {
					if (void* c = data()) { QSignalBlocker block(box); box->setChecked(fieldCopy.Get<bool>(c)); }
				});
				form->addRow(field.Name, box);
				break;
			}
			case FieldType::Int:
			{
				QSpinBox* box = new QSpinBox();
				box->setRange(INT32_MIN, INT32_MAX);
				box->setKeyboardTracking(false);
				connect(box, &QSpinBox::valueChanged, this, [=](int value) {
					if (void* c = data()) { fieldCopy.Get<int32>(c) = value; changed(); }
				});
				m_Refreshers.push_back([=]() {
					if (void* c = data(); c && !IsEditing(box)) { QSignalBlocker block(box); box->setValue(fieldCopy.Get<int32>(c)); }
				});
				form->addRow(field.Name, box);
				break;
			}
			case FieldType::Float:
			{
				QDoubleSpinBox* box = MakeFloatBox();
				connect(box, &QDoubleSpinBox::valueChanged, this, [=](double value) {
					if (void* c = data()) { fieldCopy.Get<float>(c) = static_cast<float>(value); changed(); }
				});
				m_Refreshers.push_back([=]() {
					if (void* c = data(); c && !IsEditing(box)) { QSignalBlocker block(box); box->setValue(fieldCopy.Get<float>(c)); }
				});
				form->addRow(field.Name, box);
				break;
			}
			case FieldType::Vec2:
			case FieldType::Vec3:
			{
				// Vec2 and Vec3 are both runs of floats so one editor handles both.
				const int count = field.Type == FieldType::Vec2 ? 2 : 3;
				QDoubleSpinBox* boxes[3] = {};
				QWidget* row = MakeVectorRow(count, boxes);
				for (int i = 0; i < count; ++i)
				{
					connect(boxes[i], &QDoubleSpinBox::valueChanged, this, [=](double value) {
						if (void* c = data()) { fieldCopy.Get<float[3]>(c)[i] = static_cast<float>(value); changed(); }
					});
				}
				m_Refreshers.push_back([=]() {
					void* c = data();
					if (!c)
						return;
					for (int i = 0; i < count; ++i)
					{
						if (!IsEditing(boxes[i])) { QSignalBlocker block(boxes[i]); boxes[i]->setValue(fieldCopy.Get<float[3]>(c)[i]); }
					}
				});
				form->addRow(field.Name, row);
				break;
			}
			case FieldType::Color:
			{
				QPushButton* button = new QPushButton();
				connect(button, &QPushButton::clicked, this, [=, this]() {
					void* c = data();
					if (!c)
						return;
					const Color current = fieldCopy.Get<Color>(c);
					const QColor picked = QColorDialog::getColor(QColor::fromRgbF(current.R, current.G, current.B, current.A),
						this, fieldCopy.Name, QColorDialog::ShowAlphaChannel);
					if (!picked.isValid())
						return;
					if (void* again = data())
					{
						fieldCopy.Get<Color>(again) = { static_cast<float>(picked.redF()), static_cast<float>(picked.greenF()),
							static_cast<float>(picked.blueF()), static_cast<float>(picked.alphaF()) };
						SetSwatch(button, fieldCopy.Get<Color>(again));
						changed();
					}
				});
				m_Refreshers.push_back([=]() {
					if (void* c = data())
						SetSwatch(button, fieldCopy.Get<Color>(c));
				});
				form->addRow(field.Name, button);
				break;
			}
			case FieldType::String:
			{
				auto apply = [=](const QString& text) {
					if (void* c = data())
					{
						String& value = fieldCopy.Get<String>(c);
						const String next = ToString(text);
						if (value != next.View())
						{
							value = next;
							changed();
						}
					}
				};

				if (field.Choices && !field.ChoicesEditable)
				{
					QComboBox* combo = new QComboBox();
					for (const char* choice : *field.Choices)
						combo->addItem(choice);
					connect(combo, &QComboBox::currentTextChanged, this, apply);
					m_Refreshers.push_back([=]() {
						if (void* c = data(); c && !IsEditing(combo))
						{
							QSignalBlocker block(combo);
							combo->setCurrentText(ToQString(fieldCopy.Get<String>(c)));
						}
					});
					form->addRow(field.Name, combo);
					break;
				}

				QWidget* row = new QWidget();
				QHBoxLayout* layout = new QHBoxLayout(row);
				layout->setContentsMargins(0, 0, 0, 0);
				layout->setSpacing(4);

				QLineEdit* edit = nullptr;
				QComboBox* combo = nullptr;
				if (field.Choices)
				{
					// Editable list: pick a built in name or type a path.
					combo = new QComboBox();
					combo->setEditable(true);
					combo->setLineEdit(new AssetLineEdit());
					combo->setInsertPolicy(QComboBox::NoInsert);
					for (const char* choice : *field.Choices)
						combo->addItem(choice);
					edit = combo->lineEdit();
					connect(combo, &QComboBox::activated, this, [=](int) { apply(combo->currentText()); });
					layout->addWidget(combo, 1);
				}
				else
				{
					edit = field.AssetFilter ? new AssetLineEdit() : new QLineEdit();
					layout->addWidget(edit, 1);
				}
				connect(edit, &QLineEdit::editingFinished, this, [=]() { apply(edit->text()); });

				if (field.AssetFilter)
				{
					QPushButton* browse = new QPushButton("...");
					browse->setFixedWidth(28);
					browse->setToolTip("Pick a file inside Content");
					const QString filter = field.AssetFilter;
					connect(browse, &QPushButton::clicked, this, [=, this]() {
						const QDir content(ToQString(Paths::GetContentDir()));
						const QString picked = QFileDialog::getOpenFileName(this, fieldCopy.Name, content.absolutePath(), filter);
						if (picked.isEmpty())
							return;
						const QString relative = content.relativeFilePath(picked);
						if (relative.startsWith(".."))
						{
							FP_LOG_WARN("Assets must live inside Content: %s", ToString(picked).CStr());
							return;
						}
						edit->setText(relative);
						apply(relative);
					});
					layout->addWidget(browse);
				}

				m_Refreshers.push_back([=]() {
					if (void* c = data(); c && !IsEditing(edit))
					{
						QSignalBlocker block(edit);
						edit->setText(ToQString(fieldCopy.Get<String>(c)));
					}
				});
				form->addRow(field.Name, row);
				break;
			}
		}
	}
}
