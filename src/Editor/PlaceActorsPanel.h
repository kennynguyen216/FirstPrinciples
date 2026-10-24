#pragma once

#include <QWidget>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QListWidget;

namespace FP::Editor
{
	// The UE style Place Actors panel. Click an entry to create it at the origin or drag it into the viewport.
	class PlaceActorsPanel : public QWidget
	{
		Q_OBJECT

	public:
		// Drags carry the actor kind as this mime type so the viewport knows it is not a file.
		static constexpr const char* MimeType = "application/x-firstprinciples-actor";

		explicit PlaceActorsPanel(QWidget* parent = nullptr);

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
