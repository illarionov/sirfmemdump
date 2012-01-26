CC?=gcc
#CFLAGS?= -O2 -s -pipe
CFLAGS= -W  -Wall -g -O0
CFLAGS+= -Iarm/include

DESTDIR?=/usr/local

all: sirfmemdump

sirfmemdump.bin:
	cd arm && $(MAKE)
	cp arm/sirfmemdump.bin .

flashutils.o: flashutils.c flashutils.h
	$(CC) $(CFLAGS) $(LDFLAGS) -c \
	  flashutils.c

mdproto.o: arm/include/mdproto.h arm/src/mdproto.c
	$(CC) $(CFLAGS) -c arm/src/mdproto.c

sirfmemdump: sirfmemdump.bin flashutils.o mdproto.o flashutils.h sirfmemdump.c
	$(CC) $(CFLAGS) flashutils.o mdproto.o sirfmemdump.c \
	-o sirfmemdump

clean:
	cd arm && $(MAKE) clean
	rm -f *.o sirfmemdump.bin sirfmemdump

install:
	mkdir -p ${DESTDIR}/bin 2> /dev/null
	cp -p sirfmemdump ${DESTDIR}/bin


.PHONY : sirfmemdump.bin
