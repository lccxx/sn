/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef KEY_H
#define KEY_H

/*
 * Generic hash table lookup.  Store only keys, not data.  All keys
 * must be unique.  Cannot delete.  This is used in 2 places: to
 * store a list of subscribed newsgroups in snntpd; and to remember
 * seen message IDs in snfetch.
 */

extern char *key_exists (char *key, int len);
extern int key_add (char **keyp, int len);
extern void key_free (void);

struct key {
   struct key *next;
   int len;
};

#define KEY(kp) ((char *)(kp) + sizeof(struct key))

#define KEY_TABLE_SIZE 128

extern struct key *key_table[KEY_TABLE_SIZE];
extern int nr_keys;

#define for_each_key(i,n,kp) \
   for (n = nr_keys, i = 0; i < KEY_TABLE_SIZE && n > 0; i++) \
      for (kp = key_table[i]; kp; kp = kp->next, n--)

#endif
