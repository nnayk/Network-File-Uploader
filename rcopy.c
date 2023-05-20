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


void talkToServer(int socketNum, struct sockaddr_in6 * server);
int readFromStdin(char * buffer);
int checkArgs(int argc, char * argv[]);
int setupConnection(int, char *,uint32_t,uint16_t,struct sockaddr_in6 *);


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

        if(status == TIMEOUT)
        {
                fprintf(stderr,"%s\n","Server cannot write to given output file.");
                exit(EXIT_FAILURE);
        }

        while(1);

	
        /*talkToServer(socketNum, &server);*/
	
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
        uint32_t netWindowSize = htons(windowSize);

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
                                return 1;
                        }
                        else
                        {
                                if(DBUG) printf("File is bad\n");
                                return -1;
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





