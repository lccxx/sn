/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef CACHE_H
#define CACHE_H

/*
 * Generic caching module.  We will be using this to cache article
 * files as well as group structures.
 */

/* cache_init() fails only when args are invalid or if memory fails */

extern int cache_init (int max, int (*cmp) (void *, void *), void (*freeobj) (void *),
                       int (*isstale) (void *));
extern void *cache_find (int desc, void *object);
extern void *cache_top (int desc);
extern void cache_insert (int desc, void *object);
extern void cache_invalidate (int desc, void *object);
extern void cache_fin (int desc);
extern void cache_stat (int desc, int *hit, int *miss);

#ifdef CACHE_DEBUG
extern void cache_debug (void (*printer) (void *));
#endif /* CACHE_DEBUG */

#endif
