/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Generic caching module.  Not very sophisticated, but very heavily
 * used to avoid repeated object construction/deconstruction.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "cache.h"

static const char ver_ctrl_id[] = "$Id: cache.c 29 2004-04-24 23:02:38Z patrik $";

/* Max number of caches (of possibly different types of object)
   that we can handle */
#ifndef CACHE_MAX
#define CACHE_MAX 4
#endif

struct entry {
   void *object;
   struct entry *prev;
   struct entry *next;
};

struct table {
   int maxsize;
   int (*cmp) (void *, void *);
   void (*freeobj) (void *);
   int (*isstale) (void *);
   struct entry *entries;
   struct entry *freelist;
   struct entry *freeblocks;
   int hit;
   int miss;
};

struct table table[CACHE_MAX] = { { 0, }, };

int cache_init (int max, int (*cmp) (void *, void *),
		         void (*freeobj) (void *),
		         int (*isstale) (void *))
{
   int i;
   int j;

   if (max <= 1 || NULL == cmp || NULL == freeobj)
   {
      errno = EINVAL;
      return -1;
   }

   for (i = 0; i < CACHE_MAX; i++)
      if (0 == table[i].maxsize)
         break;

   if (CACHE_MAX == i)
   {
      errno = ENFILE;
      return -1;
   }

   table[i].freeblocks = calloc(max, sizeof (struct entry));
   if (NULL == table[i].freeblocks)
      return -1;
   for (j = 0; j < max - 1; j++)
      table[i].freeblocks[j].next = &table[i].freeblocks[j + 1];
   table[i].freelist = table[i].freeblocks;
   table[i].maxsize = max;
   table[i].cmp = cmp;
   table[i].freeobj = freeobj;
   table[i].isstale = isstale; /* This is permitted to be NULL */
   table[i].hit = table[i].miss = 0;

   return i;
}

/* Either get a free entry from the freelist, or remove the
   last of the entries list */

static struct entry *new_entry (int desc)
{
   struct entry *ep;

   if ((ep = table[desc].freelist))
      table[desc].freelist = ep->next;
   else
   {
      for (ep = table[desc].entries; ep->next; ep = ep->next) ;
      ep->prev->next = NULL;
      table[desc].freeobj(ep->object);
   }
   ep->prev = ep->next = NULL;
   ep->object = NULL;
   return ep;
}

void cache_fin (int desc)
{
   struct entry *ep;
   struct entry *tmp;
   void (*freeobj) (void *) = table[desc].freeobj;

   for (ep = table[desc].entries; ep; ep = tmp)
   {
      (*freeobj) (ep->object);
      tmp = ep->next;
   }
   free(table[desc].freeblocks);
   memset(&table[desc], 0, sizeof (struct table));
}

void *cache_find (int desc, void *object)
{
   register struct table *tp = &table[desc];
   int (*cmp) (void *, void *) = tp->cmp;
   int (*isstale) (void *) = tp->isstale;
   struct entry *head = table[desc].entries;
   register struct entry *ep;

   for (ep = head; ep; ep = ep->next)

      if (isstale && (*isstale) (ep->object))
      {

         if (ep->prev)
            ep->prev->next = ep->next;
         else
            tp->entries = ep->next;
         if (ep->next)
            ep->next->prev = ep->prev;
         ep->next = NULL;
         (*tp->freeobj) (ep->object);
         ep->next = tp->freelist;
         tp->freelist = ep;

      }
      else if (0 == (*cmp) (object, ep->object))
      {

         /* Once found, move it to the head of the list */
         if (ep != tp->entries)
         {
            ep->prev->next = ep->next;
            if (ep->next)
               ep->next->prev = ep->prev;
            tp->entries->prev = ep;
            ep->next = tp->entries;
            tp->entries = ep;
            ep->prev = NULL;
         }
         tp->hit++;
         return tp->entries->object;
      }
   tp->miss++;
   return NULL;
}

void cache_stat (int desc, int *hit, int *miss)
{
   if (hit)
      *hit = table[desc].hit;
   if (miss)
      *miss = table[desc].miss;
}

/* Return the first item in the cache.  To be used as a hint.
   Should we be checking for staleness here? */

void *cache_top (int desc)
{
   return table[desc].entries->object;
}

/* Since new_entry() uses only pre-allocated memory, this function
   cannot fail. */

void cache_insert (int desc, void *object)
{
   register struct entry *ep;

   ep = new_entry(desc);
   ep->object = object;
   ep->next = table[desc].entries;
   if (table[desc].entries)
      table[desc].entries->prev = ep;
   table[desc].entries = ep;
}

void cache_invalidate (int desc, void *objp)
{
   struct entry *ep;
   struct entry *tmp;

   for (ep = table[desc].entries; ep; ep = tmp)
      if (0 == (*table[desc].cmp) (objp, ep->object))
      {
         if (ep->next)
            ep->next->prev = ep->prev;
         if (ep->prev)
            ep->prev->next = ep->next;
         else
            table[desc].entries = ep->next;
         (*table[desc].freeobj) (ep->object);
         tmp = ep->next;
         ep->object = NULL;
         ep->prev = NULL;
         ep->next = table[desc].freelist;
         table[desc].freelist = ep;
         break;
      }
      else
         tmp = ep->next;
}

#ifdef CACHE_DEBUG
void cache_dump (int desc, void (*printer) (void *))
{
   struct entry *ep;

   for (ep = table[desc].entries; ep; ep = ep->next)
      (*printer) (ep->object);
}
#endif /* CACHE_DEBUG */
