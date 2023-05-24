
// 
// Writen by Hugh Smith, April 2020, Feb. 2021
//
// Put in system calls with error checking
// keep the function paramaters same as system call

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "safeUtil.h"
#include "Payload.h"

#ifdef __LIBCPE464_
#include "cpe464.h"
#endif

#if 1
#include <stdint.h>
#define DROP_MSG_LEN 0
uint32_t drop_dbug2[] = {20,21,22,23,24,25,26,27,28,29,30};
//uint32_t drop_dbug2[] = {5,18,30,31, 35,37};



int in_drop2(uint32_t seq)
{
        int i;
        for(i=0;i<DROP_MSG_LEN;i++)
        {
                if(drop_dbug2[i] == seq) return 1;
        }

        return 0;
}

int rm_drop2(uint32_t seq)
{
        int i;
        for(i=0;i<DROP_MSG_LEN;i++)
        {
                if(drop_dbug2[i] == seq) 
                {
                        drop_dbug2[i] = -1;
                        return 1;
                }
        }

        return 0;
}
#endif 

int safeRecvfrom(int socketNum, void * buf, int len, int flags, struct sockaddr *srcAddr, int * addrLen)
{
	int returnValue = 0;
	if ((returnValue = recvfrom(socketNum, buf, (size_t) len, flags, srcAddr, (socklen_t *) addrLen)) < 0)
	{
		perror("recvfrom: ");
		exit(-1);
	}

        printf("READ %d bytes\n",returnValue);

        if((in_cksum((unsigned short *)buf,returnValue)))
        {
                return CRC_ERR;
        }
	
	return returnValue;
}

int safeSendto(int socketNum, void * buf, int len, int flags, struct sockaddr *srcAddr, int addrLen)
{
#if 1
        static int msg = 0;
        if(in_drop2(msg) && len < 20)
        {
                fprintf(stderr,"%s %d\n","SAFE SEND DROPPED PACKET ",msg); 
                rm_drop2(msg);
                msg++;
                return 0;
        }
        msg++;
#endif
#if 0
        #include <unistd.h>
        #include <string.h>
        char response[2] = {0};
        printf("DROP packet %d (Y/N)? ",((char *)buf)[0]);
        fflush(stdout);
        read(STDIN_FILENO,response,1);
        if(!strcmp(response,"y")) 
        {
                printf("CUSTOM DROPPED PACKET %d\n",((char *)buf)[0]);
                return 1;
        }
        else
        {
                int returnValue;
                if((returnValue = sendto(socketNum,buf,(size_t)len,flags,srcAddr,(socklen_t)addrLen)) < 0)
                {
                        perror("sendto");
                        exit(-1);
                }

                return returnValue;
        }
#endif
        int returnValue = 0;
	if ((returnValue = sendtoErr(socketNum, buf, (size_t) len, flags, srcAddr, (socklen_t) addrLen)) < 0)
	{
		perror("sendtoErr: ");
		exit(-1);
	}
	
	return returnValue;
}

int safeRecv(int socketNum, void * buf, int len, int flags)
{
	int returnValue = 0;
	if ((returnValue = recv(socketNum, buf, (size_t) len, flags)) < 0)
	{
		perror("recv: ");
		exit(-1);
	}
	
	return returnValue;
}

int safeSend(int socketNum, void * buf, int len, int flags)
{
	int returnValue = 0;
	if ((returnValue = send(socketNum, buf, (size_t) len, flags)) < 0)
	{
		perror("send: ");
		exit(-1);
	}
	
	return returnValue;
}

void * srealloc(void *ptr, size_t size)
{
	void * returnValue = NULL;
	
	if ((returnValue = realloc(ptr, size)) == NULL)
	{
		printf("Error on realloc (tried for size: %d\n", (int) size);
		exit(-1);
	}
	
	return returnValue;
} 

void * sCalloc(size_t nmemb, size_t size)
{
	void * returnValue = NULL;
	if ((returnValue = calloc(nmemb, size)) == NULL)
	{
		perror("calloc");
		exit(-1);
	}
	return returnValue;
}

