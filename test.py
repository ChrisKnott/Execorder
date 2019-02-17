import execorder

code = '''
import random

X = []
for i in range(10):
    X += [random.randint(1, 100)]
    random.shuffle(X)
   
'''


recording = execorder.exec(code)       		# Execute the code and get a Recording back

for n in range(0):                        	# Efficiently query the state at any time
    X = recording.state(n).get('X', None)
    print(X)
