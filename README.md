# Execorder
A Python module with an improved 'exec' function that allows the execution to be recorded

```python
import execorder

code = '''
import random
X = []
for i in range(10):
    X += [random.randint(1, 100)]
    random.shuffle(X)
'''

recording = execorder.exec(code)

for n in range(40):
    X = recording.state(n).get('X', None)
    print(X)

```
