#include "Editor/ContentBrowser.h"
#include "Engine/Material.h"
#include "Engine/Paths.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
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
		m_Files = new QListWidget();
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
