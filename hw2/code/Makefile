CC=gcc
CFLAGS=-Wall
CVERSION=-std=gnu99
SERVER_FIFO_PATH = /tmp/server_fifo
FIFO1_PATH = /tmp/fifo1
FIFO2_PATH = /tmp/fifo2

ALL: program

program: program.c
	$(CC) $(CFLAGS) $(CVERSION) program.c -o program

clean:
	rm -f program
	rm -f $(SERVER_FIFO_PATH)
	rm -f $(FIFO1_PATH)
	rm -f $(FIFO2_PATH)