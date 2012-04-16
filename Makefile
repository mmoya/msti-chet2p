CC = gcc
CFLAGS = -Wall
LDFLAGS = -lncurses

chet2p: main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $^

clean:
	rm -f *.o
	rm -f chet2p

.PHONY: clean
