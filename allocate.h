/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef ALLOCATE_H
#define ALLOCATE_H

/* int functions return -1 on error, 0 on success */

/* Return the object described by offset */

extern void *allo_deref (unsigned int offset);

/* Returns the offset of the object */

extern int allo_ref (void *object);

/* Return a descriptor to refer to this allocated arena */

extern int allo_init (char *path, int openflag);

/* Get a block of size size from the allocator */

extern int allo_make (int size);

/* Give back a block of size size to the allocator */

extern int allo_free (int chunk, int size);

/* Close this arena */

extern int allo_destroy (void);

#endif
