#include "Editor/InspectorPanel.h"
#include "Engine/ComponentRegistry.h"
#include "Engine/Material.h"
#include "Engine/Paths.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
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
		Rebuild();
	}

	void InspectorPanel::SetTarget(World* world, Entity entity)
	{
		m_World = world;
		m_Entity = entity;
		Rebuild();
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
					Rebuild();
				}
			});
			m_Layout->addWidget(create);
			return;
		}

		// Edits change the shared asset, so every entity using it updates at once.
		QGroupBox* group = new QGroupBox(QString("Material Asset: %1").arg(ToQString(path)));
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
		if (!m_World || !m_World->IsValid(m_Entity))
			return;
		for (auto& refresh : m_Refreshers)
			refresh();
	}

	void InspectorPanel::Rebuild()
	{
		m_Refreshers.clear();
		while (QLayoutItem* item = m_Layout->takeAt(0))
		{
			delete item->widget();
			delete item;
		}

		if (!m_World || !m_World->IsValid(m_Entity))
		{
			QLabel* empty = new QLabel("Nothing selected");
			empty->setEnabled(false);
			m_Layout->addWidget(empty);
			return;
		}

		entt::registry& registry = m_World->GetRegistry();
		QMenu* addMenu = new QMenu(this);

		for (const ComponentInfo* info : ComponentRegistry::Get().GetAll())
		{
			if (!info->ShowInEditor)
				continue;
			if (!info->TryGet(registry, m_Entity))
			{
				addMenu->addAction(info->Name, this, [this, info]() {
					info->Add(m_World->GetRegistry(), m_Entity);
					emit Modified();
					Rebuild();
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
			for (const FieldInfo& field : info->Fields)
				AddFieldEditor(form, field, data, [this]() { emit Modified(); });

			QPushButton* remove = new QPushButton("Remove");
			remove->setFlat(true);
			connect(remove, &QPushButton::clicked, this, [this, info]() {
				info->Remove(m_World->GetRegistry(), m_Entity);
				emit Modified();
				Rebuild();
			});
			groupLayout->addWidget(remove, 0, Qt::AlignRight);
			m_Layout->addWidget(group);

			if (info == ComponentRegistry::Get().Find<MaterialComponent>())
				AddMaterialSection();
		}

		QToolButton* add = new QToolButton();
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
					combo->setInsertPolicy(QComboBox::NoInsert);
					for (const char* choice : *field.Choices)
						combo->addItem(choice);
					edit = combo->lineEdit();
					connect(combo, &QComboBox::activated, this, [=](int) { apply(combo->currentText()); });
					layout->addWidget(combo, 1);
				}
				else
				{
					edit = new QLineEdit();
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
