#include "cnet.h"
#include "net.h"
#include <stdio.h>
#include <string.h>
#ifndef WIN32
#include <arpa/inet.h>
#include <signal.h>
#else
#include <winsock2.h>
#endif

static struct net* N = NULL;

static cnet_dispatch _dispatch = NULL;

int 
cnet_poll(int timeout) {
    struct net_event *events;
    int n = net_poll(N, timeout, &events);
    int i;
    for (i=0; i<n; ++i) {
        struct net_event *event = &events[i];
        if (event->type == NETE_CONN_THEN_READ) {
            event->type = NETE_CONNECT;
            _dispatch(event);
            event->type = NETE_READ;
            _dispatch(event);
        } else {
            _dispatch(event);
        }
    }
    return n;
}

int 
cnet_send(int id, void *data, int sz) {
    struct net_event event;
    int n = net_send(N, id, data, sz, &event);
    if (n<=0) return 0;
    else return event.err;
}

int 
cnet_init(int cmax, cnet_dispatch f) {
#ifdef WIN32
    WSADATA wd;
    WSAStartup( MAKEWORD(2, 2) , &wd);
#else
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
#endif
    _dispatch = f;
    N = net_create(cmax);
    return !N ? 0:1;
}

void 
cnet_fini() {
    if (N) {
        net_free(N);
        N = NULL;
#ifdef WIN32
        WSACleanup();
#endif
    }
}

int cnet_listen(const char *addr, int port) { return net_listen(N,addr,port,0); }
int cnet_connect(const char *addr, int port) { return net_connect(N,addr,port,0,0);}
int cnet_close(int id, int force) { return net_close(N,id,force);}
int cnet_subscribe(int id, int read) { return net_subscribe(N,id,read);}
int cnet_read(int id, void **data) { return net_read(N,id,data); }
int cnet_address(int id, struct net_addr *addr) { return net_address(N,id,addr); }
int cnet_slimit(int id, int slimit) { return net_slimit(N,id,slimit); }
int cnet_lasterrno() { return net_lasterrno(N); }
const char *cnet_error(int err) { return net_error(N, err); }
