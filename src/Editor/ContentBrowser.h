#pragma once

#include <QHash>
#include <QIcon>
#include <QWidget>

class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace FP::Editor
{
	// Browses the project's assets: everything in Content plus the C++ scripts in Game/Scripts.
	// Folders on the left, the selected folder's files on the right with thumbnails for images
	// and color swatches for materials. Refreshes by itself when files change on disk.
	class ContentBrowser : public QWidget
	{
		Q_OBJECT

	public:
		explicit ContentBrowser(QWidget* parent = nullptr);

		QString GetCurrentFolder() const { return m_CurrentFolder; }
		void ShowFolder(const QString& absolutePath);

		// Rebuilds the folder tree and file list from disk, keeping the current folder.
		void Refresh();

		// Kind of asset from the file extension, for example "Scene" or "Texture".
		static QString AssetKind(const QString& path);

	signals:
		// Double clicked. Paths are absolute.
		void AssetActivated(const QString& path);
		void AssetSelected(const QString& path);

	private:
		void RebuildTree();
		void RebuildFiles();
		void AddFolderItems(QTreeWidgetItem* parent, const QString& path);
		void WatchFolders();
		QIcon IconFor(const QString& path);
		QIcon BadgeIcon(const QString& text, const QColor& color) const;
		bool IsHidden(const QString& name) const;

		QTreeWidget* m_Folders = nullptr;
		QListWidget* m_Files = nullptr;
		QLineEdit* m_Search = nullptr;
		QLabel* m_Location = nullptr;
		QFileSystemWatcher* m_Watcher = nullptr;
		QTimer* m_RefreshTimer = nullptr;

		QString m_ContentRoot;
		QString m_ScriptsRoot;
		QString m_CurrentFolder;

		// Thumbnails keyed by path and modified time so edits show up.
		QHash<QString, QIcon> m_IconCache;
	};
}
