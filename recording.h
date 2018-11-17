#include "Python.h"
#include <vector>
#include "sparsepp/spp.h"

using Step = std::tuple<int, int, PyFrameObject*>;
using Mutation = std::tuple<int, unsigned char, PyObject*, PyObject*, PyObject*>;
using MutationList = std::vector<Mutation>;
using PickleOrder = std::vector<PyObject*>;
using Milestone = std::tuple<MutationList*, PickleOrder*, PyObject*>;

typedef struct {
    PyObject_HEAD
    PyObject* code;

    PyObject* global_frame;
    std::vector<Step> steps;
    std::vector<Milestone> milestones;
    spp::sparse_hash_set<PyObject*> tracked_objects;
    PyObject* pickler;			// Uses BytesIO from current Milestone
    PickleOrder* pickle_order;	// Points into current Milestone
    MutationList* mutations;	// Points into current Milestone
    bool new_milestone;
} RecordingObject;

PyObject* Recording_New(void);
PyTypeObject* Recording_Type(void);
void Recording_record(RecordingObject* recording, int event, PyObject* a, PyObject* b, PyObject* c);
void Recording_save_object(RecordingObject* recording, PyObject* obj);
