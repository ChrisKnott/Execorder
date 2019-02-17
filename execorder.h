#include "Python.h"
#include "frameobject.h"
#include "recording.h"

#define PRNT(obj) PyObject_Print(obj, stdout, 0); printf("\n");	// debug

void do_callback(RecordingObject*);

extern "C"
void Execorder_Mutate(PyFrameObject* frame, int opcode, int i, PyObject* a, PyObject* b, PyObject* c);
