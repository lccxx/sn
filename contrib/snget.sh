#!/bin/bash
# This script is no longer used, but you can look at it to get an
# idea of how to feed the news spool in some other way.

# Fetch articles via NNTP.  Get the groups to fetch from the command
# line, or else fetch all global groups in the spool.
# Errors and messages to fd 2.

[ -z "$SNROOT" ] && SNROOT="/var/sn"

PATH=/usr/sn:$PATH
export PATH SNROOT

umask 022

progname=`basename $0`

case "$1" in
  "-V") echo "$progname version 0.2.2" >&2; exit 0 ;;
  "-"*) echo "Usage: $progname [newsgroup]..." >&2; exit 1;;
esac

if which tcpclient >/dev/null; then
  use_suck=
elif which suck >/dev/null && which rpost >/dev/null; then
  use_suck="-c"  # Also used as option to snstore
else
  echo "Can't find tcpclient or suck and rpost" >&2
  exit 1
fi

function fail () { echo "$@" >&2; exit 2; }
function oops () { echo "$@" >&2; }

cd $SNROOT || fail "Can't cd to $SNROOT"

#
# Collect newsgroups
#

newsgroups="$@"
if [ -z "$newsgroups" ]; then
  for i in `ls -1`; do
    [ -d $i -a -d $i/.outgoing ] &&
      newsgroups="$newsgroups $i"
  done
fi

#
# Using suck?
#

function putquit () { echo "QUIT" >&7; }

declare -fx putquit oops fail

#
# Define the function which will fetch articles from a particular
# server.  Articles fetched are written to standard output, so
# caller must have redirected.
#

if [ -z "$use_suck" ]; then
  #
  # Not using suck, use tcpclient.
  #
  function runfetch () {
    local max=
    local group="$1"
    local serial=
  
    serial=`cat /var/sn/$group/.serial` 2>/dev/null
    [ -z "$serial" ] && serial="0"
    [ "$serial" = "0" ] && max=200
  
    snfetch $group $serial $max &&
      mv /var/sn/$group/.serial.tmp /var/sn/$group/.serial
    :
  }
  function getfromserver () {
    local server="$1"
    local port="$2"

    shift 2

    #
    # Want arg to sh -c to expand before exec.
    # Optional: replace "sh -c ..." with
    # "throttle 6r1024 sh -c ..." below to prevent snget from hogging
    # the modem.  The 1024 means to throttle to 1024 bytes/sec.
    #
    tcpclient -RHl0 -T10 $server $port sh -c \
      'snlogin '"$1"'; f=$?
      if [ $f = 0 -o $f = 9 ]; then snpost '"$1"'; else exit 1; fi
      for g in '"$*"'; do runfetch $g || break; done; putquit'
  }
  declare -fx runfetch
else

  #
  # Using suck
  #
  function getfromserver () {
    local server="$1"
    local port="$2"
    local serial
    local username=
    local password=
    local extension="$$"
    local sedcmd='s///
/^\.$/d
/^Path:/d
/^NNTP-Posting-Host:/d'

    shift 2

    if [ -f .outgoing/$server:$port/username ]; then
      username=`cat .outgoing/$server:$port/username` 2>/dev/null
      [ "$username" ] && username="-U $username"
    fi
    if [ -f .outgoing/$server:$port/password ]; then
      password=`cat .outgoing/$server:$port/password` 2>/dev/null
      [ "$password" ] && password="-P $password"
    fi

    #
    # Send outgoing articles first.  Can't lock article, oh well.
    #
    (
    cd .outgoing/$server:$port &&
    for p in `ls -1 '$'* 2>/dev/null`; do
      sed "$sedcmd" $p |rpost $server -N $port $username $password &&
        rm -f $p
    done
    ) >&2

    #
    # Create a temporary sucknewsrc file, run suck in "stdout" mode
    #
    for g in "$@"; do
      serial=`cat $g/.serial` 2>/dev/null
      [ -z "$serial" ] && serial=0
      echo "$g $serial"
    done >sucknewsrc.$extension

    suck $server -p .$extension -N $port -H -q $username $password |
      sed -e 's/$//' -e 's/^\.\([^].*\)/..\1/'

    if [ -f ./suck.newrc.$extension ]; then
      while read group serial; do
        [ -z "$group" -o -z "$serial" ] && continue
        echo "$serial" >$group/.serial
      done <./suck.newrc.$extension
    else
      oops "Couldn't run suck properly"
    fi
    rm -f suck*.$extension
  }
fi

declare -fx getfromserver

#
#  Sort newsgroups primarily by server, secondarily by name.
#

tmp=/tmp/$progname.$$
trap 'rm -f $tmp' 0

enable -n pwd; :

prefix="./"
for i in $newsgroups; do
  [ ! -d ${prefix}$i ] && fail "Not subscribed to group $i"
  [ ! -L ${prefix}$i/.outgoing ] && continue
  if cd ${prefix}$i/.outgoing; then
    prefix="../../"
    address=`basename \`command pwd\``
    case "$address" in
    '') continue ;;
    ?*:?*) echo `echo "$address" |tr ':' ' '` $i ;;
    ?*:) echo `echo "$address" |tr ':' ' '` 119 $i ;;
    ?*) echo "$address 119 $i" ;;
    *) continue ;;
    esac
  fi
done |sort >$tmp

#
# then go get them.
#

{
read oldserver oldport groups
[ -z "$groups" ] && exit 0
while read server port group; do
  if [ "$server" = "$oldserver" -a "$port" = "$oldport" ]; then
    groups="$groups $group"
  else
    if [ "$groups" ]; then
      getfromserver $oldserver $oldport $groups
      oldserver=$server
      oldport=$port
      groups=$group
    fi
  fi
done
getfromserver $oldserver $oldport $groups
} <$tmp |snstore $use_suck &&

#
# Index the news spool, maybe.
#

if [ -z "$*" ]; then
  if which sn-words >/dev/null; then
    if which index-make >/dev/null; then
      if sn-words * |index-make >.sn-index.new; then
        mv -f .sn-index.new .sn-index
      else
        rm -f .sn-index
      fi 2>/dev/null
    fi
  fi
fi
:
