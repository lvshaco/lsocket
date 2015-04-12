.PHONY: all socket.so socketbuffer.so clean cleanall test

CFLAGS=-g -Wall -Werror -DLUA_COMPAT_APIINTCASTS
#SHARED=-shared -fPIC
SHARED=-fPIC -dynamiclib -Wl,-undefined,dynamic_lookup
UNAME=$(shell uname)
SYS=$(if $(filter MINGW%, $(UNAME)), mingw, undefined)

ifeq ($(SYS), mingw) 
	LDFLAGS += -lws2_32 -llua
endif

all: socket.so socketbuffer.so
socket.so: src/lsocket.c src/psocket.c src/socket.c
	gcc $(CFLAGS) $(SHARED) -o $@ $^ -I/usr/local/include #-llua
socketbuffer.so: src/lsocketbuffer.c
	gcc $(CFLAGS) $(SHARED) -o $@ $^ -I/usr/local/include #-llua
test:
	cp socket.so socketbuffer.so lib/socket.lua test
clean:
	rm -f socket.so socketbuffer.so
	rm -rf socket.so.* socketbuffer.so.*
	rm -f test/socket.so test/socketbuffer.so test/socket.lua
cleanall: clean
	rm -f cscope.* tags
