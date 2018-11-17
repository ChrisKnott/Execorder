#include "Python.h"
#include "frameobject.h"

#define PRNT(obj) PyObject_Print(obj, stdout, 0); printf("\n");	// debug

extern "C"
void Execorder_Mutate(PyFrameObject* frame, int opcode, int i, PyObject* a, PyObject* b, PyObject* c);
