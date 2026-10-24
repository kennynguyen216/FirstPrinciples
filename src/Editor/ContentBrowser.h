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
class QToolButton;
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

		// Shows a folder. Back and forward remember the folders shown before.
		void ShowFolder(const QString& absolutePath);

		// Rebuilds the folder tree and file list from disk, keeping the current folder.
		void Refresh();

		// Kind of asset from the file extension, for example "Scene" or "Texture".
		static QString AssetKind(const QString& path);

		// Points every scene and material that used oldPath at newPath instead. Paths are relative
		// to Content. Returns how many files changed.
		static int ReplaceReferences(const QString& oldPath, const QString& newPath);

	signals:
		// Double clicked. Paths are absolute.
		void AssetActivated(const QString& path);
		void AssetSelected(const QString& path);

		// The user asked for a new script. The editor owns script creation.
		void NewScriptRequested();

		// The toolbar's Save All. The editor owns the scene and script tabs.
		void SaveAllRequested();

		// Files on disk were renamed and references rewritten, so open scenes may be stale.
		void ReferencesChanged();

	protected:
		bool eventFilter(QObject* watched, QEvent* event) override;

	private:
		void ShowContextMenu(const QPoint& position);
		void NewFolder();
		void NewMaterial();
		void NewScene();
		void RenameSelected();
		void DuplicateSelected();
		void DeleteSelected();
		QStringList SelectedPaths() const;
		QString AskForName(const QString& title, const QString& initial);
		void SelectPath(const QString& path);

		void ImportFiles();
		void GoBack();
		void GoForward();
		void RebuildCrumbs();

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
		QWidget* m_Crumbs = nullptr;
		QLabel* m_Count = nullptr;
		QToolButton* m_Back = nullptr;
		QToolButton* m_Forward = nullptr;
		QStringList m_BackHistory;
		QStringList m_ForwardHistory;
		QFileSystemWatcher* m_Watcher = nullptr;
		QTimer* m_RefreshTimer = nullptr;

		QString m_ContentRoot;
		QString m_ScriptsRoot;
		QString m_CurrentFolder;

		// Thumbnails keyed by path and modified time so edits show up.
		QHash<QString, QIcon> m_IconCache;
	};
}
