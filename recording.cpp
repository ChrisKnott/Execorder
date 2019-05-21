// DEBUG stuff ==================================================
#define PRNT(obj) PyObject_Print(obj, stdout, 0);
#define PRNTn(obj) PRNT(obj); printf("\n");
//===============================================================

#include "recording.h"
#include "structmember.h"
#include "opcode.h"

using ObjectMap = spp::sparse_hash_map<PyObject*, PyObject*>;

PyObject* io_module = NULL;
PyObject* pickle_module = NULL;
auto pickler_str = PyUnicode_FromString("Pickler");
auto unpickler_str = PyUnicode_FromString("Unpickler");
auto dump_str = PyUnicode_FromString("dump");
auto bytesio_str = PyUnicode_FromString("BytesIO");

//PyObject* Recording_check_const(RecordingObject*, PyObject*&);
bool Recording_check_const(RecordingObject*, PyObject*&);

static void Recording_new_milestone(RecordingObject* self){
    self->pickle_order = new PickleOrder();
    self->mutations = new MutationList(); 
    self->mutations->reserve(200000);

    Py_XDECREF(self->pickler);  // Forget the Pickler, we keep the BytesIO it wrote to
    auto pickle_bytes = PyObject_CallMethodObjArgs(io_module, bytesio_str, NULL);  
    self->pickler = PyObject_CallMethodObjArgs(pickle_module, pickler_str, pickle_bytes, NULL);

    auto milestone = Milestone(self->mutations, self->pickle_order, pickle_bytes);
    self->milestones.push_back(milestone);

    self->tracked_objects.clear();
    self->fresh_milestone = true;  // Make sure we take full memory snapshot
}

static PyObject* Recording_new(PyTypeObject *type, PyObject *args, PyObject *kwds){
    auto self = (RecordingObject*)type->tp_alloc(type, 0);
    self->tracked_objects = ObjectSet();
    self->steps.reserve(10000);
    self->visits = PyList_New(0);
    self->consts = PyDict_New();
    self->global_frame = NULL;
    self->callback_counter = 0;
    self->pickler = NULL;
    Recording_new_milestone(self);
    return (PyObject*) self;
}

static void Recording_dealloc(RecordingObject *self){
    Py_DECREF(self->pickler);
    Py_DECREF(self->code);
    Py_DECREF(self->visits);
    Py_DECREF(self->consts);
    MutationList* mutations; PickleOrder* pickle_order; PyObject* pickle_bytes;
    for(auto& milestone : self->milestones){
        std::tie(mutations, pickle_order, pickle_bytes) = milestone;
        delete mutations;
        delete pickle_order;
        Py_DECREF(pickle_bytes);
    }
    self->pickle_order = NULL;
    std::vector<Step>().swap(self->steps);
    std::vector<Milestone>().swap(self->milestones);
    Py_TYPE(self)->tp_free((PyObject *) self);
}

static void inplace_opcode(int opcode, ObjectMap& objects, PyObject* a, PyObject* b){
    PyObject* obj = objects[b];
    obj = obj ? obj : b;

    switch(opcode){
        case INPLACE_POWER:
            objects[a] = PyNumber_InPlacePower(objects[a], obj, Py_None);
            break;
        case INPLACE_MULTIPLY:
            objects[a] = PyNumber_InPlaceMultiply(objects[a], obj);
            break;
        case INPLACE_MATRIX_MULTIPLY:
            objects[a] = PyNumber_InPlaceMatrixMultiply(objects[a], obj);
            break;
        case INPLACE_TRUE_DIVIDE:
            objects[a] = PyNumber_InPlaceTrueDivide(objects[a], obj);
            break;
        case INPLACE_FLOOR_DIVIDE:
            objects[a] = PyNumber_InPlaceFloorDivide(objects[a], obj);
            break;
        case INPLACE_MODULO:
            objects[a] = PyNumber_InPlaceRemainder(objects[a], obj);
            break;
        case INPLACE_ADD:
            // TODO: unicode stuff
            //printf("INPLACE_ADD %p %p\n", a, b);
            objects[a] = PyNumber_InPlaceAdd(objects[a], obj);
            break;
        case INPLACE_SUBTRACT:
            objects[a] = PyNumber_InPlaceSubtract(objects[a], obj);
            break;
        case INPLACE_LSHIFT:
            objects[a] = PyNumber_InPlaceLshift(objects[a], obj);
            break;
        case INPLACE_RSHIFT:
            objects[a] = PyNumber_InPlaceRshift(objects[a], obj);
            break;
        case INPLACE_AND:
            objects[a] = PyNumber_InPlaceAnd(objects[a], obj);
            break;
        case INPLACE_XOR:
            objects[a] = PyNumber_InPlaceXor(objects[a], obj);
            break;
        case INPLACE_OR:
            objects[a] = PyNumber_InPlaceOr(objects[a], obj);
            break;
        default:
            printf("UNKNOWN OPCODE %d\n", opcode);
    }
}

static PyObject* Recording_dicts(PyObject *self, PyObject *args){
    // TODO: preserve state so that subsequent calls to step + 1 are fast?

    PyObject* step_obj;
    if (PyArg_UnpackTuple(args, "dicts", 1, 1, &step_obj)) {
        RecordingObject* recording = (RecordingObject*)self;
        int step = std::min((int)recording->steps.size() - 1, (int)PyLong_AsLong(step_obj));
        auto frame = (PyObject*)std::get<2>(recording->steps[step]);

        // Find the relevant Milestone
        MutationList* mutations = NULL; PickleOrder* pickle_order; PyObject* pickle_bytes;
        std::tie(mutations, pickle_order, pickle_bytes) = recording->milestones[0];
        
        for(auto& milestone : recording->milestones){
            auto milestone_mutations = std::get<0>(milestone);
            if(milestone_mutations->size() == 0){
                break;
            } else {
                auto first_mutation = milestone_mutations->at(0);
                if(std::get<0>(first_mutation) <= step){
                    std::tie(mutations, pickle_order, pickle_bytes) = milestone;
                } else {
                    break;
                }
            }
        }

        // Replay mutations up to 'step' so objects are in the correct state (baby VM!)
        auto locals = PyDict_New();
        auto globals = PyDict_New();

        // Unpickle objects to their state at start of Milestone
        auto bytes = PyObject_CallMethod(pickle_bytes, "getvalue", NULL);               // bytes = pickle_bytes.getvalue()
        auto bytesio = PyObject_CallMethodObjArgs(io_module, bytesio_str, bytes, NULL); // bytes_io = io.BytesIO(bytes)
        auto unpickler = PyObject_CallMethodObjArgs(pickle_module, unpickler_str, bytesio, NULL);
        
        auto objects = ObjectMap();     // Map that goes id -> python object
        
        //printf("----- BEGIN consts ----------------------------------\n");
        PyObject *key, *value; Py_ssize_t pos = 0;
        while(PyDict_Next(recording->consts, &pos, &key, &value)) {
            //printf("%p = ", key);
            //PRNTn(value);
            objects[value] = value;   // (in this dictionary, we have key == value except for non-int numbers)    
        }

        //printf("----- BEGIN tracked --------------------------------- \n");
        for(auto& obj_id : *pickle_order){
            //printf("%p = ", obj_id);
            //PRNTn(objects[obj_id]);
            objects[obj_id] = PyObject_CallMethod(unpickler, "load", NULL);
        }

        //printf("----- BEGIN mutations -------------------------------\n");
        size_t s; unsigned char op; PyObject *a, *b, *c, *obj;
        for(auto& mutation : *mutations){
            std::tie(s, op, a, b, c) = mutation;
            if(s <= step){
                //b = Recording_check_const(recording, b);
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

                    default:
                        inplace_opcode(op, objects, a, b);
                }
            } else {
                break;
            }
        }

        if(PyErr_Occurred()){
            PyErr_Print();
        }

        auto dicts = Py_BuildValue("NN", globals, locals);
        //Py_DECREF(globals);
        //Py_DECREF(locals);
        return dicts;   // state is now as it was on requested step
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

static PyObject* Recording_steps(PyObject *self, PyObject *args){
    if (PyArg_UnpackTuple(args, "steps", 0, 0)) {
        RecordingObject* recording = (RecordingObject*)self;
        return PyLong_FromLong((long)recording->steps.size());
    }
    return NULL;
}

static PyObject* Recording_line(PyObject *self, PyObject *args){
    PyObject *n_obj, *line_number = NULL; 
    if (PyArg_UnpackTuple(args, "line", 1, 1, &n_obj)) {
        line_number = PyLong_FromLong(0);
        if(PyNumber_Check(n_obj)){
            RecordingObject* recording = (RecordingObject*)self;
            auto n = PyLong_AsLong(n_obj);
            if(0 <= n && n < recording->steps.size()){
                auto step = recording->steps[n];
                auto line = std::get<0>(step);
                Py_DECREF(line_number);
                line_number = PyLong_FromLong(line);
            }
        }
    }
    return line_number;
}

static PyObject* Recording_visits(PyObject *self, PyObject *args){
    PyObject* l_obj;
    if (PyArg_UnpackTuple(args, "visits", 1, 1, &l_obj)) {
        auto line_num = PyLong_AsLong(l_obj);
        auto recording = (RecordingObject*)self;
        if(line_num <= 0 || line_num > PyList_Size(recording->visits)){
            return PyList_New(0);
        } else {
            auto visit_list = PyList_GetItem(recording->visits, PyLong_AsSsize_t(l_obj) - 1);
            Py_INCREF(visit_list);
            return visit_list;
        }
    }
    return NULL;
}

static PyMemberDef Recording_members[] = {
    {"code", T_OBJECT_EX, offsetof(RecordingObject, code), 0, "Source code executed for this recording"},
    {NULL}
};

static PyMethodDef Recording_methods[] = {
    {"dicts",  (PyCFunction) Recording_dicts,  METH_VARARGS, "Get globals and locals dicts at step n"},
    {"state",  (PyCFunction) Recording_state,  METH_VARARGS, "Get state dict at step n"},
    {"steps",  (PyCFunction) Recording_steps,  METH_VARARGS, "Get total number of steps in recording"},
    {"line",   (PyCFunction) Recording_line,   METH_VARARGS, "Get the line that was executed at step n"},
    {"visits", (PyCFunction) Recording_visits, METH_VARARGS, "Get list of steps that visit line l"},
    {NULL}
};

static PyTypeObject RecordingType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "execorder.Recording",
    sizeof(RecordingObject),
    0,
    (destructor) Recording_dealloc,             /* tp_dealloc */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HEAPTYPE,   /* tp_flags */
    "A recording of an execution of some code", /* tp_doc */
    0, 0, 0, 0, 0, 0, 
    Recording_methods,                          /* tp_methods */
    Recording_members,                          /* tp_members */
    0, 0, 0, 0, 0, 0, 0, 0, 
    Recording_new,                              /* tp_new */
};

PyTypeObject* Recording_Type(){
    return &RecordingType;
}

RecordingObject* Recording_New(PyObject* code){
    if(io_module == NULL){
        io_module = PyImport_ImportModule("io");
        pickle_module = PyImport_ImportModule("_pickle");
    }

    auto self = (RecordingObject*)Recording_new(&RecordingType, NULL, NULL);
    self->code = code;
    Py_INCREF(code);
    return self;
}

bool Recording_object_tracked(RecordingObject* self, PyObject* obj){
    //printf("Recording_object_tracked\n");
    return self->tracked_objects.contains(obj);
}

int track_object(PyObject* obj, PyObject* args){
    auto self = (RecordingObject*)args;
    if(obj != NULL && !PyModule_Check(obj)){
        bool is_const = Recording_check_const(self, obj);
        if(!Recording_object_tracked(self, obj)){
            self->tracked_objects.insert(obj);

            // This object hasn't been pickled for this Milestone yet...
            if(!is_const){
                PyObject_CallMethodObjArgs(self->pickler, dump_str, obj, NULL);
            }

            if(PyErr_Occurred() == NULL){
                if(!is_const){
                    self->pickle_order->push_back(obj);
                }

                // Saved object successfully, track it's sub-objects too
                auto type = Py_TYPE(obj);
                bool traversable = type->tp_flags & (Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_HEAPTYPE);
                if(traversable && !PyType_Check(obj)){
                    traverseproc traverse = type->tp_traverse;
                    if(traverse){
                        traverse(obj, (visitproc)track_object, args);
                    }
                }
            }
            PyErr_Clear();  
        }
    }
    return 0;
}

void Recording_track_object(RecordingObject* self, PyObject* object){
    track_object(object, (PyObject*)self);
}

bool Recording_check_const(RecordingObject* self, PyObject* &obj){
    //printf("Recording_check_const\n");
    if(obj != NULL){
        if(Py_TYPE(obj)->tp_hash == PyObject_HashNotImplemented){
            //return obj;
            return false;
        }
        else {
            PyObject* key = obj;
            if(PyNumber_Check(obj) && !PyLong_CheckExact(obj)){
                // 1 == 1.0, True, 1+0j as dict keys, so need to special case this
                auto hash = PyObject_Hash(obj);
                hash ^= PyObject_Hash((PyObject*)Py_TYPE(obj));
                key = PyLong_FromLong((long)hash);
            }
            
            PyObject* saved_obj = PyDict_GetItemWithError(self->consts, key);
            if(PyErr_Occurred()){
                PyErr_Clear();  // Can happen if obj is something like (1, 2, [])
                //return obj;
                return false;
            }

            if(saved_obj == NULL){
                PyDict_SetItem(self->consts, key, obj);     // Save new const object (this also stops GC)
                saved_obj = obj;
            }

            if(key != obj){
                Py_DECREF(key);
            }

            obj = saved_obj;
            return true;
            //Recording_track_object(self, saved_obj);
            //return saved_obj;
        }
    }
    //return NULL;
    return true;
}

int Recording_record_trace_event(RecordingObject* self, int event, PyFrameObject* frame){
    //printf("Recording_record_trace_event\n");
    int line_number = frame->f_lineno;
    // Save step number for this line visit
    while(PyList_Size(self->visits) < line_number){
        auto new_list = PyList_New(0);
        PyList_Append(self->visits, new_list);
        Py_DECREF(new_list);
    }
    auto visit_list = PyList_GetItem(self->visits, line_number - 1);
    auto step = (long)self->steps.size();
    auto py_step = PyLong_FromLong(step);
    PyList_Append(visit_list, py_step);
    Py_DECREF(py_step);
    // Save line number for this step
    self->steps.push_back(Step(line_number, event, frame));
/*
*/
    if(self->callback){
        self->callback_counter += 1;
        if(self->callback_counter >= 50000){
            self->callback_counter = 0;
            Recording_make_callback(self);
        }
    }

    if(self->max_steps > 0 && step >= self->max_steps){
        PyErr_SetString(PyExc_RuntimeError, "Reached maximum execution steps");
    }

    if(PyErr_Occurred()){
        return -1;
    }
    return 0;
}

void Recording_make_callback(RecordingObject* self){
    //printf("Recording_make_callback\n");
    if(self->callback != NULL && PyCallable_Check(self->callback)){
        // We actually want to override safeguard about tracing during trace callback
        auto tstate = PyThreadState_Get();
        bool am_tracing = tstate->tracing;
        if(am_tracing){
            tstate->tracing--;
            tstate->use_tracing = 1;
        }

        auto args = Py_BuildValue("(O)", self);
        auto ret = PyEval_CallObject(self->callback, args);   // Call into to user code
        Py_DECREF(ret);
        Py_DECREF(args);

        if(am_tracing){
            tstate->tracing++;
            tstate->use_tracing = 0;
        }
    }
}

int Recording_record(RecordingObject* self, int event, PyObject* a, PyObject* b, PyObject* c){
    //printf("Recording_record\n");
    Mutation mutation;
    int err = 0;
    switch(event){
        case PyTrace_CALL:
        case PyTrace_EXCEPTION:
        case PyTrace_LINE:
        case PyTrace_RETURN:
            err = Recording_record_trace_event(self, event, (PyFrameObject*)a);
            break;
        default:
            // Mutation event
            
            /*
            printf("\n%p %p %p =================================\n", a, b, c);
            PRNTn(a);
            PRNTn(b);
            PRNTn(c);
            */
            Recording_check_const(self, b);
            Recording_check_const(self, c);
            //printf("%p %p %p ---------------------------------\n", a, b, c);
            Recording_track_object(self, b);
            Recording_track_object(self, c);
            mutation = Mutation(self->steps.size(), event, a, b, c);
            self->mutations->push_back(mutation);
            //printf("====================================================================================\n\n");
            break;
    }

    // We have recorded 200,000 mutations, make new milestone
    if(self->mutations->size() >= 200000){
        Recording_new_milestone(self);
    }

    return err;
}
