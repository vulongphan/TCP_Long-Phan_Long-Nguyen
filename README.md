# TCP_Long-Phan_Long-Nguyen
A project that implements TCP reliable data transfer protocl and congestion control

## Reliable Data Transfer:
1. For the first task, we set a fixed cwnd=10.
2. For the logic, for every packet that the sender sends, it contains seqno which is the number of the first byte of the data from data stream. The fixed MSS size is 1500 bytes.
3. On the receiver end, we keep track of the expecting seqno number of the pkt to check if next pkt we receive is in-order to the data stream. Immediately after receiving the pkt, we send back an ACK which tells the sender that we have received the pkts. Even if our ACK got loss, higher ACK seq will still confirm that earlier pkts have been received. If we received an out-of-order pkt, we discard them and wait for retransmission.
4. On the sender side, we check the ACK that we receive. If the ACK is larger than our sendbase (meaning former pkts received), then we increase our sendbase to the next seqno number right after the lastpktreceived. If we timeout before receiving ACK, we restransmit all the pkts starting from the sendbase.

##TCP Congestion Control:

