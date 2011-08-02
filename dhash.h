/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef DHASH_H
#define DHASH_H

/* Relative to snroot, current directory */

#ifndef DH_TABLEFILE
#define DH_TABLEFILE ".table"
#endif
#ifndef DH_CHAINFILE
#define DH_CHAINFILE ".chain"
#endif
#ifndef DH_GROUPFILE
#define DH_GROUPFILE ".newsgroup"
#endif

/*
 * Size (static) of the hash table.  This is the number of possible
 * hash chains.
 */

#ifndef DH_SIZE
#error "DH_SIZE not defined"
#endif

struct data {
   char *messageid;
   char *newsgroup;
   int serial;
};

/*
 * Functions return 0 on success, -1 on error.  For dh_find()
 * dp is in/out param, on entry must contain valid messageid.
 * On return, newsgroup and serial are filled in.  For dh_delete(),
 * only dp->messageid is used.  All returned values are readonly.
 */

extern int dh_open (char *pre, bool readonly);
extern int dh_close (void);
extern int dh_insert (struct data *dp);
extern int dh_find (struct data *dp, bool readonly);
extern int dh_delete (struct data *dp);

/* Stuff to link in with sndumpdb */

struct chain {
   int serial;                       /* Serial number of the messsage within the ng */
   int next;                         /* offset into database arena */
   char newsgroup[2];                /* Newsgroup identifier, from ng_*() */
   char messageid[sizeof (int) - 2]; /* Fake size */
};

#define DH_MAGIC 0xd0bed00

#define char2toint(p) ((p[0] << 8) | p[1])
#define char3toint(p) (((p)[0] << 18) | ((p)[1] << 10) | ((p)[2] << 2))

struct table {
   int magic;
   unsigned char next[DH_SIZE * 3];
};

extern struct table *dh_table;

#endif
