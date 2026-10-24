#include "Editor/InspectorPanel.h"
#include "Engine/ComponentRegistry.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
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
			for (const FieldInfo& field : info->Fields)
				AddFieldEditor(form, *info, field);

			QPushButton* remove = new QPushButton("Remove");
			remove->setFlat(true);
			connect(remove, &QPushButton::clicked, this, [this, info]() {
				info->Remove(m_World->GetRegistry(), m_Entity);
				emit Modified();
				Rebuild();
			});
			groupLayout->addWidget(remove, 0, Qt::AlignRight);
			m_Layout->addWidget(group);
		}

		QToolButton* add = new QToolButton();
		add->setText("Add Component");
		add->setPopupMode(QToolButton::InstantPopup);
		add->setMenu(addMenu);
		add->setEnabled(!addMenu->isEmpty());
		m_Layout->addWidget(add);
		RefreshValues();
	}

	void InspectorPanel::AddFieldEditor(QFormLayout* form, const ComponentInfo& info, const FieldInfo& field)
	{
		const ComponentInfo* infoPtr = &info;
		const FieldInfo fieldCopy = field;

		// Looks the component up every time because EnTT may move it when others are added.
		auto data = [this, infoPtr]() -> void* {
			return m_World && m_World->IsValid(m_Entity) ? infoPtr->TryGet(m_World->GetRegistry(), m_Entity) : nullptr;
		};

		switch (field.Type)
		{
			case FieldType::Bool:
			{
				QCheckBox* box = new QCheckBox();
				connect(box, &QCheckBox::toggled, this, [=, this](bool value) {
					if (void* c = data()) { fieldCopy.Get<bool>(c) = value; emit Modified(); }
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
				connect(box, &QSpinBox::valueChanged, this, [=, this](int value) {
					if (void* c = data()) { fieldCopy.Get<int32>(c) = value; emit Modified(); }
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
				connect(box, &QDoubleSpinBox::valueChanged, this, [=, this](double value) {
					if (void* c = data()) { fieldCopy.Get<float>(c) = static_cast<float>(value); emit Modified(); }
				});
				m_Refreshers.push_back([=]() {
					if (void* c = data(); c && !IsEditing(box)) { QSignalBlocker block(box); box->setValue(fieldCopy.Get<float>(c)); }
				});
				form->addRow(field.Name, box);
				break;
			}
			case FieldType::Vec2:
			{
				QWidget* row = new QWidget();
				QHBoxLayout* layout = new QHBoxLayout(row);
				layout->setContentsMargins(0, 0, 0, 0);
				QDoubleSpinBox* x = MakeFloatBox();
				QDoubleSpinBox* y = MakeFloatBox();
				x->setPrefix("X ");
				y->setPrefix("Y ");
				layout->addWidget(x);
				layout->addWidget(y);
				connect(x, &QDoubleSpinBox::valueChanged, this, [=, this](double value) {
					if (void* c = data()) { fieldCopy.Get<Vec2>(c).X = static_cast<float>(value); emit Modified(); }
				});
				connect(y, &QDoubleSpinBox::valueChanged, this, [=, this](double value) {
					if (void* c = data()) { fieldCopy.Get<Vec2>(c).Y = static_cast<float>(value); emit Modified(); }
				});
				m_Refreshers.push_back([=]() {
					void* c = data();
					if (!c)
						return;
					const Vec2 v = fieldCopy.Get<Vec2>(c);
					if (!IsEditing(x)) { QSignalBlocker block(x); x->setValue(v.X); }
					if (!IsEditing(y)) { QSignalBlocker block(y); y->setValue(v.Y); }
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
						emit Modified();
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
				QLineEdit* edit = new QLineEdit();
				connect(edit, &QLineEdit::editingFinished, this, [=, this]() {
					if (void* c = data())
					{
						const QByteArray utf8 = edit->text().toUtf8();
						String& value = fieldCopy.Get<String>(c);
						if (value != std::string_view(utf8.constData(), static_cast<usize>(utf8.size())))
						{
							value = std::string_view(utf8.constData(), static_cast<usize>(utf8.size()));
							emit Modified();
						}
					}
				});
				m_Refreshers.push_back([=]() {
					if (void* c = data(); c && !IsEditing(edit))
					{
						QSignalBlocker block(edit);
						edit->setText(QString::fromUtf8(fieldCopy.Get<String>(c).CStr()));
					}
				});
				form->addRow(field.Name, edit);
				break;
			}
		}
	}
}
