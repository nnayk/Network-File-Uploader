/* 
 * Nakul Nayak
 * CPE 453
 * Description: 
 */

/* header files */
#include <stdio.h>
#include <stdlib.h>
#include "Windows.h"
#include "safeUtil.h"

/* macros, if any */

/* function prototypes */

/* global vars, if any */

Window *initWindow(int capacity)
{
        int i;
        Window *win = sCalloc(1,sizeof(Window)); /* switch to malloc */
        win->capacity = capacity;
        win->numItems = 0;
        win->lower = 0;
        win->current = 0;
        win->upper = 0;
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

WBuff *getSavedPDU(Window *win, int index)
{
        if(!win->pduBuff[index])
        {
                fprintf(stderr,"index %d out of bounds\n", index);
                return NULL;
        }

        return win->pduBuff[index];
}
