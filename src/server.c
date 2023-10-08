/* 
 * Nakul Nayak
 * CPE 464
 * Description:
 This file contains the server side code to listen for client connections and enable capability
 to process data from these clients simultaneously following selective reject (SREJ) protocol.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>

#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "myPDU.h"
#include "cpe464.h"
#include "pollLib.h"
#include "Payload.h"
#include "Flags.h"
#include "Windows.h"

#define MAXBUF 1410
#define DBUG 0

/* function prototypes */
int checkArgs(int argc, char *argv[]);
int handle_file_req(Endpoint,uint8_t *,uint32_t *, uint16_t *,int);
void send_file_response(Endpoint,uint8_t);
void serverUsePhase(int,Endpoint *,uint32_t,uint16_t);
int processClientMsg(int,Endpoint *,Window *,uint8_t *,uint32_t *,uint32_t *,int);
void handle_data(Endpoint *,Window *,uint8_t *,int,uint32_t *,uint32_t *,int);
void send_rr(Endpoint *,uint32_t);
void send_srej(Endpoint *,uint32_t);
int handle_eof(Endpoint *,uint32_t *,uint32_t *);

void handleZombies(int sig);

int main ( int argc, char *argv[]  )
{ 
	/* runtime args */
        int socketNum = 0;				
	int portNumber = 0;
        double errorRate = 0.0;
        /* stores data for the file request PDU from client */
        uint8_t fileReqBuffer[MAXBUF];
        int pduLength = 0;
        pid_t pid; 
	struct sockaddr_in6 client;		
	int clientAddrLen = sizeof(client);	
        uint16_t bufferSize;
        uint32_t windowSize;
	/* make sure args are valid */
        portNumber = checkArgs(argc, argv);
        errorRate = atof(argv[1]);
	socketNum = udpServerSetup(portNumber);
        /* setup structures to manage the connection(s) */
        sendErr_init(errorRate,DROP_ON,FLIP_ON,DEBUG_ON,RSEED_OFF);
        setupPollSet();
        addToPollSet(socketNum);
        signal(SIGCHLD, handleZombies);
        /* accept new clients and communicate w/them via child processes */
        while(1)
        {
                if((pduLength = safeRecvfrom(socketNum, fileReqBuffer,MAXBUF, 0, (struct sockaddr *) &client, &clientAddrLen)) == CRC_ERR)
                {
                        continue; /* packet is corrupted, ignore it */
                }
                /* child exits loop */
                if(!(pid=fork()))
                {
                        break;
                }
                else if(pid == -1)
                {
                        perror("fork");
                        exit(2);
                }
        }
        /* child process -- setup, use, teardown phase to connect with client */
        if(pid==0)
        {
                int fd = 0;
                Endpoint childConn;
                childConn.addr = client;
                removeFromPollSet(socketNum);
                close(socketNum);
                /* setup phase */
                if((childConn.socket = socket(AF_INET6,SOCK_DGRAM,0)) == -1)
                {
                        perror("socket");
                        exit(-2);
                }
                addToPollSet(childConn.socket);
                /* send file reject packet */
                if((fd = handle_file_req(childConn,fileReqBuffer,&windowSize,&bufferSize,pduLength))==-1)
                {
                        send_file_response(childConn,FILE_BAD);
                        close(childConn.socket);
                        return 0;
                }
                /* approve file */
                else
                {
                        send_file_response(childConn,FILE_OK);
                }
                /* use phase */
                serverUsePhase(fd,&childConn,windowSize,bufferSize);
                /* teardown phase */
                close(fd);
                close(childConn.socket);
        }
        else close(socketNum);
	return 0;
}

/**
 * Handle client reuquest to upload a file.
 * @param childConn: connection info for the client
 * @param buffer: the buffer to store the file request PDU
 * @param windowSize: the window size to use for the connection
 * @param bufferSize: the buffer size for future PDU message data from this client
 * @param pduLength: the length of the file request PDU
 * @return: 1 for request approval, -1 for rejection (specifically, server is unable
 * to open the requested output file)
*/
int handle_file_req(Endpoint childConn,uint8_t *buffer,uint32_t *windowSize,uint16_t *bufferSize,int pduLength)
{
        char filename[MAX_FILE_SIZE] = {0};
        uint8_t filenameLen = buffer[PAYLOAD_OFF + OUT_FILE_LEN_OFF_7];
        int fd = 0;
        uint32_t netWindowSize = 0;
        uint16_t netBufferSize = 0;

        /* process and store the file request PDU data */
        memcpy(filename,buffer+PAYLOAD_OFF+OUT_FILE_OFF_7,filenameLen);
        memcpy(&netBufferSize,buffer+PAYLOAD_OFF+BUFF_SIZE_OFF_7,2);
        *bufferSize = ntohs(netBufferSize);
        memcpy(&netWindowSize,buffer+PAYLOAD_OFF+WINDOW_SIZE_OFF_7,4);
        *windowSize = ntohl(netWindowSize);
        if((fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) == -1)
        {
                perror("open");
                return -1;
        }
        return fd;
}

/**
 * Approve or reject the client file request.
 * @param childConn: connection info for the client
 * @param status: the status to send back to the client
*/
void send_file_response(Endpoint childConn, uint8_t status)
{
        uint8_t pduBuffer[MAXBUF] = {0};
        int pduLength = createPDU(pduBuffer,0,F_FILEREQ_RESPONSE,&status,1);

        safeSendto(childConn.socket,pduBuffer,pduLength, 0, (struct sockaddr *)&childConn.addr,sizeof(struct sockaddr_in6));
}

/**
 * Check runtime args. Returns port number if args are valid, else terminates process 
 * @param argc: number of runtime args
 * @param argv: runtime args
 * @return: port number to use for server
*/
int checkArgs(int argc, char *argv[])
{
	int portNumber = 0;

	if (argc > 3 || argc < 2)
	{
		fprintf(stderr, "Usage %s [error rate] [optional port number]\n", argv[0]);
		exit(-1);
	}
	if (argc == 3) portNumber = atoi(argv[2]);
	return portNumber;
}

/* facilitates actual exchange of packets view SREJ protocol */
/**
 * Wrapper function to listen for and process client messages.
 * @param fd: the file descriptor to write the received data to
 * @param conn: the connection info for the client
 * @param windowSize: the window size to use for the connection
 * @param bufferSize: the buffer size for the PDU message data
*/
void serverUsePhase(int fd,Endpoint *conn,uint32_t windowSize,uint16_t bufferSize)
{
        int clientFlag = -1;
        uint8_t clientPDU[bufferSize+HDR_LEN];
        uint32_t expRR = 0;
        uint32_t maxRR = 0;
        Window *circBuff = initWindow(windowSize);

        setupPollSet();
        addToPollSet(conn->socket);
        /* loop indefinitely and accept client packets. Exit once the EOF Ack from client is received or server times out */
        while(1)
        {
                if(pollCall(10000) == -1)
                {
                        fprintf(stderr,"%s\n","No message from client after 10 seconds, exiting");
                        break;
                }
                /* received flag to close connection from client */
                if((clientFlag = processClientMsg(fd,conn,circBuff,clientPDU,&expRR,&maxRR,bufferSize)) == F_EOF_ACK_2)
                {
                        break;
                }
        }
        /* free up allocated resources */
        freeWindow(circBuff);
}

/**
 * Given a PDU, drop it if there's a CRC error, else send an appropriate RR and/or SREJ to the client.
 * @param fd: the file descriptor to write the received data to
 * @param conn: the connection info for the client
 * @param windowSize: the window size to use for the connection
 * @param bufferSize: the buffer size for the PDU message data
 * @return: Flag of the client PDU
*/
int processClientMsg(int fd,Endpoint *conn,Window *circBuff,uint8_t *clientPDU,uint32_t *expRR, uint32_t *maxRR,int bufferSize)
{
        int clientAddrLen = sizeof(struct sockaddr_in6);
        int flag = -1;
        int pduLength = 0;
        
        if((pduLength = safeRecvfrom(conn->socket,clientPDU,1410,0,(struct sockaddr *)&conn->addr,&clientAddrLen)) == CRC_ERR)
        {
                return -1; /* pretend like we didn't receive a packet if there's a CRC error */
        }
        flag = clientPDU[FLAG_OFF];
        /* received data from client */
        if(flag == F_DATA)
        {
                handle_data(conn,circBuff,clientPDU,pduLength,expRR,maxRR,fd);
        }
        /* received EOF from client */
        else if(flag == F_EOF)
        {
                handle_eof(conn,expRR,maxRR);
        }
        /* else received EOF ACK 2 from client, just return the flag and teardown phase will begin */
        return flag;
}

/**
 * Take care of data packets received from client. Decide and send appropriate packet(s) response to client.
 * @param conn: the connection info for the client
 * @param circBuff: the window buffer to store data packets in
 * @param dataPDU: the data PDU received from the client
 * @param pduLength: the length of the data PDU
 * @param expRR: the expected RR number
 * @param maxRR: the maximum RR number sent so far
 * @param fd: the file descriptor to write the received data to
*/
void handle_data(Endpoint *conn,Window *circBuff,uint8_t *dataPDU,int pduLength,uint32_t *expRR,uint32_t *maxRR,int fd)
{
        uint32_t network_seq_num = 0;
        uint32_t host_seq_num = 0;
        static uint8_t sent_srej = 0;
        int i; /* loop var */
        WBuff *entry = NULL;

        memcpy(&network_seq_num,dataPDU+SEQ_OFF,SEQ_NUM_SIZE);
        int payloadLength = pduLength - HDR_LEN;
        host_seq_num = ntohl(network_seq_num);
        /* no loss case: got the expected sequence number, send RR */
        if(host_seq_num == *expRR)
        {
                /* received a packet w/highest sequence number so far */
                if(host_seq_num >= *maxRR) 
                {   
                        *maxRR = host_seq_num;
                        *(expRR) = *(expRR) + 1;
                        send_rr(conn,*expRR);
                        if(write(fd,dataPDU+PAYLOAD_OFF,payloadLength)==-1)
                        {
                                perror("write");
                                exit(EXIT_FAILURE);
                        }
                }
                /* received a packet that was previously lost and resent by client */
                else
                {
                        addEntry(circBuff,dataPDU,pduLength,*expRR);
                        for(i=*expRR;i<=(*maxRR);i++)
                        {
                                /* already buffered a packet, write it to the file and increment expected rr */
                                if(existsEntry(circBuff,i)) 
                                {
                                        entry = getEntry(circBuff,i);
                                        if(write(fd,entry->savedPDU+PAYLOAD_OFF,entry->pduLength-HDR_LEN)==-1)
                                        {
                                                perror("write");
                                                exit(EXIT_FAILURE);
                                        }
                                        delEntry(circBuff,i); 
                                        *expRR = (*expRR) + 1;
                                }
                                /* missing packet, set expected rr to seq num of missing packet,send rr and srej, and exit the loop */
                                else 
                                {
                                        *expRR = i;
                                        /*send_rr(conn,*expRR);*/
                                        send_srej(conn,*expRR);
                                        send_rr(conn,*expRR);
                                        sent_srej = 1;
                                        break;
                                }
                        }
                        /* new "window" incoming */
                        if((*expRR) == ((*maxRR) + 1))
                        {
                                sent_srej = 0;
                                send_rr(conn,*expRR);
                        }
                        if(host_seq_num > *maxRR) *maxRR = host_seq_num;
                }
        }
        /* loss case: buffer data and possibly send SREJ */
        else if(host_seq_num > *expRR)
        {
                /* save the given packet */
                addEntry(circBuff,dataPDU,pduLength,host_seq_num); 
                /* send srej if not already done so for the window */
                if(!sent_srej) 
                {
                        send_srej(conn,*expRR);
                        sent_srej = 1;
                }
                if(host_seq_num > *maxRR) *maxRR = host_seq_num;
        }
        /* previous RR was lost, just resend it to please the client */
        else
        {
                if((*maxRR) > (*expRR)) 
                {
                        send_srej(conn,*expRR);
                        sent_srej = 1;
                }
                send_rr(conn,*expRR);
        }
}

/**
 * Send EOF Ack to client if all data has been received.
 * @param conn: the connection info for the client
 * @param expRR: the expected RR number
 * @param maxRR: the maximum RR number received so far
 * @return: 1 if all data packets have been received from client, 0 if not.
*/
int handle_eof(Endpoint *conn,uint32_t *expRR,uint32_t *maxRR)
{
        uint8_t dummy[1];
        int pduLength;
        uint8_t pduBuffer[MAXBUF];
        int reallyDone = 0;

        /* received all packets */
        if(*expRR > *maxRR || (*expRR == 0 && *maxRR == 0))
        {
                pduLength = createPDU(pduBuffer,0,F_EOF_ACK,dummy,0);
                safeSendto(conn->socket,pduBuffer,pduLength,0,(struct sockaddr *)&conn->addr,sizeof(struct sockaddr_in6));
                reallyDone = 1;
        }

        /* else, still waiting on some packets so ignore the EOF packet*/
        return reallyDone;
}

/**
 * Send RR with given number to client .
 * @param conn: the connection info for the client
 * @param rr_num: the RR number to send
*/
void send_rr(Endpoint *conn,uint32_t rr_num)
{
        uint8_t pduBuffer[MAXBUF] = {0};
        int pduLength = -1;
        uint32_t net_rr_num = htonl(rr_num);

        pduLength = createPDU(pduBuffer,0,F_RR,(uint8_t *)&net_rr_num,4);
        safeSendto(conn->socket,pduBuffer,pduLength,0,(struct sockaddr *)&conn->addr,sizeof(struct sockaddr_in6));
}

/**
 * Send SREJ with given number to client .
 * @param conn: the connection info for the client
 * @param seq_num: the seq. number to send
*/
void send_srej(Endpoint *conn,uint32_t seq_num)
{
        uint8_t pduBuffer[MAXBUF] = {0};
        int pduLength = -1;
        uint32_t net_seq_num = htonl(seq_num);
        
        pduLength = createPDU(pduBuffer,0,F_SREJ,(uint8_t *)&net_seq_num,4);
        safeSendto(conn->socket,pduBuffer,pduLength,0,(struct sockaddr *)&conn->addr,sizeof(struct sockaddr_in6));
}

/**
 * Handle child server processes that have terminated.
 * @param sig: the signal that was received (should always be SIGCHLD)
*/
void handleZombies(int sig)
{
        int stat = 0;
        while (waitpid(-1, &stat, WNOHANG) > 0)
        {}

}
