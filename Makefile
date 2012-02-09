TARGET ?= /usr/local
LIBEVENT ?= /usr/local

CFLAGS += -I. -I$(LIBEVENT)/include -g -Wall -O2
AR = ar
AR_FLAGS = rc
RANLIB = ranlib

all: libdoozer-c.a

libdoozer-c.a: doozer-c.o msg.pb-c.o
	/bin/rm -f $@
	$(AR) $(AR_FLAGS) $@ $^
	$(RANLIB) $@

install:
	/usr/bin/install -d $(TARGET)/lib/
	/usr/bin/install -d $(TARGET)/bin/
	/usr/bin/install -d $(TARGET)/include/doozer-c
	/usr/bin/install libdoozer-c.a $(TARGET)/lib/
	/usr/bin/install doozer-c.h $(TARGET)/include/doozer-c
	/usr/bin/install msg.pb-c.h $(TARGET)/include/doozer-c

clean:
	/bin/rm -f *.a *.o
