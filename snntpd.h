/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

#ifndef SNNTPD_H
#define SNNTPD_H

extern bool posting_ok;
extern char *client_ip;
extern char *me;
extern struct readln input;
extern int fifo;
extern int nrgroups;

extern int alltolower (char *buf);
extern int topline (char *group, char *fn, char *buf, int size);

extern char *currentgroup;
extern int currentserial;
extern struct group currentinfo;

extern int make_current (char *group);
extern int glob (char *group, char *pattern);
extern int pipefork (int p[2]);

#endif /* SNNTPD_H */
