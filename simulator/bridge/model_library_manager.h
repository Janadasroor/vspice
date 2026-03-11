#ifndef MODEL_LIBRARY_MANAGER_H
#define MODEL_LIBRARY_MANAGER_H

#include <QObject>
#include <QStringList>
#include <QMap>
#include <QVector>
#include "../core/sim_netlist.h"

struct SpiceModelInfo {
    QString name;
    QString type; // e.g., "NPN", "NMOS", "Diode", "Subcircuit"
    QString libraryPath;
    QString description;
    QStringList params;
};

class ModelLibraryManager : public QObject {
    Q_OBJECT
public:
    static ModelLibraryManager& instance();
    ModelLibraryManager* ptr() { return this; }

    void reload();
    
    QVector<SpiceModelInfo> allModels() const;
    QVector<SpiceModelInfo> search(const QString& query) const;
    
    const SimModel* findModel(const QString& name) const;
    const SimSubcircuit* findSubcircuit(const QString& name) const;

signals:
    void libraryReloaded();

private:
    ModelLibraryManager();
    ~ModelLibraryManager();
    
    void scanDirectory(const QString& path);
    void loadLibraryFile(const QString& path);

    QVector<SpiceModelInfo> m_modelIndex;
    SimNetlist m_masterNetlist; // Holds all loaded models and subcircuits
};

#endif // MODEL_LIBRARY_MANAGER_H
