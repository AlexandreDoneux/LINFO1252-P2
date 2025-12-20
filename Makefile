CFLAGS=-g -Wall -Werror

all: tests lib_tar.o
	echo "all"

lib_tar.o: lib_tar.c lib_tar.h

tests: tests.c lib_tar.o archive
	cd archive && tar -cf ../archive.tar *
	#cd archive && tar -cf ../archive.tar -T /dev/null # for testing empty archive
	gcc $(CFLAGS) -o tests tests.c lib_tar.o
	./tests archive.tar

clean:
	rm -f lib_tar.o tests soumission.tar

submit: all
	tar --posix --pax-option delete=".*" --pax-option delete="*time*" --no-xattrs --no-acl --no-selinux -c *.h *.c Makefile > soumission.tar