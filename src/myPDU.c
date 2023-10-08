/* 
 * Nakul Nayak
 * CPE 453
 * Description: 
 */

/* header files */
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>

#include "myPDU.h"
#include "cpe464.h"

/* macros, if any */

#define DBUG 0

/* function prototypes */

/* global vars, if any */

int createPDU(uint8_t * pduBuffer, uint32_t sequenceNumber, uint8_t flag, uint8_t * payload, int payloadLen)
{
        int pduLength = payloadLen + HDR_LEN;
        uint32_t network_seq_num = htonl(sequenceNumber);
        uint16_t checksum = 0;

        memcpy(pduBuffer,&network_seq_num,4);
        memcpy(pduBuffer + FLAG_OFF, &flag, 1);
        memcpy(pduBuffer + PAYLOAD_OFF, payload, payloadLen);
        memset(pduBuffer+CRC_OFF,0,CRC_SIZE);

        if(DBUG) printf("pduLength = %d\n",pduLength);
        checksum = in_cksum((unsigned short *)pduBuffer,pduLength);
        memcpy(pduBuffer + CRC_OFF,&checksum,2);
        
        return pduLength;
}

void printPDU(uint8_t *aPDU, int pduLength)
{
        if(DBUG) printf("pduLength = %d\n",pduLength);
        if(in_cksum((unsigned short *)aPDU,pduLength))
        {
                fprintf(stderr,"%s\n","CHECKSUM MISMATCH");
                return; /* bail if the checksum doesn't match */
        }
        

        uint32_t raw_seq_num, host_seq_num;
        uint8_t flag;
        uint8_t *payload;
        int payloadLength;
        
        memcpy(&raw_seq_num,aPDU,SEQ_NUM_SIZE);
        host_seq_num = ntohl(raw_seq_num);

        memcpy(&flag,aPDU+FLAG_OFF,FLAG_SIZE);

        payload = aPDU + PAYLOAD_OFF;

        /* will need to modify this for prog3, here the payload is guaranteed to be text so it's fine */
        payloadLength = (int)strlen((const char *)payload) + 1; /* add +1 for the null byte since it's sent */ 

        printf("Sequence number = %d\n",host_seq_num);
        printf("Flag = %d\n",flag);
        printf("Payload: %s\n",payload);
        printf("Payload length: %d\n",payloadLength);
}
