/* Server side - UDP Code				    */
/* By Hugh Smith	4/1/2017	*/

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
#define DBUG 1

void processClient(int socketNum);
int checkArgs(int argc, char *argv[]);
int handle_file_req(Endpoint,uint8_t *,uint32_t *, uint16_t *,int);
void send_file_response(Endpoint,uint8_t);
void serverUsePhase(Endpoint *,uint32_t,uint16_t);
int processClientMsg(Endpoint *,Window *,uint8_t *,uint32_t *,uint32_t *,int);
void handle_data(Endpoint *,Window *,uint8_t *,uint32_t *,uint32_t *);
void send_RR(Endpoint *,uint32_t);


int main ( int argc, char *argv[]  )
{ 
	int socketNum = 0;				
	int portNumber = 0;
        double errorRate = 0.0;
        uint8_t fileReqBuffer[MAXBUF];
        int pduLength = 0;
        pid_t pid; 
	struct sockaddr_in6 client;		
	int clientAddrLen = sizeof(client);	
        uint16_t bufferSize;
        uint32_t windowSize;

	portNumber = checkArgs(argc, argv);
        
        errorRate = atof(argv[1]);
        if(DBUG) printf("Given error rate %.2f\n",errorRate); 
		
	socketNum = udpServerSetup(portNumber);
        
        sendErr_init(errorRate,DROP_ON,FLIP_ON,DEBUG_ON,RSEED_OFF);
        setupPollSet();
        addToPollSet(socketNum);

        while(1)
        {
                if(pollCall(-1) != socketNum)
                {
                        fprintf(stderr,"%s\n","Huh?");
                        exit(-1);
                }

                if((pduLength = safeRecvfrom(socketNum, fileReqBuffer,MAXBUF, 0, (struct sockaddr *) &client, &clientAddrLen)) == CRC_ERR)
                {
                        continue; /* packet is corrupted, ignore it */
                }
		
                if(DBUG) printf("RECV %d bytes\n",pduLength);
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
                else
                {
                        send_file_response(childConn,FILE_OK);
                }
                
                serverUsePhase(&childConn,windowSize,bufferSize);
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


        printf("SERVER GOT FILENAME OUTPUT %s, len = %ld,windowSize = %d, bufferSize = %d\n",filename,strlen(filename),*windowSize,*bufferSize);

        if((fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) == -1)
        {
                perror("open");
                return -1;
        }

        return 1;
}

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
	
                printPDU((uint8_t *)buffer,dataLen);
		printf("Received message from client with ");
		printIPInfo(&client);
		printf(" Len: %d \'%s\'\n", dataLen, buffer+7);

		// just for fun send back to client number of bytes received
		sprintf(buffer, "bytes: %d", dataLen);
		//safeSendto(socketNum, buffer, strlen(buffer)+1, 0, (struct sockaddr *) & client, clientAddrLen);

	}
}

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

void serverUsePhase(Endpoint *conn,uint32_t windowSize,uint16_t bufferSize)
{
        int status = -1;
        int clientFlag = -1;
        uint8_t clientPDU[bufferSize+HDR_LEN];
        uint32_t expRR = 0;
        uint32_t maxRR = -1;
        Window *circBuff = initWindow(windowSize);

        setupPollSet();
        addToPollSet(conn->socket);

        while(1)
        {
                if(pollCall(10000) == -1)
                {
                        fprintf(stderr,"%s\n","No message from client after 10 seconds, exiting");
                        break;
                }

                if((clientFlag = processClientMsg(conn,circBuff,clientPDU,&expRR,&maxRR,bufferSize)) == F_EOF_ACK_2)
                {
                        break;
                }
        }

        close(conn->socket);
}

int processClientMsg(Endpoint *conn,Window *circBuff,uint8_t *clientPDU,uint32_t *expRR, uint32_t *maxRR,int bufferSize)
{
        int clientAddrLen = sizeof(struct sockaddr_in6);
        int flag = -1;

        
        if(safeRecvfrom(conn->socket,clientPDU,1410,0,(struct sockaddr *)&conn->addr,&clientAddrLen) == CRC_ERR)
        {
                if(DBUG) printf("CRC err\n");
                return -1; /* pretend like we didn't receive a packet if there's a CRC error */
        }
 
        flag = clientPDU[FLAG_OFF];
        
        printf("CLIENT FLAG = %d\n",flag);
        /* received data from client */
        if(flag == F_DATA)
        {
                printf("GONNA HANDLE DATA\n");
                handle_data(conn,circBuff,clientPDU,expRR,maxRR);
        }
        /* received EOF from client */
        else if(flag == F_EOF)
        {
                /*handle_eof(conn,expRR,maxRR);*/
        }

        /* else received EOF ACK 2 from client, just return the flag and teardown phase will begin */

        return flag;
}

void handle_data(Endpoint *conn,Window *circBuff,uint8_t *dataPDU,uint32_t *expRR,uint32_t *maxRR)
{
        uint32_t network_seq_num = -1;
        uint32_t host_seq_num = -1;

        memcpy(&network_seq_num,dataPDU+SEQ_OFF,SEQ_NUM_SIZE);

        host_seq_num = ntohl(network_seq_num);

        printf("HOST SEQ NUM = %d, expexted = %d, max = %d\n",host_seq_num,*expRR,*maxRR);

        /* no loss case: got the expected sequence number, send RR */
        if(host_seq_num == *expRR)
        {
                /* received a packet w/highest sequence number so far */
                if(host_seq_num > *maxRR) 
                {
                        *maxRR = host_seq_num;
                        *expRR++;
                }
                /* received a packet that was previously lost and resent by client */
                else
                {
                        *expRR = *maxRR + 1;
                }
                
                send_RR(conn,*expRR);
        }
        /* loss case: buffer data and possibly send SREJ */
}

void send_RR(Endpoint *conn,uint32_t rr_num)
{
        uint8_t pduBuffer[MAXBUF] = {0};
        int pduLength = -1;
        uint32_t net_rr_num = htonl(rr_num);


        printf("SENDING RR %d\n",rr_num);

        pduLength = createPDU(pduBuffer,0,F_RR,(uint8_t *)&net_rr_num,4);

        safeSendto(conn->socket,pduBuffer,pduLength,0,(struct sockaddr *)&conn->addr,sizeof(struct sockaddr_in6));
}
