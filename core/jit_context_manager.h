#ifndef JIT_CONTEXT_MANAGER_H
#define JIT_CONTEXT_MANAGER_H

#ifdef HAVE_FLUXSCRIPT
#pragma push_macro("emit")
#undef emit
#include <flux/jit/flux_jit.h>
#include <flux/compiler/compiler_instance.h>
#include <flux/compiler/lexer.h>
#include <flux/compiler/parser.h>
#pragma pop_macro("emit")
#endif

#include <QObject>
#include <QString>
#include <QMap>
#include <memory>

namespace Flux {

/**
 * @brief Manages the FluxScript JIT environment for the current VioSpice session.
 * This class isolates the LLVM state and handles compilation requests from the UI.
 */
class JITContextManager : public QObject {
    Q_OBJECT
public:
    static JITContextManager& instance();

    /**
     * @brief Compiles and loads a script into the JIT engine.
     * @param id Unique identifier for this script (e.g. block reference).
     * @param source The FluxScript source code.
     * @param errors Map of line numbers to error messages.
     * @return true if compilation succeeded, false otherwise.
     */
    bool compileAndLoad(const QString& id, const QString& source, QMap<int, QString>& errors);

    /**
     * @brief Executes the 'update' function in the JIT with inputs.
     * @param id Unique identifier for the script to execute.
     * @param time The current simulation time.
     * @param inputs List of input voltages from pins.
     * @return The computed output voltage/value.
     */
    double runUpdate(const QString& id, double time, const std::vector<double>& inputs);

    /**
     * @brief Resets the JIT state (clears all loaded modules).
     */
    void reset();

    /**
     * @brief Logs a message from the JIT to the console.
     */
    void logMessage(const QString& msg);

Q_SIGNALS:
    void compilationFinished(bool success, QString message);
    void scriptOutput(const QString& message);

private:
    JITContextManager();
    ~JITContextManager();

#ifdef HAVE_FLUXSCRIPT
    std::unique_ptr<FluxJIT> m_jit;
    QMap<QString, void*> m_updateFunctions;
#endif
};

} // namespace Flux

#endif // JIT_CONTEXT_MANAGER_H
