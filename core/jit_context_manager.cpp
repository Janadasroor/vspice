#include "jit_context_manager.h"
#include <QDebug>

#ifdef HAVE_FLUXSCRIPT
#include <flux/compiler/compiler_instance.h>
#include <flux/compiler/lexer.h>
#include <flux/compiler/parser.h>
#endif

namespace Flux {

JITContextManager& JITContextManager::instance() {
    static JITContextManager manager;
    return manager;
}

JITContextManager::JITContextManager() {
#ifdef HAVE_FLUXSCRIPT
    m_jit = std::make_unique<FluxJIT>();
#endif
}

JITContextManager::~JITContextManager() {}

bool JITContextManager::compileAndLoad(const QString& id, const QString& source, QMap<int, QString>& errors) {
#ifdef HAVE_FLUXSCRIPT
    if (!m_jit) return false;

    // 1. Create compiler components
    std::string code = source.toStdString();
    CompilerOptions options;
    options.moduleName = ("viospice_jit_" + id).toStdString();
    CompilerInstance compiler(options);
    
    std::string errorStr;
    auto artifacts = compiler.compileToIR(code, &errorStr);
    
    if (!artifacts || !artifacts->codegenContext || !artifacts->codegenContext->TheModule) {
        errors[0] = QString::fromStdString(errorStr.empty() ? "Compilation failed" : errorStr);
        return false;
    }

    // 4. Add module to JIT
    m_jit->addModule(std::move(artifacts->codegenContext->TheModule), std::move(artifacts->codegenContext->OwnedContext));

    // 5. Look up 'update' function for simulation hook
    void* func = m_jit->getPointerToFunction("update");
    if (func) {
        m_updateFunctions[id] = func;
        qDebug() << "FluxScript: Found 'update' function for" << id << "at" << func;
    } else {
        errors[0] = "FluxScript: Function 'update' not found in script.";
        return false;
    }

    Q_EMIT compilationFinished(true, "Script compiled and loaded in JIT.");
    return true;
#else
    Q_UNUSED(id);
    Q_UNUSED(source);
    errors[0] = "FluxScript support is disabled in this build.";
    return false;
#endif
}

double JITContextManager::runUpdate(const QString& id, double time, const std::vector<double>& inputs) {
#ifdef HAVE_FLUXSCRIPT
    void* func = m_updateFunctions.value(id);
    if (func) {
        // Optimized JIT call: passing inputs as a double array and count
        // FluxScript compiler generates this signature for 'def update(t, inputs)'
        typedef double (*UpdateFunc)(double, const double*, int);
        return reinterpret_cast<UpdateFunc>(func)(time, inputs.data(), static_cast<int>(inputs.size()));
    }
#else
    Q_UNUSED(id);
    Q_UNUSED(time);
    Q_UNUSED(inputs);
#endif
    return 0.0;
}

void JITContextManager::logMessage(const QString& msg) {
    Q_EMIT scriptOutput(msg);
}

void JITContextManager::reset() {
#ifdef HAVE_FLUXSCRIPT
    m_jit = std::make_unique<FluxJIT>();
    m_updateFunctions.clear();
#endif
}

} // namespace Flux
