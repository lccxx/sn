/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Generic hash table lookup.  Store only keys, not data.  All keys
 * must be unique.  Cannot delete.  This is used in 2 places: to
 * store a list of subscribed newsgroups in snntpd; and to remember
 * seen message IDs in snfetch.
 */

#include <stdlib.h>
#include <string.h>
#include "key.h"

static const char ver_ctrl_id[] = "$Id: key.c 40 2004-04-28 18:00:25Z patrik $";

struct buf {
   struct buf *next;
   char buf[1000];
};

static struct buf *head;
static int avail = 0;
static char *keybuf = NULL;

struct key *key_table[KEY_TABLE_SIZE] = { 0, };
int nr_keys = 0;

static unsigned hv;

static void hash (char *key, int len)
{
   hv = 5381;
   while (len)
   {
      --len;
      hv += (hv << 5);
      hv ^= (unsigned int) *key++;
   }
   hv %= 128;
}

char *key_exists (char *key, int len)
{
   struct key *kp;

   hash(key, len);
   for (kp = key_table[hv]; kp; kp = kp->next)
      if (kp->len == len)
         if (0 == memcmp(KEY(kp), key, len))
            return KEY(kp);
   return NULL;
}

int key_add (char **keyp, int len)
{
   struct key *kp;
   char *key;
   int size;

   key = *keyp;
   if (key_exists(key, len))
      return 1;
   size = len + sizeof (*kp);
   size += 4 - (size % 4);
   if (avail < size)
   {
      struct buf *x;
      if (!(x = malloc(sizeof (struct buf))))
         return -1;
      x->next = head;
      head = x;
      avail = 1000;
      keybuf = head->buf;
   }
   kp = (struct key *) keybuf;
   kp->len = len;
   memcpy(*keyp = KEY(kp), key, len + 1); /* Bugfix: +1 so that it copies the '\0' also. */
   kp->next = key_table[hv];
   key_table[hv] = kp;
   keybuf += size;
   avail -= size;
   nr_keys++;
   return 0;
}

void key_free (void)
{
   int i;

   for (i = 0; i < sizeof (key_table) / sizeof (*key_table); i++)
      key_table[i] = 0;
   while (head)
   {
      struct buf *tmp;

      tmp = head->next;
      free(head);
      head = tmp;
   }
   nr_keys = avail = 0;
}
