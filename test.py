import execorder, bisect

code = '''import random

X = []
for i in range(10):
    X += [random.randint(1, 100)]
    random.shuffle(X)
   
print(X)
'''


recording = execorder.exec(code)       		# Execute the code and get a Recording back

for n in range(0):                        	# Efficiently query the state at any time
    X = recording.state(n).get('X', None)
    print(X)

for l in range(10):
	visits = recording.visits(l)
	print(' %2d' % l, [visits[v] for v in range(len(visits))])

i = bisect.bisect_left(recording.visits(4), 20)
print(i)