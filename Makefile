
# Check config.h for system-specific vars.
# You MUST edit the following.  Make sure there are no leading or
# trailing spaces around the values you change:

## Enable compression? Yes: specify LIBZ as "-lz" or use path, e.g.:
ZLIB =-lz
## or (if you prefer it static):
#ZLIB =/usr/lib/libz.a
## No: Leave ZLIB undefined.
#ZLIB =

## Where to install the executables and the man pages
PREFIX =/usr/local
#PREFIX =/home/patrik/stow/sn-current

## Where the news spool will be
SNROOT =/var/spool/news
#SNROOT =/home/patrik/spool/news

## Where to send mail for the admin if neither the NEWSMASTER nor
## the LOGNAME environment variable is set
DEFAULT_ADMIN_EMAIL =newsmaster

#
# OS-specific settings. Uncomment only one section below.
#

## For Linux:
INSTALL =install
LIBS =-L./lib -lstuff

## For Solaris:
#INSTALL =ginstall
#LIBS =-L./lib -lstuff -lxnet

## For FreeBSD / Mac OS X:
#INSTALL =ginstall
#LIBS =-L./lib -lstuff

#
# Stuff you probably won't need to edit.
#

CC =gcc
LD =gcc

BINDIR =$(PREFIX)/sbin
MANDIR =$(PREFIX)/man

#
# You can stop editing here.
#

VERSION =0.3.8
AOBJS =art.o cache.o group.o times.o dh_find.o dhash.o \
 allocate.o newsgroup.o hostname.o \
 store.o parameters.o args.o body.o unfold.o path.o \
 addr.o valid.o key.o field.o
OBJS =$(AOBJS) snscan.o snprimedb.o sndumpdb.o snntpd.o list.o \
 post.o commands.o snexpire.o \
 snmail.o snget.o
BINS =snprimedb snntpd snfetch snexpire snsend \
 snmail snget sngetd snscan sndumpdb \
 snnewgroup sndelgroup snlockf snsplit
SCRIPTS =dot-outgoing.ex SNHELLO SNPOST
PROGS =$(BINS) $(SCRIPTS)
MANS =sn.8 sncat.8 sndelgroup.8 sndumpdb.8 snexpire.8 \
snfetch.8 snget.8 snmail.8 snnewgroup.8 snntpd.8 \
snprimedb.8 snscan.8 sncancel.8 snsend.8 snstore.8 \
sngetd.8 snsplit.8

all: cc-flags $(OBJS) $(AOBJS) libs $(PROGS) sed-cmd $(MANS) $(SCRIPTS)
cc-flags:
	echo ' -g -Wall -pedantic -O' >$@.t
	echo ' -I./lib' >>$@.t
	echo ' -DVERSION="$(VERSION)"' >>$@.t
	echo ' -DSNROOT="$(SNROOT)"' >>$@.t
	echo ' -DBINDIR="$(BINDIR)"' >>$@.t
	-[ 'x$(ZLIB)' = x ] || echo ' -DUSE_ZLIB' >>$@.t
	mv $@.t $@
sed-cmd:
	echo 's,!!SNROOT!!,$(SNROOT),g' >$@.t
	echo 's,!!BINDIR!!,$(BINDIR),g' >>$@.t
	echo 's,!!VERSION!!,$(VERSION),g' >>$@.t
	echo 's,!!DEFAULT_ADMIN_EMAIL!!,$(DEFAULT_ADMIN_EMAIL),g' >>$@.t
	bash=`which bash`; echo "s,!!BASH!!,$${bash:-/bin/bash},g" >>$@.t
	mv $@.t $@

libs: sn.a lib/libstuff.a
lib/libstuff.a:
	cd lib; $(MAKE) all CC=$(CC)
sn.a: $(AOBJS)
	ar rc $@ $^
	ranlib $@

snsplit: snsplit.o sn.a
	$(LD) `cat cc-flags` $^ -o $@ $(LIBS)
snscan: snscan.o sn.a
	$(LD) `cat cc-flags` $^ -o $@ $(LIBS) $(ZLIB)
sncat: snscan
	ln -s snscan sncat
sncancel: snscan
	ln -s snscan sncancel
snprimedb: snprimedb.o sn.a
	$(LD) `cat cc-flags` $^ -o $@ $(LIBS)
sndumpdb: sndumpdb.o sn.a
	$(LD) `cat cc-flags` $^ -o $@ $(LIBS)
snntpd: snntpd.o post.o commands.o list.o sn.a
	$(LD) `cat cc-flags` $^ -o $@ $(LIBS) $(ZLIB)
snsend: snsend.o sn.a
	$(LD) `cat cc-flags` $^ -o $@ $(LIBS) $(ZLIB)
snstore: snsend
	ln -s snsend snstore
snfetch: snfetch.o sn.a
	$(LD) `cat cc-flags` $^ -o $@ $(LIBS)
snexpire: snexpire.o sn.a
	$(LD) `cat cc-flags` $^ -o $@ $(LIBS)
snmail: snmail.o sn.a
	$(LD) `cat cc-flags` $^ -o $@ $(LIBS)
snget: snget.o get.o sn.a
	$(LD) `cat cc-flags` $^ -o $@ $(LIBS)
sngetd: sngetd.o get.o sn.a
	$(LD) `cat cc-flags` $^ -o $@ $(LIBS)
snnewgroup: snnewgroup.o sn.a
	$(LD) `cat cc-flags` $^ -o $@ $(LIBS)
sndelgroup: sndelgroup.o sn.a
	$(LD) `cat cc-flags` $^ -o $@ $(LIBS)
snlockf: snlockf.o
	$(LD) `cat cc-flags` $^ -o $@ $(LIBS)

%: %.in sed-cmd
	sed -f sed-cmd $< >$@
	read magic <$@; case $$magic in "#!"*) chmod 755 $@ ;; esac

%.o: %.c cc-flags
	$(CC) -c `cat cc-flags` $< -o $@

clean:
	rm -f *.a *.o $(PROGS) $(SCRIPTS) a.out core \
	*.1 *.5 *.8 *.ex cc-flags* sed-cmd* gmon.out
	cd lib; $(MAKE) clean

strip: $(BINS)
	strip $^

install: all #$(SNROOT) $(BINDIR) $(MANDIR)/man8 # commented out so make -n install works without the dirs
	$(INSTALL) $(PROGS) $(BINDIR)
	$(INSTALL) *.8 $(MANDIR)/man8
	-cd $(BINDIR); rm -f sncat; ln -s snscan sncat
	-cd $(BINDIR); rm -f sncancel; ln -s snscan sncancel
	-cd $(BINDIR); rm -f snstore; ln -s snsend snstore

spoolclean:
	@echo -n "make $@ will wipe out your news spool!  Sure? [y/n] "
	@read ans; case "$$ans" in "y") : ;; *) echo "Not cleaning"; exit 1 ;; esac
	rm -f $(SNROOT)/*/[0123456789]* $(SNROOT)/*/.times
	rm -f $(SNROOT)/{.table,.chain,.newsgroup}
	for i in $(SNROOT)/*; do echo 0 >$$i/.serial; done
	-./snprimedb -i

echo-snroot:
	@echo $(SNROOT)
echo-bindir:
	@echo $(BINDIR)

_dep: Makefile cc-flags
	(sed '/^## DO NOT REMOVE ##/q' Makefile; echo; for i in $(OBJS); \
	do $(CC) -MM -MG `cat cc-flags` `basename $$i .o`.c; done) >.dep
	mv .dep Makefile

## DO NOT REMOVE ##

art.o: art.c config.h art.h artfile.h cache.h
cache.o: cache.c cache.h
group.o: group.c config.h cache.h group.h artfile.h
times.o: times.c config.h times.h cache.h
dh_find.o: dh_find.c config.h dhash.h allocate.h newsgroup.h
dhash.o: dhash.c config.h allocate.h dhash.h newsgroup.h
allocate.o: allocate.c config.h allocate.h
newsgroup.o: newsgroup.c config.h
hostname.o: hostname.c config.h
store.o: store.c config.h times.h art.h artfile.h cache.h group.h \
 body.h
parameters.o: parameters.c
args.o: args.c
body.o: body.c art.h
unfold.o: unfold.c
path.o: path.c
addr.o: addr.c addr.h
valid.o: valid.c config.h parameters.h
key.o: key.c key.h
field.o: field.c
snscan.o: snscan.c config.h artfile.h art.h group.h dhash.h hostname.h \
 parameters.h times.h body.h
snprimedb.o: snprimedb.c config.h dhash.h parameters.h
sndumpdb.o: sndumpdb.c config.h allocate.h newsgroup.h dhash.h \
 parameters.h
snntpd.o: snntpd.c config.h art.h dhash.h group.h hostname.h \
 parameters.h args.h lib/readln.h snntpd.h valid.h key.h
list.o: list.c config.h art.h group.h args.h lib/readln.h snntpd.h \
 key.h
post.o: post.c snntpd.h args.h lib/readln.h unfold.h
commands.o: commands.c config.h times.h dhash.h art.h group.h args.h \
 lib/readln.h body.h snntpd.h key.h
snexpire.o: snexpire.c config.h art.h group.h dhash.h times.h \
 parameters.h valid.h
snmail.o: snmail.c config.h parameters.h hostname.h unfold.h path.h \
 addr.h field.h
snget.o: snget.c config.h get.h parameters.h path.h valid.h
