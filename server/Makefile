CROSS_COMPILE ?=

CC ?= $(CROSS_COMPILE)gcc

CFLAGS ?= -Wall -Wextra

# Comment/uncomment the following line to not/do /dev/aesdchar device
USE_AESD_CHAR_DEVICE ?= y

ifeq ($(strip $(USE_AESD_CHAR_DEVICE)),y)
  CFLAGS += -DUSE_AESD_CHAR_DEVICE
endif

LDFLAGS ?= -lpthread -lrt

all default: aesdsocket

aesdsocket: aesdsocket.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o aesdsocket aesdsocket.o

aesdsocket.o: aesdsocket.c
	$(CC) -I./ $(CFLAGS) -c aesdsocket.c -o aesdsocket.o

clean:
	rm -f aesdsocket
	rm -f aesdsocket.o
