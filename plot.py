import matplotlib.pyplot as plt
import numpy as np

file = open("../obj/CWND.csv", "r")
lines = file.readlines()
cwnd, ssthresh, time= [],[],[]
n=0
#create data
for l in lines:
	n+=1
	l= l.split(' ')
	cwnd.append(float(l[0]))
	ssthresh.append(float(l[1]))
	time.append(int(l[2]))

plt.plot(time,cwnd, color="red",label="cwnd")
plt.axhline(y=ssthresh[0], xmin=0,xmax=time[1]/time[n-1], linestyle="--", label="ssthresh")
x_last=time[1]
for i in range(1,len(ssthresh)):
	plt.axhline(y=ssthresh[i], xmin=x_last/time[n-1], xmax=time[i]/time[n-1], linestyle="--")
	x_last=time[i]
	
plt.ylabel("cwnd/ssthresh")
plt.xlabel("time")
plt.ylim(bottom=0)
plt.xlim(left=0,right=time[n-1])
plt.title("cwnd and ssthresh over time")
plt.legend()
plt.show()

