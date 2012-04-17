CC = gcc
CFLAGS = -Wall $(shell pkg-config --cflags glib-2.0)
LDFLAGS = -lncurses $(shell pkg-config --libs glib-2.0)

chet2p: main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $^

clean:
	rm -f *.o
	rm -f chet2p

.PHONY: clean
