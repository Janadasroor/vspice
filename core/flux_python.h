#ifndef FLUX_PYTHON_H
#define FLUX_PYTHON_H

#include <QObject>
#include <QString>
#include <memory>

#ifdef slots
#undef slots
#endif
#include <pybind11/pybind11.h>
#define slots Q_SLOTS

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

    /**
     * @brief Validates a script for security and syntax errors using the internal linter.
     */
    bool validateScript(const QString& code, QString* error = nullptr);

    /**
     * @brief Calls a method on a Python object safely.
     * @param object The Python object instance.
     * @param method The method name to call.
     * @param args Positional arguments.
     * @param error Output for error message.
     * @return The result as a py::object, or an empty object on failure.
     */
    pybind11::object safeCall(pybind11::object object, const char* method, pybind11::tuple args, QString* error = nullptr);

private:
    explicit FluxPython(QObject* parent = nullptr);
    ~FluxPython();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_initialized;
};

#endif // FLUX_PYTHON_H
