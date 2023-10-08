#include <stdint.h>


#define HDR_LEN 7
#define SEQ_OFF 0
#define CRC_OFF 4
#define FLAG_OFF 6
#define PAYLOAD_OFF 7

#define SEQ_NUM_SIZE 4
#define CRC_SIZE 2
#define FLAG_SIZE 1

int createPDU(uint8_t *, uint32_t, uint8_t, uint8_t *, int);