CROSS_COMPILE ?=

CC = $(CROSS_COMPILE)gcc

CFLAGS ?= -Wall -Wextra

all default: aesdsocket

aesdsocket: aesdsocket.o
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.o

aesdsocket.o: aesdsocket.c
	$(CC) $(CFLAGS) -c aesdsocket.c -o aesdsocket.o

clean:
	rm -f aesdsocket
	rm -f aesdsocket.o
