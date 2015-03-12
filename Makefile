.PHONY: all socket.so socketbuffer.so clean cleanall test

CFLAGS=-g -Wall -Werror -fPIC
SHARED=-shared

all: socket.so socketbuffer.so
socket.so: src/lsocket.c src/psocket.c src/socket.c
	gcc $(CFLAGS) $(SHARED) -o $@ $^ 
socketbuffer.so: src/lsocketbuffer.c
	gcc $(CFLAGS) $(SHARED) -o $@ $^
test:
	cp socket.so socketbuffer.so lib/socket.lua test
clean:
	rm -f socket.so socketbuffer.so
	rm -rf socket.so.* socketbuffer.so.*
	rm -f test/socket.so test/socketbuffer.so test/socket.lua
cleanall: clean
	rm -f cscope.* tags
