/*
 * This file is part of the sn package.
 * Distribution of sn is covered by the GNU GPL. See file COPYING.
 * Copyright © 1998-2000 Harold Tay.
 * Copyright © 2000- Patrik Rådman.
 */

/*
 * Check the field name of a header line.
 */

static const char ver_ctrl_id[] = "$Id: field.c 52 2004-07-26 16:12:16Z patrik $";

int check_field (char *field, int len)
{
   int i;

   for (i = 0; i < len; i++)
   {
      /* Allow printable US-ASCII characters, except colon */
      if (field[i] >= '!' && field[i] <= '9')
         continue;
      if (field[i] >= ';' && field[i] <= '~')
         continue;
      if (field[i] == ':')
         return i;
      if (field[i] == ' ' || field[i] == '\t')
         break;
      return 0;
   }
   do
      i++;
   while (field[i] == ' ' || field[i] == '\t');
   if (field[i] != ':')
      return 0;
   return i;
}
