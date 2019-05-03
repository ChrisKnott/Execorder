import sys, os, shutil

# === Make ceval.cpp =======================================================

src_folder = sys.argv[1]    # Pass a top level Python source directory

calls = [
    ('STORE_FAST oparg NULL NULL value',                'FAST_DISPATCH();'),
    ('INPLACE_POWER 0 base exp NULL',                   'Py_DECREF(base);'),
    ('INPLACE_MULTIPLY 0 left right NULL',              'Py_DECREF(left);'),
    ('INPLACE_MATRIX_MULTIPLY 0 left right NULL',       'Py_DECREF(left);'),
    ('INPLACE_TRUE_DIVIDE 0 dividend divisor NULL',     'Py_DECREF(dividend);'),
    ('INPLACE_FLOOR_DIVIDE 0 dividend divisor NULL',    'Py_DECREF(dividend);'),
    ('INPLACE_MODULO 0 left right NULL',                'Py_DECREF(left);'),
    ('INPLACE_ADD 0 left right NULL',                   'Py_DECREF(right);'),
    ('INPLACE_SUBTRACT 0 left right NULL',              'Py_DECREF(left);'),
    ('INPLACE_LSHIFT 0 left right NULL',                'Py_DECREF(left);'),
    ('INPLACE_RSHIFT 0 left right NULL',                'Py_DECREF(left);'),
    ('INPLACE_AND 0 left right NULL',                   'Py_DECREF(left);'),
    ('INPLACE_XOR 0 left right NULL',                   'Py_DECREF(left);'),
    ('INPLACE_OR 0 left right NULL',                    'Py_DECREF(left);'),
    ('STORE_SUBSCR 0 container sub v',                  'Py_DECREF(v);'),
    ('DELETE_SUBSCR 0 container sub NULL',              'Py_DECREF(container);'),
    ('STORE_NAME 0 ns name v',                          'Py_DECREF(v);'),
    ('DELETE_NAME 0 ns name NULL',                      'if (err != 0) {'),
    ('STORE_ATTR 0 owner name v',                       'Py_DECREF(v);'),
    ('DELETE_ATTR 0 owner name NULL',                   'Py_DECREF(owner);'),
    ('STORE_GLOBAL 0 name NULL v',                      'Py_DECREF(v);'),
    ('DELETE_GLOBAL 0 name NULL NULL',                  'if (err != 0) {'),
    ('DELETE_FAST oparg NULL NULL NULL',                'DISPATCH();')
]

with open(os.path.join(src_folder, 'Python/ceval.c'), encoding='utf8') as ceval_file:
    code = ceval_file.read()
    for args, end in calls:
        args = args.split()
        start = 'TARGET(%s)' % args[0]
        if start in code:
            i = code.index(end, code.index(start))
            j = code.rfind('\n', 0, i)
            indent = ' ' * (i - j - 1)
            mutate = 'Execorder_Mutate(f, %s, %s, %s, %s, %s);' % tuple(args)
            code = code[:i] + mutate + '\n' + indent + code[i:]
        else:
            print('NOT FOUND:', start)

extern = '''
// This is a near-exact copy of ceval.c, with calls to Execorder_Mutate()
// added so that we can efficiently record mutations of objects

#define Py_BUILD_CORE
#include "execorder.h"
extern "C" {

//=====================================================================================
//==================================== ceval.c ========================================
//=====================================================================================

%s

} // extern "C"
'''

with open('ceval.cpp', 'w', encoding='utf8') as ceval_file:
    ceval_file.write(extern % code)

# === Copy necessary headers ===================================================

shutil.rmtree('include', ignore_errors=True)
shutil.copytree(os.path.join(src_folder, 'Include'), 'include')

os.makedirs('include_py', exist_ok=True)
shutil.copy(os.path.join(src_folder, 'PC/pyconfig.h'), 'include_py/pyconfig.h')
shutil.copy(os.path.join(src_folder, 'Python/condvar.h'), 'include_py/condvar.h')
shutil.copy(os.path.join(src_folder, 'Python/ceval_gil.h'), 'include_py/ceval_gil.h')

