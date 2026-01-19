CROSS_COMPILE ?=

CC = $(CROSS_COMPILE)gcc

CFLAGS ?= -Wall -Wextra

writer: writer.o
	$(CC) $(CFLAGS) -o writer writer.o

writer.o: writer.c
	$(CC) $(CFLAGS) -c writer.c -o writer.o

clean:
	rm -f writer
	rm -f writer.o
