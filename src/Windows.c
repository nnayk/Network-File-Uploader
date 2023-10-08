/* 
 * Nakul Nayak
 * CPE 464
 * Description:
 * This file contains functions relevant to the window data structure.
 */

/* header files */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Windows.h"
#include "safeUtil.h"
#include "cpe464.h"

/* macros, if any */
#define DBUG 0

/* function prototypes */
Window *argCheck(Window *);

/**
 * Initialize the window structure.
 * @param capacity: the capacity of the window
 * @return: a pointer to the window structure
*/
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
        
        /* create an empty entry for each window slot */
        for(i=0;i<capacity;i++)
        {
                win->pduBuff[i] = (WBuff *)sCalloc(1,sizeof(WBuff));
        }
        return win;
}

/**
 * Retrieve number of occupied window slots.
 * @param win: a pointer to the window structure
 * @return: the number of occupied window slots
*/
int getNumItems(Window *win)
{
        return win->numItems;
}

/**
 * Retrieve lower bound sequence number of window.
 * @param win: a pointer to the window structure
 * @return: the lower bound sequence number of the window
*/
int getLower(Window *win)
{
        return win->lower;
}

/**
 * Retrieve current index of the window.
 * @param win: a pointer to the window structure
 * @return: the current index of the window
*/
int getCurrent(Window *win)
{
        return win->current;
}

/**
 * Retrieve upper bound sequence number of the window.
 * @param win: a pointer to the window structure
 * @return: the upper bound sequence number of the window
*/
int getUpper(Window *win)
{
        return win->upper;
}

/**
 * Slide the window the given number of increments.
 * @param win: a pointer to the window structure
 * @param offset: the number of increments to slide the window
 * @return: 1 on success
*/
int slideWindow(Window *win, int offset)
{
        int i;
        int newLower = win->lower + offset;

        for(i=win->lower;i<newLower;i++)
        {
                delEntry(win,i);
        }
        win->lower += offset;
        win->upper += offset;
        /* completely new window, reset current */
        if(win->lower > win->current)
                win->current = win->lower;
        return 1;
}

/**
 * Shift current index of the window.
 * @param win: a pointer to the window structure
 * @param offset: the number of increments to shift the current index
 * @return: the upper bound sequence number of the window
*/
int shiftCurrent(Window *win, int offset)
{
        if(!argCheck(win)) return 0;
        win->current += offset;
        return 1;
}

/**
 * Add a new entry to the window.
 * @param win: a pointer to the window structure
 * @param pduBuffer: the pdu buffer to add to the window
 * @param pduLength: the length of the pdu buffer
 * @param seq_num: the sequence number of the pdu buffer
 * @return: 1 on success
*/
int addEntry(Window *win, uint8_t *pduBuffer, int pduLength, int seq_num)
{
        if(!argCheck(win) || !pduBuffer) return 0;
        int index = getIndex(win,seq_num);
        if(index >= win->capacity)
        {
                if(DBUG) fprintf(stderr,"%s\n","Index out of bounds!");
                return -1;
        }
        /* simple error check */
        if(win->pduBuff[index] && win->pduBuff[index]->filled)
        {
                if(win->pduBuff[index]->seq_num != seq_num)
                {
                        fprintf(stderr,"Bug with window! Trying to add seq num = %d even though %d is at that spot!\n",seq_num,win->pduBuff[index]->seq_num);
                        exit(EXIT_FAILURE);
                }
        }
        memcpy(win->pduBuff[index]->savedPDU, pduBuffer, pduLength);
        /* populate the entry */
        win->pduBuff[index]->seq_num = seq_num;
        win->pduBuff[index]->pduLength = pduLength;
        win->pduBuff[index]->filled = 1;
        if(in_cksum((unsigned short *)pduBuffer,pduLength))
                fprintf(stderr,"%s","addEntry: CHECKSUM MISMATCH IN WINDOW!!!\n");
        win->numItems++;
        return 1;
}

/**
 * Delete a specifc entry in the window.
 * @param win: a pointer to the window structure
 * @param seq_num: the sequence number of the pdu buffer to delete
 * @return: 1 on success, 0 on failure
*/
int delEntry(Window *win, int seq_num)
{
        if(!argCheck(win)) return 0;   
        int index = getIndex(win,seq_num);
        if(index >= win->capacity)
        {
                if(DBUG) fprintf(stderr,"%s\n","Index out of bounds!");
                return 0;
        }
        if(DBUG) printf("seq_num = %d\n",seq_num);
        if((win->pduBuff[index]->seq_num) != seq_num) 
        {
                if(DBUG) printf("Bad delete!\n");
                return 0;
        }
        if(DBUG) printf("ccc\n");
        win->pduBuff[index]->filled = 0;
        win->pduBuff[index]->pduLength = -1;
        win->pduBuff[index]->seq_num = 0;
        win->numItems--;
        return 1;
}

/**
 * Get a specifc entry in the window.
 * @param win: a pointer to the window structure
 * @param seq_num: the sequence number of the window buffer entry to get
 * @return: a pointer to the window buffer entry
*/
WBuff *getEntry(Window *win, int seq_num)
{
        if(!argCheck(win)) return NULL;
        int index = getIndex(win,seq_num);
        return win->pduBuff[index];
}

/**
 * Determine whether an entry exists in the window.
 * @param win: a pointer to the window structure
 * @param seq_num: the sequence number of the window buffer entry to check
 * @return: True if the entry exists, False otherwise
*/
int existsEntry(Window *win,int seq_num)
{
        WBuff *entry = getEntry(win,seq_num);
        fflush(stdout);
        if((!entry) || (!entry->filled) || (entry->seq_num != seq_num)) 
        {
                return 0;
        }
        return 1;
}

/**
 * Get a specifc PDU for a window buffer entry.
 * @param win: a pointer to the window structure
 * @param index: the index of the window buffer entry to get
 * @return: a pointer to the PDU in the window buffer entry
*/
WBuff *getSavedPDU(Window *win, int index)
{
        if(!win->pduBuff[index])
        {
                fprintf(stderr,"index %d out of bounds\n", index);
                return NULL;
        }
        return win->pduBuff[index];
}

/**
 * Get the window buffer entry index for a given sequence number
 * @param win: a pointer to the window structure
 * @param seq_num: the sequence number of the window buffer entry to get
 * @return: the index of the window buffer entry
*/
int getIndex(Window *win, int seq_num)
{
        if(!argCheck(win)) return 0;
        return seq_num % win->capacity;
}

/**
 * Determine whether the window is open.
 * @param win: a pointer to the window structure
 * @return: True if the window is open, False otherwise
*/
int windowOpen(Window *win)
{
        if(!argCheck(win)) return 0;
        if(win->current < win->upper) return 1;
        return 0;
}

/**
 * Determine whether the given window pointer is valid.
 * @param win: a pointer to the window structure
 * @return: the window pointer if valid, NULL otherwise
*/
Window *argCheck(Window *win)
{
        return win;
}

/**
 * Free the given window and its associated slots.
 * @param win: a pointer to the window structure
*/
void freeWindow(Window *win)
{
        int i;
        for(i=0;i<win->capacity;i++)
        {
                if(win->pduBuff[i]) free(win->pduBuff[i]);
        }
        free(win);
}
