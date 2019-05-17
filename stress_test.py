import execorder, gevent

bubble = '''
import random

random.seed(123)
X = [random.randint(0, 10) for n in range(700)]

done = False
while not done:
    done = True
    for i, _ in enumerate(X):
        if i < len(X) - 1:
            a, b = X[i], X[i + 1]
            if a > b:
                done = False
                X[i], X[i + 1] = b, a
    #x = repr(gevent.getcurrent())
print(X)
print('DONE %d')
'''

import time

def ptr(obj):
	return hex(id(obj)).upper()[2:].zfill(16)

def callback(recording):
	print('Callback', ptr(recording), recording.steps(), 'steps')
	gevent.sleep(0.001)

def start():
	print('Starting', id(gevent.getcurrent()))
	s = time.perf_counter()
	rec = execorder.exec(bubble % id(gevent.getcurrent()), callback=callback)
	print(rec.steps())
	print('Finished', id(gevent.getcurrent()), time.perf_counter() - s)

gevent.spawn(start)
gevent.spawn(start)
gevent.spawn(start)
print('Spawned')

for _ in range(5):
	gevent.sleep(1.0)
