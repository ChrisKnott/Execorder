import execorder, time

code = '''
import random

random.seed(1231231)

X = [random.randint(0, 100) for n in range(100)]

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

def callback(recording):
    print('User code')

start = time.process_time()
try:
	exec(code, {})
except:
	pass
normal_time = time.process_time() - start
print('Normal exec:   %.5f' % normal_time)

start = time.process_time()
recording = execorder.exec(code, 0, callback)
recorded_time = time.process_time() - start
print('Recorded exec: %.5f (%.2fx slower)' % (recorded_time, recorded_time / normal_time))

