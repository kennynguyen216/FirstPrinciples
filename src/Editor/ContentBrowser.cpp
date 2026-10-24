#include "Editor/ContentBrowser.h"
#include "Engine/Material.h"
#include "Engine/Paths.h"
#include "Core/Misc/Guid.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
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

	ContentBrowser::ContentBrowser(QWidget* parent) : QWidget(parent)
	{
		m_ContentRoot = QDir::cleanPath(QString::fromUtf8(Paths::GetContentDir().CStr()));
		m_ScriptsRoot = QDir::cleanPath(ProjectDir() + "/Game/Scripts");

		QVBoxLayout* layout = new QVBoxLayout(this);
		layout->setContentsMargins(4, 4, 4, 4);
		layout->setSpacing(4);

		QHBoxLayout* bar = new QHBoxLayout();
		m_Location = new QLabel();
		m_Location->setTextInteractionFlags(Qt::TextSelectableByMouse);
		m_Search = new QLineEdit();
		m_Search->setPlaceholderText("Search");
		m_Search->setClearButtonEnabled(true);
		m_Search->setMaximumWidth(240);
		bar->addWidget(m_Location, 1);
		bar->addWidget(m_Search);
		layout->addLayout(bar);

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
		split->addWidget(m_Folders);
		split->addWidget(m_Files);
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
		// Engine shaders are not something a game edits, and dot files are OS noise.
		return name.startsWith('.') || name == "Engine";
	}

	QString ContentBrowser::AssetKind(const QString& path)
	{
		const QString suffix = QFileInfo(path).suffix().toLower();
		if (suffix == "scene") return "Scene";
		if (suffix == "material") return "Material";
		if (QStringList{ "png", "jpg", "jpeg", "tga", "bmp" }.contains(suffix)) return "Texture";
		if (suffix == "obj") return "Mesh";
		if (QStringList{ "cpp", "h", "hpp" }.contains(suffix)) return "Script";
		return "File";
	}

	void ContentBrowser::ShowFolder(const QString& absolutePath)
	{
		m_CurrentFolder = QDir::cleanPath(absolutePath);
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
			root->setData(0, PathRole, path);
			AddFolderItems(root, path);
			root->setExpanded(true);
		}
		ShowFolder(m_CurrentFolder);
	}

	void ContentBrowser::RebuildFiles()
	{
		m_Files->clear();
		const QString filter = m_Search->text().trimmed();
		const QDir project(ProjectDir());
		m_Location->setText(project.relativeFilePath(m_CurrentFolder) + (filter.isEmpty() ? "" : QString("  (searching for \"%1\")").arg(filter)));

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
				if (info.fileName().contains(filter, Qt::CaseInsensitive) && !info.absoluteFilePath().contains("/Engine/"))
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
	}

	void ContentBrowser::WatchFolders()
	{
		if (!m_Watcher->directories().isEmpty())
			m_Watcher->removePaths(m_Watcher->directories());
		QStringList dirs;
		for (const QString& root : { m_ContentRoot, m_ScriptsRoot })
		{
			dirs.append(root);
			QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
			while (it.hasNext())
				dirs.append(it.next());
		}
		m_Watcher->addPaths(dirs);
	}

	QIcon ContentBrowser::BadgeIcon(const QString& text, const QColor& color) const
	{
		QPixmap pixmap(IconSize, IconSize);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setBrush(color);
		painter.setPen(Qt::NoPen);
		painter.drawRoundedRect(QRectF(6, 10, IconSize - 12, IconSize - 20), 8, 8);
		painter.setPen(Qt::white);
		QFont font = painter.font();
		font.setBold(true);
		font.setPixelSize(13);
		painter.setFont(font);
		painter.drawText(pixmap.rect(), Qt::AlignCenter, text);
		return QIcon(pixmap);
	}

	QIcon ContentBrowser::IconFor(const QString& path)
	{
		const QFileInfo info(path);
		if (info.isDir())
			return BadgeIcon("DIR", QColor(90, 90, 100));

		const QString key = path + "|" + QString::number(info.lastModified().toMSecsSinceEpoch());
		if (const auto cached = m_IconCache.find(key); cached != m_IconCache.end())
			return *cached;

		QIcon icon;
		const QString kind = AssetKind(path);
		if (kind == "Texture")
		{
			// Ask the reader for a small image so big textures stay cheap to browse.
			QImageReader reader(path);
			const QSize size = reader.size();
			if (size.isValid())
				reader.setScaledSize(size.scaled(IconSize, IconSize, Qt::KeepAspectRatio));
			const QImage image = reader.read();
			icon = image.isNull() ? BadgeIcon("IMG", QColor(120, 80, 160)) : QIcon(QPixmap::fromImage(image));
		}
		else if (kind == "Material")
		{
			// A swatch of the base color, or the base color map when there is one.
			const QString relative = QDir(m_ContentRoot).relativeFilePath(path);
			const Material* material = MaterialLibrary::Get().Load(String(relative.toUtf8().constData()));
			QPixmap pixmap(IconSize, IconSize);
			pixmap.fill(Qt::transparent);
			QPainter painter(&pixmap);
			painter.setRenderHint(QPainter::Antialiasing);
			QColor base(160, 160, 160);
			QImage map;
			if (material)
			{
				const MaterialData& data = material->GetData();
				base = QColor::fromRgbF(data.BaseColor.R, data.BaseColor.G, data.BaseColor.B);
				if (!data.BaseColorMap.IsEmpty())
					map = QImage(m_ContentRoot + "/" + QString::fromUtf8(data.BaseColorMap.CStr())).scaled(IconSize, IconSize);
			}
			QPainterPath circle;
			circle.addEllipse(QRectF(6, 6, IconSize - 12, IconSize - 12));
			painter.setClipPath(circle);
			if (!map.isNull())
				painter.drawImage(0, 0, map);
			else
				painter.fillRect(pixmap.rect(), base);
			painter.setClipping(false);
			painter.setPen(QPen(QColor(255, 255, 255, 60), 2));
			painter.drawEllipse(QRectF(6, 6, IconSize - 12, IconSize - 12));
			icon = QIcon(pixmap);
		}
		else if (kind == "Scene") icon = BadgeIcon("SCENE", QColor(40, 120, 200));
		else if (kind == "Mesh") icon = BadgeIcon("MESH", QColor(200, 130, 40));
		else if (kind == "Script") icon = BadgeIcon(info.suffix().toLower() == "h" ? ".H" : "C++", QColor(70, 150, 80));
		else icon = BadgeIcon(info.suffix().toUpper().left(4), QColor(100, 100, 100));

		m_IconCache.insert(key, icon);
		return icon;
	}
}
