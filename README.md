# TCP_Long-Phan_Long-Nguyen
A project that implements TCP reliable data transfer protocl and congestion control
**Students**:
- Long Phan (lvp243)
- Long Nguyen (lhn232)
 
## Reliable Data Transfer:
1. For the first task, we set a fixed cwnd=10.
2. For the logic, for every packet that the sender sends, it contains seqno which is the number of the first byte of the data from data stream. The fixed MSS size is 1500 bytes.
3. On the receiver end, we keep track of the expecting seqno number of the pkt to check if next pkt we receive is in-order to the data stream. Immediately after receiving the pkt, we send back an ACK which tells the sender that we have received the pkts. Even if our ACK got loss, higher ACK seq will still confirm that earlier pkts have been received. If we received an out-of-order pkt, we discard them and wait for retransmission.
4. On the sender side, we check the ACK that we receive. If the ACK is larger than our sendbase (meaning former pkts received), then we increase our sendbase to the next seqno number right after the lastpktreceived. If we timeout before receiving ACK, we restransmit all the pkts starting from the sendbase.

##TCP Congestion Control:
1. For reliability, we implement a sliding window on the sender side. We keep a limit on the max number of packets can be sent and not ACKed at any given time. If we receive back an ACK with sequence number higher than our current send base, then we slide the window forward and transmit new packets. Of course, we are also keeping a timer on the earliest unACKed packet. There can be cumulative ACK which will indicate all the earlier packets have been successfully received. If there is a time-out or 3 duplicate ACKs, we retransmit the missing packet(s).
2. On the receiver side, we have an out-of-order buffer that contains all out-of-order packets (there is a limit for this buffer, if it's full then all other out-of-orders packets will be dropped). After receiving an out-of-order packet, receiver immediately sends back a duplicate ACK to indicate packet loss.
3. For Congestion Control, we will implement slow start, congestion avoidance, and fast retransmit.
	1. Slow Start: Starting window is set to 1 and ssthresh is set to 64 packets. For cwnd, we increase the size by 1 for every new ACK and watch out for any packet loss. If there is a packet loss, we set the ssthresh to max(cwnd/2,2).
	2. Congestion Avoidance: We set our sender to enter this mode when the window size is equal to current ssthresh. We reduce the window size growing pace and set it to, for every ACK, we increase the size by 1/windowsize. If there is a packet loss, we switch to fast retransmit.
	3. Fast Retransmit: if there occurs any packet loss either in slow start phase or congestion avoidance, here's what we do: we calculate the new ssthresh=max(currentcwnd/2,2) and then we set the cwnd=1 and start the slow start phase again.

4. Plotting CWND size: for every transmission round, we write down the exact current window size, ssthresh, and time in msec in a file named CWND.csv. We then use matplotlib in Python to plot out how our network transmission rate changes across the time. For visual, cd into our TCP folder and run: `python3 plot.py` (assuming you have installed python3 and matplotlib on your OS). There will be a beautiful plot with a red line indicates the window size and a striped blue line indicates the ssthresh over time. Note: because we are plotting this based on time, you will always see a sharp incline in window size in a small amount of time and it slopes down quite slowly due to the fact that we only note down the numbers every time there's 3 ACK received or timeout which usually takes longer to notice than receiving ACKs.  
