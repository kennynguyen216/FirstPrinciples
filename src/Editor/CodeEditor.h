#pragma once

#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QRegularExpression>

namespace FP::Editor
{
	// Colors C++ for the script editor. Multi line comments carry over between blocks.
	class CppHighlighter : public QSyntaxHighlighter
	{
	public:
		explicit CppHighlighter(QTextDocument* document);

	protected:
		void highlightBlock(const QString& text) override;

	private:
		struct Rule
		{
			QRegularExpression Pattern;
			QTextCharFormat Format;
		};
		QList<Rule> m_Rules;
		QTextCharFormat m_CommentFormat;
		QRegularExpression m_CommentStart{ "/\\*" };
		QRegularExpression m_CommentEnd{ "\\*/" };
	};

	// Plain text editor tuned for C++. Line numbers, current line highlight, auto indent,
	// block indent with Tab and Shift Tab, and file load and save.
	class CodeEditor : public QPlainTextEdit
	{
	public:
		explicit CodeEditor(QWidget* parent = nullptr);

		bool LoadFile(const QString& path);
		bool Save();
		const QString& GetPath() const { return m_Path; }
		bool IsDirty() const { return document()->isModified(); }

		// One based, like compiler messages.
		void GoToLine(int line, int column = 1);

		int LineNumberWidth() const;
		void PaintLineNumbers(QPaintEvent* event);

	protected:
		void keyPressEvent(QKeyEvent* event) override;
		void resizeEvent(QResizeEvent* event) override;

	private:
		void HighlightCurrentLine();
		void IndentSelection(bool indent);

		QWidget* m_LineNumbers = nullptr;
		CppHighlighter* m_Highlighter = nullptr;
		QString m_Path;
	};
}
