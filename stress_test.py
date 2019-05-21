import execorder, time, sys, gevent

bubble = '''
import random

#random.seed(123)
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

print('DONE %d')
'''

def ptr(obj):
	return hex(id(obj)).upper()[2:].zfill(16)

def callback(recording):
	print('Callback', ptr(recording), recording.steps(), 'steps')
	gevent.sleep(0)

def start():
	print('Starting', id(gevent.getcurrent()))
	s = time.perf_counter()
	rec = execorder.exec(bubble % id(gevent.getcurrent()), record_state=True, callback=callback)
	t = time.perf_counter() - s
	steps = rec.steps()
	X = rec.state(steps)['X']
	print('Finished', id(gevent.getcurrent()), t, steps, X[:3], X[-3:])
	
N = 8
for rep in range(3):
	print('\n======= NEW ROUND ======\n')
	for t in range(N):
		gevent.spawn(start)

	for _ in range(int(1.4 * N)):
		gevent.sleep(1.0)
		print('waiting..........')

