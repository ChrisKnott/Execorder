import time

code = '''
import random

random.seed(123)

X = [random.randint(0, 100) for n in range(2000)]

done = False
while not done:
    done = True
    for i, _ in enumerate(X):
        if i < len(X) - 1:
            a, b = X[i], X[i + 1]
            if a > b:
                done = False
                X[i], X[i + 1] = b, a

'''
code = compile(code, '<test>', 'exec')

def callback(recording):
    print('User code')

start = time.perf_counter()
exec(code, {})
normal_time = time.perf_counter() - start
print('Normal exec:   %.5f' % normal_time)

import execorder
start = time.perf_counter()
recording = execorder.exec(code) #, callback=callback)
recorded_time = time.perf_counter() - start
print('Recorded exec: %.5f (%.2fx slower)' % (recorded_time, recorded_time / normal_time))
print('Recorded', format(recording.steps(), ','), 'execution steps')
