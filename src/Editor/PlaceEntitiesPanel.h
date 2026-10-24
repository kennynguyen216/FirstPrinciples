#pragma once

#include <QWidget>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QListWidget;

namespace FP::Editor
{
	// Lists the kinds of entity the editor can make. Click an entry to create it at the origin or drag it into the viewport.
	class PlaceEntitiesPanel : public QWidget
	{
		Q_OBJECT

	public:
		// Drags carry the entity kind as this mime type so the viewport knows it is not a file.
		static constexpr const char* MimeType = "application/x-firstprinciples-entity";

		explicit PlaceEntitiesPanel(QWidget* parent = nullptr);

	signals:
		void CreateRequested(const QString& kind);

	private:
		void Rebuild();

		QLineEdit* m_Search = nullptr;
		QButtonGroup* m_Categories = nullptr;
		QLabel* m_CategoryLabel = nullptr;
		QListWidget* m_List = nullptr;
	};
}
