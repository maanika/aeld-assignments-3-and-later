CROSS_COMPILE ?=

CC ?= $(CROSS_COMPILE)gcc

CFLAGS ?= -Wall -Wextra

LDFLAGS ?= -lpthread -lrt

all default: aesdsocket

aesdsocket: aesdsocket.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o aesdsocket aesdsocket.o

aesdsocket.o: aesdsocket.c
	$(CC) -I./ $(CFLAGS) -c aesdsocket.c -o aesdsocket.o

clean:
	rm -f aesdsocket
	rm -f aesdsocket.o
