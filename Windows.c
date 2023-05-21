/* 
 * Nakul Nayak
 * CPE 453
 * Description: 
 */

/* header files */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Windows.h"
#include "safeUtil.h"

/* macros, if any */
#define DBUG 1

/* function prototypes */
int argCheck(Window *);

/* global vars, if any */

Window *initWindow(int capacity)
{
        int i;
        Window *win = sCalloc(1,sizeof(Window)); /* switch to malloc */
        win->capacity = capacity;
        win->numItems = 0;
        win->lower = 0;
        win->current = 0;
        win->upper = capacity;
        win->pduBuff = (WBuff **)sCalloc(capacity,sizeof(WBuff *));


        return win;
}

int getNumItems(Window *win)
{
        return win->numItems;
}


int getLower(Window *win)
{
        return win->lower;
}

int getCurrent(Window *win)
{
        return win->current;
}

int getUpper(Window *win)
{
        return win->upper;
}

int slideWindow(Window *win, int offset)
{
        int i;
        int newLower = win->lower + offset;

        win->lower += offset;
        win->upper += offset;

        for(i=win->lower;i<newLower;i++)
        {
                delEntry(win,i);
                win->numItems--;
        }

        win->lower += offset;
        win->upper += offset;

        /* completely new window, reset current */
        if(win->lower > win->current)
                win->current = win->lower;

        return 1;
}

int shiftCurrent(Window *win, int offset)
{
        if(!argCheck(win)) return 0;

        win->current += offset;

        return 1;
}

int addEntry(Window *win, uint8_t *pduBuffer, int pduLength, int seq_num)
{
        if(!argCheck(win) || !pduBuffer) return 0;
       
        int index = getIndex(win,seq_num);

        if(DBUG) printf("Index to add in window = %dn",index);

        if(index >= win->capacity)
        {
                if(DBUG) fprintf(stderr,"%s\n","Index out of bounds!");
                return -1;
                
        }

        if(win->pduBuff[index])
        {
                if(win->pduBuff[index]->seq_num != seq_num)
                {
                        fprintf(stderr,"Bug with window! Trying to add seq num = %d even though %d is at that spot!\n",seq_num,win->pduBuff[index]->seq_num);
                        return -1;
                }
        }
        
        win->pduBuff[index] = (WBuff *)sCalloc(1,sizeof(WBuff));

        memcpy(win->pduBuff[index]->savedPDU, pduBuffer, pduLength);
        win->pduBuff[index]->seq_num = seq_num;
        win->pduBuff[index]->pduLength = pduLength;

        win->numItems++;

        return 1;
}

int delEntry(Window *win, int seq_num)
{
        if(!argCheck(win)) return 0;
       
        int index = getIndex(win,seq_num);

        if(index >= win->capacity)
        {
                if(DBUG) fprintf(stderr,"%s\n","Index out of bounds!");
                return 0;
        }

        memset(win->pduBuff[index]->savedPDU, 0, MAX_PDU);
        
        win->numItems--;
        
        return 1;
}

WBuff *getEntry(Window *win, int seq_num)
{
        if(!argCheck(win)) return NULL;
        
        int index = getIndex(win,seq_num);
        
        if(index >= win->capacity)
        {
                if(DBUG) fprintf(stderr,"%s\n","Index out of bounds!");
                return NULL;
        }

        if(DBUG) printf("index = %d,pduLength = %d\n",index,win->pduBuff[index]->pduLength);


        return win->pduBuff[index];
}

WBuff *getSavedPDU(Window *win, int index)
{
        if(!win->pduBuff[index])
        {
                fprintf(stderr,"index %d out of bounds\n", index);
                return NULL;
        }

        return win->pduBuff[index];
}

int getIndex(Window *win, int seq_num)
{
        if(!argCheck(win)) return 0;

        return seq_num % win->capacity;
}

int windowOpen(Window *win)
{
        if(!argCheck(win)) return 0;
        
        if(win->current < win->upper) return 1;

        return 0;
}

int argCheck(Window *win)
{
        return win;
}
