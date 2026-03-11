#ifndef FLUX_PYTHON_H
#define FLUX_PYTHON_H

#include <QObject>
#include <QString>
#include <memory>

/**
 * @brief Singleton managing the embedded Python interpreter.
 * Required for high-performance scripted blocks during simulation.
 */
class FluxPython : public QObject {
    Q_OBJECT
public:
    static FluxPython& instance();

    /**
     * @brief Starts the Python interpreter if not already running.
     */
    void initialize();

    /**
     * @brief Stops the Python interpreter and cleans up resources.
     */
    void finalize();

    /**
     * @brief Returns true if the interpreter is initialized.
     */
    bool isInitialized() const;

    /**
     * @brief Executes a string of Python code.
     */
    bool executeString(const QString& code, QString* error = nullptr);

private:
    explicit FluxPython(QObject* parent = nullptr);
    ~FluxPython();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_initialized;
};

#endif // FLUX_PYTHON_H
