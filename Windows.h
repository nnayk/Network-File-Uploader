#include <stdint.h>

#define MAX_PDU 1410 

typedef struct WBuff
{
        uint8_t savedPDU[MAX_PDU];
        int pduLength;
        uint32_t seq_num;
        uint8_t filled;
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

Window *initWindow(int);
void freeWindow(Window *);

int getNumItems(Window *);
int getIndex(Window *,int);

int windowOpen(Window *);

int getLower(Window *);
int getCurrent(Window *);
int getUpper(Window *);

int slideWindow(Window *,int);
int shiftCurrent(Window *,int);

int addEntry(Window *,uint8_t *,int,int);
int delEntry(Window *,int);
WBuff *getEntry(Window *, int);
