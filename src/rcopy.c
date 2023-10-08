/* 
 * Nakul Nayak
 * CPE 464
 * Description:
 This file contains the client side code
 */
			    
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
#define DBUG 0
#define BUFF_BYTES 2 // need enough bytes to represent log2(1400) 
#define WINDOW_BYTES 4// need enough bytes to represent log2(2^30)

#define MAX_FILE_LEN 100

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
#define SREJ_BAD 20

int checkArgs(int argc, char * argv[]);

int setupConnection(int, char *,uint32_t,uint16_t,struct sockaddr_in6 *);

void usePhase(int,int,int,int,struct sockaddr_in6 *);
int populatePayload(int, uint8_t *,int);
int sendData(int,uint8_t *,int,uint8_t *,int,struct sockaddr_in6 *,uint32_t,uint8_t);

int processServerMsg(int socket,Window *win,struct sockaddr_in6 *,uint8_t *,uint32_t *);

void handle_rr(int,Window *,uint8_t *,uint32_t *);
void handle_srej(int,Window *,uint8_t *,struct sockaddr_in6 *);
void handle_eof_ack(int,struct sockaddr_in6 *);


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
	
	/* check runtime args and get port number if they're valid */
        portNumber = checkArgs(argc, argv);

        /* parse runtime args */
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

        if(strlen(toFile) > MAX_FILE_LEN)
        {
                fprintf(stderr,"Output file name length %d too long (max. %d)",(int)strlen(toFile),MAX_FILE_LEN);
                exit(EXIT_FAILURE);
        }

        /* args are good, start the setup */

        if((fd = open(fromFile,O_RDONLY))==-1)
        {
                perror("open");
                exit(EXIT_FAILURE);
        }
	
	/* setup structures to create connection */
        socketNum = setupUdpClientToServer(&server, argv[IN_HOST_OFF], portNumber);
        setupPollSet();
        addToPollSet(socketNum);

	/* set the error rate */
        sendErr_init(errorRate,DROP_ON,FLIP_ON,DEBUG_ON,RSEED_OFF);

        /* setup phase -- send file request up to 10 times (exit if no response after 10 attempts) */
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
	
	/* teardown phase */
        if(DBUG) printf("CLIENT TEARDOWN!\n");
        close(fd);
        close(socketNum);

	return 0;
}

/**
 * Setup the application connection via a file upload request to the server.
 * @param socketNum: the socket number to use
 * @param toFile: the name of the file to store on the server
 * @param windowSize: the window size to use
 * @param bufferSize: the buffer size to use
 * @param server: the server address
 * @return: 1 upon successful connection, 0 if no server response, -1 if rejected.
*/
int setupConnection(int socketNum,char *toFile, uint32_t windowSize, uint16_t bufferSize, struct sockaddr_in6 * server)
{
        int payloadLen = 1 + strlen(toFile) + 1 + BUFF_BYTES + WINDOW_BYTES;
        int pduLength = 0;
        uint8_t pduBuffer[MAXBUF] = {0};
        uint8_t responseBuffer[MAXBUF] = {0};
        uint8_t payloadBuffer[MAXPAYLOAD] = {0};
        uint8_t outfileLen = strlen(toFile);
        if(DBUG) printf("outfile = %s, len = %d\n",toFile,outfileLen);
	int serverAddrLen = sizeof(struct sockaddr_in6);
        
        uint16_t netBuffSize = htons(bufferSize);
        uint32_t netWindowSize = htonl(windowSize);

        memcpy(payloadBuffer+BUFF_SIZE_OFF_7,&netBuffSize,2);
        memcpy(payloadBuffer+WINDOW_SIZE_OFF_7,&netWindowSize,4);
        memcpy(payloadBuffer+OUT_FILE_LEN_OFF_7,&outfileLen,1);
        memcpy(payloadBuffer+OUT_FILE_OFF_7,toFile,outfileLen+1);

        pduLength = createPDU(pduBuffer,0,F_FILEREQ,payloadBuffer,payloadLen);

        if(DBUG) printf("outLen = %d, OUTPUT NAME in created pdu = %s\n",pduBuffer[13],pduBuffer+14);
        safeSendto(socketNum, pduBuffer, pduLength, 0, (struct sockaddr *) server, serverAddrLen);

        if(pollCall(1000) == -1)
        {
                return TIMEOUT;
        }
        else
        {
                if(DBUG) printf("GOT A RESPONSE\n");
                if(safeRecvfrom(socketNum,responseBuffer,MAXBUF,0,(struct sockaddr *)server,&serverAddrLen) == CRC_ERR)
                {
                        if(DBUG) printf("CRC err\n");
                        return TIMEOUT; /* ignore the packet if there's a CRC error */
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

/**
 * Validate command line args.
 * @param socketNum: the socket number to use
 * @param toFile: the name of the file to store on the server
 * @param windowSize: the window size to use
 * @param bufferSize: the buffer size to use
 * @param server: the server address
 * @return: the server port number given by user
*/
int checkArgs(int argc, char * argv[])
{

        int portNumber = 0;
	
        /* check command line arguments  */
	if (argc != 8)
	{
		fprintf(stderr,"usage: %s from-filename to-filename window-size buffer-size error-percent remote-machine port-port\n", argv[0]);
		exit(1);
	}
	
	portNumber = atoi(argv[7]);
		
	return portNumber;
}

/**
 * Communicate w/server via SREJ protocol.
 * @param fd: the file descriptor of the file to read from
 * @param socket: the socket number for the UDP connection
 * @param windowSize: the window size for SREJ protocol
 * @param bufferSize: the buffer size for SREJ protocol
 * @param server: the server address
*/
void usePhase(int fd, int socket, int windowSize,int bufferSize,struct sockaddr_in6 *server)
{
        uint8_t pduBuffer[MAXBUF] = {0};
        uint8_t serverPDU[MAXBUF] = {0};
        uint8_t payloadBuffer[MAXPAYLOAD] = {0};
        int status = -1;
        int bytesRead = -1;
        int reached_EOF = 0; /* < 0 means not reached EOF, > 0 = seq num of last packet + means reached EOF */
        Window *win = NULL;
        int counter = 0;
        int pduLength = 0;
        
        /* set up the window structure */
        if(!(win = initWindow(windowSize)))
        {
                fprintf(stderr,"%s\n","Init window");
                return;
        }

        WBuff *entry = NULL;
        int lowestSeq = 0;
        uint32_t maxRR = 0;
        int eof_counter = 0;

        /* engage in data communication w/server until all data has been received by server (or client times out */
        while(status != DONE)
        {
                /* send data while the window is open and we still have new data from the file */
                while(windowOpen(win) && !reached_EOF)
                {
                        if(DBUG) printf("FIRST LOOP!\n");
                        if(DBUG) printf("WINDOW OPEN, seq num = %d\n",getCurrent(win));
                        /* populare the buffer with the next chunk of file data */
                        if((bytesRead = populatePayload(fd,payloadBuffer,bufferSize)) < bufferSize)
                        {
                                if(DBUG) printf("HIT EOG!\n");
                                reached_EOF = 1;
                        }
                        if(bytesRead)
                        {
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
                        
                        /* after sending a packet, check and handle any server messages */
                        while(pollCall(0) != -1)
                        {
                                /* process server packet */
                                if(DBUG) printf("GOT A MSG!\n");
                                if(DBUG) printf("B4: Lower = %d, curr = %d, upper = %d, numItems = %d\n",getLower(win),getCurrent(win),getUpper(win),getNumItems(win));
                                processServerMsg(socket,win,server,serverPDU,&maxRR);
                                if(DBUG) printf("AFTER Lower = %d, curr = %d, upper = %d, numItems = %d\n",getLower(win),getCurrent(win),getUpper(win),getNumItems(win));
                        }
                }

                if(DBUG) printf("EXITED OPEN WINDOW LOOP!\n");
                
                counter = 0; /* used to indicate whether client has sent 10 unsuccessful packets to server, in which case client will quit */

                if(DBUG) printf("MAXRR = %d, seq_num-1 = %d\n",maxRR,getCurrent(win)-1);
                if(DBUG) printf("AFTER FIRST LOOP Lower = %d, curr = %d, upper = %d, numItems = %d\n",getLower(win),getCurrent(win),getUpper(win),getNumItems(win));


                /*if our window is closed or all data has been sent but not all data has been acknowledged by server, send lowest packet in the window until we receive a response (or quit after 10 tries) */
                while(!windowOpen(win) || ((maxRR < (getCurrent(win))) && reached_EOF))
                {
                        if(DBUG) printf("2nd LOOP! counter = %d\n",counter);
                        if(counter == 10) 
                        {
                                if(DBUG) printf("Counter timed out!\n");
                                return; /* failed to get a server response after 10 transmission attempts, quit*/
                        }
                        
                        /* received a message from server */
                        if(pollCall(1000) != -1)
                        {
                                if(DBUG) printf("POLL READY!\n");
                                processServerMsg(socket,win,server,serverPDU,&maxRR);
                                counter = 0;
                        }

                        /* else, resort to sending lowest packet */
                        else
                        {
                                if(DBUG) printf("POLL TIMED OUT!\n");
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
                        if(eof_counter == 10) 
                        {
                                if(DBUG) printf("EOF COUNTER TIMED OUT!\n");
                                return;
                        }
                        if(DBUG) printf("REACHED EOF\n");
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

        if(DBUG) printf("STATUS = %d, DONE = %d,maxRR = %d,bytesRead = %d\n",status,DONE,maxRR,bytesRead);
        
        freeWindow(win);
}

/**
 * Fill the given buffer with the next sequence of bytes from the file.
 * @param fd: the file descriptor of the file to read from
 * @param payload: the buffer to store the file data in
 * @param bufferSize: the size of the buffer
 * @return: the number of bytes read from the file
*/
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

/**
 * Wrapper function to create and send PDU to server.
 * @param socket: the socket number to use
 * @param pduBuffer: the buffer to store the pdu in
 * @param pduLength: the length of the pdu
 * @param payload: the payload to send
 * @param payloadLength: the length of the payload
 * @param server: the server address
 * @param seq_num: the sequence number of the pdu
 * @param flag: the flag of the pdu
 * @return: the length of the pdu
*/
int sendData(int socket, uint8_t *pduBuffer,int pduLength,uint8_t *payload,int payloadLength, struct sockaddr_in6 *server,uint32_t seq_num, uint8_t flag)
{
        
        if(pduLength == -1) pduLength = createPDU(pduBuffer,seq_num,flag,payload,payloadLength);

        if(DBUG) printf("PDU length = %d\n",pduLength);
        
        safeSendto(socket, pduBuffer, pduLength, 0, (struct sockaddr *) server, sizeof(struct sockaddr_in6));

        return pduLength;
}

/**
 * Handle RR and SREJ messages from server. This function essentially carries out client side implementation of SREJ protocol.
 * @param socket: the socket number to use
 * @param win: the window structure to use
 * @param server: the server address
 * @param serverPDU: the pdu received from the server
 * @param max_rr: the maximum RR number received from the server so far
*/
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
                if(DBUG) printf("RECEVED RR!\n");
                handle_rr(socket,win,serverPDU,max_rr);
        }
        /* received SREJ from server */
        else if(flag == F_SREJ)
        {
                if(DBUG) printf("RECEVED SREJ!\n");
                handle_srej(socket,win,serverPDU,server);
        }
        /* received EOF ACK from server */
        else if(flag == F_EOF_ACK)
        {
                handle_eof_ack(socket,server);
        }

        return flag;
}

/**
 * Handle RR messages from server.
 * @param socket: the socket number to use
 * @param win: the window structure to use
 * @param serverPDU: the pdu received from the server
 * @param max_rr: the maximum RR number received from the server so far
*/
void handle_rr(int socket,Window * win,uint8_t *serverPDU,uint32_t *maxRR)
{
        uint32_t net_rr_num;
        uint32_t host_rr_num;
        
        memcpy(&net_rr_num,serverPDU+PAYLOAD_OFF,4);
        host_rr_num = ntohl(net_rr_num);

        int offset = host_rr_num - getLower(win);

        if(DBUG) printf("PRE RECVED RR = %d, lower = %d, offset = %d\n",host_rr_num,getLower(win),offset);

        /* modify the max RR -- current message has the highest RR*/
        if(host_rr_num > *maxRR)
        {
                *maxRR = host_rr_num;
        }

        /* slide window if there's space */
        if(offset) slideWindow(win,offset);
        if(DBUG) printf("POST RECVED RR = %d, lower = %d, offset = %d\n",host_rr_num,getLower(win),offset);
}

/**
 * Appropriately respond to an SREJ from the server.
 * @param socket: the socket number to use
 * @param win: the window structure to use
 * @param serverPDU: the pdu received from the server
 * @param server: the server address
*/
void handle_srej(int socket,Window * win,uint8_t *serverPDU, struct sockaddr_in6 *server)
{
        uint32_t net_srej_num = 0;
        uint32_t host_srej_num = 0;
        memcpy(&net_srej_num,serverPDU + PAYLOAD_OFF,4);
        host_srej_num = ntohl(net_srej_num);
        
        if(DBUG) printf("SREJ = %d\n",host_srej_num);

        /* get the entry in the current window for the rejected packet */
        WBuff *entry = getEntry(win,host_srej_num);

        /* resend the rejected packet */
        sendData(socket,entry->savedPDU,entry->pduLength,NULL,0,server,host_srej_num,F_DATA);
        if(DBUG) printf("SREJ: SENT %d bytes!, seq num = %d vs actual = %d\n",entry->pduLength,entry->seq_num,host_srej_num);
}

/**
 * Appropriately respond to an EOF ACK from server.
 * @param socket: the socket number to use

 * @param server: the server address
*/
void handle_eof_ack(int socket,struct sockaddr_in6 *server)
{
        uint8_t pduBuffer[MAXBUF];
        uint8_t payloadBuffer[1];
        /* send a final ack for the received EOF ack */
        sendData(socket,pduBuffer,-1,payloadBuffer,0,server,0,F_EOF_ACK_2);
}
