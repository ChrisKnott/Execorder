#include "Python.h"
#include "execorder.h"
#include "recording.h"
#include "structmember.h"
#include "opcode.h"

using ObjectMap = spp::sparse_hash_map<PyObject*, PyObject*>;
auto io_module = PyImport_ImportModule("io");
auto pickle_module = PyImport_ImportModule("_pickle");
auto pickler_str = PyUnicode_FromString("Pickler");
auto unpickler_str = PyUnicode_FromString("Unpickler");
auto dump_str = PyUnicode_FromString("dump");
auto bytesio_str = PyUnicode_FromString("BytesIO");

static void Recording_dealloc(RecordingObject *self){
    Py_DECREF(self->pickler);
    Py_DECREF(self->code);
    self->pickle_order = NULL;

    MutationList* mutations; PickleOrder* pickle_order; PyObject* pickle_bytes;
    for(auto& milestone : self->milestones){
        std::tie(mutations, pickle_order, pickle_bytes) = milestone;
        delete mutations;
        delete pickle_order;
        Py_DECREF(pickle_bytes);
    }

    Py_TYPE(self)->tp_free((PyObject *) self);
}

static void new_milestone(RecordingObject* recording){
    recording->pickle_order = new PickleOrder();
    recording->mutations = new MutationList(); 

    auto pickle_bytes = PyObject_CallMethodObjArgs(io_module, bytesio_str, NULL);  
    recording->pickler = PyObject_CallMethodObjArgs(pickle_module, pickler_str, pickle_bytes, NULL);

    auto milestone = Milestone(recording->mutations, recording->pickle_order, pickle_bytes);
    recording->milestones.push_back(milestone);

    recording->new_milestone = true;
}

static PyObject* Recording_new(PyTypeObject *type, PyObject *args, PyObject *kwds){
    auto self = (RecordingObject *) type->tp_alloc(type, 0);
    self->tracked_objects = ObjectSet();
    self->steps.reserve(1000);
    self->callback_counter = 0;
    new_milestone(self);
    return (PyObject*) self;
}

static void inplace_opcode(int opcode, ObjectMap& objects, PyObject* a, PyObject* b){
    PyObject* obj = objects[b];
    obj = obj ? obj : b;

    switch(opcode){
        case INPLACE_POWER:
            objects[a] = PyNumber_InPlacePower(objects[a], obj, Py_None);
            break;
        case INPLACE_MULTIPLY:
        case INPLACE_MATRIX_MULTIPLY:
        case INPLACE_TRUE_DIVIDE:
        case INPLACE_FLOOR_DIVIDE:
        case INPLACE_MODULO:
        case INPLACE_ADD:
            objects[a] = PyNumber_InPlaceAdd(objects[a], obj);
            break;
        case INPLACE_SUBTRACT:
        case INPLACE_LSHIFT:
        case INPLACE_RSHIFT:
        case INPLACE_AND:
        case INPLACE_XOR:
        case INPLACE_OR:
            break;
    }
}

static PyObject* Recording_dicts(PyObject *self, PyObject *args){
    PyObject* step_obj;
    if (PyArg_UnpackTuple(args, "dicts", 1, 1, &step_obj)) {
        RecordingObject* recording = (RecordingObject*)self;
        int step = std::min((int)recording->steps.size() - 1, (int)PyLong_AsLong(step_obj));
        auto frame = (PyObject*)std::get<2>(recording->steps[step]);

        // Find the relevant Milestone
        MutationList* mutations; PickleOrder* pickle_order; PyObject* pickle_bytes;
        for(auto& milestone : recording->milestones){
            auto milestone_mutations = std::get<0>(milestone);
            if(milestone_mutations->size() == 0){
                break;  // Brand new milestone (this might be unreachable...)
            } else {
                auto first_mutation = milestone_mutations->at(0);
                if(std::get<0>(first_mutation) <= step){
                    std::tie(mutations, pickle_order, pickle_bytes) = milestone;
                } else {
                    break;
                }
            }
        }

        // Unpickle objects to their state at start of Milestone
        auto bytes = PyObject_CallMethod(pickle_bytes, "getvalue", NULL);               // bytes = pickle_bytes.getvalue()
        auto bytesio = PyObject_CallMethodObjArgs(io_module, bytesio_str, bytes, NULL); // bytes_io = io.BytesIO(bytes)
        auto unpickler = PyObject_CallMethodObjArgs(pickle_module, unpickler_str, bytesio, NULL);
        auto objects = ObjectMap();
        
        for(auto& obj_id : *pickle_order){
            objects[obj_id] = PyObject_CallMethod(unpickler, "load", NULL);
        }

        // Replay mutations up to 'step' so objects are in the correct state (baby VM!)
        auto locals = PyDict_New();
        auto globals = PyDict_New();

        int s; unsigned char op; PyObject *a, *b, *c, *obj;
        for(auto& mutation : *mutations){
            std::tie(s, op, a, b, c) = mutation;
            if(s <= step){
                switch(op){
                    case STORE_ATTR:    // a.b = c
                        PyObject_SetAttr(objects[a], b, c);
                        break;
                    case STORE_SUBSCR:  // a[b] = c
                        PyObject_SetItem(objects[a], b, c);
                        break;

                    case STORE_FAST:    // b = c
                    case STORE_NAME:    
                        if(a == recording->global_frame){
                    case STORE_GLOBAL:
                            obj = objects[c];
                            PyDict_SetItem(globals, b, obj ? obj : c);
                        } else if(a == frame){
                            obj = objects[c];
                            PyDict_SetItem(locals, b, obj ? obj : c);
                        }
                        break;

                    case DELETE_ATTR:   // del a.b
                        PyObject_DelAttr(objects[a], b);
                        break;
                    case DELETE_SUBSCR: // del a[b]
                        PyObject_DelItem(objects[a], b);
                        break;

                    case DELETE_FAST:   // del b
                    case DELETE_NAME:
                        if(a == recording->global_frame){
                    case DELETE_GLOBAL:
                            PyDict_DelItem(globals, b);
                        } else if(a == frame){
                            PyDict_DelItem(locals, b);
                        }
                        break;

                    case INPLACE_POWER:
                    case INPLACE_MULTIPLY:
                    case INPLACE_MATRIX_MULTIPLY:
                    case INPLACE_TRUE_DIVIDE:
                    case INPLACE_FLOOR_DIVIDE:
                    case INPLACE_MODULO:
                    case INPLACE_ADD:
                    case INPLACE_SUBTRACT:
                    case INPLACE_LSHIFT:
                    case INPLACE_RSHIFT:
                    case INPLACE_AND:
                    case INPLACE_XOR:
                    case INPLACE_OR:
                        inplace_opcode(op, objects, a, b);
                        break;
                }
            } else {
                break;
            }
        }

        return Py_BuildValue("NN", globals, locals);   // state is now as it was on requested step
    }
    return NULL;
}

static PyObject* Recording_state(PyObject *self, PyObject *args){
    auto globals_locals = Recording_dicts(self, args);
    if(globals_locals == NULL){
        return NULL;
    } else {
        PyObject* state = PyTuple_GetItem(globals_locals, 0);
        PyDict_Update(state, PyTuple_GetItem(globals_locals, 1));
        return state;
    }
}

static PyMemberDef Recording_members[] = {
    {"code", T_OBJECT_EX, offsetof(RecordingObject, code), 0, "Source code executed for this recording"},
    //{"steps", T_OBJECT_EX, offsetof(RecordingObject, steps), 0, "Line executed at each step"},
    {NULL}
};

static PyMethodDef Recording_methods[] = {
    {"state", (PyCFunction) Recording_state, METH_VARARGS, "Get state dict at step n"},
    {"dicts", (PyCFunction) Recording_dicts, METH_VARARGS, "Get globals and locals dicts at step n"},
    {NULL}
};

static PyTypeObject RecordingType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "execorder.Recording",
    .tp_doc = "A recording of the execution of some code",
    .tp_basicsize = sizeof(RecordingObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_members = Recording_members,
    .tp_methods = Recording_methods,
    .tp_dealloc = (destructor) Recording_dealloc,
    .tp_new = Recording_new,
};

PyTypeObject* Recording_Type(){
    return &RecordingType;
}

RecordingObject* Recording_New(){
    return (RecordingObject*)Recording_new(&RecordingType, NULL, NULL);
}

int track_object(PyObject* obj, PyObject* args){
    auto recording = (RecordingObject*)args;
    if(obj != NULL && !PyModule_Check(obj)){
        if(!recording->tracked_objects.contains(obj)){
            recording->tracked_objects.insert(obj);    
            PyObject_CallMethodObjArgs(recording->pickler, dump_str, obj, NULL);
            if(PyErr_Occurred() == NULL){
                // Saved object successfully, track it's sub-objects too
                recording->pickle_order->push_back(obj);
                auto type = Py_TYPE(obj);
                if((type->tp_flags & Py_TPFLAGS_HEAPTYPE)){
                    traverseproc traverse = type->tp_traverse;
                    if(traverse){
                        traverse(obj, (visitproc)track_object, recording);
                    }
                }
            }
            PyErr_Clear();  
        }
    }
    return 0;
}

// Adds an event to a recording
RecordingObject* Recording_record(  RecordingObject* recording, int event, 
                                    PyObject* a, PyObject* b, PyObject* c){
    Mutation mutation;
    switch(event){
        case STORE_GLOBAL:
        case STORE_FAST:
        case STORE_NAME:
        case INPLACE_POWER:
        case INPLACE_MULTIPLY:
        case INPLACE_MATRIX_MULTIPLY:
        case INPLACE_TRUE_DIVIDE:
        case INPLACE_FLOOR_DIVIDE:
        case INPLACE_MODULO:
        case INPLACE_ADD:
        case INPLACE_SUBTRACT:
        case INPLACE_LSHIFT:
        case INPLACE_RSHIFT:
        case INPLACE_AND:
        case INPLACE_XOR:
        case INPLACE_OR:
            track_object(c, (PyObject*)recording);
        case STORE_SUBSCR:
        case STORE_ATTR:
            track_object(b, (PyObject*)recording);
        case DELETE_GLOBAL:
        case DELETE_FAST:
        case DELETE_NAME:
        case DELETE_SUBSCR:
        case DELETE_ATTR:
            mutation = Mutation(recording->steps.size(), event, a, b, c);
            recording->mutations->push_back(mutation);
            break;

        case PyTrace_CALL:
        case PyTrace_EXCEPTION:
        case PyTrace_LINE:
        case PyTrace_RETURN:
            auto frame = (PyFrameObject*)a;
            int line_number = frame->f_lineno;
            recording->steps.push_back(Step(line_number, event, frame));
            if(recording->callback){
                recording->callback_counter++;
            }
    }

    // We have recorded 200,000 mutations, make new milestone
    if(recording->mutations->size() >= 200000){
        recording->tracked_objects.clear();
        new_milestone(recording);
    }

    // We have exceeded the specified number of execution steps, exit
    int current_steps = (int)recording->steps.size();
    if(recording->max_steps && current_steps >= recording->max_steps){
        PyErr_SetString(PyExc_RuntimeError, "Reached maximum execution steps");
        return NULL;
    }

    // TODO: make this customisable, based on time as well
    // We haven't called back in a while, do it now
    if(recording->callback && recording->callback_counter >= 50000){
        recording->callback_counter = 0;
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

    return recording;
}



