import sys
import numpy as np
reader = open("/dev/pts/22","r");
writer = open("/dev/pts/22","w");
table = []
while True:
	a = reader.readline()
	a =a[:-1]
	if a.isnumeric():
		table += [int(a)]
	else:
		print(a)
	if(len(table)==10):
		print("regression")
		x = np.array(table)
		y = np.array([1,2,3,4,5,6,7,8,9])
		V = np.vstack([x, np.ones(len(x))]).T	
		m, c = np.linalg.lstsq(A, y, rcond=None)[0]
		if m>1.0:
			writer.write("1");
		table=[]
	
reader.close();
