import matplotlib.pyplot as plt

file = open("../obj/CWND.csv", "r")
lines = file.readlines()
# close(file)
cwnd, ssthresh, time= [],[],[]

for l in lines:
	l= l.split(' ')
	cwnd.append(l[0])
	ssthresh.append(l[1])
	time.append(l[2])

plt.scatter(time, cwnd, c = 'blue', label = 'cwnd')
plt.scatter(time, ssthresh, c = 'red', label = "ssthresh")
plt.xlabel("time")
plt.ylabel("cwnd or ssthresh")
plt.title("cwnd and ssthresh over time")
plt.legend()
plt.show()

