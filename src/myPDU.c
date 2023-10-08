/* 
 * Nakul Nayak
 * CPE 464
 * Description:
 This file contains the functions for working with the custom PDUs
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

/**
 * Populate the given pduBuffer with the given data
 * @param pduBuffer: the buffer to store the pdu in
 * @param sequenceNumber: the pdu sequence number
 * @param flag: the pdu flag
 * @param payload: the message payload
 * @param payloadLen: the length of the payload
 */
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