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
	time.append(float(l[2]))

#print(time)
# fig, ax = plt.subplots()
# ax2=plt.twinx()
# ax.plot(time, cwnd, color="red",label='cwnd')
# ax.set_ylabel('cwnd')
# ax.set_xlabel('time')

# ax2.plot(time,ssthresh, color="green", label="ssthresh")
# ax2.set_ylabel('ssthresh')

plt.plot(time,cwnd, color="red",label="cwnd")
plt.axhline(y=ssthresh[0], xmin=0,xmax=time[1]/time[n-1], linestyle="--", label="ssthresh")
x_last=time[1]
for i in range(1,len(ssthresh)):
	plt.axhline(y=ssthresh[i], xmin=x_last/time[n-1], xmax=time[i]/time[n-1], linestyle="--")
	x_last=time[i]
	
plt.ylabel("cwnd/ssthresh")
plt.xlabel("time")
plt.ylim(bottom=0)
plt.xlim(left=0)
plt.title("cwnd and ssthresh over time")
# ax.legend()
plt.legend()
plt.show()

