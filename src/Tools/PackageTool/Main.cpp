// Builds a distributable folder from Release binaries and cooked content.
#include "Core/Misc/Pak.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextStream>

#include <filesystem>
#include <stdexcept>

namespace
{
	QString Run(const QString& program, const QStringList& arguments)
	{
		QProcess process;
		process.start(program, arguments);
		if (!process.waitForStarted() || !process.waitForFinished(-1) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
			throw std::runtime_error((program + " failed: " + QString::fromLocal8Bit(process.readAllStandardError()) + process.errorString()).toStdString());
		return QString::fromLocal8Bit(process.readAllStandardOutput());
	}

	std::filesystem::path Native(const QString& path)
	{
		return std::filesystem::path(reinterpret_cast<const char8_t*>(path.toUtf8().constData()));
	}

	void Copy(const QString& source, const QString& destination)
	{
		if (!QDir().mkpath(QFileInfo(destination).absolutePath()) || !QFile::copy(source, destination))
			throw std::runtime_error(("Could not copy " + source + " to " + destination).toStdString());
	}

	void CopyDirectory(const QString& source, const QString& destination)
	{
		if (!QDir(source).exists())
			throw std::runtime_error(("Missing resources: " + source).toStdString());
		QDirIterator it(source, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
		while (it.hasNext())
		{
			const QString path = it.next();
			Copy(path, destination + "/" + QDir(source).relativeFilePath(path));
		}
	}

	void Write(const QString& path, const QByteArray& bytes)
	{
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.flush())
			throw std::runtime_error(("Could not write " + path).toStdString());
	}

#if defined(__APPLE__)
	QStringList Dependencies(const QString& binary)
	{
		QStringList result;
		const auto lines = Run("/usr/bin/otool", { "-L", binary }).split('\n');
		for (const auto& line : lines.mid(1))
		{
			const QString dependency = line.trimmed().section(" (compatibility", 0, 0);
			if (!dependency.isEmpty())
				result.append(dependency);
		}
		return result;
	}

	QString ResolveLibrary(const QString& dependency, const QString& source, const QString& binaries)
	{
		QString candidate = dependency;
		candidate.replace("@loader_path", QFileInfo(source).absolutePath());
		candidate.replace("@executable_path", binaries);
		if (QFileInfo(candidate).isAbsolute() && QFileInfo::exists(candidate))
			return QFileInfo(candidate).canonicalFilePath();
		if (dependency.startsWith("@rpath/"))
		{
			const auto lines = Run("/usr/bin/otool", { "-l", source }).split('\n');
			for (qsizetype i = 0; i + 2 < lines.size(); ++i)
			{
				if (lines[i].trimmed() != "cmd LC_RPATH")
					continue;
				QString path = lines[i + 2].trimmed().section(" (offset", 0, 0).mid(5);
				path.replace("@loader_path", QFileInfo(source).absolutePath());
				path.replace("@executable_path", binaries);
				candidate = path + "/" + dependency.mid(7);
				if (QFileInfo::exists(candidate))
					return QFileInfo(candidate).canonicalFilePath();
			}
			candidate = binaries + "/" + QFileInfo(dependency).fileName();
			if (QFileInfo::exists(candidate))
				return QFileInfo(candidate).canonicalFilePath();
		}
		throw std::runtime_error(("Could not resolve runtime library " + dependency + " used by " + source).toStdString());
	}

	void DeployLibraries(const QString& runtime, const QString& scripts, const QString& destination)
	{
		struct Binary
		{
			QString Source;
			QString Destination;
		};
		QList<Binary> pending = { { runtime, destination + "/Runtime" }, { scripts, destination + "/GameScripts.dylib" } };
		QMap<QString, QString> copied;
		for (const auto& binary : pending)
		{
			Copy(binary.Source, binary.Destination);
			copied.insert(QFileInfo(binary.Source).canonicalFilePath(), QFileInfo(binary.Destination).fileName());
		}
		// Resolve from the original binaries before replacing paths in their packaged copies.
		for (qsizetype i = 0; i < pending.size(); ++i)
		{
			const Binary binary = pending[i];
			const QStringList dependencies = Dependencies(binary.Source);
			for (const QString& dependency : dependencies)
			{
				if (dependency.startsWith("/System/Library/") || dependency.startsWith("/usr/lib/"))
					continue;
				const QString source = ResolveLibrary(dependency, binary.Source, QFileInfo(runtime).absolutePath());
				QString name = copied.value(source);
				if (name.isEmpty())
				{
					name = QFileInfo(source).fileName();
					if (copied.values().contains(name))
						throw std::runtime_error(("Runtime library name collision: " + name).toStdString());
					Copy(source, destination + "/" + name);
					copied.insert(source, name);
					pending.append({ source, destination + "/" + name });
				}
				Run("/usr/bin/install_name_tool", { "-change", dependency, "@loader_path/" + name, binary.Destination });
			}
			if (binary.Destination.endsWith(".dylib"))
				Run("/usr/bin/install_name_tool", { "-id", "@loader_path/" + QFileInfo(binary.Destination).fileName(), binary.Destination });
		}
		// Editing load commands invalidates the original signatures.
		for (const auto& binary : pending)
			Run("/usr/bin/codesign", { "--force", "--sign", "-", binary.Destination });
	}
#elif defined(_WIN32)
	QStringList Dependencies(const QString& binary)
	{
		// dumpbin is installed with the same MSVC toolchain that builds the game.
		QString dumpbin = qEnvironmentVariable("VCToolsInstallDir") + "/bin/Hostx64/x64/dumpbin.exe";
		if (!QFileInfo::exists(dumpbin))
		{
			const QString vswhere = qEnvironmentVariable("ProgramFiles(x86)") + "/Microsoft Visual Studio/Installer/vswhere.exe";
			const QString installation = Run(vswhere, { "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath" }).trimmed();
			const QDir tools(installation + "/VC/Tools/MSVC");
			const auto versions = tools.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
			if (versions.isEmpty())
				throw std::runtime_error("MSVC tools were not found");
			dumpbin = tools.absoluteFilePath(versions.first() + "/bin/Hostx64/x64/dumpbin.exe");
		}
		QStringList result;
		const auto lines = Run(dumpbin, { "/nologo", "/dependents", binary }).split('\n');
		const QRegularExpression dll("^\\s+([^\\s]+\\.dll)\\s*$", QRegularExpression::CaseInsensitiveOption);
		for (const auto& line : lines)
		{
			const auto match = dll.match(line);
			if (match.hasMatch())
				result.append(match.captured(1));
		}
		return result;
	}

	void DeployLibraries(const QString& runtime, const QString& scripts, const QString& destination)
	{
		const QString binaries = QFileInfo(runtime).absolutePath();
		QStringList pending = { runtime, scripts };
		QSet<QString> copied;
		for (const auto& source : pending)
		{
			Copy(source, destination + "/" + QFileInfo(source).fileName());
			copied.insert(QFileInfo(source).fileName().toLower());
		}
		QStringList redist;
		const QString vswhere = qEnvironmentVariable("ProgramFiles(x86)") + "/Microsoft Visual Studio/Installer/vswhere.exe";
		const QString installation = Run(vswhere, { "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath" }).trimmed();
		QDirIterator it(installation + "/VC/Redist/MSVC", { "*.dll" }, QDir::Files, QDirIterator::Subdirectories);
		while (it.hasNext())
		{
			const QString path = it.next();
			if (path.contains("/x64/") && !path.contains("/debug_", Qt::CaseInsensitive))
				redist.append(path);
		}
		redist.sort();
		for (qsizetype i = 0; i < pending.size(); ++i)
		{
			for (const QString& dependency : Dependencies(pending[i]))
			{
				const QString key = dependency.toLower();
				if (copied.contains(key))
					continue;
				QString source = binaries + "/" + dependency;
				if (!QFileInfo::exists(source))
				{
					for (auto file = redist.crbegin(); file != redist.crend(); ++file)
					{
						if (QFileInfo(*file).fileName().compare(dependency, Qt::CaseInsensitive) == 0)
						{
							source = *file;
							break;
						}
					}
				}
				if (!QFileInfo::exists(source))
				{
					if (key.startsWith("api-ms-") || key.startsWith("ext-ms-") || QFileInfo::exists(qEnvironmentVariable("SystemRoot") + "/System32/" + dependency))
						continue;
					throw std::runtime_error(("Missing runtime library: " + dependency).toStdString());
				}
				Copy(source, destination + "/" + dependency);
				copied.insert(key);
				pending.append(source);
			}
		}
	}
#else
#error PackageTool supports macOS and Windows
#endif

	void Package(const QString& root, const QString& project, const QString& output)
	{
		if (QFileInfo::exists(output))
			throw std::runtime_error("The package destination already exists. Choose a new destination.");
		const QStringList projects = QDir(project).entryList({ "*.fpproject" }, QDir::Files);
		if (projects.size() != 1)
			throw std::runtime_error("The project folder must contain one .fpproject file");
		const QString projectFile = project + "/" + projects.first();
		QFile descriptor(projectFile);
		if (!descriptor.open(QIODevice::ReadOnly))
			throw std::runtime_error("Could not read the project");
		const QString text = QString::fromUtf8(descriptor.readAll());
		const auto name = QRegularExpression("(?:^|\\n)Name\\s+\"?([A-Za-z][A-Za-z0-9_]*)\"?\\s*(?:\\n|$)").match(text);
		const auto scene = QRegularExpression("(?:^|\\n)StartScene\\s+(?:\"([^\"]+)\"|([^\\s]+))").match(text);
		if (!name.hasMatch())
			throw std::runtime_error("The project needs a valid Name");
		const QString scenePath = !scene.hasMatch() ? "Scenes/Main.scene" : (scene.captured(1).isEmpty() ? scene.captured(2) : scene.captured(1));
		const QString content = QFileInfo(project + "/Content").canonicalFilePath();
		const QString startScene = QFileInfo(content + "/" + scenePath).canonicalFilePath();
		if (content.isEmpty() || startScene.isEmpty() || !startScene.startsWith(content + "/") || !QFileInfo(startScene).isFile())
			throw std::runtime_error("StartScene must name an existing file inside Content");

		const QString parent = QFileInfo(output).absolutePath();
		if (!QDir().mkpath(parent))
			throw std::runtime_error("Could not create the destination folder");
		QTemporaryDir staging(parent + "/.fp-package-XXXXXX");
		if (!staging.isValid())
			throw std::runtime_error("Could not create the staging folder");
		const QString package = staging.path() + "/" + QFileInfo(output).fileName();
		const QString binaries = project + "/Binaries/Release";
#if defined(__APPLE__)
		if (!output.endsWith(".app"))
			throw std::runtime_error("macOS package paths must end with .app");
		const QString resources = package + "/Contents/Resources";
		const QString executableDir = package + "/Contents/MacOS";
		QDir().mkpath(resources);
		QDir().mkpath(executableDir);
		DeployLibraries(binaries + "/Runtime", binaries + "/GameScripts.dylib", executableDir);
		const QString escapedName = name.captured(1).toHtmlEscaped();
		Write(package + "/Contents/Info.plist", QString(
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
			"<plist version=\"1.0\"><dict>\n"
			"<key>CFBundleExecutable</key><string>Runtime</string>\n"
			"<key>CFBundleName</key><string>%1</string>\n"
			"<key>CFBundleIdentifier</key><string>games.firstprinciples.%2</string>\n"
			"<key>CFBundlePackageType</key><string>APPL</string>\n"
			"<key>CFBundleVersion</key><string>1</string>\n"
			"<key>LSMinimumSystemVersion</key><string>13.0</string>\n"
			"<key>NSHighResolutionCapable</key><true/>\n"
			"<key>NSMicrophoneUsageDescription</key><string>Voice chat uses the microphone.</string>\n"
			"</dict></plist>\n").arg(escapedName, name.captured(1).toLower().replace('_', '-')).toUtf8());
#else
		const QString resources = package;
		const QString executableDir = package;
		QDir().mkpath(package);
		DeployLibraries(binaries + "/Runtime.exe", binaries + "/GameScripts.dll", executableDir);
#endif
		Copy(projectFile, resources + "/Game.fpproject");
		if (QDir(binaries + "/UltralightResources").exists())
			CopyDirectory(binaries + "/UltralightResources", resources + "/UltralightResources");
		QTextStream(stdout) << "Cooking project and engine content\n" << Qt::flush;
		std::string error;
		if (!FP::Pak::Cook({ { Native(project + "/Content"), "Content" }, { Native(root + "/Engine/Content"), "Engine/Content" } }, Native(resources + "/Content.pak"), error))
			throw std::runtime_error(error);
		// Read the complete archive before publishing so disk errors leave no finished package.
		QTemporaryDir verification(staging.path() + "/verify-XXXXXX");
		if (!verification.isValid() || !FP::Pak::Extract(Native(resources + "/Content.pak"), Native(verification.path()), error))
			throw std::runtime_error("Pak verification failed: " + error);
		const QJsonObject manifest { { "format", 1 }, { "project", name.captured(1) }, { "configuration", "Release" }, { "archive", "Content.pak" } };
		Write(resources + "/Package.json", QJsonDocument(manifest).toJson());
#if defined(__APPLE__)
		Run("/usr/bin/codesign", { "--force", "--sign", "-", package });
#endif
		if (!QDir().rename(package, output))
			throw std::runtime_error("Could not publish the package. The destination may already exist.");
		QTextStream(stdout) << "Packaged game: " << output << '\n';
	}
}

int main(int argc, char** argv)
{
	QCoreApplication application(argc, argv);
	const QStringList args = application.arguments();
	if (args.size() != 4)
	{
		QTextStream(stderr) << "Usage: PackageTool <engine root> <project folder> <new output path>\n";
		return 1;
	}
	try
	{
		Package(QFileInfo(args[1]).absoluteFilePath(), QFileInfo(args[2]).absoluteFilePath(), QFileInfo(args[3]).absoluteFilePath());
		return 0;
	}
	catch (const std::exception& exception)
	{
		QTextStream(stderr) << "Packaging failed: " << exception.what() << '\n';
		return 1;
	}
}
