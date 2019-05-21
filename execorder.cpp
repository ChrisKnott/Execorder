// DEBUG stuff ==================================================
#define PRNT(obj) PyObject_Print(obj, stdout, 0);
#define PRNTn(obj) PRNT(obj); printf("\n");
//===============================================================

#include "Python.h"
#include "ceval.h"
#include "frameobject.h"
#include "opcode.h"
#include "recording.h"
#include <atomic>

#define TOP()       (frame->f_stacktop[-1])
#define SECOND()    (frame->f_stacktop[-2])
#define THIRD()     (frame->f_stacktop[-3])
#define NAME()      (PyTuple_GET_ITEM(((PyTupleObject*)frame->f_code->co_names), (oparg)))

Py_ssize_t recording_i, my_code_i;
std::atomic<int> running_execs = 0;

bool get_recording(PyFrameObject* frame, RecordingObject** recording){
    PyObject* in_my_code = NULL;
    _PyCode_GetExtra((PyObject*)frame->f_code, recording_i, (void**)recording);
    _PyCode_GetExtra((PyObject*)frame->f_code, my_code_i, (void**)&in_my_code);
    return in_my_code != NULL;
}

void mutation(PyFrameObject* frame, int opcode, int i, PyObject* a, PyObject* b, PyObject* c){
    // A mutation occurred, see whether we need to record it...
    RecordingObject* recording = NULL;
    bool in_my_code = get_recording(frame, &recording);
    auto f = (PyObject*)frame;

    if(in_my_code){
        PyObject *name = NULL;
        switch(opcode){
            // Name bind
            case STORE_NAME:                // a[b] = c (b = c in namespace a)
            case DELETE_NAME:
                name = b;
                break;
            case STORE_GLOBAL:              // a = c
            case DELETE_GLOBAL:
                name = a;
                break;
            case STORE_FAST:                // FASTS[i] = c
            case DELETE_FAST:
                name = PyTuple_GetItem(frame->f_code->co_varnames, i);
                break;
            case STORE_DEREF:
            case DELETE_DEREF:
                // TODO: logic about i >? co_varnames etc.
                break;
        }

        if(name != NULL){
            Recording_record(recording, opcode, f, name, c);
        }
    }

    if(recording != NULL && Recording_object_tracked(recording, a)){
        // We aren't in my code, but we are tracking this object's state
        // e.g. my code called random.shuffle(X) and we are in shuffle's implementation
        switch(opcode){
            case STORE_SUBSCR:              // a[b] = c
            case DELETE_SUBSCR:
            case STORE_ATTR:                // a.b = c
            case DELETE_ATTR:
            case INPLACE_POWER:             // a **= b
            case INPLACE_MULTIPLY:          // a *= b
            case INPLACE_MATRIX_MULTIPLY:   // a @= b
            case INPLACE_TRUE_DIVIDE:       // a /= b
            case INPLACE_FLOOR_DIVIDE:      // a //= b
            case INPLACE_MODULO:            // a %= b
            case INPLACE_ADD:               // a += b
            case INPLACE_SUBTRACT:          // a -= b
            case INPLACE_LSHIFT:            // a <<= b
            case INPLACE_RSHIFT:            // a >>= b
            case INPLACE_AND:               // a &= b
            case INPLACE_XOR:               // a ^= b
            case INPLACE_OR:                // a |= b
                Recording_record(recording, opcode, a, b, c);
                break;
        }
    }
}

int trace_opcode(PyFrameObject* frame){
    // Check whether the next opcode can potentially mutate state...
    auto instructions = PyBytes_AS_STRING(frame->f_code->co_code);
    auto opcode = instructions[frame->f_lasti];
    auto oparg  = instructions[frame->f_lasti + 1];
    
    switch(opcode){
        case STORE_FAST:
            mutation(frame, STORE_FAST, oparg, NULL, NULL, TOP());
            break;
        case DELETE_FAST:
            mutation(frame, DELETE_FAST, oparg, NULL, NULL, NULL);
            break;
        case STORE_SUBSCR:
            mutation(frame, STORE_SUBSCR, 0, SECOND(), TOP(), THIRD());
            break;
        case DELETE_SUBSCR:
            mutation(frame, DELETE_SUBSCR, 0, SECOND(), TOP(), NULL);
            break;
        case STORE_NAME:
            mutation(frame, STORE_NAME, 0, frame->f_locals, NAME(), TOP());
            break;
        case DELETE_NAME:
            mutation(frame, STORE_NAME, 0, frame->f_locals, NAME(), NULL);
            break;
        case STORE_ATTR:
            mutation(frame, STORE_ATTR, 0, TOP(), NAME(), SECOND());
            break;
        case DELETE_ATTR:
            mutation(frame, DELETE_ATTR, 0, TOP(), NAME(), NULL);
            break;
        case STORE_GLOBAL:
            mutation(frame, STORE_GLOBAL, 0, NAME(), NULL, TOP());
            break;
        case DELETE_GLOBAL:
            mutation(frame, DELETE_GLOBAL, 0, NAME(), NULL, NULL);
            break;
        case INPLACE_ADD:   // TODO: deal with unicode
        case INPLACE_POWER:
        case INPLACE_MULTIPLY:
        case INPLACE_MATRIX_MULTIPLY:
        case INPLACE_TRUE_DIVIDE:
        case INPLACE_FLOOR_DIVIDE:
        case INPLACE_MODULO:
        case INPLACE_SUBTRACT:
        case INPLACE_LSHIFT:
        case INPLACE_RSHIFT:
        case INPLACE_AND:
        case INPLACE_XOR:
        case INPLACE_OR:
            mutation(frame, opcode, 0, SECOND(), TOP(), NULL);
            break;
    }
    return 0;
}

int trace_step(PyFrameObject *frame, RecordingObject* recording, int what){
    if(recording->record_state){
        if(recording->global_frame == NULL){
            recording->global_frame = (PyObject*)frame;    // First trace step, save the frame
        }

        // Deal with 'hidden' name bindings when entering new frame
        if(recording->fresh_milestone || what == PyTrace_CALL){
            PyFrame_FastToLocals(frame);        // TODO: this is slow, refactor it out?

            std::vector<PyObject*> dicts; int opcode;
            if(recording->fresh_milestone){
                dicts = {frame->f_globals, frame->f_locals};
                opcode = STORE_GLOBAL;
                recording->fresh_milestone = false;
            } else {
                dicts = {frame->f_locals};
                opcode = STORE_NAME;
            }

            PyObject *key, *value; Py_ssize_t pos = 0;
            for(auto& dict : dicts){
                while(PyDict_Next(dict, &pos, &key, &value)) {
                    Recording_record(recording, opcode, (PyObject*)frame, key, value);
                    opcode = STORE_NAME;
                }
            }
        }
    }

    int err = Recording_record(recording, what, (PyObject*)frame, NULL, NULL);
    return err;
}


int trace(PyObject *obj, PyFrameObject *frame, int what, PyObject *arg){
    int err = 0;
    if(what == PyTrace_OPCODE){
        err = trace_opcode(frame);
    } else {
        RecordingObject* recording = NULL;
        bool in_my_code = get_recording(frame, &recording);

        if(recording == NULL && what == PyTrace_CALL && frame->f_back != NULL){
            // Newly called frame - copy possible Recording from parent
            _PyCode_GetExtra((PyObject*)frame->f_back->f_code, recording_i, (void**)&recording);
            _PyCode_SetExtra((PyObject*)frame->f_code, recording_i, (void*)recording);
        }

        if(recording != NULL){
            if(recording->record_state){
                frame->f_trace_opcodes = 1;
            }

            if(in_my_code){
                err = trace_step(frame, recording, what);
            }
        }
    }
    return err;
}

void mark_code_with_recording(PyObject* code, RecordingObject* recording){
    _PyCode_SetExtra(code, my_code_i, (void*)true);
    _PyCode_SetExtra(code, recording_i, (void*)recording);

    auto co_consts = ((PyCodeObject*)code)->co_consts;
    auto n = PyTuple_Size(co_consts);
    for(Py_ssize_t i = 0; i < n; i++){
        auto co_const = PyTuple_GetItem(co_consts, i);
        if(PyCode_Check(co_const)){
            // Mark any sub-code objects (e.g. functions)
            mark_code_with_recording(co_const, recording);
        }
    }
}

static PyObject* exec(PyObject *self, PyObject *args, PyObject *kwargs){
    PyObject *code_str, *globals, *callback = NULL;
    long max_steps = 0, record_state = 1;
    char *keywords[] = {"", "", "callback", "max_steps", "record_state", NULL};
    if(PyArg_ParseTupleAndKeywords(args, kwargs, "O|O$Olp:exec", keywords, &code_str, &globals,
                                                                &callback, &max_steps, &record_state)){
        auto code_utf8 = PyUnicode_AsUTF8(code_str);
        auto code = Py_CompileStringExFlags(code_utf8, "<execorder>", Py_file_input, NULL, -1);
        if(PyErr_Occurred()){
            return NULL;    // Compile failure
        }

        auto recording = Recording_New(code);
        recording->record_state = (bool)record_state;
        recording->callback = callback;
        recording->max_steps = max_steps;

        mark_code_with_recording(code, recording);  // Attached recoding to code object

        globals = PyDict_New();
        auto builtins = PyDict_Copy(PyEval_GetBuiltins());
        PyDict_SetItemString(builtins, "__cffi_backend_extern_py", Py_None);    // Clear gevent nonsense
        PyDict_SetItemString(globals, "__builtins__", builtins);
        Py_DECREF(builtins);

        running_execs++;
        PyEval_SetTrace((Py_tracefunc)trace, NULL);     // Turn on tracing
        Recording_make_callback(recording);             // Starting callback 
        PyEval_EvalCode(code, globals, NULL);           // Run the code
        running_execs--;

        if(running_execs == 0){
            // No other threads are running - safe to turn off tracing
            PyEval_SetTrace(NULL, NULL);
        }

        Py_DECREF(globals);

        if(PyErr_Occurred()){
            PyObject *type, *value, *traceback;
            PyErr_Fetch(&type, &value, &traceback);
            Recording_make_callback(recording);
            PyErr_Restore(type, value, traceback);
            recording = NULL; 
        }
        return (PyObject*)recording;
    }
    return NULL;
}

static PyMethodDef
methods[] = {
    {"exec", (PyCFunction)exec, METH_VARARGS | METH_KEYWORDS, "Execute code object"},
    {NULL}
};

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    "execorder", NULL, -1, methods,
    NULL, NULL, NULL, NULL,
};

extern "C"
PyObject* PyInit_execorder(void) {
    auto module = PyModule_Create(&moduledef);

    my_code_i = _PyEval_RequestCodeExtraIndex(NULL);
    recording_i = _PyEval_RequestCodeExtraIndex(NULL);
    
    auto recording_type = Recording_Type();
    PyType_Ready(recording_type);
    Py_INCREF(recording_type);
    PyModule_AddObject(module, "Recording", (PyObject*)recording_type);

    return module;
}
