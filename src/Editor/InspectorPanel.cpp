#include "Editor/InspectorPanel.h"
#include "Engine/ComponentRegistry.h"
#include "Engine/Material.h"
#include "Engine/Paths.h"
#include "Engine/Scripting/ScriptRuntime.h"
#include "Editor/ContentBrowser.h"
#include "Editor/EditorIcons.h"
#include "Editor/EditorStyle.h"

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
#include <QSet>
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

		// A collapsible category like UE's Details panel. The header holds the title and any actions.
		// Collapsed titles are remembered so rebuilding after an edit keeps the layout.
		class DetailsSection : public QWidget
		{
		public:
			explicit DetailsSection(const QString& title)
			{
				QVBoxLayout* outer = new QVBoxLayout(this);
				outer->setContentsMargins(0, 0, 0, 0);
				outer->setSpacing(0);

				QWidget* header = new QWidget();
				header->setObjectName("SectionHeader");
				header->setStyleSheet("#SectionHeader { background: #2f2f2f; border-radius: 3px; }");
				m_Header = new QHBoxLayout(header);
				m_Header->setContentsMargins(4, 2, 4, 2);
				m_Toggle = new QToolButton();
				m_Toggle->setCheckable(true);
				m_Toggle->setText(title);
				m_Toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
				m_Toggle->setStyleSheet("QToolButton { background: transparent; border: none; font-weight: bold; color: #ffffff; padding: 3px; }");
				m_Header->addWidget(m_Toggle, 1, Qt::AlignLeft);
				outer->addWidget(header);

				m_Body = new QWidget();
				m_Body->setObjectName("SectionBody");
				m_Body->setStyleSheet("#SectionBody { background: #1a1a1a; }");
				m_Layout = new QVBoxLayout(m_Body);
				m_Layout->setContentsMargins(10, 6, 6, 6);
				outer->addWidget(m_Body);

				const bool collapsed = Collapsed().contains(title);
				m_Toggle->setChecked(!collapsed);
				Update();
				connect(m_Toggle, &QToolButton::toggled, this, [this, title](bool open) {
					if (open)
						Collapsed().remove(title);
					else
						Collapsed().insert(title);
					Update();
				});
			}

			QVBoxLayout* Body() const { return m_Layout; }

			// Adds a small flat button on the right side of the header.
			QToolButton* AddAction(const QIcon& icon, const QString& text, const QString& tip)
			{
				QToolButton* button = new QToolButton();
				button->setIcon(icon);
				button->setText(text);
				button->setToolTip(tip);
				button->setAutoRaise(true);
				button->setToolButtonStyle(text.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
				button->setStyleSheet("QToolButton { background: transparent; border: none; padding: 2px 4px; } QToolButton:hover { background: #575757; border-radius: 3px; }");
				m_Header->addWidget(button);
				return button;
			}

		private:
			static QSet<QString>& Collapsed()
			{
				static QSet<QString> titles;
				return titles;
			}

			void Update()
			{
				m_Body->setVisible(m_Toggle->isChecked());
				m_Toggle->setArrowType(m_Toggle->isChecked() ? Qt::DownArrow : Qt::RightArrow);
			}

			QHBoxLayout* m_Header = nullptr;
			QToolButton* m_Toggle = nullptr;
			QWidget* m_Body = nullptr;
			QVBoxLayout* m_Layout = nullptr;
		};

		QFormLayout* MakeForm(QVBoxLayout* body)
		{
			QFormLayout* form = new QFormLayout();
			form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
			form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
			form->setHorizontalSpacing(12);
			form->setVerticalSpacing(4);
			body->addLayout(form);
			return form;
		}

		// Spin boxes for Vec2 and Vec3 share this so every axis behaves the same.
		QWidget* MakeVectorRow(int count, QDoubleSpinBox** boxes)
		{
			static const char* const axisColors[3] = { "#c83a3a", "#5eb03a", "#3a7ae0" };
			QWidget* row = new QWidget();
			QHBoxLayout* layout = new QHBoxLayout(row);
			layout->setContentsMargins(0, 0, 0, 0);
			layout->setSpacing(4);
			for (int i = 0; i < count; ++i)
			{
				boxes[i] = MakeFloatBox();
				boxes[i]->setMinimumWidth(56);
				boxes[i]->setStyleSheet(QString("QDoubleSpinBox { border-left: 3px solid %1; padding-left: 5px; }").arg(axisColors[i]));
				layout->addWidget(boxes[i]);
			}
			return row;
		}
	}

	InspectorPanel::InspectorPanel(QWidget* parent) : QWidget(parent)
	{
		QVBoxLayout* outer = new QVBoxLayout(this);
		outer->setContentsMargins(6, 6, 6, 0);
		outer->setSpacing(6);

		// Header: what is shown, plus the Add button for components.
		QHBoxLayout* header = new QHBoxLayout();
		m_TitleIcon = new QLabel();
		header->addWidget(m_TitleIcon);
		m_Title = new QLabel();
		m_Title->setStyleSheet("font-size: 14px; font-weight: bold; color: #ffffff;");
		header->addWidget(m_Title, 1);
		m_AddButton = new QToolButton();
		m_AddButton->setText("Add");
		m_AddButton->setIcon(EditorIcons::Get(EditorIcon::Add, EditorStyle::Play));
		m_AddButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		m_AddButton->setPopupMode(QToolButton::InstantPopup);
		m_AddButton->setToolTip("Add a component");
		header->addWidget(m_AddButton);
		outer->addLayout(header);

		m_Filter = new QLineEdit();
		m_Filter->setPlaceholderText("Search");
		m_Filter->setProperty("search", true);
		m_Filter->addAction(EditorIcons::Get(EditorIcon::Search), QLineEdit::LeadingPosition);
		m_Filter->setClearButtonEnabled(true);
		connect(m_Filter, &QLineEdit::textChanged, this, &InspectorPanel::ApplyFilter);
		outer->addWidget(m_Filter);

		QScrollArea* scroll = new QScrollArea();
		scroll->setWidgetResizable(true);
		scroll->setFrameShape(QFrame::NoFrame);
		outer->addWidget(scroll);

		m_Content = new QWidget();
		m_Layout = new QVBoxLayout(m_Content);
		m_Layout->setAlignment(Qt::AlignTop);
		m_Layout->setContentsMargins(0, 0, 0, 6);
		m_Layout->setSpacing(4);
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

			DetailsSection* box = new DetailsSection(ToQString(slot.Name));
			QVBoxLayout* boxLayout = box->Body();
			QFormLayout* form = MakeForm(boxLayout);
			form->setRowWrapPolicy(QFormLayout::WrapLongRows); // script field names are often long

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

			if (type && type->SourceFile)
			{
				QToolButton* editButton = box->AddAction(EditorIcons::Get(EditorIcon::Script), "Edit", "Open the script source");
				const QString source = QString::fromUtf8(type->SourceFile);
				connect(editButton, &QToolButton::clicked, this, [this, source]() { emit OpenScriptRequested(source); });
			}
			QToolButton* remove = box->AddAction(EditorIcons::Get(EditorIcon::Close), "", "Remove script");
			connect(remove, &QToolButton::clicked, this, [this, index]() {
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

		DetailsSection* group = new DetailsSection(QString("%1: %2").arg(kind, info.fileName()));
		QVBoxLayout* layout = group->Body();
		QFormLayout* form = MakeForm(layout);
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
		DetailsSection* group = new DetailsSection(title);
		QFormLayout* form = MakeForm(group->Body());

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

		QToolButton* save = group->AddAction(EditorIcons::Get(EditorIcon::Save), "Save", "Save the material asset");
		connect(save, &QToolButton::clicked, this, [materialPath]() {
			if (Material* m = MaterialLibrary::Get().Load(materialPath))
				m->Save();
		});
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

		// The old menu belongs to the Add button, so replace it each time.
		delete m_AddButton->menu();
		m_AddButton->setMenu(nullptr);
		m_AddButton->setEnabled(false);

		if (!m_AssetPath.isEmpty())
		{
			SetTitle(QFileInfo(m_AssetPath).completeBaseName(), EditorIcon::ContentBrowser);
			AddAssetView();
			RefreshValues();
			ApplyFilter();
			return;
		}

		if (!m_World || !m_World->IsValid(m_Entity))
		{
			SetTitle("Select an entity to see its details", EditorIcon::Details);
			return;
		}

		SetTitle(ToQString(m_World->GetEntityName(m_Entity)), EditorIcon::Entity);
		entt::registry& registry = m_World->GetRegistry();
		QMenu* addMenu = new QMenu(m_AddButton);

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

			DetailsSection* group = new DetailsSection(info->Name);
			QVBoxLayout* groupLayout = group->Body();
			QFormLayout* form = MakeForm(groupLayout);
			// Looks the component up every time because EnTT may move it when others are added.
			auto data = [this, info]() -> void* {
				return m_World && m_World->IsValid(m_Entity) ? info->TryGet(m_World->GetRegistry(), m_Entity) : nullptr;
			};
			if (info->CustomInspector && info == ComponentRegistry::Get().Find<ScriptComponent>())
				AddScriptsSection(groupLayout);
			for (const FieldInfo& field : info->Fields)
				AddFieldEditor(form, field, data, [this]() { emit Modified(); });

			QToolButton* remove = group->AddAction(EditorIcons::Get(EditorIcon::Close), "", "Remove component");
			connect(remove, &QToolButton::clicked, this, [this, info]() {
				info->Remove(m_World->GetRegistry(), m_Entity);
				emit Modified();
				RebuildLater();
			});
			m_Layout->addWidget(group);

			if (info == ComponentRegistry::Get().Find<MaterialComponent>())
				AddMaterialSection();
		}

		m_AddButton->setMenu(addMenu);
		m_AddButton->setEnabled(!addMenu->isEmpty());
		RefreshValues();
		ApplyFilter();
	}

	void InspectorPanel::SetTitle(const QString& text, EditorIcon icon)
	{
		m_Title->setText(text);
		m_TitleIcon->setPixmap(EditorIcons::Get(icon).pixmap(18, 18));
	}

	void InspectorPanel::ApplyFilter()
	{
		// Hides rows whose field name does not match, like UE's details search.
		const QString filter = m_Filter->text().trimmed();
		for (QFormLayout* form : m_Content->findChildren<QFormLayout*>())
		{
			for (int row = 0; row < form->rowCount(); ++row)
			{
				QLayoutItem* label = form->itemAt(row, QFormLayout::LabelRole);
				const QLabel* text = label ? qobject_cast<QLabel*>(label->widget()) : nullptr;
				form->setRowVisible(row, filter.isEmpty() || !text || text->text().contains(filter, Qt::CaseInsensitive));
			}
		}
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
