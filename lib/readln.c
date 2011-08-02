/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Get a single line from an fd.
 */

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include "readln.h"

static const char ver_ctrl_id[] = "$Id: readln.c 29 2004-04-24 23:02:38Z patrik $";

int readln_ready (int fd, int tmo, struct readln *rp)
{
   rp->buf = rp->bf;
   rp->size = sizeof (rp->bf);
   rp->fd = fd;
   rp->eaten = rp->used = 0;
   rp->tmo = tmo;
   return 0;
}

void readln_done (struct readln *rp)
{
   if (rp && rp->buf != rp->bf)
      free(rp->buf);
}

int readln (register struct readln *rp, char **line, int ch)
{
   char *endp;
   int len;

   if (rp->eaten == rp->used)
      rp->eaten = rp->used = 0;
   else if (rp->eaten && rp->eaten + 64 >= rp->used)
   {
      register char *from;
      register char *to;
      int n;

      from = rp->buf + rp->eaten;
      to = rp->buf;
      for (n = rp->used - rp->eaten; n; n--)
         *to++ = *from++;
      rp->used -= rp->eaten;
      rp->eaten = 0;
   }

   while (1)
   {
      char *lim;
      int count;

      lim = rp->buf + rp->used;
      for (endp = rp->buf + rp->eaten; endp < lim; endp++)
         if (ch == *endp)
            goto done;
      if (rp->size - rp->used < 16)
      {
         char *tmp;

         if (rp->buf == rp->bf)
            tmp = malloc(rp->size = 504);
         else
            tmp = malloc(rp->size *= 2);
         if (!tmp)
            return -1;
         memcpy(tmp, rp->buf + rp->eaten, rp->used - rp->eaten);
         rp->used -= rp->eaten;
         rp->eaten = 0;
         if (rp->buf != rp->bf)
            free(rp->buf);
         rp->buf = tmp;
      }
      if (rp->tmo > 0)
      {
         struct timeval tv;
         fd_set set;

         FD_ZERO(&set);
         for (;;)
         {
            FD_SET(rp->fd, &set);
            tv.tv_usec = 0;
            tv.tv_sec = rp->tmo;
            switch (select(rp->fd + 1, &set, 0, 0, &tv))
            {
               case 0:
                  return -1;
               case -1:
                  if (EINTR == errno)
                     continue;
                  return -1;
            }
            break;
         }
      }
      count = read(rp->fd, rp->buf + rp->used, rp->size - rp->used);
      if (count <= 0)
         return count;
      rp->used += count;
   }

done:
   *line = rp->buf + rp->eaten;
   len = 1 + endp - (rp->buf + rp->eaten);
   rp->eaten = 1 + endp - rp->buf;
   return len;
}
