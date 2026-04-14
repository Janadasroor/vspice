/**
 * @file python_executor.cpp
 * @brief Python execution helper — compiled without Qt headers.
 *
 * Captures output by temporarily replacing sys.stdout at the C level.
 */

#ifdef HAVE_PYTHON
#include <Python.h>
#endif

#include "python_executor.h"

#include <cstdlib>
#include <cstring>
#include <cctype>

#ifdef HAVE_PYTHON

extern "C" {

char* py_executor_execute(const char* code, int* out_is_error) {
    *out_is_error = 0;
    
    if (!Py_IsInitialized()) {
        *out_is_error = 1;
        return strdup("Error: Python runtime not initialized.");
    }

    PyGILState_STATE gstate = PyGILState_Ensure();
    
    // Get sys module
    PyObject* sysModule = PyImport_ImportModule("sys");
    if (!sysModule) {
        PyGILState_Release(gstate);
        *out_is_error = 1;
        return strdup("Error: Cannot import sys.");
    }
    
    // Create StringIO for capture
    PyObject* ioModule = PyImport_ImportModule("io");
    if (!ioModule) {
        Py_DECREF(sysModule);
        PyGILState_Release(gstate);
        *out_is_error = 1;
        return strdup("Error: Cannot import io.");
    }
    
    PyObject* stringIOClass = PyObject_GetAttrString(ioModule, "StringIO");
    Py_DECREF(ioModule);
    if (!stringIOClass) {
        Py_DECREF(sysModule);
        PyGILState_Release(gstate);
        *out_is_error = 1;
        return strdup("Error: Cannot get StringIO.");
    }
    
    PyObject* capture = PyObject_CallObject(stringIOClass, NULL);
    Py_DECREF(stringIOClass);
    if (!capture) {
        Py_DECREF(sysModule);
        PyGILState_Release(gstate);
        *out_is_error = 1;
        return strdup("Error: Cannot create StringIO.");
    }
    
    // Save original stdout
    PyObject* origStdout = PyObject_GetAttrString(sysModule, "stdout");
    Py_INCREF(origStdout);
    
    // Replace sys.stdout with our StringIO
    PyObject_SetAttrString(sysModule, "stdout", capture);
    // Also replace sys.stderr
    PyObject_SetAttrString(sysModule, "stderr", capture);
    
    // Execute user code
    PyObject* mainDict = PyModule_GetDict(PyImport_AddModule("__main__"));
    PyObject* result = PyRun_String(code, Py_single_input, mainDict, mainDict);
    int hadError = 0;
    if (!result) {
        // Try as multi-line input
        PyErr_Clear();
        result = PyRun_String(code, Py_file_input, mainDict, mainDict);
        if (!result) {
            PyErr_Print();
            hadError = 1;
        }
    } else {
        // For single expressions, print the result
        PyObject* repr = PyObject_Repr(result);
        if (repr) {
            PyObject* printFunc = PyObject_GetAttrString(PyImport_AddModule("builtins"), "print");
            if (printFunc) {
                PyObject_CallFunctionObjArgs(printFunc, repr, NULL);
                Py_DECREF(printFunc);
            }
            Py_DECREF(repr);
        }
    }
    Py_XDECREF(result);
    
    // Capture output
    char* output = NULL;
    PyObject* getvalue = PyObject_GetAttrString(capture, "getvalue");
    if (getvalue) {
        PyObject* outStr = PyObject_CallObject(getvalue, NULL);
        if (outStr) {
            const char* cstr = PyUnicode_AsUTF8(outStr);
            if (cstr && strlen(cstr) > 0) {
                output = strdup(cstr);
            }
            Py_DECREF(outStr);
        }
        Py_DECREF(getvalue);
    }
    
    // Restore original stdout
    PyObject_SetAttrString(sysModule, "stdout", origStdout);
    PyObject_SetAttrString(sysModule, "stderr", origStdout);
    Py_DECREF(origStdout);
    Py_DECREF(sysModule);
    Py_DECREF(capture);
    
    if (!output) {
        output = strdup("");
    }
    
    PyGILState_Release(gstate);
    
    *out_is_error = hadError;
    return output;
}

int py_executor_is_initialized(void) {
    return Py_IsInitialized() ? 1 : 0;
}

int py_executor_is_complete(const char* code) {
    if (!code) return 1;
    const char* p = code;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '\0') return 1;
    const char* end = code + strlen(code) - 1;
    while (end > code && isspace((unsigned char)*end)) end--;
    char lastChar = *end;
    if (lastChar == ':') return 0;
    if (lastChar == '\\') return 0;
    int parens = 0, brackets = 0, braces = 0;
    for (const char* c = code; c <= end; c++) {
        if (*c == '(') parens++;
        else if (*c == ')') parens--;
        else if (*c == '[') brackets++;
        else if (*c == ']') brackets--;
        else if (*c == '{') braces++;
        else if (*c == '}') braces--;
    }
    if (parens > 0 || brackets > 0 || braces > 0) return 0;
    return 1;
}

void py_executor_free(void* ptr) { free(ptr); }

} // extern "C"

#else

extern "C" {
int py_executor_is_initialized(void) { return 0; }
char* py_executor_execute(const char*, int* out_is_error) {
    *out_is_error = 1;
    return strdup("Python support not compiled in.");
}
void py_executor_free(void* ptr) { free(ptr); }
int py_executor_is_complete(const char*) { return 1; }
}

#endif
