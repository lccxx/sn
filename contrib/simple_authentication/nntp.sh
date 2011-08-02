#!/bin/bash

# simple script for doing authentication with sn, 
# the usernames and passwords are defined inline
# in this file

# settings

timeout=20  # seconds!

SNNTPD=/usr/local/sbin/snntpd 

#users (array)
users=(user1 user2 user3)

#corresponding passwords (must not be shorter than users array!)
passwords=(password1 password2 password3)

# end settings

checkuser () { 
  user_count=${#users[@]}
  index=0
  while [ "$index" -lt "$user_count" ] ; do
    if [ "${users[$index]}" = "$1" ] ; then
      if [ "${passwords[$index]}" = "$2" ] ; then
        return 0
      else
        return 1
      fi
    fi
    let "index = $index + 1"
  done
  return 1
}

printf "200 Hi, you can post (sn simple auth script)\r\n"

haveuser="" # no one yet

# simple loop as long as no timeout or other read error occurs
# loop also terminates when exec()'ing snntpd, for obvious reasons

while read -t $timeout currentline ; do
  lenm1=`expr ${#currentline} - 1`
  currentline=${currentline:0:$lenm1}
  switchpart=`echo ${currentline:0:14} | tr [a-z] [A-Z]`
  #switchpart=`echo ${currentline:0:14} | tr [:lower:] [:upper:]`
  case "$switchpart" in
    "LIST EXTENSIONS")

      printf "202-Extensions supported:\r\n"
      printf " AUTHINFO USER\r\n"
      printf " MODE READER\r\n"
      printf ".\r\n"
      haveuser=""
    ;;
    "MODE READER")
      # some newsreaders (e.g. MacSoup ;-))
      # send this command at the very beginning
      printf "200 You are already in this mode. Ignored.\r\n"
    ;;
    "QUIT")
      exit 0
    ;;
    "AUTHINFO USER")
      # extract username
      haveuser=${currentline:14}
      printf "381 Continue with authorization sequence\r\n"
      #      echo $haveuser
    ;;
    "AUTHINFO PASS")
      # check password

      havepass=${currentline:14} # extract password

      if checkuser $haveuser $havepass ; then
        printf "281 Authorization accepted\r\n"
        export POSTING_OK=1 # is this necessary?

        exec $SNNTPD -S logger -p news.info
      else
        printf "482 Authorization rejected $msg\r\n"
      fi
      haveuser=""
    ;;
    *)
      printf "480 Authorization required for this command\r\n"
      haveuser=""
    ;;
  esac
done