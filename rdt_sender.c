#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>
#include <math.h>

#include "packet.h"
#include "common.h"

#define max(x,y) (((x) >= (y)) ? (x) : (y))

#define STDIN_FD 0
#define RETRY 200 //milli second

int next_seqno; // next byte to send
int exp_seqno;  // expected byte to be acked
int send_base = 0;  // first byte in the window
float window_size = 1; // window size at the beginning of slow start
float ssthresh = 64; // initial value for slow start threshold
int cong_state = 0; // intially cong_state sets to 0 which means slow start

int timer_on = 0;
FILE *fp;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer;
tcp_packet *recvpkt;
sigset_t sigmask;

void transit(); // transit sender between slow start and congestion avoidance
void increment_window();
void send_packet(char* buffer, int len, int seqno);
void resend_packets(int sig);
void init_timer(int delay, void (*sig_handler)(int));
void start_timer();
void stop_timer();

void transit(){
    if (cong_state == 0) {
        ssthresh = (int)max(window_size/2, 2); // ssthresh set to half the previous value of the window size
        window_size = 1;
        printf("In Slow Start, return to Slow Start mode\n");
        printf("Current window_size: %f\n", window_size);
        printf("Current ssthresh: %f\n", ssthresh);
    }
    else if (cong_state == 1) {
        ssthresh = (int)max(window_size/2, 2); // ssthresh set to half the previous value of the window size
        window_size = 1;
        cong_state = 0;
        printf("In Congestion Avoidance mode, entering Slow Start\n");
        printf("Current window_size: %f\n", window_size);
        printf("Current ssthresh: %f\n", ssthresh);
    }
}

void increment_window() {
    if (cong_state == 0) { // in slow start
        if (window_size == ssthresh-1) {
            cong_state = 1;
            printf("Window size reaches ssthresh, In Slow Start and Entering Congestion Avoidance mode\n");
        }
        else window_size += 1; 
    } 
    
    else if (cong_state == 1) window_size += 1.0/(int)window_size; // in congestion avoidance
    printf("Current window_size: %f\n", window_size);
}

void send_packet(char* buffer, int len, int seqno) {
    tcp_packet *sndpkt = make_packet(len);
    memcpy(sndpkt->data, buffer, len);
    sndpkt->hdr.seqno = seqno;
    VLOG(DEBUG, "Sending packet of sequence number %d of data size %d to %s",
                 seqno, len, inet_ntoa(serveraddr.sin_addr));
    if (sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                       (const struct sockaddr *)&serveraddr, serverlen) < 0)
    {
        error("sendto");
    }
    free(sndpkt);
}


void resend_packets(int sig)
{
    char buffer[DATA_SIZE];
    int len;

    if (sig == SIGALRM)
    {
        VLOG(INFO, "Timeout happened");

        transit();
        
        //resend all packets range between send_base and next_seqno

        for (int i = send_base; i < next_seqno; i += DATA_SIZE)
        {
            // locate the pointer to be read at next_seqno
            fseek(fp, i, SEEK_SET);
            // read bytes from fp to buffer
            len = fread(buffer, 1, DATA_SIZE, fp);
            // send pkt
            send_packet(buffer, len, i);
        }
    }
}

void start_timer()
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
    timer_on = 1;
    printf("Timer on\n");
}

void stop_timer()
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
    timer_on = 0;
    printf("Timer off\n");
}

/*
 * init_timer: Initialize timeer
 * delay: delay in milli seconds
 * sig_handler: signal handler function for resending unacknoledge packets
 */
void init_timer(int delay, void (*sig_handler)(int))
{
    signal(SIGALRM, resend_packets);
    timer.it_interval.tv_sec = delay / 1000; // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;
    timer.it_value.tv_sec = delay / 1000; // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}

int main(int argc, char **argv)
{
    int portno;
    char *hostname;
    char buffer[DATA_SIZE];
    int len;
    int dup_cnt; // count of continuous duplicate ACKs

    /* check command line arguments */
    if (argc != 4)
    {
        fprintf(stderr, "usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "r");
    if (fp == NULL)
    {
        error(argv[3]);
    }

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    /* initialize server server details */
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    /* covert host into network byte order */
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0)
    {
        fprintf(stderr, "ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    // init timer
    init_timer(RETRY, resend_packets);

    next_seqno = 0;
    exp_seqno = DATA_SIZE;

    dup_cnt = 1;

    while (1)
    {
        // send all pkts in the effective window
        printf("..Sending packets in the effective window ..\n");
        while (next_seqno < send_base + (int)(window_size) * DATA_SIZE)
        {
            printf("*\n");

            // start the timer if not alr started
            if (timer_on == 0) start_timer();

            printf("current send_base: %d \n", send_base);

            // locate the pointer to be read at next_seqno
            fseek(fp, next_seqno, SEEK_SET);
            // read bytes from fp to buffer
            len = fread(buffer, 1, DATA_SIZE, fp);
            // if end of file
            if (len <= 0)
            {
                VLOG(INFO, "End Of File read");
                send_packet(buffer, 0, 0);
                printf("*\n");
                break;
            }

            // send pkt
            send_packet(buffer, len, next_seqno);

            // increment the next sequence number to be sent
            next_seqno += len;
            printf("*\n");
        }
        printf(".. Finish sending packets in effective window ..\n");

        printf("Sequence number expected: %d \n", exp_seqno);

        printf("Waiting for ACK from receiver...\n");

        // wait for ACK
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
                     (struct sockaddr *)&serveraddr, (socklen_t *)&serverlen) < 0)
        {
            error("recvfrom");
        }

        // make recv pkt
        recvpkt = (tcp_packet *)buffer;

        // printf("%d \n", get_data_size(recvpkt));
        printf("ACK from receiver: %d \n", recvpkt->hdr.ackno);

        assert(get_data_size(recvpkt) <= DATA_SIZE);

        // increase window size when an ACK is received
        increment_window();
        if (window_size == ssthresh && cong_state == 0) {
            cong_state = 1;
            printf("***Window size reaches ssthresh, In Slow Start and Entering Congestion Avoidance mode\n");
        }

        // if receive ack for last pkt
        if (recvpkt->hdr.ackno % DATA_SIZE > 0) {
            printf("***All packets sent successfully\n");
            stop_timer();
            break;
        }

        if (recvpkt->hdr.ackno == send_base) {
            // printf("1 more dup ACK with ackno = %d received!\n", recvpkt->hdr.ackno);
            dup_cnt += 1;
            if (dup_cnt == 3) {
                printf("***3 dup ACKs received, with ackno = %d, packet loss detected!\n", recvpkt->hdr.ackno);     
                transit();
            }
        }

        // if ACK number from the receiver is greater than the expected sequence number then move the window to the new position
        // start timer if there are still unacked pkts in the window
        if (exp_seqno <= recvpkt->hdr.ackno)
        {
            send_base = recvpkt->hdr.ackno;
            dup_cnt = 1; // reset count for dup ACKs 
            exp_seqno = send_base + DATA_SIZE;
            stop_timer();
            // start timer for unacked pkts
            if (send_base < next_seqno)
                start_timer();
        }
    }
    return 0;
}
