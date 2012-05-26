CC = gcc
CFLAGS = -Wall -ggdb $(shell pkg-config --cflags glib-2.0,ncursesw)
LDFLAGS = -lpthread $(shell pkg-config --libs glib-2.0,ncursesw)

ifeq ($D, 1)
	CFLAGS += -DDEBUG
endif

chet2p: chet2p.o commands.o chatgui.o peers.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $^

clean:
	rm -f *.o
	rm -f chet2p

.PHONY: clean
