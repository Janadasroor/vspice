#ifndef PYTHON_MANAGER_H
#define PYTHON_MANAGER_H

#include <QObject>
#include <QString>
#include <QProcess>
#include <QVariantMap>

/**
 * @brief Manages Python script execution for the Viora EDA plugin system.
 */
class PythonManager : public QObject {
    Q_OBJECT
public:
    static PythonManager& instance();

    /**
     * @brief Executes a python script located in the python/scripts directory.
     * @param scriptName Name of the script (e.g., "gemini_query.py")
     * @param args Arguments to pass to the script
     */
    void runScript(const QString& scriptName, const QStringList& args = {});

signals:
    /**
     * @brief Emitted when a script produces output.
     */
    void scriptOutput(const QString& output);

    /**
     * @brief Emitted when a script produces an error.
     */
    void scriptError(const QString& error);

    /**
     * @brief Emitted when a script finishes execution.
     */
    void scriptFinished(int exitCode);

private:
    explicit PythonManager(QObject* parent = nullptr);
    QString getScriptsPath() const;
};

#endif // PYTHON_MANAGER_H
