// Client side - UDP Code				    
// By Hugh Smith	4/1/2017		

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "myPDU.h"
#include "cpe464.h"
#include "pollLib.h"
#include "Payload.h"
#include "Flags.h"
#include "Windows.h"

#define MAXBUF 1410 // technically max packet size if 1407
#define MAXPAYLOAD 1400 // technically max packet size if 1407
#define DBUG 1
#define BUFF_BYTES 2 // need enough bytes to represent log2(1400) 
#define WINDOW_BYTES 4// need enough bytes to represent log2(2^30)

/* input arg macros */
#define IN_FROM_OFF 1
#define IN_TO_OFF 2
#define IN_WIN_OFF 3
#define IN_BUFF_OFF 4
#define IN_ERR_OFF 5
#define IN_HOST_OFF 6
#define IN_PORT_OFF 7


/* data phase status macros */
#define DONE 0


void talkToServer(int socketNum, struct sockaddr_in6 * server);
int readFromStdin(char * buffer);
int checkArgs(int argc, char * argv[]);

int setupConnection(int, char *,uint32_t,uint16_t,struct sockaddr_in6 *);

void usePhase(int,int,int,int,struct sockaddr_in6 *);
int populatePayload(int, uint8_t *,int);
int sendData(int,uint8_t *,int,uint8_t *,int,struct sockaddr_in6 *,uint32_t,uint8_t);

int processServerMsg(int socket,Window *win,struct sockaddr_in6 *,uint8_t *,uint32_t *);

void handle_rr(int,Window *,uint8_t *,uint32_t *);
void handle_srej(int,Window *,uint8_t *,struct sockaddr_in6 *);
void handle_eof_ack(int,struct sockaddr_in6 *);

/* custom drop functions -- delete later */
int in_drop(uint32_t);
int rm_drop(uint32_t);

#define DROP_LEN 1
uint32_t drop_dbug[] = {100};


int in_drop(uint32_t seq)
{
        int i;
        for(i=0;i<DROP_LEN;i++)
        {
                if(drop_dbug[i] == seq) return 1;
        }

        return 0;
}

int rm_drop(uint32_t seq)
{
        int i;
        for(i=0;i<DROP_LEN;i++)
        {
                if(drop_dbug[i] == seq) 
                {
                        drop_dbug[i] = -1;
                        return 1;
                }
        }

        return 0;
}

int main (int argc, char *argv[])
 {
	int socketNum = 0;				
	struct sockaddr_in6 server;		// Supports 4 and 6 but requires IPv6 struct
	int portNumber = 0;
        double errorRate = 0.0;
        char *fromFile = NULL;
        char *toFile = NULL;
        int windowSize = 0;
        int bufferSize = 0;
        int status = 0;
        int counter = 0;
        int fd = 0;
	
	portNumber = checkArgs(argc, argv);

        errorRate = atof(argv[IN_ERR_OFF]);
        
        if((windowSize = atoi(argv[IN_WIN_OFF])) <= 0)
        {
                fprintf(stderr,"%s%d\n","Invalid window size ",windowSize);
                exit(EXIT_FAILURE);
        
        }

        if((bufferSize = atoi(argv[IN_BUFF_OFF])) <= 0)
        {
                fprintf(stderr,"%s%d\n","Invalid buffer size ",bufferSize);
                exit(EXIT_FAILURE);
        }


        fromFile = argv[IN_FROM_OFF];
        toFile = argv[IN_TO_OFF];

        if(strlen(toFile) > 255)
        {
                fprintf(stderr,"Output file name length %d too long (max. 255)",(int)strlen(toFile));
                exit(EXIT_FAILURE);
        }

        if(DBUG) 
        {
                printf("ARGS: from = %s, to = %s, window = %d, buffer = %d, err = %.2f, host = %s, port = %d\n", fromFile, toFile, windowSize, bufferSize, errorRate, argv[IN_HOST_OFF], portNumber);
        }

        /* args are good, start the setup */

        if((fd = open(fromFile,O_RDONLY))==-1)
        {
                perror("open");
                exit(EXIT_FAILURE);
        }
	
	socketNum = setupUdpClientToServer(&server, argv[IN_HOST_OFF], portNumber);
        setupPollSet();
        addToPollSet(socketNum);

	
        sendErr_init(errorRate,DROP_ON,FLIP_ON,DEBUG_ON,RSEED_OFF);

        /* setup phase */
        while((counter < 10) && ((status = setupConnection(socketNum,toFile,windowSize,bufferSize, &server)) == TIMEOUT))
        {
                close(socketNum);
                socketNum = setupUdpClientToServer(&server, argv[IN_HOST_OFF], portNumber);
                counter++;
        }

        if(status == FILE_BAD)
        {
                fprintf(stderr,"%s\n","Server cannot write to given output file.");
                exit(EXIT_FAILURE);
        }
        else if(status == TIMEOUT)
        {
                fprintf(stderr,"%s\n","Timed out.");
                exit(EXIT_FAILURE);
        }

        /* use phase */
        usePhase(fd,socketNum,windowSize,bufferSize,&server);

	
        /*talkToServer(socketNum, &server);*/
	
	/* teardown */
        printf("CLIENT TEARDOWN!\n");
        close(fd);
        close(socketNum);

	return 0;
}

/* returns 1 if file approved, 0 if no response, -1 if rejected*/
int setupConnection(int socketNum,char *toFile, uint32_t windowSize, uint16_t bufferSize, struct sockaddr_in6 * server)
{
        int payloadLen = 1 + strlen(toFile) + 1 + BUFF_BYTES + WINDOW_BYTES;
        int pduLength = 0;
        uint8_t pduBuffer[MAXBUF] = {0};
        uint8_t responseBuffer[MAXBUF] = {0};
        uint8_t payloadBuffer[MAXPAYLOAD] = {0};
        uint8_t outfileLen = strlen(toFile);
        printf("outfile = %s, len = %d\n",toFile,outfileLen);
	int serverAddrLen = sizeof(struct sockaddr_in6);
        
        uint16_t netBuffSize = htons(bufferSize);
        uint32_t netWindowSize = htonl(windowSize);

        memcpy(payloadBuffer+BUFF_SIZE_OFF_7,&netBuffSize,2);
        memcpy(payloadBuffer+WINDOW_SIZE_OFF_7,&netWindowSize,4);
        memcpy(payloadBuffer+OUT_FILE_LEN_OFF_7,&outfileLen,1);
        memcpy(payloadBuffer+OUT_FILE_OFF_7,toFile,outfileLen+1);

        pduLength = createPDU(pduBuffer,0,F_FILEREQ,payloadBuffer,payloadLen);

        printf("outLen = %d, OUTPUT NAME in created pdu = %s\n",pduBuffer[13],pduBuffer+14);
        safeSendto(socketNum, pduBuffer, pduLength, 0, (struct sockaddr *) server, serverAddrLen);

        if(pollCall(1000) == -1)
        {
                return TIMEOUT;
        }
        else
        {
                printf("GOT A RESPONSE\n");
                if(safeRecvfrom(socketNum,responseBuffer,MAXBUF,0,(struct sockaddr *)server,&serverAddrLen) == CRC_ERR)
                {
                        if(DBUG) printf("CRC err\n");
                        return TIMEOUT; /* pretend like we didn't receive a packet if there's a CRC error */
                }
                else
                {
                        if(responseBuffer[PAYLOAD_OFF + FILE_STATUS_OFF] == FILE_OK)
                        {
                                if(DBUG) printf("File is good!\n");
                                return FILE_OK;
                        }
                        else
                        {
                                if(DBUG) printf("File is bad\n");
                                return FILE_BAD;
                        }
                }
        }
}

void talkToServer(int socketNum, struct sockaddr_in6 * server)
{
	int serverAddrLen = sizeof(struct sockaddr_in6);
	char * ipString = NULL;
	int dataLen = 0;
        char pduBuffer[MAXBUF+1] = {0};
	char buffer[MAXBUF+1] = {0};
        int pduLength = 0;
        static int counter = 0;

	
	buffer[0] = '\0';
	while (buffer[0] != '.')
	{
		dataLen = readFromStdin(buffer);

		printf("Sending: %s with len: %d\n", buffer,dataLen);
	
		pduLength = createPDU((uint8_t *)pduBuffer,counter,92,(uint8_t *)buffer,dataLen);
                counter += 1;
                printPDU((uint8_t *)pduBuffer,pduLength);

                safeSendto(socketNum, pduBuffer, pduLength, 0, (struct sockaddr *) server, serverAddrLen);

		
		//safeRecvfrom(socketNum, buffer, MAXBUF, 0, (struct sockaddr *) server, &serverAddrLen);
		
		// print out bytes received
		ipString = ipAddressToString(server);
		printf("Server with ip: %s and port %d said it received %s\n", ipString, ntohs(server->sin6_port), buffer);
	      
	}
}

int readFromStdin(char * buffer)
{
	char aChar = 0;
	int inputLen = 0;        
	
	// Important you don't input more characters than you have space 
	buffer[0] = '\0';
	printf("Enter data: ");
	while (inputLen < (MAXBUF - 1) && aChar != '\n')
	{
		aChar = getchar();
		if (aChar != '\n')
		{
			buffer[inputLen] = aChar;
			inputLen++;
		}
	}
	
	// Null terminate the string
	buffer[inputLen] = '\0';
	inputLen++;
	
	return inputLen;
}

int checkArgs(int argc, char * argv[])
{

        int portNumber = 0;
	
        /* check command line arguments  */
	if (argc != 8)
	{
		printf("usage: %s from-filename to-filename window-size buffer-size error-percent remote-machine port-port\n", argv[0]);
		exit(1);
	}
	
	portNumber = atoi(argv[7]);
		
	return portNumber;
}

void usePhase(int fd, int socket, int windowSize,int bufferSize,struct sockaddr_in6 *server)
{
        uint8_t pduBuffer[MAXBUF] = {0};
        uint8_t serverPDU[MAXBUF] = {0};
        uint8_t payloadBuffer[MAXPAYLOAD] = {0};
        int status = -1;
        int seq_num = 0;
        int bytesRead = -1;
        int reached_EOF = 0; /* < 0 means not reached EOF, > 0 = seq num of last packet + means reached EOF */
        Window *win = NULL;
        int counter = 0;
        int pduLength = 0;
        int serverFlag = -1;
        
        if(!(win = initWindow(windowSize)))
        {
                fprintf(stderr,"%s\n","Init window");
        }

        WBuff *entry = NULL;
        int lowestSeq = 0;
        uint32_t maxRR = 0;
        int eof_counter = 0;

        while(status != DONE)
        {
                while(windowOpen(win) && bytesRead)
                {
                        printf("FIRST LOOP!\n");
                        if(DBUG) printf("WINDOW OPEN, seq num = %d\n",getCurrent(win));
                        if((bytesRead = populatePayload(fd,payloadBuffer,bufferSize)) < bufferSize)
                        {
                                printf("HIT EOG!\n");
                                reached_EOF = 1;
                        }
                        if(bytesRead)
                        {
                                /* delete this if check later */
                                if((pduLength = sendData(socket,pduBuffer,-1,payloadBuffer,bytesRead,server,(uint32_t)getCurrent(win),F_DATA)) == -1)
                                {
                                        fprintf(stderr,"%s\n","Send data issue");
                                        return;
                                }

                                if(DBUG) printf("SENT %d bytes!, seq num = %d\n",pduLength,getCurrent(win));

                                addEntry(win,pduBuffer,pduLength,getCurrent(win));
                                
                                /* following line has to be after addEntry() b/c we're useing win.current above */
                                shiftCurrent(win,1);

                        }
                        
                        while(pollCall(0) != -1)
                        {
                                /* process server packet */
                                printf("GOT A MSG!\n");
                                printf("B4: Lower = %d, curr = %d, upper = %d, numItems = %d\n",getLower(win),getCurrent(win),getUpper(win),getNumItems(win));
                                serverFlag = processServerMsg(socket,win,server,serverPDU,&maxRR);
                                printf("AFTER Lower = %d, curr = %d, upper = %d, numItems = %d\n",getLower(win),getCurrent(win),getUpper(win),getNumItems(win));
                        }
                }

                if(DBUG) printf("EXITED OPEN WINDOW LOOP!\n");
                
                counter = 0;

                printf("MAXRR = %d, seq_num-1 = %d\n",maxRR,getCurrent(win)-1);
                printf("AFTER FIRST LOOP Lower = %d, curr = %d, upper = %d, numItems = %d\n",getLower(win),getCurrent(win),getUpper(win),getNumItems(win));
                while(!windowOpen(win) || ((maxRR < (getCurrent(win))) && reached_EOF))
                {
                        printf("2nd LOOP! counter = %d\n",counter);
                        if(counter == 10) 
                        {
                                printf("Counter timed out!\n");
                                return; /* failed to get a server response after 10 transmission attempts, quit*/
                        }
                        
                        /* received a message from server */
                        if(pollCall(1000) != -1)
                        {
                                if(DBUG) printf("POLL READY!\n");
                                serverFlag = processServerMsg(socket,win,server,serverPDU,&maxRR);
                                counter = 0;
                        }

                        /* else, resort to sending lowest packet */
                        else
                        {
                                printf("POLLY TIMED OUT!\n");
                                lowestSeq = getLower(win);
                                if(DBUG) printf("lowest seq num = %d\n",lowestSeq);
                                if(!(entry = getEntry(win,lowestSeq)))
                                {
                                        fprintf(stderr,"%s\n","getEntry");
                                        exit(3);
                                }

                                if(DBUG) fprintf(stderr,"WNDOW CLOSED!,maxRR = %d, current = %d\n",maxRR,getCurrent(win));
                                sendData(socket,entry->savedPDU,entry->pduLength,NULL,0,server,lowestSeq,F_DATA);
                                counter++;
                        }

                }

                /* send eof packet -- but do NOT exit loop until server sends rr for last packet */
                if(reached_EOF)
                {
                        if(eof_counter == 10) return;
                        printf("REACHED EOF\n");
                        sendData(socket,pduBuffer,-1,payloadBuffer,0,server,0,F_EOF);
                        
                        /* check this with him */
                        if(pollCall(1000) != -1)
                        {
                                if((processServerMsg(socket,win,server,serverPDU,&maxRR)) == F_EOF_ACK)
                                        status = DONE;
                        }
                        eof_counter++;
                }
        }

        printf("STATUS = %d, DONE = %d,maxRR = %d,bytesRead = %d\n",status,DONE,maxRR,bytesRead);
        
        freeWindow(win);
}

int populatePayload(int fd, uint8_t *payload,int bufferSize)
{
        int bytesRead = 0;

        if((bytesRead = read(fd,payload,bufferSize)) == -1)
        {
                perror("read");
                exit(3);
        }

        if(DBUG) printf("Ready %d bytes\n",bytesRead);
        return bytesRead;
}

int sendData(int socket, uint8_t *pduBuffer,int pduLength,uint8_t *payload,int payloadLength, struct sockaddr_in6 *server,uint32_t seq_num, uint8_t flag)
{
        
        if(pduLength == -1) pduLength = createPDU(pduBuffer,seq_num,flag,payload,payloadLength);

        if(DBUG) printf("PDU length = %d\n",pduLength);
        
        if(in_drop(seq_num)) 
        {
                fprintf(stderr,"%s%d\n","DROPPED PACKET ",seq_num);
                rm_drop(seq_num);
                return pduLength;
        }
        
        safeSendto(socket, pduBuffer, pduLength, 0, (struct sockaddr *) server, sizeof(struct sockaddr_in6));

        return pduLength;
}

int processServerMsg(int socket, Window *win, struct sockaddr_in6 *server, uint8_t *serverPDU,uint32_t *max_rr)
{
        int serverAddrLen = sizeof(struct sockaddr_in6);
        int flag = -1;

        if(safeRecvfrom(socket,serverPDU,MAXBUF,0,(struct sockaddr *)server,&serverAddrLen) == CRC_ERR)
        {
                if(DBUG) printf("CRC err\n");
                return -1; /* pretend like we didn't receive a packet if there's a CRC error */
        }
 
        flag = serverPDU[FLAG_OFF];
        
        /* received RR from server */
        if(flag == F_RR)
        {
                printf("RECEVED RR!\n");
                handle_rr(socket,win,serverPDU,max_rr);
        }
        /* received SREJ from server */
        else if(flag == F_SREJ)
        {
                printf("RECEVED SREJ!\n");
                handle_srej(socket,win,serverPDU,server);
        }
        /* received EOF ACK from server */
        else if(flag == F_EOF_ACK)
        {
                handle_eof_ack(socket,server);
        }

        return flag;
}

void handle_rr(int socket,Window * win,uint8_t *serverPDU,uint32_t *maxRR)
{
        uint32_t net_rr_num;
        uint32_t host_rr_num;
        
        memcpy(&net_rr_num,serverPDU+PAYLOAD_OFF,4);
        host_rr_num = ntohl(net_rr_num);

        int offset = host_rr_num - getLower(win);

        printf("PRE RECVED RR = %d, lower = %d, offset = %d\n",host_rr_num,getLower(win),offset);

        if(host_rr_num > *maxRR)
        {
                *maxRR = host_rr_num;
        }

        if(offset) slideWindow(win,offset);
        printf("POST RECVED RR = %d, lower = %d, offset = %d\n",host_rr_num,getLower(win),offset);
}

void handle_srej(int socket,Window * win,uint8_t *serverPDU, struct sockaddr_in6 *server)
{
        uint32_t net_srej_num = 0;
        uint32_t host_srej_num = 0;
        memcpy(&net_srej_num,serverPDU + PAYLOAD_OFF,4);
        host_srej_num = ntohl(net_srej_num);
        
        printf("SREJ = %d\n",host_srej_num);
        WBuff *entry = getEntry(win,host_srej_num);

        sendData(socket,entry->savedPDU,entry->pduLength,NULL,0,server,host_srej_num,F_DATA);
        if(DBUG) printf("SREJ: SENT %d bytes!, seq num = %d\n",entry->pduLength,entry->seq_num);
}

void handle_eof_ack(int socket,struct sockaddr_in6 *server)
{
        uint8_t pduBuffer[MAXBUF];
        uint8_t payloadBuffer[1];
        sendData(socket,pduBuffer,-1,payloadBuffer,0,server,0,F_EOF_ACK_2);
}
