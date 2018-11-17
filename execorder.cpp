#define Py_BUILD_CORE
#include <Python.h>
#include "frameobject.h"
#include "pystate.h"
#include "opcode.h"

#include "ceval.h"
#include "execorder.h"
#include "recording.h"

auto execorder_str = PyUnicode_FromString("<execorder>");
auto consts = PyDict_New();
auto recordings = PyDict_New();
int exec_num = 0;

RecordingObject* get_recording(PyFrameObject* frame, bool backtrack){
    while(frame != NULL){
        auto recording = PyDict_GetItemWithError(recordings, frame->f_code->co_filename);
        if(recording != NULL){
            return (RecordingObject*)recording;
        }
        frame = backtrack ? frame->f_back : NULL;
    }
    return NULL;
}

static int trace(PyObject *obj, PyFrameObject *frame, int what, PyObject *arg){
    auto recording = get_recording(frame, false);
    if(recording != NULL){
        if(recording->global_frame == NULL){
            recording->global_frame = (PyObject*)frame; // First trace step, save the frame
        }

        if(recording->new_milestone || what == PyTrace_CALL){
            PyFrame_FastToLocals(frame);

            std::vector<PyObject*> dicts; int opcode;
            if(recording->new_milestone){
                dicts = {frame->f_globals, frame->f_locals};
                opcode = STORE_GLOBAL;
                recording->new_milestone = false;
            } else {
                dicts = {frame->f_locals};
                opcode = STORE_NAME;
            }

            PyObject *key, *value; Py_ssize_t pos = 0;
            for(auto& dict : dicts){
                while(PyDict_Next(dict, &pos, &key, &value)) {
                    Recording_record(recording, STORE_NAME, (PyObject*)frame, key, value);
                    opcode = STORE_NAME;
                }
            }
        }

        Recording_record(recording, what, (PyObject*)frame, NULL, NULL);
    }
    return 0;
}

static PyObject* compile_and_exec(PyObject *code_str, PyObject* recording){
    // code = compile(code_str, '<execorder>', 'exec')
    const char* code_utf8 = PyUnicode_AsUTF8(code_str);
    auto filename = PyUnicode_FromFormat("<execorder%d>", exec_num++);
    auto code = (PyCodeObject*)Py_CompileStringObject(code_utf8, filename, Py_file_input, NULL, -1);
    if(PyErr_Occurred()){
        return NULL;
    } else {
        PyDict_SetItem(recordings, filename, recording);
    }

    // exec(code, {}, {})
    auto globals = PyDict_New();
    PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins());
    PyEval_EvalCode((PyObject*)code, globals, NULL);

    Py_DECREF(globals);
    PyDict_DelItem(recordings, filename);
    
    if(PyErr_Occurred()){
        return NULL;
    }
    return recording;
}

static PyObject* exec(PyObject *self, PyObject *args){
    auto interpreter = PyThreadState_Get()->interp;
    auto eval_frame = interpreter->eval_frame;
    auto code_str = PyTuple_GET_ITEM(args, 0);
    auto recording = Recording_New();

    // Replace eval_frame with adjusted version from this binary
    interpreter->eval_frame = _PyEval_EvalFrameDefault;
    PyEval_SetTrace((Py_tracefunc) trace, NULL);        // Start tracing

    recording = compile_and_exec(code_str, recording);

    interpreter->eval_frame = eval_frame;               // Put eval_frame back
    PyEval_SetTrace(NULL, NULL);                        // Stop tracing

    return recording;
}

// If an object is immutable (well, hashable) then save it and use that object always in recordings
PyObject* check_const(PyObject* obj){
    if(obj == NULL){
        return NULL;
    }
    
    PyObject* const_obj = PyDict_GetItemWithError(consts, obj);
    if(PyErr_Occurred()){
        PyErr_Clear();
        return obj;                             // Not hashable, use original object
    } else {
        if(const_obj == NULL){
            PyDict_SetItem(consts, obj, obj);   // Save new const object
            return obj;
        } else {
            return const_obj;                   // Re-use previously saved const object
        }
    }
}

extern "C"
void Execorder_Mutate(PyFrameObject* frame, int opcode, int i, PyObject* a, PyObject* b, PyObject* c){
    PyObject* name = NULL;
    RecordingObject* recording = NULL;

    switch(opcode){
        // Name bind
        case DELETE_NAME:
        case STORE_NAME:                // a[b] = c (b = c in namespace a)
            name = b;
            break;
        case DELETE_GLOBAL:
        case STORE_GLOBAL:              // a = c
            name = a;
            break;
        case DELETE_FAST:
        case STORE_FAST:                // FASTS[i] = c
            name = PyTuple_GetItem(frame->f_code->co_varnames, i);
            break;

        // Bound object mutates
        case DELETE_SUBSCR:
        case STORE_SUBSCR:              // a[b] = c
        case DELETE_ATTR:
        case STORE_ATTR:                // a.b = c
            recording = get_recording(frame, true);
            if(recording && recording->tracked_objects.contains(a)){
                Recording_record(recording, opcode, a, check_const(b), c);
            }
    }

    if(name != NULL){
        recording = get_recording(frame, false);
        if(recording != NULL){
            PyObject* named_obj = check_const(c);
            Recording_record(recording, opcode, (PyObject*)frame, name, named_obj);
        }   
    }
}

static PyMethodDef
methods[] = {
    {"exec", (PyCFunction) exec, METH_VARARGS, ""},
    {NULL}
};

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    "execorder", NULL, -1, methods,
    NULL, NULL, NULL, NULL,
};

extern "C"
PyObject *PyInit_execorder(void) {
    auto module = PyModule_Create(&moduledef);

    auto recording_type = Recording_Type();
    PyType_Ready(recording_type);
    Py_INCREF(recording_type);
    PyModule_AddObject(module, "Recording", (PyObject*)recording_type);

    return module;
}

