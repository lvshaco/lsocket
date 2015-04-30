#include "psocket.h"
#include "socket.h"
#include <stdio.h>
#include <string.h>
#ifndef WIN32
#include <arpa/inet.h>
#include <signal.h>
#else
#include <winsock2.h>
#endif

static struct net* N = NULL;

static psocket_dispatch _dispatch = NULL;

int 
psocket_poll(int timeout) {
    struct socket_event *events;
    int n = socket_poll(N, timeout, &events);
    int i;
    for (i=0; i<n; ++i) {
        struct socket_event *event = &events[i];
        if (event->type == LS_ECONN_THEN_READ) {
            event->type = LS_ECONNECT;
            _dispatch(event);
            event->type = LS_EREAD;
            _dispatch(event);
        } else {
            _dispatch(event);
        }
    }
    return n;
}

int 
psocket_send(int id, void *data, int sz) {
    struct socket_event event;
    int n = socket_send(N, id, data, sz, &event);
    if (n<=0) return 0;
    else return event.err;
}

int 
psocket_init(int cmax, psocket_dispatch f) {
#ifdef WIN32
    WSADATA wd;
    WSAStartup( MAKEWORD(2, 2) , &wd);
#else
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
#endif
    _dispatch = f;
    N = net_create(cmax);
    return N ? 0:1;
}

void 
psocket_fini() {
    if (N) {
        net_free(N);
        N = NULL;
#ifdef WIN32
        WSACleanup();
#endif
    }
}

int psocket_listen(const char *addr, int port) { return socket_listen(N,addr,port,0); }
int psocket_connect(const char *addr, int port) { return socket_connect(N,addr,port,0,0);}
int psocket_close(int id, int force) { return socket_close(N,id,force);}
int psocket_subscribe(int id, int read) { return socket_subscribe(N,id,read);}
int psocket_read(int id, void **data) { return socket_read(N,id,data); }
int psocket_address(int id, struct socket_addr *addr) { return socket_address(N,id,addr); }
int psocket_limit(int id, int slimit, int rlimit) { return socket_limit(N,id,slimit, rlimit); }
int psocket_lasterrno() { return socket_lasterrno(N); }
const char *psocket_error(int err) { return socket_error(N, err); }
