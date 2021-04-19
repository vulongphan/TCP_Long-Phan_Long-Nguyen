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

int next_seqno; // next byte to send
int exp_seqno;  // expected byte to be acked
int send_base = 0;  // first byte in the window
int window_size = 10;

FILE *fp;
int len;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer;
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;

void resend_packets(int sig)
{
    char buffer[DATA_SIZE];

    if (sig == SIGALRM)
    {
        //Resend all packets range between
        //sendBase and next_seqno
        VLOG(INFO, "Timeout happened");
        for (int i = send_base; i <= next_seqno; i += DATA_SIZE)
        {
            // locate the pointer to be read at next_seqno
            fseek(fp, i, SEEK_SET);
            // read bytes from fp to buffer
            len = fread(buffer, 1, DATA_SIZE, fp);
            // make the pkt to send
            sndpkt = make_packet(len);
            memcpy(sndpkt->data, buffer, len);
            sndpkt->hdr.seqno = next_seqno;
            VLOG(DEBUG, "Sending packet of sequence number %d of data size %d to %s",
                 next_seqno, len, inet_ntoa(serveraddr.sin_addr));

            if (sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                       (const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }
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
    int portno;
    char *hostname;
    char buffer[DATA_SIZE];

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
    exp_seqno = DATA_SIZE;

    while (1)
    {
        while (next_seqno < send_base + window_size * DATA_SIZE)
        {
            printf("---------------------------------------------------------------------------------\n");

            printf("current send_base: %d \n", send_base);

            // locate the pointer to be read at next_seqno
            fseek(fp, next_seqno, SEEK_SET);
            // read bytes from fp to buffer
            len = fread(buffer, 1, DATA_SIZE, fp);
            // if end of file
            if (len <= 0)
            {
                VLOG(INFO, "End Of File read");
                sndpkt = make_packet(0);
                VLOG(DEBUG, "Sending packet of sequence number %d of data size %d to %s",
                     next_seqno, len, inet_ntoa(serveraddr.sin_addr));
                sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
                       (const struct sockaddr *)&serveraddr, serverlen);
                printf("-------------------------------------------------------------------------------\n");
                break;
            }

            // make the pkt to send
            sndpkt = make_packet(len);
            memcpy(sndpkt->data, buffer, len);
            sndpkt->hdr.seqno = next_seqno;
            VLOG(DEBUG, "Sending packet of sequence number %d of data size %d to %s",
                 next_seqno, len, inet_ntoa(serveraddr.sin_addr));

            if (sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                       (const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }

            // increment the next sequence number to be sent
            next_seqno += len;

            free(sndpkt);
            printf("---------------------------------------------------------------------------------\n");
        }

        // if timer not started then start it
        start_timer();

        printf("Expected sequence number: %d \n", exp_seqno);

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

        // if receive ack for last pkt
        if (recvpkt->hdr.ackno < exp_seqno) {
            printf("All packets sent successfully\n");
            break;
        }

        // if ACK number from the receiver is greater than the expected sequence number then move the window to the new position
        // start timer if there are still unacked pkts in the window
        if (exp_seqno <= recvpkt->hdr.ackno)
        {
            send_base = recvpkt->hdr.ackno;
            exp_seqno = send_base + DATA_SIZE;
            stop_timer();
            // start timer for unacked pkts
            if (send_base < next_seqno)
                start_timer();
        }
    }
    return 0;
}
