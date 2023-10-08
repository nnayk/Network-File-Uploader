/* Server side - UDP Code				    */

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

void processClient(int socketNum);
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
        if(DBUG) printf("Given error rate %.2f\n",errorRate); 
		
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
		
                if(DBUG) printf("RECV %d bytes\n",pduLength);
                /* child exits loop */
                if(!(pid=fork()))
                {
                        if(DBUG) printf("FORKED!\n");
                        break;
                }
                else if(pid == -1)
                {
                        perror("fork");
                        exit(2);
                }
        }

        /* child process -- setup, use, teardown phases to connect  with client */
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
                if(DBUG) printf("SERVER TEARDOWN!\n");
                close(fd);
                close(childConn.socket);
        }

        else close(socketNum);
	
	return 0;
}

/* returns 1 if file OK, -1 if not able to open file */
int handle_file_req(Endpoint childConn,uint8_t *buffer,uint32_t *windowSize,uint16_t *bufferSize,int pduLength)
{
        char filename[MAX_FILE_SIZE] = {0};
        uint8_t filenameLen = buffer[PAYLOAD_OFF + OUT_FILE_LEN_OFF_7];
        int fd = 0;
        uint32_t netWindowSize = 0;
        uint16_t netBufferSize = 0;

        memcpy(filename,buffer+PAYLOAD_OFF+OUT_FILE_OFF_7,filenameLen);

        memcpy(&netBufferSize,buffer+PAYLOAD_OFF+BUFF_SIZE_OFF_7,2);
        *bufferSize = ntohs(netBufferSize);

        memcpy(&netWindowSize,buffer+PAYLOAD_OFF+WINDOW_SIZE_OFF_7,4);
        *windowSize = ntohl(netWindowSize);


        if(DBUG) printf("SERVER GOT FILENAME OUTPUT %s, len = %ld,windowSize = %d, bufferSize = %d\n",filename,strlen(filename),*windowSize,*bufferSize);

        if((fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) == -1)
        {
                perror("open");
                return -1;
        }

        return fd;
}

/* either approve or reject client file request */
void send_file_response(Endpoint childConn, uint8_t status)
{
        uint8_t pduBuffer[MAXBUF] = {0};
        int pduLength = createPDU(pduBuffer,0,F_FILEREQ_RESPONSE,&status,1);

        safeSendto(childConn.socket,pduBuffer,pduLength, 0, (struct sockaddr *)&childConn.addr,sizeof(struct sockaddr_in6));
}

void processClient(int socketNum)
{
	int dataLen = 0; 
	char buffer[MAXBUF + 1];	  
	struct sockaddr_in6 client;		
	int clientAddrLen = sizeof(client);	
	
	buffer[0] = '\0';
	while (buffer[0] != '.')
	{
		dataLen = safeRecvfrom(socketNum, buffer, MAXBUF, 0, (struct sockaddr *) &client, &clientAddrLen);
	
		if(DBUG) printf("Received message from client with ");
		printIPInfo(&client);
		if(DBUG) printf(" Len: %d \'%s\'\n", dataLen, buffer+7);

		// just for fun send back to client number of bytes received
		sprintf(buffer, "bytes: %d", dataLen);
		//safeSendto(socketNum, buffer, strlen(buffer)+1, 0, (struct sockaddr *) & client, clientAddrLen);

	}
}

/* utility function that checks runtime args. Returns port number if args are valid, else terminates process */
int checkArgs(int argc, char *argv[])
{
	// Checks args and returns port number
	int portNumber = 0;

	if (argc > 3 || argc < 2)
	{
		fprintf(stderr, "Usage %s [error rate] [optional port number]\n", argv[0]);
		exit(-1);
	}
	
	if (argc == 3)
	{
		portNumber = atoi(argv[2]);
	}
	
	return portNumber;
}

/* facilitates actual exchange of packets view SREJ protocol */
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

                if((clientFlag = processClientMsg(fd,conn,circBuff,clientPDU,&expRR,&maxRR,bufferSize)) == F_EOF_ACK_2)
                {
                        break;
                }
        }

        freeWindow(circBuff);
}

/* given a PDU, drops it if there's a CRC error, else sends an appropriate RR and/or SREJ. Returns the flag of the client PDU.*/
int processClientMsg(int fd,Endpoint *conn,Window *circBuff,uint8_t *clientPDU,uint32_t *expRR, uint32_t *maxRR,int bufferSize)
{
        int clientAddrLen = sizeof(struct sockaddr_in6);
        int flag = -1;
        int pduLength = 0;

        
        if((pduLength = safeRecvfrom(conn->socket,clientPDU,1410,0,(struct sockaddr *)&conn->addr,&clientAddrLen)) == CRC_ERR)
        {
                if(DBUG) printf("CRC err\n");
                return -1; /* pretend like we didn't receive a packet if there's a CRC error */
        }
 
        flag = clientPDU[FLAG_OFF];
        
        if(DBUG) printf("CLIENT FLAG = %d\n",flag);
        /* received data from client */
        if(flag == F_DATA)
        {
                if(DBUG) printf("GONNA HANDLE DATA\n");
                handle_data(conn,circBuff,clientPDU,pduLength,expRR,maxRR,fd);
        }
        /* received EOF from client */
        else if(flag == F_EOF)
        {
                if(DBUG) printf("GONNA HANDLE EOF!\n");
                handle_eof(conn,expRR,maxRR);
        }

        /* else received EOF ACK 2 from client, just return the flag and teardown phase will begin */

        return flag;
}

/* takes care of data packets received from client. Decides what packet(s) to send back */
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

        if(DBUG) printf("HOST SEQ NUM = %d, expexted = %d, max = %d,payload length = %d\n",host_seq_num,*expRR,*maxRR,payloadLength);


        /* no loss case: got the expected sequence number, send RR */
        if(host_seq_num == *expRR)
        {
                /* received a packet w/highest sequence number so far */
                if(host_seq_num >= *maxRR) 
                {
                        
                        *maxRR = host_seq_num;
                        if(DBUG) printf("BEFORE expr = %d\n",*expRR);
                        *(expRR) = *(expRR) + 1;
                        if(DBUG) printf("NEW EXPRR = %d\n",*expRR);
                        send_rr(conn,*expRR);
                        if(write(fd,dataPDU+PAYLOAD_OFF,payloadLength)==-1)
                        {
                                perror("write");
                                exit(EXIT_FAILURE);
                        }

                        if(DBUG)
                        {
                                printf("WROTE1 %d\n",host_seq_num);
                        }
                }
                
                /* received a packet that was previously lost and resent by client */
                else
                {
                        addEntry(circBuff,dataPDU,pduLength,*expRR);
                        for(i=*expRR;i<=(*maxRR);i++)
                        {
                                if(DBUG) printf("INSIDE LOOP: i = %d, expRR = %d, maxRR = %d\n",i,*expRR,*maxRR);
                                /* already buffered a packet, write it to the file and increment expected rr */
                                if(existsEntry(circBuff,i)) 
                                {
                                        if(DBUG) printf("i=%d exists!\n",i);
                                        entry = getEntry(circBuff,i);
                                        if(write(fd,entry->savedPDU+PAYLOAD_OFF,entry->pduLength-HDR_LEN)==-1)
                                        {
                                                perror("write");
                                                exit(EXIT_FAILURE);
                                        }
                                        if(DBUG) printf("WROTE2 %d\n",i);
                                        delEntry(circBuff,i); 
                                        *expRR = (*expRR) + 1;
                                }
                                
                                /* missing packet, set expected rr to seq num of missing packet,send rr and srej, and exit the loop */
                                else 
                                {
                                        if(DBUG) printf("i=%d dne!\n",i);
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
                                if(DBUG) printf("RESET SENT_SREJ!\n");
                                sent_srej = 0;
                                send_rr(conn,*expRR);
                        }
                        else
                        {
                                if(DBUG) printf("expRR = %d does not match maxRR = %d! sent_srej = %d\n",*expRR,*maxRR,sent_srej);
                        }
                        
                        if(host_seq_num > *maxRR) *maxRR = host_seq_num;
                }
                
                if(DBUG) printf("GOT EXPECTED RR = %d\n",*expRR);
        }
        /* loss case: buffer data and possibly send SREJ */
        else if(host_seq_num > *expRR)
        {
                if(DBUG) printf("GOT %d, expected %d\n",host_seq_num,*expRR);
                /* save the given packet */
                addEntry(circBuff,dataPDU,pduLength,host_seq_num); 
                /* send srej if not already done so for the window */
                if(!sent_srej) 
                {
                        if(DBUG) printf("SENDING SREJ = %d\n",*expRR);
                        send_srej(conn,*expRR);
                        sent_srej = 1;
                }
                else
                {
                        if(DBUG) printf("UNABLE TO SEND SREJ = %d\n",*expRR);
                }
                if(host_seq_num > *maxRR) *maxRR = host_seq_num;
        }
        /* previous RR was lost, just resend it to please the client */
        else
        {
                if(DBUG) printf("GOT LOWER RR = %d, expected = %d, sendu = %d\n",host_seq_num,*expRR,sent_srej);
                if((*maxRR) > (*expRR)) 
                {
                        if(DBUG) printf("SENDY SREJ!\n");
                        send_srej(conn,*expRR);
                        sent_srej = 1;
                }
                send_rr(conn,*expRR);
        }
}

/* Sends EOF Ack to client if all data has been received. Returns 1 if all data packets have been received from client, 0 if not */
int handle_eof(Endpoint *conn,uint32_t *expRR,uint32_t *maxRR)
{
        uint8_t dummy[1];
        int pduLength;
        uint8_t pduBuffer[MAXBUF];
        int reallyDone = 0;

        /* received all packets */
        if(*expRR > *maxRR || (*expRR == 0 && *maxRR == 0))
        {
                if(DBUG) printf("SENDING EOF ACK!\n");
                pduLength = createPDU(pduBuffer,0,F_EOF_ACK,dummy,0);
                safeSendto(conn->socket,pduBuffer,pduLength,0,(struct sockaddr *)&conn->addr,sizeof(struct sockaddr_in6));
                reallyDone = 1;
        }

        /* else, still waiting on some packets so ignore the EOF packet*/
        return reallyDone;
}

/* Sends RR with given number to client */
void send_rr(Endpoint *conn,uint32_t rr_num)
{
        uint8_t pduBuffer[MAXBUF] = {0};
        int pduLength = -1;
        uint32_t net_rr_num = htonl(rr_num);

        if(DBUG) printf("SENDING RR %d\n",rr_num);
        
        pduLength = createPDU(pduBuffer,0,F_RR,(uint8_t *)&net_rr_num,4);
        safeSendto(conn->socket,pduBuffer,pduLength,0,(struct sockaddr *)&conn->addr,sizeof(struct sockaddr_in6));
}

/* Sends SREJ with given number to client */
void send_srej(Endpoint *conn,uint32_t seq_num)
{
        uint8_t pduBuffer[MAXBUF] = {0};
        int pduLength = -1;
        uint32_t net_seq_num = htonl(seq_num);
        
        pduLength = createPDU(pduBuffer,0,F_SREJ,(uint8_t *)&net_seq_num,4);
        safeSendto(conn->socket,pduBuffer,pduLength,0,(struct sockaddr *)&conn->addr,sizeof(struct sockaddr_in6));
}

/* simple function to handle child server processes */
void handleZombies(int sig)
{
        int stat = 0;
        while (waitpid(-1, &stat, WNOHANG) > 0)
        {}

}
