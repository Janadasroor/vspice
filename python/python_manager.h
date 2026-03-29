#ifndef PYTHON_MANAGER_H
#define PYTHON_MANAGER_H

#include <QObject>
#include <QString>
#include <QProcess>
#include <QVariantMap>

/**
 * @brief Manages Python script execution for the viospice plugin system.
 */
class PythonManager : public QObject {
    Q_OBJECT
public:
    static PythonManager& instance();

    void runScript(const QString& scriptName, const QStringList& args = {});

    static QString getPythonExecutable();
    static QProcessEnvironment getConfiguredEnvironment();
    static QString getScriptsDir();

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
