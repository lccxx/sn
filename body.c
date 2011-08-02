/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * How to deal with article bodies, which may be compressed.
 * On input, compression takes place in store.c without our help.
 * Caller must not free the article body on return.
 */

#ifdef USE_ZLIB

#include <stdlib.h>
#include <string.h>
#include <zconf.h>
#include <zlib.h>
#include "art.h"

static const char ver_ctrl_id[] = "$Id: body.c 29 2004-04-24 23:02:38Z patrik $";

int body (char **artbod, int *len)
{
   static char *bodbuf = NULL;
   z_stream zs;
   int e, msize, inc;

   if (bodbuf)
   {
      free(bodbuf);
      bodbuf = NULL;
   }

   if (*len < 0)
      return 1;
   if (0 == *len || *len < COMPRESS_MAGIC_LEN)
      return 0;
   for (e = 0; e < COMPRESS_MAGIC_LEN; e++)
      if ((*artbod)[e] != COMPRESS_MAGIC[e])
         return 0;

   inc = (*len / 2) + 1;
   zs.next_out = malloc(msize = zs.avail_out = 5 * inc);
   if (!(bodbuf = (char *) zs.next_out))
      return -1;
   zs.next_in = (unsigned char *) *artbod + COMPRESS_MAGIC_LEN;
   zs.avail_in = (unsigned) (*len - COMPRESS_MAGIC_LEN);
   zs.zalloc = 0;
   zs.zfree = 0;
   if ((e = inflateInit(&zs)))
      return 0;

   while (0 == (e = inflate(&zs, Z_NO_FLUSH)))
      if (zs.avail_out < 10)
      {
         char *tmp;
         int used;

         if (!(tmp = malloc(msize + inc)))
            goto bomb;
         used = msize - zs.avail_out;
         memcpy(tmp, bodbuf, used);
         zs.next_out = (unsigned char *) tmp + used;
         msize += inc;
         zs.avail_out += inc;
         free(bodbuf);
         bodbuf = tmp;
      }

   inflateEnd(&zs);
   if (Z_STREAM_END == e
       #if 0
       /* I have this problem; apparently I'm the only one who does */
       || Z_DATA_ERROR == e
       #endif
      )
   {
      *len = zs.next_out - (unsigned char *) bodbuf;
      *artbod = bodbuf;
      bodbuf[*len] = '\0';
      return 0;
   }

bomb:
   free(bodbuf);
   bodbuf = NULL;
   return -1;
}

#endif /* USE_ZLIB */
