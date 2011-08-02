/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef TOKENSEP_H
#define TOKENSEP_H

/*
 * Like strtok(3), but reentrant.  Returns the next token, as
 * delimited by any of the characters in delim, or NULL if no more
 * tokens.  *p is altered on each call, and is the start of the
 * buffer to read tokens from.
 */

extern char *tokensep (char **p, char *delim);

#endif
