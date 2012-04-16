CC = gcc
CFLAGS = -Wall
LDFLAGS =

chet2p: main.o
	$(CC) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $^

clean:
	rm -f *.o
	rm -f chatserver

.PHONY: clean
