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

#include "packet.h"
#include "common.h"

#define STDIN_FD 0
#define RETRY 120 //milli second

int next_seqno = 0; // next byte to send
int exp_seqno = 0; // expected byte to be acked 
int send_base = 0; // first byte in the window
int window_size = 10;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer;
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;

void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
        //Resend all packets range between
        //sendBase and nextSeqNum
        VLOG(INFO, "Timout happend");
        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                   (const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }
    }
}

void start_timer()
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}

void stop_timer()
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
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
    int portno, len;
    int next_seqno;
    char *hostname;
    char buffer[DATA_SIZE];
    FILE *fp;

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

    //Stop and wait protocol

    init_timer(RETRY, resend_packets);
    next_seqno = 0;
    long sequence[window_size]; 
    int seqno_ind = 0;
    int exp_seqno_ind = 1;
    while (1)
    {
        printf("------------------------------------------\n");
        // we will need to keep an order of all the bytes sent in the window by using an array of size = window size
        // this way the expected byte number can be updated using this array 
        while (next_seqno < send_base + window_size * DATA_SIZE && next_seqno >= send_base)
        {
            // populate the sequence array
            sequence[seqno_ind] = next_seqno;
            printf("next_seqno to send: %d \n", next_seqno);
            printf("current send_base: %d \n", send_base);
            // locate the pointer to be read at next_seqno 
            fseek(fp, next_seqno, SEEK_SET);
            // read bytes from fp to buffer
            len = fread(buffer, 1, DATA_SIZE, fp);
            // if end of file
            if (len <= 0)
            {
                VLOG(INFO, "End Of File has been reached");
                sndpkt = make_packet(0);
                sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
                       (const struct sockaddr *)&serveraddr, serverlen);
                break;
            }

            // make the pkt to send
            sndpkt = make_packet(len);
            memcpy(sndpkt->data, buffer, len);
            sndpkt->hdr.seqno = next_seqno;
            VLOG(DEBUG, "Sending packet %d to %s",
                 next_seqno, inet_ntoa(serveraddr.sin_addr));

            if (sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                       (const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }
            // if timer not started then start it
            start_timer();

            // increment index of the sequence array if the current pkt is not the last one in the window
            if (seqno_ind < window_size-1) seqno_ind += 1;
            // increment the next sequence number to be sent
            next_seqno += len;

            free(sndpkt);

            // last pkt of the file to be sent that has len < DATA_SIZE
            if (len < DATA_SIZE) break;
        }

        exp_seqno = sequence[exp_seqno_ind];
        printf("Expected sequence number: %d \n", exp_seqno);
        printf("Sequence array of the window: \n");
        for (int i = 0; i < window_size; i++) {
            printf("%ld ",sequence[i]);
        };
        printf("\n");

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
        stop_timer();

        // check the ack nb from the receiver for a timeout
        if (recvpkt->hdr.ackno != exp_seqno) {
            // if the expected pkt is timeout, resent all packets starting from send_base
            next_seqno = send_base;
            continue;
        }

        // in case the pkt is received correctly (recvpkt->hdr.ackno == exp_seqno)
        send_base += DATA_SIZE; // slide window forward
        // exp_seqno_ind += 1; // the expected sequence number is the next element in the sequence array
        
        // we need to delete the starting byte number of the successfully received pkt (which is the first element in the sequence array)
        for (int i = 0; i < window_size-1; i++) {
            sequence[i] = sequence[i+1];
        }  

    }
    return 0;
}
