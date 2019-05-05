#include "Python.h"

#include "execorder.h"
#include "recording.h"

#include "opcode.h"

auto consts = PyDict_New();
auto recordings = PyDict_New();
int exec_num = 0;

void do_callback(RecordingObject*);

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
                    Recording_record(recording, opcode, (PyObject*)frame, key, value);
                    opcode = STORE_NAME;
                }
            }
        }

        if(!Recording_record(recording, what, (PyObject*)frame, NULL, NULL)){
            PyEval_SetTrace(NULL, NULL);
            Py_CLEAR(frame->f_trace);
            return -1;
        }
    }
    return 0;
}

static void compile_and_exec(PyObject *code_str, RecordingObject* recording){
    const char* code_utf8 = PyUnicode_AsUTF8(code_str);
    auto filename = PyUnicode_FromFormat("<execorder%d>", exec_num++);
    auto code = (PyCodeObject*)Py_CompileStringObject(code_utf8, filename, Py_file_input, NULL, -1);
    
    if(!PyErr_Occurred()){
        // Save recording object so it is accessible from elsewhere
        PyDict_SetItem(recordings, filename, (PyObject*)recording);

        auto globals = PyDict_New();
        PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins());
        PyEval_EvalCode((PyObject*)code, globals, NULL);

        Py_DECREF(globals);
        PyDict_DelItem(recordings, filename);
    }
}

static PyObject* exec(PyObject *self, PyObject *args){
    PyObject *code_str, *callback = NULL, *max_steps = NULL;
    if (PyArg_UnpackTuple(args, "exec", 1, 3, &code_str, &max_steps, &callback)) {
        auto recording = Recording_New();
        recording->max_steps = max_steps ? (int)PyLong_AsLong(max_steps) : 0;
        recording->callback = callback;
        recording->interpreter = PyThreadState_Get()->interp;
        recording->real_eval_frame = recording->interpreter->eval_frame;
        recording->trace_func = (Py_tracefunc)trace;
        recording->code = code_str;
        Py_INCREF(recording->code);

        // Replace eval_frame with adjusted version from this binary
        recording->interpreter->eval_frame = _PyEval_EvalFrameDefault;
        
        PyEval_SetTrace(recording->trace_func, NULL);       // Start tracing
        
        // Execute the user supplied code
        compile_and_exec(code_str, recording);
        
        recording->trace_func = NULL;                       // Stop tracing
        PyEval_SetTrace(recording->trace_func, NULL);

        // Put eval_frame back
        recording->interpreter->eval_frame = recording->real_eval_frame;

        if(PyErr_Occurred()){
            PyObject *type, *value, *traceback;
            PyErr_Fetch(&type, &value, &traceback);
            do_callback(recording);
            PyErr_Restore(type, value, traceback);
            return NULL; 
        }
        return (PyObject*)recording;
    }
    return NULL;
}

void do_callback(RecordingObject* recording){
    if(PyCallable_Check(recording->callback)){
        auto args = Py_BuildValue("(O)", recording);

        // Restore Python's normal eval_frame function
        auto eval_frame = recording->interpreter->eval_frame;
        recording->interpreter->eval_frame = recording->real_eval_frame;

        PyEval_SetTrace(NULL, NULL);                    // Stop tracing
        PyEval_CallObject(recording->callback, args);   // Call into to user code        
        PyEval_SetTrace(recording->trace_func, NULL);   // Start tracing again

         // Go back to adjusted version of eval_frame
        recording->interpreter->eval_frame = eval_frame;
    }
}

// If an object is immutable (well, hashable) then save it and use that object always in 
// recordings. This is an important optimisation for nested loops that reuse numbers a lot
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

        // Bound object mutates
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
            recording = get_recording(frame, true);
            if(recording && recording->tracked_objects.contains(a)){
                Recording_record(recording, opcode, a, check_const(b), c);
            }
            break;
    }

    if(name != NULL){
    	// New binding - record it. 
        recording = get_recording(frame, false);
        if(recording != NULL){
            PyObject* named_obj = check_const(c);	// 'c' is the object bound to the new name
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

