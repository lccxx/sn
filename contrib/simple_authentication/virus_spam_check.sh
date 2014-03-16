#!/bin/bash
#
# This script is invoked by snntpd in response to the POST command.
# Basically it forwards any cancel requests, and invokes snsend on
# the others.  It augments snsend by checking for newsgroups for
# which posting is not permitted.  This script's simple-minded idea
# of access control is determined by the presence or absence of the
# .nopost file in the newsgroup directories.  Obviously there's a lot
# of flexibility if you want to do more sophisticated access control.
#
# Invocation environment:
# Article to be posted will be available on descriptor 0.
# $NEWSGROUPS will always contain a non-empty space-separated list
# of newsgroups to which the article is posted.
# If the article is a control message, $CONTROL will be set to the
# value of the Control field.
# $TCPREMOTEIP and $TCPLOCALIP will be set to the posting client's IP
# and the local server IP, if invoked from the network.
# Current directory will always be the appropriate sn spool
# directory, $SNROOT if set, otherwise /var/spool/sn.
# PATH will always contain /usr/sbin.
# The exit status gets translated into the NNTP response code;
# the output (if any) becomes the text of the NNTP response.
#

fail () { echo "$@"; exit 9; }

#
# Is it a control message?  Notify owner and let her verify the
# request.  I won't make auto-cancellation the default.
# If you don't want control messages identified, remove or comment
# out the next if ... fi block.
#
if [ "x$CONTROL" != x ]; then
  parse () { CANCEL=`echo $1 |tr 'A-Z' 'a-z'`; MSGID=$2; }
  parse $CONTROL
  [ "x$CANCEL" = xcancel ] || fail "I only do \"cancel\" messages"
  tmp="/tmp/.SNPOST.$$"
  trap 'rm -f $tmp $tmp.1' 0
  sncat -i "$MSGID" |sed -e 's///' -e '/^$/q' >$tmp
  [ -s $tmp ] || fail "I can't find \"$MSGID\""
  set -e
  { echo "This is an automated message, don't reply."
  echo; echo "I received this cancel request:"; echo
  sed -e 's///' -e 's/^/> /'
  echo; echo "It refers to this article (headers only):"; echo
  sed 's/^/> /' $tmp
  echo "You can cancel it with \"/usr/sbin/sncancel -i '$MSGID'\""
  } >$tmp.1
  mail -s "Cancellation request" ${NEWSMASTER:-${LOGNAME:-postmaster}} <$tmp.1 ||
    fail "Unable to forward your request"
  echo "Your request will be considered"
  exit 0
fi

#
# Is a normal posting.
#

postable=
exists=

for ng in $NEWSGROUPS; do
  mentioned=x$mentioned
  # Is it a newsgroup we have?
  [ -f $ng/.created ] || continue
  exists=x$exists

  if [ -f $ng/.allow ]
  then
    /bin/grep -x -q "$VERIFIED_SENDER" $ng/.allow || continue
  else
    # .nopost files "belong" to us; we alone control group post access
    [ -f $ng/.nopost ] && continue
  fi

  [ "x$postable" = x ] || postable="$postable,"
  postable="$postable$ng"
done

[ "$exists" ] || fail "I don't have any of those newsgroups"
[ "x$postable" = x ] && fail "Posting not allowed to those newsgroups"


# PIPE FILE TO A TEMPFILE
TMP_MSG="/tmp/.SNPOST-msg.$$"
trap '/bin/rm -f $TMP_MSG' 0 9
/bin/cat > $TMP_MSG

# check virus ONLY IF CLAMD IS RUNNING
[ -e /usr/bin/clamdscan ] && virus=`/usr/bin/clamdscan --no-summary - <$TMP_MSG` || fail "Message contains virus (`echo $virus | /bin/sed s/.*://` ) and is rejected"
# check for spam 
[ -e /usr/bin/spamc ] && result=`/usr/bin/spamc -c < $TMP_MSG` || fail "Message is considered as SPAM ($result) and is rejected"

{
  echo "X-sn-Newsgroups: $postable"
  test -r /etc/news/organization && \
	echo Organization: `/bin/cat /etc/news/organization`
  # VERIFIED_SENDER is inherited from the authenticating script that
  # invoked snntpd, if any.
  [ "x$VERIFIED_SENDER" = x ] || echo "Sender: $VERIFIED_SENDER"
  /bin/cat $TMP_MSG
} | snsend -v || fail "Unable to post"
echo "Posted to $postable"
: Success
