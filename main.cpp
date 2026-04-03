#include "ui/splash_screen.h"
#include "core/theme_manager.h"
#include "core/recent_projects.h"
#include "schematic/editor/schematic_editor.h"
#include "schematic/ui/netlist_editor.h"
#include "ui/csv_viewer.h"
#include "ui/project_manager.h"
#include "symbols/symbol_editor.h"
#include "schematic/factories/schematic_item_registry.h"
#include "schematic/tools/schematic_tool_registry_builtin.h"
#include "symbols/symbol_library.h"
#include "simulator/bridge/model_library_manager.h"
#include "simulator/bridge/flux_sim_bridge.h"
#include "core/flux_script_engine.h"
#include "pcb/editor/mainwindow.h"
#include "pcb/factories/pcb_item_registry.h"
#include "pcb/tools/pcb_tool_registry_builtin.h"
#include "footprints/footprint_library.h"


#include <QApplication>
#include <QIcon>
#include <QDebug>
#include <QLocalServer>
#include <QLocalSocket>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QtConcurrent/QtConcurrent>
#include <QTimer>
#include <QFuture>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // Initialize global theme
    ThemeManager::instance();
    
    // Initialize FluxScript Simulation Bridge
    initializeFluxSimBridge();
    FluxScriptEngine::instance().initialize();


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
        return 0; 
    }

    // No existing instance, start server for this one
    QLocalServer* server = new QLocalServer(&a);
    QLocalServer::removeServer(serverName);
    server->listen(serverName);

    // Show splash screen
    SplashScreen* splash = new SplashScreen();
    splash->show();
    a.processEvents();

    // Initialize systems
    SchematicItemRegistry::registerBuiltInItems();
    SchematicToolRegistryBuiltIn::registerBuiltInTools();
    PCBItemRegistry::registerBuiltInItems();
    PCBToolRegistryBuiltIn::registerBuiltInTools();

    // Initialize symbol, footprint, and model libraries
    SymbolLibraryManager::instance();
    FootprintLibraryManager::instance();

    // Set up loading logic
    auto startMainApp = [&a, &fileToOpen, server](SplashScreen* s) {
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
            } else if (ext == "pcb" || ext == "kicad_pcb" || ext == "pcbdoc") {
                MainWindow* pcbEditor = new MainWindow();
                pcbEditor->setAttribute(Qt::WA_DeleteOnClose);
                pcbEditor->openFile(path);
                pcbEditor->show();
                return (QWidget*)pcbEditor;
            } else if (ext == "fp" || ext == "mod" || ext == "kicad_mod") {
                // Open footprint editor - would need to add loadFootprint method
                SymbolEditor* fpEditor = new SymbolEditor(); // TODO: Create FootprintEditor
                fpEditor->setAttribute(Qt::WA_DeleteOnClose);
                fpEditor->show();
                return (QWidget*)fpEditor;
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
            openFile(fileToOpen);
        } else {
            ProjectManager* projectManager = new ProjectManager;
            projectManager->setAttribute(Qt::WA_DeleteOnClose);
            projectManager->show();
        }
        
        s->deleteLater();
    };

    // Background loading
    auto updateSplash = [splash](const QString& status, int progress, int total) {
        splash->setStatus(status);
        splash->setProgress(progress, total);
    };

    QObject::connect(&SymbolLibraryManager::instance(), &SymbolLibraryManager::progressUpdated, splash, updateSplash);
    QObject::connect(&ModelLibraryManager::instance(), &ModelLibraryManager::progressUpdated, splash, updateSplash);
    QObject::connect(&FootprintLibraryManager::instance(), &FootprintLibraryManager::progressUpdated, splash, updateSplash);

    QFuture<void> future = QtConcurrent::run([splash, startMainApp]() {
        // Step 1: Load Symbols
        SymbolLibraryManager::instance().loadUserLibraries(QDir::homePath() + "/ViospiceLib/sym");

        // Step 2: Load Models
        ModelLibraryManager::instance().reload();

        // Step 3: Load Footprints
        FootprintLibraryManager::instance().loadUserLibraries(QDir::homePath() + "/ViospiceLib/footprints");

        // Finalize properly on UI thread
        QMetaObject::invokeMethod(qApp, [startMainApp, splash]() {
            startMainApp(splash);
        }, Qt::QueuedConnection);
    });

    return a.exec();
}
