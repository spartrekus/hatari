/*
  Hatari - stMemory.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  ST Memory access functions.
*/
char STMemory_rcsid[] = "Hatari $Id: stMemory.c,v 1.6 2004-04-23 15:33:59 thothy Exp $";

#include "stMemory.h"


Uint8 STRam[16*1024*1024];      /* This is our ST Ram, includes all TOS/hardware areas for ease */
Uint32 STRamEnd;                /* End of ST Ram, above this address is no-mans-land and hardware vectors */


/*-----------------------------------------------------------------------*/
/*
  Clear section of ST's memory space.
*/
void STMemory_Clear(unsigned long StartAddress, unsigned long EndAddress)
{
  memset((void *)((unsigned long)STRam+StartAddress), 0, EndAddress-StartAddress);
}

