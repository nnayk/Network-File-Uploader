#include <stdint.h>

#define MAX_PDU 1400 

typedef struct WBuff
{
        uint8_t savedPDU[MAX_PDU];
}WBuff;

typedef struct Window
{
        int numItems;
        int capacity;
        int lower;
        int current;
        int upper;
        WBuff **pduBuff;
}Window;
