#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <assert.h>

#include "common.h"
#include "packet.h"

int sockfd;                    /* socket */
int portno;                    /* port to listen on */
int clientlen;                 /* byte size of client's address */
struct sockaddr_in serveraddr; /* server's addr */
struct sockaddr_in clientaddr; /* client addr */
FILE *fp;


void write_to_file(char* file_name, FILE* fp, int pos, char* data, int len);
void send_ACK(int ackno);

void write_to_file(char* file_name, FILE* fp, int pos, char* data, int len) {
    fp = fopen(file_name, "r+");
    fseek(fp, pos, SEEK_SET);
    fwrite(data, 1, len, fp);
    fclose(fp);
    printf("pkt with sequence number %d, data_size %d, written to file\n", pos, len);

}

void send_ACK(int ackno) {
    tcp_packet *sndpkt = make_packet(0);
    sndpkt->hdr.ackno = ackno; 
    sndpkt->hdr.ctr_flags = ACK;
    if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
               (struct sockaddr *)&clientaddr, clientlen) < 0)
    {
        error("ERROR in sendto");
    }

}

int main(int argc, char **argv)
{
    int optval;                    /* flag value for setsockopt */
    tcp_packet *recvpkt;
    char* file_name;
    char buffer[MSS_SIZE];
    struct timeval tp;
    int cur_seqno;
    int ackno;

    int buffer_size = 4;
    tcp_packet* buffer_pkts[4]; // buffer to store out-of-order pkts with size = 4 * DATA_SIZE
    int ind; // index of the last packet in the buffer
    /* 
     * check command line arguments 
     */
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <port> FILE_RECVD\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);
    file_name = argv[2];

    fp = fopen(file_name, "w");
    if (fp == NULL)
    {
        error(argv[2]);
    }

    /* 
     * socket: create the parent socket 
     */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    /* setsockopt: Handy debugging trick that lets 
     * us rerun the server immediately after we kill it; 
     * otherwise we have to wait about 20 secs. 
     * Eliminates "ERROR on binding: Address already in use" error. 
     */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
               (const void *)&optval, sizeof(int));

    /*
     * build the server's Internet address
     */
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    /* 
     * bind: associate the parent socket with a port 
     */
    if (bind(sockfd, (struct sockaddr *)&serveraddr,
             sizeof(serveraddr)) < 0)
        error("ERROR on binding");

    /* 
     * main loop: wait for a datagram, then echo it
     */
    VLOG(DEBUG, "epoch time, bytes received, sequence number");

    clientlen = sizeof(clientaddr);
    cur_seqno = 0; 
    ind = -1;

    // need a buffer size = N * DATA_SIZE to store out-of-order pkts
    // note that these out-of-order pkts are in increasing sequence
    // upon receiving out-of-order pkts (recvpkt->hdr.seqno - cur_seqno > DATA_SIZE) we store the data in a buffer
    // and the corresponsing sequence number in seqnos array of the same size
    // upon receiving an expected pkt (recvpkt->hdr.seqno - cur_seqno <= DATA_SIZE), we look into the buffer 
    // to find pkts that are in-order to this pkt and write all these pkts to file
    // then update buffer and seqnos array
    while (1)
    {
        printf("------------------------------------------------------------------\n");
        /*
         * recvfrom: receive a UDP datagram from a client
         */
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
                     (struct sockaddr *)&clientaddr, (socklen_t *)&clientlen) < 0)
        {
            error("ERROR in recvfrom");
        }
        recvpkt = (tcp_packet *)buffer;
        assert(get_data_size(recvpkt) <= DATA_SIZE);
        // if it is an empty pkt that signifies EOF
        if (recvpkt->hdr.data_size == 0)
        {
            continue;
        }
        
        gettimeofday(&tp, NULL);
        VLOG(DEBUG, "%lu, %d, %d", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);

        // handle in-order-packets
        if (recvpkt->hdr.seqno - cur_seqno <= DATA_SIZE)
        { 
            cur_seqno = recvpkt->hdr.seqno; // update current sequence number

            write_to_file(file_name, fp, recvpkt->hdr.seqno, recvpkt->data, recvpkt->hdr.data_size);

            ackno = recvpkt->hdr.seqno + recvpkt->hdr.data_size;

            int new_seqno = cur_seqno + recvpkt->hdr.data_size;
            int cnt = 0; // count of number of next expected pkts found in the buffer
            
            //look into the buffer to find the next expected pkt to write to file
            printf("Checking the buffer for next expected pkt...\n");

            // IMPORTANT: seqno of received pkts might not always be in an increasing order
            for (int i = 0; i <= ind; i++) {
                if (new_seqno == buffer_pkts[i]->hdr.seqno) {
                    cur_seqno = new_seqno;
                    write_to_file(file_name, fp, cur_seqno, buffer_pkts[i]->data, buffer_pkts[i]->hdr.data_size);
                    new_seqno += get_data_size(buffer_pkts[i]);
                    cnt += 1;
                    ackno = new_seqno;
                }
            }
            // shift the packets to the left a number = cnt steps
            if (cnt >= 1){
                for (int i = cnt; i < buffer_size; i++) {
                    buffer_pkts[i-cnt] = buffer_pkts[i];
                }
                // update index of the last pkt in the buffer
                ind -= cnt;
            }
            
            send_ACK(ackno);
        }

        // buffer out-of-order packets
        else {
            if (ind < buffer_size-1) {
                ind += 1;
                buffer_pkts[ind] = recvpkt;
                printf("Out-of-order pkt sequence %d buffered at index %d \n", buffer_pkts[ind]->hdr.seqno, ind);
                
            }
            else printf("Out-of-order pkt received but buffer full!\n");
        }
    }

    return 0;
}