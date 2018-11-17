import execorder

if False:
    code = '''
    def f(a):
        b = a - 100
        return (a, b, c)

    def g():
        a = 'one'
        b = 'two'
        c = 'three'
        return f(-100)

    a, b, c, d = 1, 2, 3, 'dee'
    x = g()
    '''

    recording = execorder.exec(code)

    for n in range(25):
        globs, locs = recording.state(n)
        for v in 'abcdx':
            print(str(globs.get(v, '-')).rjust(18), end='')
        print('')

        for v in 'abcdx':
            print(str(locs.get(v, '-')).rjust(18), end='')
        print('\n---------------------------------------------------------------------')

    import sys; sys.exit()

code = '''
import random
random.seed(1231231)
X = [random.randint(0, 100) for n in range(1000)]

done = False
while not done:
    done = True
    for i, _ in enumerate(X):
        #p = 'hello%d' % i
        if i < len(X) - 1:
            a, b = X[i], X[i + 1]
            if a > b:
                done = False
                X[i], X[i + 1] = b, a

'''

import time
start = time.process_time()
exec(code, {})
normal_time = time.process_time() - start
print('Normal exec:   %.5f' % normal_time)

start = time.process_time()
recording = execorder.exec(code)
recorded_time = time.process_time() - start
print('Recorded exec: %.5f (%.1fx slower)' % (recorded_time, recorded_time / normal_time))

for n in range(100000, 3000000, 10000):
    start = time.process_time()
    state = recording.state(n)
    print('State at %7d: %.8f' % (n, time.process_time() - start))
