/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Information on how many articles are in a group.
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <sys/mman.h>
#include "config.h"
#include "cache.h"
#include "group.h"
#undef VOLATILE
#include "artfile.h"
#include <out.h>
#include <openf.h>

static const char ver_ctrl_id[] = "$Id: group.c 29 2004-04-24 23:02:38Z patrik $";

#ifdef DONT_HAVE_DIRFD
#define dirfd(dirp) ((dirp)->dd_fd)
#endif

static int desc = -1;

struct dir {
   DIR *dir;
   int first;
   int last;
   time_t read;
};

struct lastf {
   int name;
   struct file *f;
   int slotsfilled;
};

struct groupobj {
   char *groupname;
   struct dir dir;
   struct lastf lastf;
   bool nopost;
};

static int cmpgroup (void *a, void *b)
{
   struct groupobj *x = (struct groupobj *) a;
   struct groupobj *y = (struct groupobj *) b;

   return strcmp(x->groupname, y->groupname);
}

static void freegroup (void *p)
{
   struct groupobj *gop = (struct groupobj *) p;

   if (gop->groupname)
      free(gop->groupname);
   if (gop->dir.dir)
      closedir(gop->dir.dir);
   if (gop->lastf.f)
      munmap((caddr_t) gop->lastf.f, sizeof (struct file));
   free(p);
}

int group_init (void)
{
   if (-1 != desc)
      return 0; /* already initialized */
   desc = cache_init(4, cmpgroup, freegroup, NULL);
   if (-1 == desc)
      return -1;
   return 0;
}

/*
 * Do we need to re-read the directory?
 */

static int refresh (struct groupobj *gop, char *groupname)
{
   struct stat st;
   struct dir *d;
   struct lastf *lp;
   bool needreread = FALSE;
   bool needrefile = FALSE;
   bool needreslot = FALSE;
   bool isemptygroup = FALSE;

   d = &gop->dir;
   lp = &gop->lastf;

   if (groupname)
   {
      memset(gop, 0, sizeof (struct groupobj));
      gop->groupname = strdup(groupname);
      if (NULL == gop->groupname)
      {
         LOG("refresh:strdup:%m");
         return -1;
      }
      gop->dir.first = -1;
      d->dir = opendir(groupname);
      if (NULL == d->dir)
      {
         LOG("refresh:opendir:%m");
         return -1;
      }
      lp->name = -1;

      needrefile = needreread = needreslot = TRUE;

   }
   else
   {

      if (-1 == d->first || 0 == d->last || !d->read)
         needreread = TRUE;
      else if (-1 == fstat(dirfd(d->dir), &st))
      {
         LOG("refresh:fstat:%m");
         return -1;
      }
      else if (d->read < st.st_mtime)
         needreread = TRUE;
      if (!lp->f)
         needrefile = needreslot = TRUE;
      if (!needrefile)
         if (!lp->f)
            needrefile = TRUE;
      if (!needrefile)
         if (-1 == lp->name)
            needrefile = TRUE;
   }

   if (needreread || needreslot)
   {
      struct dirent *dp;
      int i, first, last;
      char *end;

      first = -1;
      last = 0;
      rewinddir(d->dir);
      dp = readdir(d->dir);
      if (NULL == dp)
      {
         LOG("refresh:readdir:%m");
         return -1;
      }
      gop->nopost = FALSE;
      do
      {
         if (!isdigit(*dp->d_name))
         {
            if (!gop->nopost && '.' == *dp->d_name)
               if (0 == strcmp(dp->d_name, ".nopost"))
                  gop->nopost = TRUE;
            continue;
         }
         if ((i = strtoul(dp->d_name, &end, 10)) <= 0 || *end)
            continue;
         if (first > i || -1 == first)
            first = i;
         if (last < i)
            last = i;
      }
      while ((dp = readdir(d->dir)));
      d->first = first;
      d->last = last;
      if (-1 == first && 0 == last)
         isemptygroup = TRUE;
      if (last != lp->name)
         needrefile = TRUE;
      else
         lp->name = last;
      time(&d->read);
   }

   if (needrefile || needreslot)
   {
      int fd;

      if (lp->f)
      {
         munmap((caddr_t) lp->f, sizeof (struct file));
         lp->f = NULL;
      }
      if (isemptygroup)
         return 0;
      fd = openf(0, O_RDONLY, "%s/%d", gop->groupname, d->last);
      if (-1 == fd)
      {
         LOG("refresh:open:%m");
         return -1;
      }
      lp->f = (struct file *) mmap(0, sizeof (struct file), PROT_READ, MAP_SHARED, fd, 0);
      close(fd);
      if (lp->f == MAP_FAILED)
      { /* Bugfix: <= 0 fails for large addresses! (High bit set) */
         LOG("refresh:mmap:%m");
         return -1;
      }
      lp->slotsfilled = 0;
      needreslot = TRUE;
   }

   if (needreslot)
   {
      struct info *ip = lp->f->info;
      int slot;

      for (slot = ARTSPERFILE - 1; slot > -1; slot--)
         if (ip[slot].hoffset)
            break; /* pretend cancelled arts are there */
      lp->slotsfilled = slot + 1;
   }

   return 0;
}

int group_info (char *newsgroup, struct group *gp)
{
   struct groupobj *gop;

   {
      struct groupobj g = { 0, };

      g.groupname = newsgroup;
      gop = cache_find(desc, &g);
   }
   if (NULL == gop)
   {
      gop = malloc(sizeof (struct groupobj));
      if (NULL == gop)
      {
         LOG("group_info:malloc:%m");
         return -1;
      }
      if (-1 == refresh(gop, newsgroup))
      {
         freegroup(gop);
         LOG("group_info(%s)", newsgroup);
         return -1;
      }
      cache_insert(desc, gop);
   }
   else if (-1 == refresh(gop, NULL))
   {
      LOG("group_info(%s)", newsgroup);
      cache_invalidate(desc, gop);
      return -1;
   }

   gp->nopost = gop->nopost;
   if (-1 == gop->dir.first)
      return (gp->first = gp->last = gp->nr_articles = 0);
   gp->first = gop->dir.first * ARTSPERFILE;
   gp->last = gop->lastf.slotsfilled + (gop->dir.last * ARTSPERFILE) - 1;
   gp->nr_articles = gp->last - gp->first + 1; /* ZB was here */

   if (ARTSPERFILE == gop->lastf.slotsfilled)
   {
      munmap((caddr_t) gop->lastf.f, sizeof (struct file));
      gop->lastf.f = NULL;
   }

   return 0;
}

void group_fin (void)
{
   if (desc > -1)
      cache_fin(desc);
   desc = -1;
}
