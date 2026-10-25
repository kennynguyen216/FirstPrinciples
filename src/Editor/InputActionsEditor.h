#pragma once

#include <QWidget>

class QLabel;
class QListWidget;
class QTabWidget;
class QTableWidget;
class QTimer;

namespace FP::Editor
{
	class CodeEditor;

	// Edits binding rows and their source through the same text document.
	class InputActionsEditor : public QWidget
	{
		Q_OBJECT

	public:
		explicit InputActionsEditor(QWidget* parent = nullptr);

		bool LoadFile(const QString& path);
		bool Save();
		CodeEditor* GetSource() const { return m_Source; }

	private:
		void RebuildBindings();
		void ChangeBinding(int row, int column);
		void AddBinding();
		void RemoveBindings();
		bool Validate();

		CodeEditor* m_Source = nullptr;
		QTabWidget* m_Views = nullptr;
		QTableWidget* m_Bindings = nullptr;
		QListWidget* m_Errors = nullptr;
		QLabel* m_Status = nullptr;
		QTimer* m_RebuildTimer = nullptr;
	};
}
