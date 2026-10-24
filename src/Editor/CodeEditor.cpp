#include "Editor/CodeEditor.h"

#include <QFile>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QTextBlock>

namespace FP::Editor
{
	namespace
	{
		QTextCharFormat MakeFormat(const QColor& color, bool bold = false, bool italic = false)
		{
			QTextCharFormat format;
			format.setForeground(color);
			if (bold)
				format.setFontWeight(QFont::Bold);
			format.setFontItalic(italic);
			return format;
		}

		// Gutter that asks the editor to paint it, so numbers line up with scrolled text.
		class LineNumberArea : public QWidget
		{
		public:
			explicit LineNumberArea(CodeEditor* editor) : QWidget(editor), m_Editor(editor) {}
			QSize sizeHint() const override { return { m_Editor->LineNumberWidth(), 0 }; }

		protected:
			void paintEvent(QPaintEvent* event) override { m_Editor->PaintLineNumbers(event); }

		private:
			CodeEditor* m_Editor;
		};
	}

	CppHighlighter::CppHighlighter(QTextDocument* document) : QSyntaxHighlighter(document)
	{
		// Later rules win where they overlap, so strings and comments come last.
		const QStringList keywords = {
			"alignas", "alignof", "auto", "bool", "break", "case", "catch", "char", "class", "const", "constexpr",
			"continue", "decltype", "default", "delete", "do", "double", "else", "enum", "explicit", "extern", "false",
			"final", "float", "for", "friend", "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
			"nullptr", "operator", "override", "private", "protected", "public", "return", "short", "signed", "sizeof",
			"static", "static_assert", "static_cast", "struct", "switch", "template", "this", "true", "typename",
			"union", "unsigned", "using", "virtual", "void", "volatile", "while",
		};
		const QTextCharFormat keyword = MakeFormat(QColor(86, 156, 214), true);
		for (const QString& word : keywords)
			m_Rules.append({ QRegularExpression("\\b" + word + "\\b"), keyword });

		m_Rules.append({ QRegularExpression("\\b(int|uint)(8|16|32|64)\\b|\\b(usize|isize|String|Array|HashMap|Vec2|Vec3|Vec4|Quat|Mat4|Color|Entity|World|Script)\\b"), MakeFormat(QColor(78, 201, 176)) });
		m_Rules.append({ QRegularExpression("\\bFP_[A-Z_]+\\b"), MakeFormat(QColor(197, 134, 192), true) });
		m_Rules.append({ QRegularExpression("\\b[0-9]+(\\.[0-9]*)?f?\\b"), MakeFormat(QColor(181, 206, 168)) });
		m_Rules.append({ QRegularExpression("^\\s*#\\s*\\w+"), MakeFormat(QColor(155, 155, 155)) });
		m_Rules.append({ QRegularExpression("\"([^\"\\\\]|\\\\.)*\""), MakeFormat(QColor(206, 145, 120)) });
		m_Rules.append({ QRegularExpression("'([^'\\\\]|\\\\.)*'"), MakeFormat(QColor(206, 145, 120)) });
		m_CommentFormat = MakeFormat(QColor(106, 153, 85), false, true);
		m_Rules.append({ QRegularExpression("//[^\\n]*"), m_CommentFormat });
	}

	void CppHighlighter::highlightBlock(const QString& text)
	{
		for (const Rule& rule : m_Rules)
		{
			QRegularExpressionMatchIterator it = rule.Pattern.globalMatch(text);
			while (it.hasNext())
			{
				const QRegularExpressionMatch match = it.next();
				setFormat(static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedLength()), rule.Format);
			}
		}

		// Block state 1 means this line is inside a multi line comment.
		setCurrentBlockState(0);
		int start = previousBlockState() == 1 ? 0 : static_cast<int>(text.indexOf(m_CommentStart));
		while (start >= 0)
		{
			const QRegularExpressionMatch end = m_CommentEnd.match(text, start);
			int length = 0;
			if (end.hasMatch())
			{
				length = static_cast<int>(end.capturedEnd()) - start;
			}
			else
			{
				setCurrentBlockState(1);
				length = static_cast<int>(text.length()) - start;
			}
			setFormat(start, length, m_CommentFormat);
			start = static_cast<int>(text.indexOf(m_CommentStart, start + length));
		}
	}

	CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent)
	{
		QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
		font.setPointSize(font.pointSize() + 1);
		setFont(font);
		setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
		setLineWrapMode(QPlainTextEdit::NoWrap);

		QPalette colors = palette();
		colors.setColor(QPalette::Base, QColor(30, 30, 30));
		colors.setColor(QPalette::Text, QColor(212, 212, 212));
		setPalette(colors);

		m_Highlighter = new CppHighlighter(document());
		m_LineNumbers = new LineNumberArea(this);

		connect(this, &QPlainTextEdit::blockCountChanged, this, [this](int) { setViewportMargins(LineNumberWidth(), 0, 0, 0); });
		connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
			if (dy)
				m_LineNumbers->scroll(0, dy);
			else
				m_LineNumbers->update(0, rect.y(), m_LineNumbers->width(), rect.height());
		});
		connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::HighlightCurrentLine);
		setViewportMargins(LineNumberWidth(), 0, 0, 0);
		HighlightCurrentLine();
	}

	bool CodeEditor::LoadFile(const QString& path)
	{
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
			return false;
		setPlainText(QString::fromUtf8(file.readAll()));
		document()->setModified(false);
		m_Path = path;
		return true;
	}

	bool CodeEditor::Save()
	{
		QFile file(m_Path);
		if (m_Path.isEmpty() || !file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
			return false;
		file.write(toPlainText().toUtf8());
		document()->setModified(false);
		return true;
	}

	void CodeEditor::GoToLine(int line, int column)
	{
		const QTextBlock block = document()->findBlockByNumber(std::max(0, line - 1));
		QTextCursor cursor(block);
		cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, std::clamp(column - 1, 0, std::max(0, block.length() - 1)));
		setTextCursor(cursor);
		centerCursor();
		setFocus();
	}

	int CodeEditor::LineNumberWidth() const
	{
		int digits = 1;
		for (int count = std::max(1, blockCount()); count >= 10; count /= 10)
			++digits;
		return 12 + fontMetrics().horizontalAdvance('9') * std::max(3, digits);
	}

	void CodeEditor::PaintLineNumbers(QPaintEvent* event)
	{
		QPainter painter(m_LineNumbers);
		painter.fillRect(event->rect(), QColor(37, 37, 38));

		QTextBlock block = firstVisibleBlock();
		int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
		const int current = textCursor().blockNumber();
		while (block.isValid() && top <= event->rect().bottom())
		{
			const int height = static_cast<int>(blockBoundingRect(block).height());
			if (block.isVisible() && top + height >= event->rect().top())
			{
				painter.setPen(block.blockNumber() == current ? QColor(200, 200, 200) : QColor(110, 110, 110));
				painter.drawText(0, top, m_LineNumbers->width() - 6, fontMetrics().height(), Qt::AlignRight, QString::number(block.blockNumber() + 1));
			}
			block = block.next();
			top += height;
		}
	}

	void CodeEditor::resizeEvent(QResizeEvent* event)
	{
		QPlainTextEdit::resizeEvent(event);
		const QRect area = contentsRect();
		m_LineNumbers->setGeometry(area.left(), area.top(), LineNumberWidth(), area.height());
	}

	void CodeEditor::HighlightCurrentLine()
	{
		QTextEdit::ExtraSelection line;
		line.format.setBackground(QColor(45, 45, 48));
		line.format.setProperty(QTextFormat::FullWidthSelection, true);
		line.cursor = textCursor();
		line.cursor.clearSelection();
		setExtraSelections({ line });
		m_LineNumbers->update();
	}

	void CodeEditor::IndentSelection(bool indent)
	{
		QTextCursor cursor = textCursor();
		const int first = document()->findBlock(cursor.selectionStart()).blockNumber();
		const int last = document()->findBlock(cursor.selectionEnd() - (cursor.hasSelection() ? 1 : 0)).blockNumber();
		cursor.beginEditBlock();
		for (int i = first; i <= last; ++i)
		{
			QTextCursor line(document()->findBlockByNumber(i));
			if (indent)
			{
				line.insertText("\t");
			}
			else
			{
				const QString text = line.block().text();
				if (text.startsWith('\t'))
					line.deleteChar();
				else
					for (int n = 0; n < 4 && n < text.size() && text[n] == ' '; ++n)
						line.deleteChar();
			}
		}
		cursor.endEditBlock();
	}

	void CodeEditor::keyPressEvent(QKeyEvent* event)
	{
		const bool multiLine = textCursor().hasSelection() && textCursor().selectedText().contains(QChar::ParagraphSeparator);
		if (event->key() == Qt::Key_Backtab || (event->key() == Qt::Key_Tab && multiLine))
		{
			IndentSelection(event->key() == Qt::Key_Tab);
			return;
		}

		if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
		{
			// Keep the current indentation and add one level after an opening brace.
			const QString line = textCursor().block().text();
			QString indent;
			for (const QChar c : line)
			{
				if (c != ' ' && c != '\t')
					break;
				indent += c;
			}
			const QString beforeCursor = line.left(textCursor().positionInBlock()).trimmed();
			if (beforeCursor.endsWith('{'))
				indent += '\t';
			QPlainTextEdit::keyPressEvent(event);
			insertPlainText(indent);
			return;
		}

		if (event->text() == "}")
		{
			// A closing brace on an otherwise empty line steps back one level.
			QTextCursor cursor = textCursor();
			const QString before = cursor.block().text().left(cursor.positionInBlock());
			if (!before.isEmpty() && before.trimmed().isEmpty())
			{
				cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, before.endsWith('\t') ? 1 : std::min<int>(4, static_cast<int>(before.size())));
				cursor.removeSelectedText();
			}
		}
		QPlainTextEdit::keyPressEvent(event);
	}
}
