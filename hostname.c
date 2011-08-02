/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include "config.h"

static const char ver_ctrl_id[] = "$Id: hostname.c 29 2004-04-24 23:02:38Z patrik $";

char *myname (void)
{
   int fd;
   char *host = NULL;
   char buf[256];
   int count;

   if ((fd = open(".me", O_RDONLY)) > -1)
   {
      if ((count = read(fd, buf, sizeof (buf) - 1)) > 0)
      {
         char *cp;

         buf[count] = '\0';
         if ((cp = strchr(buf, '\n')))
            *cp = '\0';
         if ((cp = strchr(buf, '\r')))
            *cp = '\0';
         host = strdup(buf);
      }
      close(fd);
   }

   if (!host)
   {
      struct hostent *hp;

      if (0 == gethostname(buf, sizeof (buf)))
      {
         hp = gethostbyname(buf);
         if (hp)
            host = (char *) hp->h_name; /* silence cc, sometimes */
      }
   }
   if (NULL == host)
      host = strdup("localhost");
   return host;
}
