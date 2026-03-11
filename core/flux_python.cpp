#ifdef slots
#undef slots
#endif
#include <pybind11/embed.h>
#define slots Q_SLOTS

#include "flux_python.h"
#include <QDebug>

namespace py = pybind11;

struct FluxPython::Impl {
    std::unique_ptr<py::scoped_interpreter> guard;
};

FluxPython& FluxPython::instance() {
    static FluxPython inst;
    return inst;
}

FluxPython::FluxPython(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
    , m_initialized(false) {
}

FluxPython::~FluxPython() {
    finalize();
}

void FluxPython::initialize() {
    if (m_initialized) return;

    try {
        qDebug() << "Initializing embedded Python interpreter...";
        m_impl->guard = std::make_unique<py::scoped_interpreter>();
        // Ensure embedded modules are initialized
        py::module_::import("flux");
        m_initialized = true;
        qDebug() << "Python interpreter initialized successfully.";
    } catch (const std::exception& e) {
        qCritical() << "Failed to initialize Python interpreter:" << e.what();
    }
}

void FluxPython::finalize() {
    if (!m_initialized) return;

    qDebug() << "Finalizing embedded Python interpreter...";
    m_impl->guard.reset();
    m_initialized = false;
}

bool FluxPython::isInitialized() const {
    return m_initialized;
}

bool FluxPython::executeString(const QString& code, QString* error) {
    if (!m_initialized) {
        if (error) *error = "Interpreter not initialized.";
        return false;
    }

    try {
        py::exec(code.toStdString());
        return true;
    } catch (py::error_already_set& e) {
        if (error) *error = QString::fromStdString(e.what());
        return false;
    }
}

bool FluxPython::validateScript(const QString& code, QString* error) {
    if (!m_initialized) return false;

    try {
        // We'll embed the linter logic directly or load it from the scripts folder
        py::dict locals;
        locals["code"] = code.toStdString();
        
        py::exec(R"(
import ast
def lint(c):
    try:
        tree = ast.parse(c)
        forbidden = {'os', 'sys', 'shutil', 'subprocess', 'socket'}
        for node in ast.walk(tree):
            if isinstance(node, (ast.Import, ast.ImportFrom)):
                mod = node.names[0].name if isinstance(node, ast.Import) else node.module
                if mod in forbidden: return f'Forbidden import: {mod}'
        return 'OK'
    except Exception as e:
        return str(e)

result = lint(code)
)", py::globals(), locals);

        std::string result = locals["result"].cast<std::string>();
        if (result == "OK") return true;
        if (error) *error = QString::fromStdString(result);
        return false;
    } catch (const std::exception& e) {
        if (error) *error = QString::fromStdString(e.what());
        return false;
    }
}

py::object FluxPython::safeCall(py::object object, const char* method, py::tuple args, QString* error) {
    if (!m_initialized || object.is_none()) {
        if (error) *error = "Invalid state or object.";
        return py::none();
    }

    try {
        return object.attr(method)(*args);
    } catch (py::error_already_set& e) {
        if (error) *error = QString::fromStdString(e.what());
        return py::none();
    } catch (const std::exception& e) {
        if (error) *error = QString::fromStdString(e.what());
        return py::none();
    }
}
