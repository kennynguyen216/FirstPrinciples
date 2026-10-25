#include "Editor/CodeEditor.h"
#include "Editor/InputActionsEditor.h"
#include "Engine/Input.h"

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>

#include <cstdio>

using namespace FP::Editor;

int main(int argc, char** argv)
{
	QApplication app(argc, argv);
	int failed = 0;
	auto expect = [&](bool good, const char* message) {
		if (!good) { std::fprintf(stderr, "FAILED: %s\n", message); ++failed; }
	};
	QTemporaryDir folder;
	const QString path = folder.filePath("Input.actions");
	const QByteArray original = "# Project bindings\n\nJump  Key:Space  # Keep this comment\nLook  Stick:Right  800\n";
	auto read = [&]() {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) return QByteArray();
		return file.readAll();
	};
	{
		QFile file(path);
		expect(file.open(QIODevice::WriteOnly) && file.write(original) == original.size(), "write fixture");
	}
	InputActionsEditor editor;
	editor.resize(1000, 600);
	editor.show();
	expect(editor.LoadFile(path), "open actions");
	CodeEditor* source = editor.GetSource();
	QTableWidget* table = editor.findChild<QTableWidget*>("InputBindings");
	expect(table && table->rowCount() == 2 && !source->IsDirty(), "load rows without marking dirty");
	if (!table) return 1;
	table->item(0, 2)->setText("G");
	QApplication::processEvents();
	expect(source->IsDirty() && source->toPlainText().contains("Key:G"), "row edits update source");
	expect(source->toPlainText().contains("# Keep this comment"), "inline comments survive editing");
	source->undo();
	QApplication::processEvents();
	expect(source->toPlainText().toUtf8() == original && !source->IsDirty(), "one undo restores original formatting");
	source->redo();
	QApplication::processEvents();
	expect(source->Save() && !source->IsDirty(), "shared save dispatches to the actions editor");
	expect(read().contains("Key:G") && read().contains("# Project bindings"), "save preserves bindings and comments");
	table->setCurrentCell(0, 0);
	table->editItem(table->currentItem());
	QApplication::processEvents();
	QLineEdit* active = qobject_cast<QLineEdit*>(QApplication::focusWidget());
	expect(active, "binding cell has an active editor");
	if (active) active->setText("Leap");
	expect(source->Save() && read().contains("Leap"), "save commits the active cell before writing");
	QApplication::processEvents();
	const QByteArray saved = read();

	source->moveCursor(QTextCursor::End);
	source->insertPlainText("Broken Key:NotAKey\n");
	QApplication::processEvents();
	expect(!source->Save() && source->IsDirty() && read() == saved, "invalid input leaves the disk file and dirty state intact");
	expect(editor.findChild<QListWidget*>()->count() == 1, "invalid lines are reported");
	source->undo();
	QApplication::processEvents();
	table->item(1, 4)->setText("");
	QApplication::processEvents();
	expect(source->toPlainText().contains("Look  Stick:Right\n") && editor.Save(), "blank scale stays implicit");

	// Exercise the picker through the same delegate used during editing.
	const QModelIndex device = table->model()->index(0, 1);
	QWidget* control = table->itemDelegate()->createEditor(table, QStyleOptionViewItem(), device);
	QComboBox* combo = qobject_cast<QComboBox*>(control);
	expect(combo, "device picker exists");
	if (combo)
	{
		combo->setCurrentText("Mouse");
		table->itemDelegate()->setModelData(combo, table->model(), device);
	}
	delete control;
	QApplication::processEvents();
	expect(source->toPlainText().contains("Mouse:Left"), "device changes choose a valid input");

	const QString beforeAdd = source->toPlainText();
	editor.findChild<QPushButton*>("AddBinding")->click();
	QApplication::processEvents();
	expect(table->rowCount() == 3 && source->toPlainText().contains("NewAction"), "add a binding");
	table->setFocus();
	source->undo();
	QApplication::processEvents();
	expect(source->toPlainText() == beforeAdd && table->rowCount() == 2, "undo adding a binding");
	table->selectAll();
	editor.findChild<QPushButton*>("RemoveBindings")->click();
	QApplication::processEvents();
	expect(table->rowCount() == 0 && source->toPlainText().contains("# Project bindings"), "remove bindings without removing standalone comments");
	source->undo();
	QApplication::processEvents();
	expect(source->toPlainText() == beforeAdd && table->rowCount() == 2, "undo removing multiple bindings");

	source->moveCursor(QTextCursor::End);
	source->insertPlainText("Jump Key:F\n");
	QApplication::processEvents();
	expect(table->rowCount() == 3, "source edits update binding rows");
	const QString changed = source->toPlainText();
	expect(folder.remove(), "remove temporary save directory");
	expect(!editor.Save() && source->IsDirty() && source->toPlainText() == changed, "save failure keeps edits");
	std::printf("Input actions editor: %d failures\n", failed);
	return failed ? 1 : 0;
}
