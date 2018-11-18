#include "Python.h"
#include <vector>
#include "sparsepp/spp.h"

using Step = std::tuple<int, int, PyFrameObject*>;
using Mutation = std::tuple<int, unsigned char, PyObject*, PyObject*, PyObject*>;
using MutationList = std::vector<Mutation>;
using ObjectSet = spp::sparse_hash_set<PyObject*>;
using PickleOrder = std::vector<PyObject*>;
using Milestone = std::tuple<MutationList*, PickleOrder*, PyObject*>;

typedef struct {
    PyObject_HEAD
    PyObject*               code;
    int                     max_steps;
    PyObject*               callback;
    PyInterpreterState*     interpreter;
    _PyFrameEvalFunction    real_eval_frame;
    Py_tracefunc            trace_func;

    int                     callback_counter;
    bool                    new_milestone;    
    PyObject*               global_frame;
    std::vector<Step>       steps;
    std::vector<Milestone>  milestones;
    ObjectSet               tracked_objects;
    
    PyObject*               pickler;        // Uses BytesIO from current Milestone
    PickleOrder*            pickle_order;   // Points into current Milestone
    MutationList*           mutations;      // Points into current Milestone
} RecordingObject;

RecordingObject* Recording_New();
PyTypeObject* Recording_Type(void);
RecordingObject* Recording_record(RecordingObject* recording, int event, PyObject* a, PyObject* b, PyObject* c);
void Recording_save_object(RecordingObject* recording, PyObject* obj);
