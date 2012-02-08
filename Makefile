TARGET ?= /usr/local
LIBEVENT ?= /usr/local
LIBSIMPLEHTTP ?= /usr/local

CFLAGS += -I. -I$(LIBSIMPLEHTTP)/include -I.. -I$(LIBEVENT)/include -g -Wall -O2
LIBS = -L. -L$(LIBEVENT)/lib -L/usr/local/lib -lprotobuf-c -levent -lbuffered_socket
AR = ar
AR_FLAGS = rc
RANLIB = ranlib

all: libdoozer-c.a

libdoozer-c.a: doozer-c.o doozer-c.h
	/bin/rm -f $@
	$(AR) $(AR_FLAGS) $@ $^
	$(RANLIB) $@

install:
	/usr/bin/install -d $(TARGET)/lib/
	/usr/bin/install -d $(TARGET)/bin/
	/usr/bin/install -d $(TARGET)/include/doozer-c
	/usr/bin/install libdoozer-c.a $(TARGET)/lib/
	/usr/bin/install doozer-c.h $(TARGET)/include/doozer-c

clean:
	/bin/rm -f *.a *.o
