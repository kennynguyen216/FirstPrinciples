#include "Editor/InputActionsEditor.h"
#include "Editor/CodeEditor.h"
#include "Engine/Input.h"

#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBlock>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <algorithm>

namespace FP::Editor
{
	namespace
	{
		enum Column { Action, Device, InputName, Direction, Scale, Comment, ColumnCount };

		QStringList InputNames(const QString& device)
		{
			QStringList names;
			for (const std::string& name : Input::GetBindingInputs(device.toUtf8().constData()))
				names.append(QString::fromStdString(name));
			return names;
		}

		class BindingDelegate : public QStyledItemDelegate
		{
		public:
			using QStyledItemDelegate::QStyledItemDelegate;

			QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override
			{
				QStringList choices;
				if (index.column() == Device) choices = { "Key", "Mouse", "Button", "Axis", "Stick" };
				else if (index.column() == Direction) choices = { "", "+X", "-X", "+Y", "-Y" };
				else if (index.column() == InputName) choices = InputNames(index.siblingAtColumn(Device).data().toString());
				else return QStyledItemDelegate::createEditor(parent, option, index);
				QComboBox* combo = new QComboBox(parent);
				combo->setEditable(index.column() == InputName);
				combo->addItems(choices);
				return combo;
			}

			void setEditorData(QWidget* editor, const QModelIndex& index) const override
			{
				if (QComboBox* combo = qobject_cast<QComboBox*>(editor)) combo->setCurrentText(index.data().toString());
				else QStyledItemDelegate::setEditorData(editor, index);
			}

			void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override
			{
				if (QComboBox* combo = qobject_cast<QComboBox*>(editor)) model->setData(index, combo->currentText());
				else QStyledItemDelegate::setModelData(editor, model, index);
			}
		};

		class ActionsSource : public CodeEditor
		{
		public:
			explicit ActionsSource(InputActionsEditor& owner) : CodeEditor(&owner), m_Owner(owner) {}
			bool Save() override { return m_Owner.Save(); }

		private:
			InputActionsEditor& m_Owner;
		};

		class ActionsHighlighter : public QSyntaxHighlighter
		{
		public:
			using QSyntaxHighlighter::QSyntaxHighlighter;

		protected:
			void highlightBlock(const QString& text) override
			{
				static const QRegularExpression binding("\\b(Key|Mouse|Button|Axis|Stick):[^\\s#]+");
				QRegularExpressionMatchIterator matches = binding.globalMatch(text);
				while (matches.hasNext())
				{
					const auto match = matches.next();
					setFormat(match.capturedStart(), match.capturedLength(), QColor(78, 201, 176));
				}
				const int comment = text.indexOf('#');
				if (comment >= 0) setFormat(comment, text.size() - comment, QColor(106, 153, 85));
			}
		};
	}

	InputActionsEditor::InputActionsEditor(QWidget* parent) : QWidget(parent)
	{
		QVBoxLayout* layout = new QVBoxLayout(this);
		layout->setContentsMargins(8, 8, 8, 8);
		QHBoxLayout* toolbar = new QHBoxLayout();
		QPushButton* save = new QPushButton("Save");
		QPushButton* add = new QPushButton("Add Binding");
		QPushButton* remove = new QPushButton("Remove Selected");
		save->setObjectName("SaveActions");
		add->setObjectName("AddBinding");
		remove->setObjectName("RemoveBindings");
		toolbar->addWidget(save);
		toolbar->addWidget(add);
		toolbar->addWidget(remove);
		toolbar->addStretch();
		layout->addLayout(toolbar);
		QLabel* help = new QLabel("Bind an action to a key, mouse or gamepad input. Direction uses right and up as positive. A blank scale keeps the default behavior.");
		help->setWordWrap(true);
		layout->addWidget(help);

		m_Views = new QTabWidget();
		m_Bindings = new QTableWidget(0, ColumnCount);
		m_Bindings->setObjectName("InputBindings");
		m_Bindings->setHorizontalHeaderLabels({ "Action", "Device", "Input", "Direction", "Scale", "Comment" });
		m_Bindings->setSelectionBehavior(QAbstractItemView::SelectRows);
		m_Bindings->setSelectionMode(QAbstractItemView::ExtendedSelection);
		m_Bindings->setItemDelegate(new BindingDelegate(m_Bindings));
		m_Bindings->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
		m_Bindings->horizontalHeader()->setSectionResizeMode(Comment, QHeaderView::Stretch);
		m_Source = new ActionsSource(*this);
		m_Source->SetHighlighter(new ActionsHighlighter(m_Source->document()));
		m_Source->setObjectName("InputActionsSource");
		m_Views->addTab(m_Bindings, "Bindings");
		m_Views->addTab(m_Source, "Source");
		setFocusProxy(m_Bindings);
		connect(m_Views, &QTabWidget::currentChanged, this, [this](int) { setFocusProxy(m_Views->currentWidget()); });
		layout->addWidget(m_Views, 1);
		m_Status = new QLabel();
		m_Status->setWordWrap(true);
		layout->addWidget(m_Status);
		m_Errors = new QListWidget();
		m_Errors->setMaximumHeight(100);
		m_Errors->hide();
		layout->addWidget(m_Errors);

		m_RebuildTimer = new QTimer(this);
		m_RebuildTimer->setSingleShot(true);
		connect(m_Source, &QPlainTextEdit::textChanged, m_RebuildTimer, qOverload<>(&QTimer::start));
		connect(m_RebuildTimer, &QTimer::timeout, this, &InputActionsEditor::RebuildBindings);
		connect(m_Bindings, &QTableWidget::cellChanged, this, &InputActionsEditor::ChangeBinding);
		connect(save, &QPushButton::clicked, this, [this]() { Save(); });
		connect(add, &QPushButton::clicked, this, &InputActionsEditor::AddBinding);
		connect(remove, &QPushButton::clicked, this, &InputActionsEditor::RemoveBindings);
		connect(m_Errors, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
			m_Views->setCurrentWidget(m_Source);
			m_Source->GoToLine(item->text().section(':', 0, 0).section(' ', -1).toInt());
		});
	}

	bool InputActionsEditor::LoadFile(const QString& path)
	{
		if (!m_Source->LoadFile(path)) return false;
		RebuildBindings();
		return true;
	}

	bool InputActionsEditor::Validate()
	{
		std::vector<std::string> errors;
		const QByteArray text = m_Source->toPlainText().toUtf8();
		const bool valid = Input::ValidateActions(std::string_view(text.constData(), text.size()), &errors);
		m_Errors->clear();
		for (const auto& error : errors) m_Errors->addItem(QString::fromStdString(error));
		m_Errors->setVisible(!valid);
		m_Status->setText(valid ? "Bindings are valid. Saved changes apply the next time Play starts." : "Fix the invalid lines before saving. Click an error to open its source.");
		return valid;
	}

	bool InputActionsEditor::Save()
	{
		if (m_Views->currentWidget() == m_Bindings) m_Bindings->setFocus();
		if (!Validate()) return false;
		QSaveFile file(m_Source->GetPath());
		const QByteArray bytes = m_Source->toPlainText().toUtf8();
		if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
		{
			m_Status->setText("Could not save: " + file.errorString());
			return false;
		}
		m_Source->document()->setModified(false);
		m_Status->setText("Saved. Changes apply the next time Play starts.");
		return true;
	}

	void InputActionsEditor::RebuildBindings()
	{
		m_RebuildTimer->stop();
		const QSignalBlocker block(m_Bindings);
		const int selected = m_Bindings->currentRow();
		m_Bindings->setRowCount(0);
		for (QTextBlock line = m_Source->document()->begin(); line.isValid(); line = line.next())
		{
			const QString text = line.text();
			const int hash = text.indexOf('#');
			const QStringList words = text.left(hash < 0 ? text.size() : hash).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
			if (words.isEmpty()) continue;
			const int row = m_Bindings->rowCount();
			m_Bindings->insertRow(row);
			QStringList values{ words[0], words.value(1).section(':', 0, 0), words.value(1).section(':', 1), "", "", hash < 0 ? "" : text.mid(hash + 1).trimmed() };
			for (int i = 2; i < words.size(); ++i)
			{
				if (QStringList{ "+X", "-X", "+Y", "-Y" }.contains(words[i])) values[Direction] = words[i];
				else values[Scale] = words[i];
			}
			for (int column = 0; column < ColumnCount; ++column)
			{
				QTableWidgetItem* item = new QTableWidgetItem(values[column]);
				item->setData(Qt::UserRole, line.blockNumber());
				if (column == Comment) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
				m_Bindings->setItem(row, column, item);
			}
			m_Bindings->item(row, Scale)->setToolTip("Leave blank for the default scale. An explicit stick scale is a speed per second.");
			m_Bindings->item(row, Direction)->setToolTip("Applies to keys, mouse buttons, gamepad buttons and individual axes.");
		}
		if (selected >= 0 && m_Bindings->rowCount()) m_Bindings->setCurrentCell(std::min(selected, m_Bindings->rowCount() - 1), Action);
		Validate();
	}

	void InputActionsEditor::ChangeBinding(int row, int column)
	{
		if (column == Comment) return;
		const QSignalBlocker block(m_Bindings);
		if (column == Device)
		{
			const QStringList names = InputNames(m_Bindings->item(row, Device)->text());
			if (!names.contains(m_Bindings->item(row, InputName)->text(), Qt::CaseInsensitive))
				m_Bindings->item(row, InputName)->setText(names.value(0));
		}
		const QTextBlock line = m_Source->document()->findBlockByNumber(m_Bindings->item(row, Action)->data(Qt::UserRole).toInt());
		const QString old = line.text();
		const int hash = old.indexOf('#');
		QString replacement = old.left(old.indexOf(QRegularExpression("\\S")));
		replacement += m_Bindings->item(row, Action)->text().trimmed() + "  " + m_Bindings->item(row, Device)->text().trimmed() + ":" + m_Bindings->item(row, InputName)->text().trimmed();
		for (int field : { Direction, Scale })
		{
			const QString value = m_Bindings->item(row, field)->text().trimmed();
			if (!value.isEmpty()) replacement += "  " + value;
		}
		if (hash >= 0) replacement += "  " + old.mid(hash);
		QTextCursor cursor(line);
		cursor.beginEditBlock();
		cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
		cursor.removeSelectedText();
		cursor.insertText(replacement);
		cursor.endEditBlock();
	}

	void InputActionsEditor::AddBinding()
	{
		QTextCursor cursor(m_Source->document());
		cursor.movePosition(QTextCursor::End);
		cursor.beginEditBlock();
		if (!m_Source->toPlainText().isEmpty() && !m_Source->toPlainText().endsWith('\n')) cursor.insertText("\n");
		cursor.insertText("NewAction  Key:Space\n");
		cursor.endEditBlock();
		RebuildBindings();
		m_Views->setCurrentWidget(m_Bindings);
		m_Bindings->setCurrentCell(m_Bindings->rowCount() - 1, Action);
		m_Bindings->editItem(m_Bindings->currentItem());
	}

	void InputActionsEditor::RemoveBindings()
	{
		QList<int> lines;
		for (const QModelIndex& row : m_Bindings->selectionModel()->selectedRows())
			lines.append(m_Bindings->item(row.row(), Action)->data(Qt::UserRole).toInt());
		std::sort(lines.begin(), lines.end(), std::greater<int>());
		QTextCursor cursor(m_Source->document());
		cursor.beginEditBlock();
		for (int number : lines)
		{
			const QTextBlock line = m_Source->document()->findBlockByNumber(number);
			cursor.setPosition(line.position());
			cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
			if (line.next().isValid()) cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
			cursor.removeSelectedText();
		}
		cursor.endEditBlock();
		RebuildBindings();
	}
}
