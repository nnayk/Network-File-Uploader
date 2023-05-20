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

#define MAXBUF 1410
#define DBUG 1

void processClient(int socketNum);
int checkArgs(int argc, char *argv[]);
int open_output_file(Endpoint,uint8_t *, int);
void send_file_response(Endpoint,uint8_t);

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

		if((pduLength = safeRecvfrom(socketNum, fileReqBuffer, MAXBUF, 0, (struct sockaddr *) &client, &clientAddrLen)) == CRC_ERR)
                {
                        continue; /* packet is corrupted, ignore it */
                }

                
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
                if((fd = open_output_file(childConn,fileReqBuffer,pduLength))==-1)
                {
                        send_file_response(childConn,FILE_BAD);
                        return 0;
                }
                else
                {
                        send_file_response(childConn,FILE_OK);
                }
        }

        else close(socketNum);
	
	return 0;
}

/* returns 1 if file OK, -1 if not able to open file */
int open_output_file(Endpoint childConn,uint8_t *buffer, int pduLength)
{
        char filename[MAX_FILE_SIZE] = {0};
        uint8_t filenameLen = buffer[PAYLOAD_OFF + OUT_FILE_LEN_OFF_7];
        int fd = 0;

        memcpy(filename,buffer+PAYLOAD_OFF+OUT_FILE_OFF_7,filenameLen);

        printf("SERVER GOT FILENAME OUTPUT %s, len = %ld\n",filename,strlen(filename));

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
        int pduLength = createPDU(pduBuffer,SEQ_ANY,F_FILEREQ_RESPONSE,&status,1);

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


