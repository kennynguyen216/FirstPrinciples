#include "Editor/ContentBrowser.h"
#include "Editor/EditorIcons.h"
#include "Editor/EditorStyle.h"
#include "Engine/Material.h"
#include "Engine/Paths.h"
#include "Engine/Project.h"
#include "Engine/Render/ImageDecoder.h"
#include "Core/Misc/Guid.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QToolButton>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QUrl>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace FP::Editor
{
	namespace
	{
		constexpr int IconSize = 64;
		constexpr int PathRole = Qt::UserRole + 1;

		// Drags carry plain file URLs so the viewport, inspector and other apps all understand them.
		class AssetList : public QListWidget
		{
		protected:
			QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override
			{
				QList<QUrl> urls;
				for (const QListWidgetItem* item : items)
					urls.append(QUrl::fromLocalFile(item->data(PathRole).toString()));
				QMimeData* data = new QMimeData();
				data->setUrls(urls);
				return data;
			}

			QStringList mimeTypes() const override { return { "text/uri-list" }; }
		};

		QString ProjectDir() { return QString::fromUtf8(Paths::GetProjectDir().CStr()); }
	}

	ContentBrowser::ContentBrowser(GPUDevice& device, QWidget* parent) : QWidget(parent), m_Thumbnails(device)
	{
		m_ContentRoot = QDir::cleanPath(QString::fromUtf8(Paths::GetContentDir().CStr()));
		m_ScriptsRoot = QDir::cleanPath(QString::fromUtf8(Paths::GetProject().GetScriptsDir().CStr()));

		QVBoxLayout* layout = new QVBoxLayout(this);
		layout->setContentsMargins(4, 4, 4, 4);
		layout->setSpacing(4);

		// Toolbar: create, import and save, then back, forward and the folder path.
		QHBoxLayout* bar = new QHBoxLayout();
		bar->setSpacing(4);
		QToolButton* add = new QToolButton();
		add->setIcon(EditorIcons::Get(EditorIcon::Add, EditorStyle::Play));
		add->setText("Add");
		add->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		add->setPopupMode(QToolButton::InstantPopup);
		QMenu* addMenu = new QMenu(add);
		addMenu->addAction(EditorIcons::Get(EditorIcon::Folder), "New Folder...", this, &ContentBrowser::NewFolder);
		addMenu->addAction(EditorIcons::Get(EditorIcon::Material), "New Material...", this, &ContentBrowser::NewMaterial);
		addMenu->addAction(EditorIcons::Get(EditorIcon::Scene), "New Scene...", this, &ContentBrowser::NewScene);
		addMenu->addAction(EditorIcons::Get(EditorIcon::Script), "New UI Page...", this, &ContentBrowser::NewPage);
		addMenu->addAction(EditorIcons::Get(EditorIcon::Script), "New Script...", this, [this]() { emit NewScriptRequested(); });
		add->setMenu(addMenu);
		bar->addWidget(add);

		auto flatButton = [](EditorIcon icon, const QString& text, const QString& tip) {
			QToolButton* button = new QToolButton();
			button->setIcon(EditorIcons::Get(icon));
			button->setText(text);
			button->setToolTip(tip);
			button->setAutoRaise(true);
			button->setToolButtonStyle(text.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
			button->setStyleSheet("QToolButton { background: transparent; border: none; padding: 4px 6px; } QToolButton:hover { background: #383838; border-radius: 4px; }");
			return button;
		};
		QToolButton* import = flatButton(EditorIcon::Import, "Import", "Copy files into this folder");
		connect(import, &QToolButton::clicked, this, &ContentBrowser::ImportFiles);
		bar->addWidget(import);
		QToolButton* saveAll = flatButton(EditorIcon::SaveAll, "Save All", "Save the scene, materials and scripts");
		connect(saveAll, &QToolButton::clicked, this, &ContentBrowser::SaveAllRequested);
		bar->addWidget(saveAll);
		m_Back = flatButton(EditorIcon::Back, "", "Back");
		m_Forward = flatButton(EditorIcon::Forward, "", "Forward");
		connect(m_Back, &QToolButton::clicked, this, &ContentBrowser::GoBack);
		connect(m_Forward, &QToolButton::clicked, this, &ContentBrowser::GoForward);
		bar->addWidget(m_Back);
		bar->addWidget(m_Forward);

		m_Crumbs = new QWidget();
		QHBoxLayout* crumbs = new QHBoxLayout(m_Crumbs);
		crumbs->setContentsMargins(0, 0, 0, 0);
		crumbs->setSpacing(0);
		bar->addWidget(m_Crumbs, 1);
		layout->addLayout(bar);

		m_Search = new QLineEdit();
		m_Search->setPlaceholderText("Search");
		m_Search->setProperty("search", true);
		m_Search->addAction(EditorIcons::Get(EditorIcon::Search), QLineEdit::LeadingPosition);
		m_Search->setClearButtonEnabled(true);

		QSplitter* split = new QSplitter(Qt::Horizontal);
		m_Folders = new QTreeWidget();
		m_Folders->setHeaderHidden(true);
		m_Folders->setMinimumWidth(140);
		m_Files = new AssetList();
		m_Files->setDragEnabled(true);
		m_Files->setDragDropMode(QAbstractItemView::DragOnly);
		m_Files->setViewMode(QListView::IconMode);
		m_Files->setIconSize({ IconSize, IconSize });
		m_Files->setGridSize({ IconSize + 40, IconSize + 36 });
		m_Files->setResizeMode(QListView::Adjust);
		m_Files->setMovement(QListView::Static);
		m_Files->setWordWrap(true);
		m_Files->setUniformItemSizes(true);
		m_Files->setSelectionMode(QAbstractItemView::ExtendedSelection);
		QWidget* folderPane = new QWidget();
		QVBoxLayout* folderLayout = new QVBoxLayout(folderPane);
		folderLayout->setContentsMargins(0, 0, 0, 0);
		folderLayout->setSpacing(0);
		QLabel* project = new QLabel(QDir(ProjectDir()).dirName());
		project->setStyleSheet("background: #2f2f2f; font-weight: bold; color: #ffffff; padding: 6px 8px;");
		folderLayout->addWidget(project);
		folderLayout->addWidget(m_Folders, 1);

		QWidget* filePane = new QWidget();
		QVBoxLayout* fileLayout = new QVBoxLayout(filePane);
		fileLayout->setContentsMargins(6, 0, 0, 0);
		fileLayout->setSpacing(4);
		fileLayout->addWidget(m_Search);
		fileLayout->addWidget(m_Files, 1);
		m_Count = new QLabel();
		m_Count->setStyleSheet("padding: 2px 4px;");
		fileLayout->addWidget(m_Count);

		split->addWidget(folderPane);
		split->addWidget(filePane);
		split->setStretchFactor(1, 1);
		split->setSizes({ 180, 600 });
		layout->addWidget(split, 1);

		connect(m_Folders, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
			if (item)
				ShowFolder(item->data(0, PathRole).toString());
		});
		connect(m_Files, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
			const QString path = item->data(PathRole).toString();
			if (QFileInfo(path).isDir())
				ShowFolder(path);
			else
				emit AssetActivated(path);
		});
		connect(m_Files, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
			if (item && QFileInfo(item->data(PathRole).toString()).isFile())
				emit AssetSelected(item->data(PathRole).toString());
		});
		connect(m_Search, &QLineEdit::textChanged, this, [this]() { RebuildFiles(); });

		m_Files->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(m_Files, &QWidget::customContextMenuRequested, this, &ContentBrowser::ShowContextMenu);
		m_Files->installEventFilter(this);

		// Files change in bursts while saving or building, so refresh once things settle.
		m_Watcher = new QFileSystemWatcher(this);
		m_RefreshTimer = new QTimer(this);
		m_RefreshTimer->setSingleShot(true);
		m_RefreshTimer->setInterval(250);
		connect(m_RefreshTimer, &QTimer::timeout, this, &ContentBrowser::Refresh);
		connect(m_Watcher, &QFileSystemWatcher::directoryChanged, m_RefreshTimer, qOverload<>(&QTimer::start));

		m_ThumbnailTimer = new QTimer(this);
		m_ThumbnailTimer->setInterval(16);
		connect(m_ThumbnailTimer, &QTimer::timeout, this, &ContentBrowser::RenderNextThumbnail);
		connect(m_Watcher, &QFileSystemWatcher::fileChanged, m_RefreshTimer, qOverload<>(&QTimer::start));

		m_CurrentFolder = m_ContentRoot;
		Refresh();
	}

	bool ContentBrowser::eventFilter(QObject* watched, QEvent* event)
	{
		if (watched == m_Files && event->type() == QEvent::KeyPress)
		{
			const int key = static_cast<QKeyEvent*>(event)->key();
			if (key == Qt::Key_F2)
			{
				RenameSelected();
				return true;
			}
			if (key == Qt::Key_Delete || (key == Qt::Key_Backspace && (static_cast<QKeyEvent*>(event)->modifiers() & Qt::ControlModifier)))
			{
				DeleteSelected();
				return true;
			}
		}
		return QWidget::eventFilter(watched, event);
	}

	QStringList ContentBrowser::SelectedPaths() const
	{
		QStringList paths;
		for (const QListWidgetItem* item : m_Files->selectedItems())
			paths.append(item->data(PathRole).toString());
		return paths;
	}

	void ContentBrowser::SelectPath(const QString& path)
	{
		Refresh();
		for (int i = 0; i < m_Files->count(); ++i)
		{
			if (m_Files->item(i)->data(PathRole).toString() == path)
			{
				m_Files->setCurrentRow(i);
				m_Files->scrollToItem(m_Files->item(i));
				return;
			}
		}
	}

	QString ContentBrowser::AskForName(const QString& title, const QString& initial)
	{
		bool ok = false;
		const QString name = QInputDialog::getText(this, title, "Name:", QLineEdit::Normal, initial, &ok).trimmed();
		if (!ok || name.isEmpty())
			return {};
		// Names become file names, so keep out path separators and characters Windows rejects.
		static const QRegularExpression invalid(R"([\\/:*?"<>|])");
		if (name.contains(invalid) || name.startsWith('.'))
		{
			QMessageBox::warning(this, title, "Names cannot contain \\ / : * ? \" < > | or start with a dot.");
			return {};
		}
		return name;
	}

	void ContentBrowser::ShowContextMenu(const QPoint& position)
	{
		QListWidgetItem* clicked = m_Files->itemAt(position);
		if (clicked && !clicked->isSelected())
			m_Files->setCurrentItem(clicked);

		QMenu menu(this);
		const bool inScripts = m_CurrentFolder.startsWith(m_ScriptsRoot);
		if (clicked)
		{
			const QString path = clicked->data(PathRole).toString();
			menu.addAction("Open", this, [this, path]() {
				if (QFileInfo(path).isDir())
					ShowFolder(path);
				else
					emit AssetActivated(path);
			});
			menu.addAction("Rename...", QKeySequence(Qt::Key_F2), this, &ContentBrowser::RenameSelected);
			menu.addAction("Duplicate", this, &ContentBrowser::DuplicateSelected);
			menu.addAction("Delete", QKeySequence::Delete, this, &ContentBrowser::DeleteSelected);
			menu.addSeparator();
			menu.addAction("Copy Path", this, [path]() {
				const QString relative = QDir(QString::fromUtf8(Paths::GetContentDir().CStr())).relativeFilePath(path);
				QApplication::clipboard()->setText(relative.startsWith("..") ? path : relative);
			});
		}
		else
		{
			menu.addAction("New Folder...", this, &ContentBrowser::NewFolder);
			if (inScripts)
			{
				menu.addAction("New Script...", this, [this]() { emit NewScriptRequested(); });
			}
			else
			{
				menu.addAction("New Material...", this, &ContentBrowser::NewMaterial);
				menu.addAction("New Scene...", this, &ContentBrowser::NewScene);
				menu.addAction("New UI Page...", this, &ContentBrowser::NewPage);
			}
		}
		menu.addSeparator();
#if defined(Q_OS_MACOS)
		const char* revealLabel = "Show in Finder";
#elif defined(Q_OS_WIN)
		const char* revealLabel = "Show in Explorer";
#else
		const char* revealLabel = "Show in File Manager";
#endif
		const QString reveal = clicked ? clicked->data(PathRole).toString() : m_CurrentFolder;
		menu.addAction(revealLabel, this, [reveal]() {
#if defined(Q_OS_MACOS)
			QProcess::startDetached("open", { "-R", reveal });
#elif defined(Q_OS_WIN)
			QProcess::startDetached("explorer", { "/select,", QDir::toNativeSeparators(reveal) });
#else
			QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(reveal).absolutePath()));
#endif
		});
		menu.exec(m_Files->viewport()->mapToGlobal(position));
	}

	void ContentBrowser::NewFolder()
	{
		const QString name = AskForName("New Folder", "New Folder");
		if (name.isEmpty())
			return;
		const QString path = m_CurrentFolder + "/" + name;
		if (!QDir().mkpath(path))
		{
			QMessageBox::warning(this, "New Folder", "Could not create " + path);
			return;
		}
		SelectPath(path);
	}

	void ContentBrowser::NewMaterial()
	{
		const QString name = AskForName("New Material", "NewMaterial");
		if (name.isEmpty())
			return;
		const QString path = m_CurrentFolder + "/" + name + ".material";
		if (QFileInfo::exists(path))
		{
			QMessageBox::warning(this, "New Material", name + " already exists.");
			return;
		}
		const QString relative = QDir(m_ContentRoot).relativeFilePath(path);
		if (!MaterialLibrary::Get().Create(String(relative.toUtf8().constData())))
			return;
		SelectPath(path);
		emit AssetSelected(path);
	}

	void ContentBrowser::NewPage()
	{
		const QString name = AskForName("New UI Page", "NewPage");
		if (name.isEmpty())
			return;
		const QString path = m_CurrentFolder + "/" + name + ".html";
		if (QFileInfo::exists(path))
		{
			QMessageBox::warning(this, "New UI Page", name + " already exists.");
			return;
		}
		// A starter that shows each way the page and scripts talk.
		const QString page = QString::fromUtf8(R"html(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<style>
	/* The page covers the whole screen and is see through wherever nothing is drawn. */
	body { margin: 0; font-family: Helvetica, Arial, sans-serif; color: white; }
	.panel { position: absolute; top: 24px; left: 24px; padding: 12px 18px; border-radius: 10px; background: rgba(0, 0, 0, 0.6); }
	button { font: inherit; margin-left: 12px; padding: 6px 14px; border: 0; border-radius: 8px; background: #3a8dff; color: white; }
	button:hover { background: #5aa2ff; }
</style>
</head>
<body>
	<div class="panel">
		<!-- A script fills this in with GetUI()->Set("score", 10). -->
		Score: <b data-bind="score">0</b>
		<!-- Scripts on this entity and the scene hear it in OnUIEvent("clicked", data). -->
		<button onclick="engine.send('clicked', { time: Date.now() })">Click me</button>
	</div>
	<script>
		// A script sends this with GetUI()->Emit("hello", "{"name":"Noodle"}").
		engine.on("hello", function (data) { console.log("Hello " + data.name); });
		// Runs whenever a script sets score.
		engine.watch("score", function (score) { document.title = "Score " + score; });
	</script>
</body>
</html>
)html");
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		{
			QMessageBox::warning(this, "New UI Page", "Could not write " + path);
			return;
		}
		file.write(page.toUtf8());
		file.close();
		SelectPath(path);
		emit AssetSelected(path);
	}

	void ContentBrowser::NewScene()
	{
		const QString name = AskForName("New Scene", "NewScene");
		if (name.isEmpty())
			return;
		const QString path = m_CurrentFolder + "/" + name + ".scene";
		if (QFileInfo::exists(path))
		{
			QMessageBox::warning(this, "New Scene", name + " already exists.");
			return;
		}
		// A scene with just a camera, the same as File > New Scene.
		const QString scene = QString(
			"# FirstPrinciples scene\nversion 2\n\nentity %1\ncomponent Name\n  Name \"Main Camera\"\n"
			"component Transform\n  Position 0 0 0\n  Rotation 0 0 0\n  Scale 1 1 1\ncomponent Camera\n  Primary true\n")
			.arg(QString::fromUtf8(Guid::New().ToString().CStr()));
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		{
			QMessageBox::warning(this, "New Scene", "Could not write " + path);
			return;
		}
		file.write(scene.toUtf8());
		file.close();
		SelectPath(path);
	}

	int ContentBrowser::ReplaceReferences(const QString& oldPath, const QString& newPath)
	{
		const QString contentRoot = QString::fromUtf8(Paths::GetContentDir().CStr());
		const QByteArray from = ("\"" + oldPath + "\"").toUtf8();
		const QByteArray to = ("\"" + newPath + "\"").toUtf8();
		int changed = 0;
		QDirIterator it(contentRoot, { "*.scene", "*.material" }, QDir::Files, QDirIterator::Subdirectories);
		while (it.hasNext())
		{
			QFile file(it.next());
			if (!file.open(QIODevice::ReadOnly))
				continue;
			QByteArray text = file.readAll();
			file.close();
			if (!text.contains(from))
				continue;
			text.replace(from, to);
			if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
			{
				file.write(text);
				++changed;
			}
		}
		return changed;
	}

	void ContentBrowser::RenameSelected()
	{
		const QStringList selected = SelectedPaths();
		if (selected.size() != 1)
			return;
		const QFileInfo info(selected.first());
		const QString name = AskForName("Rename", info.isDir() ? info.fileName() : info.completeBaseName());
		if (name.isEmpty() || name == info.completeBaseName())
			return;
		const QString target = info.absolutePath() + "/" + name + (info.isDir() || info.suffix().isEmpty() ? "" : "." + info.suffix());
		if (QFileInfo::exists(target))
		{
			QMessageBox::warning(this, "Rename", name + " already exists.");
			return;
		}
		if (!QDir().rename(info.absoluteFilePath(), target))
		{
			QMessageBox::warning(this, "Rename", "Could not rename " + info.fileName());
			return;
		}

		// Keep scenes and materials pointing at the file under its new name.
		const QDir content(m_ContentRoot);
		const QString oldRelative = content.relativeFilePath(info.absoluteFilePath());
		const QString newRelative = content.relativeFilePath(target);
		int changed = 0;
		if (info.isDir())
		{
			QDirIterator it(target, QDir::Files, QDirIterator::Subdirectories);
			while (it.hasNext())
			{
				const QString moved = content.relativeFilePath(it.next());
				changed += ReplaceReferences(oldRelative + moved.mid(newRelative.size()), moved);
			}
		}
		else if (!oldRelative.startsWith(".."))
		{
			changed = ReplaceReferences(oldRelative, newRelative);
		}
		if (changed > 0)
		{
			MaterialLibrary::Get().Clear();
			emit ReferencesChanged();
		}
		SelectPath(target);
	}

	void ContentBrowser::DuplicateSelected()
	{
		QString last;
		for (const QString& path : SelectedPaths())
		{
			const QFileInfo info(path);
			if (info.isDir())
				continue;
			QString copy;
			for (int n = 1; copy.isEmpty() || QFileInfo::exists(copy); ++n)
				copy = info.absolutePath() + "/" + info.completeBaseName() + (n == 1 ? " Copy" : QString(" Copy %1").arg(n)) + "." + info.suffix();
			if (QFile::copy(path, copy))
				last = copy;
		}
		if (!last.isEmpty())
			SelectPath(last);
	}

	void ContentBrowser::DeleteSelected()
	{
		const QStringList selected = SelectedPaths();
		if (selected.isEmpty())
			return;
		QStringList names;
		for (const QString& path : selected)
			names.append(QFileInfo(path).fileName());
		const auto answer = QMessageBox::question(this, "Delete",
			QString("Move to the Trash?\n\n%1\n\nScenes and materials that use them will show them as missing.").arg(names.join("\n")));
		if (answer != QMessageBox::Yes)
			return;
		for (const QString& path : selected)
		{
			// Trash instead of deleting so a mistake can be undone from the OS.
			if (!QFile::moveToTrash(path))
				FP_LOG_ERROR("Could not move %s to the Trash", path.toUtf8().constData());
		}
		Refresh();
	}

	bool ContentBrowser::IsHidden(const QString& name) const
	{
		// Dot files are OS noise.
		return name.startsWith('.');
	}

	QString ContentBrowser::AssetKind(const QString& path)
	{
		const QString suffix = QFileInfo(path).suffix().toLower();
		if (suffix == "actions") return "Input Actions";
		if (suffix == "scene") return "Scene";
		if (suffix == "material") return "Material";
		if (suffix == "wav" || suffix == "mp3") return "Audio";
		if (QStringList{ "png", "jpg", "jpeg", "tga", "bmp" }.contains(suffix)) return "Texture";
		if (QStringList{ "obj", "gltf", "glb" }.contains(suffix)) return "Mesh";
		if (QStringList{ "cpp", "h", "hpp" }.contains(suffix)) return "Script";
		if (QStringList{ "html", "htm" }.contains(suffix)) return "Page";
		if (QStringList{ "css", "js", "json" }.contains(suffix)) return "Web";
		return "File";
	}

	void ContentBrowser::ShowFolder(const QString& absolutePath)
	{
		const QString folder = QDir::cleanPath(absolutePath);
		if (folder != m_CurrentFolder && !m_CurrentFolder.isEmpty())
		{
			m_BackHistory.append(m_CurrentFolder);
			m_ForwardHistory.clear();
		}
		m_CurrentFolder = folder;
		RebuildFiles();

		// Keep the tree selection in step without bouncing back into this function.
		const QSignalBlocker block(m_Folders);
		for (QTreeWidgetItemIterator it(m_Folders); *it; ++it)
		{
			if ((*it)->data(0, PathRole).toString() == m_CurrentFolder)
			{
				m_Folders->setCurrentItem(*it);
				break;
			}
		}
	}

	void ContentBrowser::Refresh()
	{
		m_IconCache.clear();
		m_Thumbnails.Clear();
		if (!QFileInfo(m_CurrentFolder).isDir())
			m_CurrentFolder = m_ContentRoot;
		RebuildTree();
		RebuildFiles();
		WatchFolders();
	}

	void ContentBrowser::AddFolderItems(QTreeWidgetItem* parent, const QString& path)
	{
		const QFileInfoList dirs = QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
		for (const QFileInfo& dir : dirs)
		{
			if (IsHidden(dir.fileName()))
				continue;
			QTreeWidgetItem* item = new QTreeWidgetItem(parent, { dir.fileName() });
			item->setIcon(0, EditorIcons::Get(EditorIcon::Folder));
			item->setData(0, PathRole, dir.absoluteFilePath());
			AddFolderItems(item, dir.absoluteFilePath());
		}
	}

	void ContentBrowser::RebuildTree()
	{
		const QSignalBlocker block(m_Folders);
		m_Folders->clear();
		for (const auto& [label, path] : { std::pair{ QString("Content"), m_ContentRoot }, std::pair{ QString("Scripts"), m_ScriptsRoot } })
		{
			QTreeWidgetItem* root = new QTreeWidgetItem(m_Folders, { label });
			root->setIcon(0, EditorIcons::Get(EditorIcon::FolderOpen));
			root->setData(0, PathRole, path);
			AddFolderItems(root, path);
			root->setExpanded(true);
		}
		ShowFolder(m_CurrentFolder);
	}

	void ContentBrowser::RebuildFiles()
	{
		m_ThumbnailTimer->stop();
		m_PendingThumbnails.clear();
		m_Thumbnails.Clear();
		m_Files->clear();
		const QString filter = m_Search->text().trimmed();
		const QDir project(ProjectDir());
		RebuildCrumbs();

		QFileInfoList entries;
		if (filter.isEmpty())
		{
			entries = QDir(m_CurrentFolder).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
		}
		else
		{
			// Searching looks through every folder below the current one.
			QDirIterator it(m_CurrentFolder, QDir::Files, QDirIterator::Subdirectories);
			while (it.hasNext())
			{
				const QFileInfo info(it.next());
				if (info.fileName().contains(filter, Qt::CaseInsensitive))
					entries.append(info);
			}
		}

		for (const QFileInfo& info : entries)
		{
			if (IsHidden(info.fileName()))
				continue;
			QListWidgetItem* item = new QListWidgetItem(IconFor(info.absoluteFilePath()), info.isDir() ? info.fileName() : info.completeBaseName());
			item->setData(PathRole, info.absoluteFilePath());
			item->setToolTip(QString("%1\n%2").arg(project.relativeFilePath(info.absoluteFilePath()), info.isDir() ? "Folder" : AssetKind(info.fileName())));
			m_Files->addItem(item);
		}
		if (!m_PendingThumbnails.isEmpty())
			m_ThumbnailTimer->start();
		m_Count->setText(QString("%1 items%2").arg(m_Files->count()).arg(filter.isEmpty() ? "" : QString(" matching \"%1\"").arg(filter)));
	}

	void ContentBrowser::RebuildCrumbs()
	{
		// All > Content > Materials, each one a button that jumps there.
		QLayout* layout = m_Crumbs->layout();
		while (QLayoutItem* item = layout->takeAt(0))
		{
			delete item->widget();
			delete item;
		}
		auto addCrumb = [this, layout](const QString& text, const QString& path, bool folderIcon) {
			QToolButton* crumb = new QToolButton();
			crumb->setText(text);
			if (folderIcon)
			{
				crumb->setIcon(EditorIcons::Get(EditorIcon::Folder));
				crumb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
			}
			crumb->setAutoRaise(true);
			crumb->setStyleSheet("QToolButton { background: transparent; border: none; padding: 4px 4px; font-size: 13px; } QToolButton:hover { color: #ffffff; }");
			connect(crumb, &QToolButton::clicked, this, [this, path]() { ShowFolder(path); });
			layout->addWidget(crumb);
			QLabel* chevron = new QLabel();
			chevron->setPixmap(EditorIcons::Get(EditorIcon::Chevron).pixmap(12, 12));
			layout->addWidget(chevron);
		};

		addCrumb("All", m_ContentRoot, true);
		const QString root = m_CurrentFolder.startsWith(m_ScriptsRoot) ? m_ScriptsRoot : m_ContentRoot;
		addCrumb(root == m_ScriptsRoot ? "Scripts" : "Content", root, false);
		const QString relative = QDir(root).relativeFilePath(m_CurrentFolder);
		QString path = root;
		for (const QString& part : relative.split('/', Qt::SkipEmptyParts))
		{
			if (part == ".")
				continue;
			path += "/" + part;
			addCrumb(part, path, false);
		}
		static_cast<QHBoxLayout*>(layout)->addStretch(1);

		m_Back->setEnabled(!m_BackHistory.isEmpty());
		m_Forward->setEnabled(!m_ForwardHistory.isEmpty());
	}

	void ContentBrowser::GoBack()
	{
		if (m_BackHistory.isEmpty())
			return;
		m_ForwardHistory.append(m_CurrentFolder);
		const QString target = m_BackHistory.takeLast();
		m_CurrentFolder.clear(); // so ShowFolder does not record this step again
		ShowFolder(target);
	}

	void ContentBrowser::GoForward()
	{
		if (m_ForwardHistory.isEmpty())
			return;
		const QString target = m_ForwardHistory.takeLast();
		const QStringList forward = m_ForwardHistory;
		ShowFolder(target);
		m_ForwardHistory = forward; // ShowFolder clears it for new browsing, not for stepping forward
		RebuildCrumbs();
	}

	void ContentBrowser::ImportFiles()
	{
		const QStringList files = QFileDialog::getOpenFileNames(this, "Import", QString(), "Assets (*.actions *.png *.jpg *.jpeg *.tga *.bmp *.obj *.gltf *.glb *.bin *.material *.scene);;All files (*)");
		QString last;
		for (const QString& file : files)
		{
			const QString target = m_CurrentFolder + "/" + QFileInfo(file).fileName();
			if (QFileInfo::exists(target))
			{
				FP_LOG_WARN("Skipped %s, a file with that name is already here", QFileInfo(file).fileName().toUtf8().constData());
				continue;
			}
			if (QFile::copy(file, target))
				last = target;
			else
				FP_LOG_ERROR("Could not import %s", file.toUtf8().constData());
		}
		if (!last.isEmpty())
			SelectPath(last);
	}

	void ContentBrowser::WatchFolders()
	{
		if (!m_Watcher->directories().isEmpty())
			m_Watcher->removePaths(m_Watcher->directories());
		if (!m_Watcher->files().isEmpty())
			m_Watcher->removePaths(m_Watcher->files());
		QStringList dirs;
		QStringList files;
		for (const QString& root : { m_ContentRoot, m_ScriptsRoot })
		{
			dirs.append(root);
			QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
			while (it.hasNext())
			{
				const QString path = it.next();
				if (it.fileInfo().isDir())
					dirs.append(path);
				else if (AssetKind(path) == "Texture" || AssetKind(path) == "Material" || AssetKind(path) == "Mesh" || it.fileInfo().suffix() == "bin")
					files.append(path);
			}
		}
		m_Watcher->addPaths(dirs);
		if (!files.isEmpty())
			m_Watcher->addPaths(files);
	}

	QIcon ContentBrowser::TileIcon(EditorIcon icon, const QColor& color) const
	{
		// UE style asset tile: a dark card with the type's icon and a colored strip along the bottom.
		QPixmap pixmap(IconSize * 2, IconSize * 2);
		pixmap.setDevicePixelRatio(2);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing);
		const QRectF card(4, 4, IconSize - 8, IconSize - 8);
		QPainterPath shape;
		shape.addRoundedRect(card, 4, 4);
		painter.fillPath(shape, QColor(0x2a, 0x2a, 0x2a));
		painter.setClipPath(shape);
		painter.fillRect(QRectF(card.left(), card.bottom() - 4, card.width(), 4), color);
		painter.setClipping(false);
		const int glyph = IconSize / 2;
		EditorIcons::Get(icon, color.lighter(130)).paint(&painter, QRect(IconSize / 2 - glyph / 2, IconSize / 2 - glyph / 2 - 2, glyph, glyph));
		return QIcon(pixmap);
	}

	QIcon ContentBrowser::ThumbnailIcon(const QImage& image, const QColor& color) const
	{
		QPixmap pixmap(IconSize * 2, IconSize * 2);
		pixmap.setDevicePixelRatio(2);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setRenderHint(QPainter::SmoothPixmapTransform);
		const QRectF card(4, 4, IconSize - 8, IconSize - 8);
		QPainterPath shape;
		shape.addRoundedRect(card, 4, 4);
		painter.setClipPath(shape);
		// A checkerboard makes transparent textures visible.
		for (int y = 4; y < IconSize - 4; y += 8)
			for (int x = 4; x < IconSize - 4; x += 8)
				painter.fillRect(x, y, 8, 8, (x / 8 + y / 8) % 2 ? QColor(75, 75, 75) : QColor(50, 50, 50));
		const QSizeF size = QSizeF(image.size()).scaled(card.size(), Qt::KeepAspectRatio);
		painter.drawImage(QRectF(card.center() - QPointF(size.width() / 2, size.height() / 2), size), image);
		painter.fillRect(QRectF(card.left(), card.bottom() - 4, card.width(), 4), color);
		return QIcon(pixmap);
	}

	void ContentBrowser::RenderNextThumbnail()
	{
		if (m_PendingThumbnails.isEmpty())
		{
			m_ThumbnailTimer->stop();
			m_Thumbnails.Clear();
			return;
		}
		const QString path = m_PendingThumbnails.takeFirst();
		const bool material = AssetKind(path) == "Material";
		const QColor color = material ? QColor(0x80, 0xc0, 0x60) : QColor(0x3a, 0xa0, 0xd0);
		const QImage image = m_Thumbnails.Render(path, material);
		const QIcon icon = image.isNull() ? TileIcon(material ? EditorIcon::Material : EditorIcon::Mesh, color) : ThumbnailIcon(image, color);
		m_IconCache.insert(path, new QIcon(icon));
		for (int i = 0; i < m_Files->count(); ++i)
		{
			QListWidgetItem* item = m_Files->item(i);
			if (item->data(PathRole).toString() == path)
			{
				item->setIcon(icon);
				break;
			}
		}
	}

	QIcon ContentBrowser::IconFor(const QString& path)
	{
		const QFileInfo info(path);
		if (info.isDir())
			return EditorIcons::Get(EditorIcon::Folder);

		if (const QIcon* cached = m_IconCache.object(path))
			return *cached;

		QIcon icon;
		const QString kind = AssetKind(path);
		if (kind == "Texture")
		{
			// Ask the reader for a small image so big textures stay cheap to browse.
			QImageReader reader(path);
			const QSize size = reader.size();
			if (size.isValid())
				reader.setScaledSize(size.scaled(IconSize * 2, IconSize * 2, Qt::KeepAspectRatio));
			reader.setAutoTransform(true);
			QImage image = reader.read();
			if (image.isNull() && info.suffix().compare("tga", Qt::CaseInsensitive) == 0)
			{
				const String relative(QDir(m_ContentRoot).relativeFilePath(path).toUtf8().constData());
				const Array<DecodedImage> decoded = DecodeImages({ relative });
				if (decoded[0].Pixels)
					image = QImage(decoded[0].Pixels, decoded[0].Width, decoded[0].Height, QImage::Format_RGBA8888)
						.scaled(IconSize * 2, IconSize * 2, Qt::KeepAspectRatio, Qt::SmoothTransformation);
			}
			icon = image.isNull() ? TileIcon(EditorIcon::Texture, QColor(0xd0, 0x5a, 0x5a)) : ThumbnailIcon(image, QColor(0xd0, 0x5a, 0x5a));
		}
		else if (kind == "Material" || kind == "Mesh")
		{
			m_PendingThumbnails.append(path);
			return kind == "Material" ? TileIcon(EditorIcon::Material, QColor(0x80, 0xc0, 0x60)) : TileIcon(EditorIcon::Mesh, QColor(0x3a, 0xa0, 0xd0));
		}
		else if (kind == "Scene") icon = TileIcon(EditorIcon::Scene, QColor(0xf0, 0xa0, 0x30));
		else if (kind == "Input Actions") icon = TileIcon(EditorIcon::Script, QColor(0x60, 0xc0, 0xb0));
		else if (kind == "Script") icon = TileIcon(EditorIcon::Script, QColor(0x4f, 0xa3, 0xe0));
		else if (kind == "Page") icon = TileIcon(EditorIcon::Script, QColor(0xe0, 0x8a, 0x3a));
		else if (kind == "Web") icon = TileIcon(EditorIcon::Script, QColor(0xc0, 0xa0, 0x40));
		else icon = TileIcon(EditorIcon::OutputLog, QColor(0x80, 0x80, 0x80));

		m_IconCache.insert(path, new QIcon(icon));
		return icon;
	}
}
