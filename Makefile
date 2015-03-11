.PHONY: all socket.so socketbuffer.so clean cleanall

CFLAGS=-g -Wall -Werror -fPIC
SHARED=-shared
LUA_INC=$(HOME)/lshaco/3rd/lua/src

all: socket.so socketbuffer.so
socket.so: lsocket.c psocket.c socket.c
	gcc $(CFLAGS) $(SHARED) -o $@ $^ -llua -I$(LUA_INC)
socketbuffer.so: lsocketbuffer.c
	gcc $(CFLAGS) $(SHARED) -o $@ $^ -llua -I$(LUA_INC)
clean:
	rm -f socket.so socketbuffer.so
	rm -rf socket.so.* socketbuffer.so.*
cleanall: clean
	rm -f cscope.* 
