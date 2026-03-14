#include "core/theme_manager.h"
#include "core/recent_projects.h"
#include "schematic/editor/schematic_editor.h"
#include "schematic/ui/netlist_editor.h"
#include "ui/csv_viewer.h"
#include "symbols/symbol_editor.h"
#include "schematic/factories/schematic_item_registry.h"
#include "schematic/tools/schematic_tool_registry_builtin.h"
#include "symbols/symbol_library.h"
#include "simulator/bridge/model_library_manager.h"

#include <QApplication>
#include <QIcon>
#include <QDebug>
#include <QLocalServer>
#include <QLocalSocket>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // Initialize global theme
    ThemeManager::instance();

    a.setApplicationName("viospice");
    a.setApplicationVersion("0.1.0");
    a.setOrganizationName("VIO");
    a.setWindowIcon(QIcon(":/icons/viospice_logo.png"));

    // --- Single Instance / Command Line Argument Handling ---
    QString serverName = "viospice_instance_server";
    QString fileToOpen;
    bool showHelp = false;
    bool showVersion = false;

    for (int i = 1; i < argc; ++i) {
        QString arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            showHelp = true;
        } else if (arg == "--version" || arg == "-v") {
            showVersion = true;
        } else if (!arg.startsWith("-")) {
            fileToOpen = QFileInfo(arg).absoluteFilePath();
        }
    }

    if (showHelp) {
        printf("viospice - High Performance Circuit Simulator\n");
        printf("Usage: viospice [options] [file]\n\n");
        printf("Options:\n");
        printf("  -h, --help     Show this help message\n");
        printf("  -v, --version  Show version information\n");
        return 0;
    }

    if (showVersion) {
        printf("viospice version 0.1.0\n");
        return 0;
    }

    // Try to connect to an existing instance
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (socket.waitForConnected(500)) {
        if (!fileToOpen.isEmpty()) {
            socket.write(fileToOpen.toUtf8());
            socket.waitForBytesWritten();
        }
        return 0; // Already running, message sent, exit this process
    }

    // No existing instance, start server for this one
    QLocalServer* server = new QLocalServer(&a);
    QLocalServer::removeServer(serverName);
    server->listen(serverName);

    // Initialize systems
    SchematicItemRegistry::registerBuiltInItems();
    SchematicToolRegistryBuiltIn::registerBuiltInTools();
    SymbolLibraryManager::instance();
    ModelLibraryManager::instance();

    // Global lambda to open a file
    auto openFile = [](const QString& path) {
        QFileInfo fi(path);
        QString ext = fi.suffix().toLower();
        
        if (ext == "cir" || ext == "sp" || ext == "spice" || ext == "txt") {
            NetlistEditor* netEditor = new NetlistEditor();
            netEditor->setAttribute(Qt::WA_DeleteOnClose);
            QFile file(path);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&file);
                netEditor->setNetlist(in.readAll());
                file.close();
            }
            netEditor->show();
            return (QWidget*)netEditor;
        } else if (ext == "sclib" || ext == "kicad_sym" || ext == "asy") {
            SymbolEditor* symbolEditor = new SymbolEditor();
            symbolEditor->setAttribute(Qt::WA_DeleteOnClose);
            if (ext == "kicad_sym") {
                symbolEditor->importKicadSymbol(path);
            } else if (ext == "asy") {
                symbolEditor->importLtspiceSymbol(path);
            } else if (ext == "sclib") {
                symbolEditor->loadLibrary(path);
            }
            symbolEditor->show();
            return (QWidget*)symbolEditor;
        } else if (ext == "csv") {
            CsvViewer* viewer = new CsvViewer();
            viewer->setAttribute(Qt::WA_DeleteOnClose);
            viewer->loadFile(path);
            viewer->show();
            return (QWidget*)viewer;
        } else {
            SchematicEditor* schEditor = new SchematicEditor();
            schEditor->setAttribute(Qt::WA_DeleteOnClose);
            schEditor->openFile(path);
            schEditor->show();
            return (QWidget*)schEditor;
        }
    };

    // Connect server to open files in this instance
    QObject::connect(server, &QLocalServer::newConnection, [server, openFile]() {
        QLocalSocket* clientSocket = server->nextPendingConnection();
        QObject::connect(clientSocket, &QLocalSocket::readyRead, [clientSocket, openFile]() {
            QString path = QString::fromUtf8(clientSocket->readAll());
            if (!path.isEmpty()) {
                QWidget* w = openFile(path);
                if (w) {
                    w->raise();
                    w->activateWindow();
                }
            }
            clientSocket->disconnectFromServer();
        });
    });

    if (!fileToOpen.isEmpty()) {
        qDebug() << "Opening requested file:" << fileToOpen;
        openFile(fileToOpen);
    } else {
        QString lastRecent = RecentProjects::instance().mostRecent();
        qDebug() << "Checking most recent project:" << lastRecent;
        if (!lastRecent.isEmpty()) {
            if (QFile::exists(lastRecent)) {
                qDebug() << "Auto-loading most recent project:" << lastRecent;
                openFile(lastRecent);
            } else {
                qWarning() << "Most recent project file does not exist:" << lastRecent;
                // Launch default empty schematic editor
                SchematicEditor* schematicEditor = new SchematicEditor;
                schematicEditor->setWindowTitle("viospice - Schematic Editor");
                schematicEditor->show();
            }
        } else {
            qDebug() << "No recent project to auto-load.";
            // Launch default empty schematic editor
            SchematicEditor* schematicEditor = new SchematicEditor;
            schematicEditor->setWindowTitle("viospice - Schematic Editor");
            schematicEditor->show();
        }
    }

    return a.exec();
}
