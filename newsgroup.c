/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Rewritten using 2 hash tables (old one had race problems).
 * Caller must ensure newsgroup names passed are in all lower case.
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include <format.h>
#include <out.h>

static const char ver_ctrl_id[] = "$Id: newsgroup.c 49 2004-07-10 22:54:35Z patrik $";

struct ng {
   struct ng *gnext;
   struct ng *inext;
   int ident;
   char group[1];
};

#define HSIZE 128

static struct ng *bygroup[HSIZE];
static struct ng *byident[HSIZE];
static int highest = 0;

static unsigned int hash (char *buf, int len)
{
   unsigned int h;

   h = 5381;
   while (len)
   {
      --len;
      h += (h << 5);
      h ^= (unsigned int) *buf++;
   }
   return h;
}

static struct chunk {
   struct chunk *next;
   char *buf;
} *chunks;

static int avail = 0;
static int nr = 0;

#undef ALIGN   /* FreeBSD apparently defines this */
#define ALIGN sizeof(char *)

static int add (int ident, char *group, int len)
{
   struct ng *np;
   struct ng *p;
   int want;
   unsigned h;

   len++;
   want = len + sizeof (struct ng) + ALIGN;
   want -= (len % ALIGN);
   len--;

   if (avail < want)
   {
      struct chunk *tmp;
      int size;

      size = (want > 240 ? want + 240 : 240);
      if (!(tmp = malloc(size)))
         return -1;
      tmp->next = chunks;
      chunks = tmp;
      chunks->buf = (char *) chunks + sizeof (struct chunk);
      avail = size - sizeof (struct chunk);
   }
   np = (struct ng *) chunks->buf;
   chunks->buf += want;
   avail -= want;
   strncpy(np->group, group, len);
   np->group[len] = '\0';
   np->ident = ident;

   h = hash(group, len) % HSIZE;
   np->gnext = bygroup[h];
   bygroup[h] = np;
   for (p = np->gnext; p; p = p->gnext)
      if (0 == strcmp(p->group, group))
         return 1;
   h = ident % HSIZE;
   np->inext = byident[h];
   byident[h] = np;
   for (p = np->inext; p; p = p->inext)
      if (p->ident == ident)
         return 1;

   if (ident > highest)
      highest = ident;
   nr++;
   return 0;
}

extern char dh_groupfile[];
static int groupfd = -1;
static int oldsize = 0;
static int mapsize = 0;
static char *mapbuf = NULL;

void ng_fin (void)
{
   struct chunk *tmp;

   if (groupfd > -1)
   {
      close(groupfd);
      groupfd = -1;
   }
   if (chunks)
      do
      {
         tmp = chunks->next;
         free(chunks);
      }
      while ((chunks = tmp));
   if (mapbuf)
   {
      munmap(mapbuf, mapsize);
      mapbuf = NULL;
   }
   highest = oldsize = mapsize = 0;
}

static int reload (void)
{
   struct stat st;
   static int pagesize = 0;
   int newsize;

   /*
    * 2 things: if file has grown beyond our map, remap the map.
    * if the file has grown at all, reread the extra.
    */

   if (-1 == fstat(groupfd, &st))
   {
      LOG("reload:fstat:%m");
      return -1;
   }
   newsize = st.st_size;
   if (!pagesize)
      pagesize = getpagesize();
   if (newsize <= oldsize)
      return 0;
   if (newsize > mapsize || !mapsize)
   {
      if (mapbuf)
         munmap((caddr_t) mapbuf, mapsize);
      mapsize = st.st_size + pagesize - (st.st_size % pagesize);
      mapbuf = mmap(0, mapsize, PROT_READ, MAP_SHARED, groupfd, 0);
      if (!mapbuf || mapbuf == MAP_FAILED)
      {
         LOG("reload:mmap:%m");
         return -1;
      }
   }
   
   /* XXX Could stand some error checking, but then how to handle? */
   
   {
      char *p;
      char *lim;
      char *ip;
      char *gr;
      int ident, state;

      lim = mapbuf + newsize;
      ident = state = 0;
      ip = gr = 0; /* dumb compiler */
      for (p = mapbuf + oldsize; p < lim; p++)
         switch (state)
         {
            case 0: if (*p >= '0' && *p <= '9') { ip = p; state++; } break;
            case 1: if (' ' == *p) { ident = atoi(ip); state++; } break;
            case 2: if (' ' != *p) { gr = p; state++; } break;
            case 3:
               if ('\n' != *p)
                  break;
               if (-1 == add(ident, gr, p - gr))
               {
                  LOG("reload:no memory");
                  return -1;
               }
               state = 0;
         }
   }
   oldsize = newsize;
   return 0;
}

int ng_init (void)
{
   if (-1 == (groupfd = open(dh_groupfile, O_RDWR | O_CREAT, 0644)))
      if (-1 == (groupfd = open(dh_groupfile, O_RDONLY)))
      {
         LOG("ng_init:open(%s):%m", dh_groupfile);
         return -1;
      }
   if (-1 == reload())
   {
      close(groupfd);
      return (groupfd = -1);
   }
   return nr;
}

int ng_ident (char *group)
{
   struct ng *np;

   for (np = bygroup[hash(group, strlen(group)) % HSIZE]; np; np = np->gnext)
      if (0 == strcmp(group, np->group))
         return np->ident;
   return -1;
}

char *ng_newsgroup (int ident)
{
   struct ng *np;

   for (np = byident[ident % HSIZE]; np; np = np->inext)
      if (ident == np->ident)
         return np->group;
   return NULL;
}

/*
 * Caller must ensure group is all lower case, and not too long.
 */

int ng_addgroup (char *group)
{
   int ident;
   char *buf;

   if (-1 == lockf(groupfd, F_LOCK, 0))
   {
      LOG("ng_addgroup:lockf(%s):%m", dh_groupfile);
      return -1;
   }
   ident = -1;
   if (0 == reload())
      if (-1 == (ident = ng_ident(group)))
      {
         int len;

         len = strlen(group);
         if ((buf = malloc(len + 15)))
         {
            int c, seek;

            c = formats(buf, len + 14, "%u %s\n", highest + 1, group);
            if ((seek = lseek(groupfd, 0, SEEK_END)) > -1)
               if (c == write(groupfd, buf, c))
               {
                  if (0 == add(highest + 1, group, len))
                     ident = highest; /* add() incs */
                  else if (-1 == ftruncate(groupfd, seek))
                     LOG("ng_addgroup:ftruncate:%m");
                  else
                     LOG("ng_addgroup:write fail for %d %s:%m?", highest + 1, group);
               }
            free(buf);
         }
         else
            LOG("ng_addgroup:malloc for %s:%m", group);
      }
   lseek(groupfd, 0, SEEK_SET);
   lockf(groupfd, F_ULOCK, 0);
   return ident;
}
