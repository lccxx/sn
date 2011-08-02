/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Write/read the recieved time of an article.  This is an array
 * in file .times.
 * TODO: maybe map to page limit and check in remap(), so we needn't
 * always actually remap.  Probably not very consequential since
 * appending and finding mostly occur independently.
 */

#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "config.h"
#include "times.h"
#include "cache.h"
#include <out.h>
#include <openf.h>
#include <format.h>

static const char ver_ctrl_id[] = "$Id: times.c 29 2004-04-24 23:02:38Z patrik $";

extern int rename(const char *old, const char *new);

/*
 * The times field in struct timesobj may be NULL, to allow for the
 * case where the .times file has not yet been created, or is empty.
 * This is not an error.
 */

struct timesobj {
   char *group;
   struct times *times;
   int fd;
   int nr;
   int mapsize;
};

static int desc = -1;

static int times_compare (void *a, void *b)
{
   struct timesobj *x = (struct timesobj *) a;
   struct timesobj *y = (struct timesobj *) b;

   return strcmp(x->group, y->group);
}

static int remap (struct timesobj *tp)
{
   struct stat st;

   if (-1 == fstat(tp->fd, &st))
   {
      LOG("remap:fstat:%m");
      return -1;
   }
   if (!tp->mapsize || st.st_size > tp->mapsize)
      do
      {
         int size;

         if (tp->times)
            munmap((caddr_t) tp->times, tp->mapsize);
         if (0 == (st.st_size % sizeof (struct times)))
         {
            size = (st.st_size + 2048) - (st.st_size % 1024);
            tp->times = (struct times *) mmap(0, size,
                                              PROT_READ | PROT_WRITE, MAP_SHARED, tp->fd, 0);
            if (tp->times != MAP_FAILED)
            {
               tp->mapsize = size;
               break;
            }
            else
               LOG("remap:mmap(%s/.times):%m", tp->group);
         }
         else
            LOG("remap:%s/.times wrong size", tp->group);
         return -1;
      }
      while (0);
   tp->nr = st.st_size / sizeof (struct times);
   return 0;
}

static struct timesobj *times_open (char *group)
{
   struct timesobj *tp;
   int len;

   len = strlen(group);
   if (len >= GROUPNAMELEN)
      return 0;
   if ((tp = malloc(len + 1 + sizeof (struct timesobj))))
   {
      tp->fd = openf(0644, O_RDWR | O_APPEND | O_CREAT, "%s/.times", group);
      if (tp->fd > -1)
      {
         tp->group = (char *) tp + sizeof (struct timesobj);
         strcpy(tp->group, group);
         tp->mapsize = 0;
         if (remap(tp) > -1)
            return tp;
         close(tp->fd);
      }
      else
         LOG("times_open:open(%s):%m", group);
      free(tp);
   }
   else
      LOG("times_open:no memory");
   return 0;
}

static void times_close (void *p)
{
   struct timesobj *tp = (struct timesobj *) p;

   if (tp->times)
      munmap((caddr_t) tp->times, tp->mapsize);
   if (tp->fd > -1)
      close(tp->fd);
   free(tp);
}

static int times_initialized = 0;

int times_init (void)
{
   desc = cache_init(3, times_compare, times_close, NULL);
   if (desc > -1)
   {
      times_initialized = 1;
      return 0;
   }
   return -1;
}

void times_fin (void)
{
   if (debug >= 3)
   {
      int hit, miss;

      cache_stat(desc, &hit, &miss);
      LOG("times_close:cache requests:%dT=%dH+%dM", hit + miss, hit, miss);
   }
   cache_fin(desc);
   desc = -1;
}

static struct timesobj *gettimes (char *group)
{
   struct timesobj t;
   struct timesobj *tp;

   if (!times_initialized)
      if (-1 == times_init())
         return NULL;

   t.group = group;
   tp = cache_find(desc, &t);
   if (tp)
      return tp;

   tp = times_open(group);
   if (NULL == tp)
      return NULL;
   cache_insert(desc, tp);
   return tp;
}

/* returns the serial number of the first article stored after
   that date */

int times_since (char *group, time_t earliest)
{
   struct timesobj *tp;
   int hi;
   int lo;
   int mid;
   struct times *t;

   if (NULL == (tp = gettimes(group)))
      return -1;
   if (-1 == remap(tp))
      return -1;
   t = tp->times;

   if (!(hi = tp->nr))
      return 0; /* NaAN */
   lo = 0;
   mid = hi / 2;

   /* Earlier than anything we have */
   if (earliest < t[0].stored)
      return 1;
   /* Later than everything we have */
   if (tp->nr <= 1 || earliest > t[hi - 1].stored)
      return 0;

   while (lo < hi)
   {
      if (lo == mid)
         break;
      if (t[mid].stored > earliest)
         hi = mid;
      else if (t[mid].stored < earliest)
         lo = mid;
      else
         return t[mid].serial;
      mid = (hi + lo) / 2;
   }
   return t[lo].serial;
}

int times_append (char *group, int serial)
{
   struct timesobj *tp;
   struct times t = { 0, };

   if (NULL == (tp = gettimes(group)))
      return -1;
   t.serial = serial;
   t.stored = time(NULL);
   if (-1 == write(tp->fd, &t, sizeof (t)))
   {
      LOG("times_append:write:%m");
      cache_invalidate(desc, tp);
   }
   return 0;
}

int times_expire (char *group, int until)
{
   struct timesobj *tp;
   char buf[GROUPNAMELEN + sizeof ("/.time.tmp")];
   char buf2[GROUPNAMELEN + sizeof ("/.times")];
   int fd;
   int er;
   int i;

   if (NULL == (tp = gettimes(group)))
      return -1;
   if (NULL == tp->times)
      return -1;

   for (i = 0; i < tp->nr; i++)
      if (tp->times[i].serial >= until)
         break;
   if (0 == i)
      return 0;
   formats(buf, sizeof (buf) - 1, "%s/.time.tmp", group);
   fd = open(buf, O_WRONLY | O_TRUNC | O_CREAT, 0644);
   if (-1 == fd)
   {
      LOG("times_expire:open(%s):%m", buf);
      return -1;
   }
   er = write(fd, &tp->times[i], (tp->nr - i) * sizeof (struct times));
   close(fd);
   if (-1 == er)
   {
      LOG("times_expire:write(%s):%m", group);
      return -1;
   }
   formats(buf2, sizeof (buf2) - 1, "%s/.times", group);
   if (-1 == rename(buf, buf2))
   {
      LOG("times_expire:rename(%s):%m", buf);
      unlink(buf2);
      return -1;
   }
   cache_invalidate(desc, tp);
   return 0;
}

#ifdef TIMES_DEBUG
int times_show (char *group)
{
   struct timesobj *tp;
   int i;

   t.group = group;
   if (NULL == (tp = gettimes(group)))
      return -1;
   if (NULL == tp->times)
      return -1;

   for (i = 0; i < tp->nr; i++)
      writef(1, "%d:%u\n", tp->times[i].serial, tp->times[i].stored);
   return 0;
}
#endif
