/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Parse email addresses, or fragments thereof.  addr_domain() also
 * used to verify legal newsgroup names.  addr_qstrchr() like
 * strchr() but ignores comments and quoted strings.
 */

/*extern int addr_addrspec (char *buf);*/
extern int addr_domain (char *buf);
extern int addr_localpart (char *buf);
extern int addr_msgid (char *buf);
extern char *addr_qstrchr (char *str, int find);
extern int addr_unescape (char *from, char *to, int len);
