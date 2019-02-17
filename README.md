# Execorder
*Execorder* is Python module with an improved `exec()` function that allows;
 - the execution to be recorded
 - the execution to be bounded to a certain time or number of steps
 - callbacks to be made into user code during execution.

```python
import execorder

code = '''
import random
X = []
for i in range(10):
    X += [random.randint(1, 100)]
    random.shuffle(X)
'''

recording = execorder.exec(code)           # Execute the code and get a Recording back

for n in range(40):                        # Efficiently query the state at any time
    X = recording.state(n).get('X', None)
    print(X)

```

Execorder is a fairly low level library, intended to be used in writing a time-travelling debugger, however it may be useful in other contexts such as from the REPL.

## Internals

Inside CPython, there is an important function `_PyEval_EvalFrameDefault()` which is the actual Python VM's main loop. Execorder's version of `exec()` replaces this function with a slightly different version. Execorder's version of `_PyEval_EvalFrameDefault` keeps track of bytecode instructions that mutate the state of objects in memory, or bind objects to names (such as `STORE_NAME`, `STORE_SUBSCR` and `INPLACE_ADD`).

For example in the following Python code...
```python
a = [1, 2, 3]
a[2] = 'hello'
a += ['world']
```
Execorder will remember that on the first line we bound an object to the name `a`, and then on the second line we mutated that object by changing one of it's elements, then on the third line we mutated it by appending an item.

Execorder saves the state of memory at certain 'milestones' in the execution, and between these milestones, records only the specific mutations that happened. By doing this, Execorder can very quickly recover the state of an object at any step of execution (far faster than the original Python code took to run), but also keeps the memory usage relatively low.

Code executed with `execorder.exec()` runs 2-3x slower than code executed with normal `exec()`, but afterwards state can be queried from a recording in a few milliseconds, even if the original script took several seconds to run.

## Installation

Execorder is in early development and might change significantly, but you can check it out like this;

```bash
git clone https://github.com/ChrisKnott/Execorder.git
cd Execorder
python3 setup.py build install
python3 test.py
```

It only works on Python 3.7+

