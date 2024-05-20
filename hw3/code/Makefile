CC=gcc
CFLAGS=-Wall
CVERSION=-std=gnu99

ALL: parking

parking: parking.c
	$(CC) $(CFLAGS) $(CVERSION) parking.c -o parking -lpthread -lrt

clean:
	rm -f parking