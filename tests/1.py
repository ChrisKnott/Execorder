# This test is difficult because objects IDs are highly reused

import random, collections, _pickle, io, sys

bytes_io = io.BytesIO()
p = _pickle.Pickler(bytes_io)

rnd = lambda n: random.randint(0, n - 1)
counts = collections.defaultdict(int)

for i in range(100):
	x = list(str(rnd(1000000)))		# A new object is created
	print(sys.getrefcount(x))
	x[rnd(len(x))] = rnd(1000)		# It is mutated
	print(sys.getrefcount(x))
	#counts[id(x)] += 1
	p.dump(x)
	print(sys.getrefcount(x))
	print('')

	del x							# Object deleted, ID can be reused

#counts = [(v, k) for k, v in counts.items()] 
#for v, k in sorted(counts):
#	print(v, k)

print('\n\n\n=======================\n\n\n')

i = 0
u = _pickle.Unpickler(io.BytesIO(bytes_io.getvalue()))
while True:
	try:
		x = u.load()
		i += 1
	except:
		print(i)
		break
