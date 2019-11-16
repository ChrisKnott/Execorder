#pragma once
#include "Python.h"
#include "frameobject.h"
#include <vector>
#include <tuple>
#include "parallel_hashmap/phmap.h"

using Step = std::tuple<int, int, PyFrameObject*>;
using Mutation = std::tuple<size_t, unsigned char, PyObject*, PyObject*, PyObject*>;
using MutationList = std::vector<Mutation>;
using ObjectSet = spp::sparse_hash_set<PyObject*>;
using ObjectMap = phmap::flat_hash_map<PyObject*, PyObject*>;
using PickleOrder = std::vector<PyObject*>;
using Milestone = std::tuple<MutationList*, PickleOrder*, PyObject*>;

/*
    The current implementation of this is fairly slow but robust.

    There are several potentially faster implementations but their implementation
    is extremely brittle or hacky.

    One major improvement would be moving all the pure C++ code (e.g. ->push_back)
    to separate threads (real, non-Python threads) to allow for concurrent execution.
*/

// ==== class Recording ====================
typedef struct {
    PyObject_HEAD
    PyObject*               code;           // Code object that is being executed
    bool                    record_state;   // Whether to record changes in state
    long                    max_steps;      // Maximum execution steps before stopping
    PyObject*               callback;
    int                     callback_counter;

    bool                    fresh_milestone;
    std::vector<Step>       steps;
    PyObject*               visits;			// Use Python list because user can access it
    std::vector<Milestone>  milestones;
    PyObject*               consts;
    ObjectMap               objects;
    ObjectSet               tracked_objects;
    PyObject*               global_frame;
    
    PyObject*               pickler;        // Uses BytesIO from current Milestone
    PickleOrder*            pickle_order;   // Points into current Milestone
    MutationList*           mutations;      // Points into current Milestone
} RecordingObject;

RecordingObject* Recording_New(PyObject* code);
PyTypeObject* Recording_Type(void);

int Recording_record(RecordingObject* self, int event, PyObject* a, PyObject* b, PyObject* c);
bool Recording_object_tracked(RecordingObject* self, PyObject* obj);
void Recording_make_callback(RecordingObject* self);
