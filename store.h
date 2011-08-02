/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef STORE_H
#define STORE_H

#include "art.h"

extern int sto_init (void);
extern int sto_add (char *newsgroup, struct article *ap);
extern void sto_fin (void);

#endif
