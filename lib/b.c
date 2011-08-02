/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Dynamic buffer routines.
 */

#include <stdlib.h>
#include <string.h>
#include "b.h"

static const char ver_ctrl_id[] = "$Id: b.c 29 2004-04-24 23:02:38Z patrik $";

int b_appendl (register struct b *bp, char *str, int len)
{
   if (NULL == bp->buf)
      if (NULL == (bp->buf = malloc(bp->size = 248)))
         return -1;
      else
         bp->used = 0;

   if (len + bp->used >= bp->size)
   {
      int newsize;
      char *tmp;

      if (len > bp->size)
         newsize = bp->size + len;
      else
         newsize = bp->size * 2;
      newsize += 16;
      tmp = malloc(newsize);
      if (NULL == tmp)
         return -1;
      memcpy(tmp, bp->buf, bp->used + 1);
      bp->buf = tmp;
      bp->size = newsize;
   }
   memcpy(bp->buf + bp->used, str, len);
   bp->used += len;
   bp->buf[bp->used] = '\0';
   return 0;
}
