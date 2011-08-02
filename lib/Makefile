all: stuff.a libstuff.a

stuff.a:
	set -e; for i in *.c; \
	  do $(CC) -c -O $$i -o `basename $$i .c`.o; done
	ar rc stuff.a *.o
	ranlib stuff.a
libstuff.a: stuff.a
	ln -s stuff.a libstuff.a
clean:
	rm -f *.o *.a
